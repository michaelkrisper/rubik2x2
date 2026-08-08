#include "rubik/bfs.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <vector>

#include "rubik/coords.hpp"
#include "rubik/prefetch.hpp"

namespace rubik {
namespace {

/// How a visited state is recorded, packed into one `std::uint32_t`:
///
///     bits  0..2   the move that arrived here (`root_move` for a root)
///     bits  3..24  the state it arrived from (3,674,160 < 2^22)
///     bit   25     which frontier claimed it
///     bits 26..31  generation stamp
///
/// A cube state is already a dense index below `coords::state_count`, so the
/// visited set is a flat array subscripted by the state itself rather than a
/// hash map: no hashing, no probing, and one array write per edge instead of a
/// lookup in one map, a lookup in the other and an insert.
constexpr std::uint32_t move_bits = 3;
constexpr std::uint32_t move_mask = (1u << move_bits) - 1;
constexpr std::uint32_t root_move = move_mask;  // no real move index reaches 7
constexpr std::uint32_t parent_position = move_bits;
constexpr std::uint32_t parent_bits = 22;
constexpr std::uint32_t parent_mask = (1u << parent_bits) - 1;
constexpr std::uint32_t side_position = parent_position + parent_bits;
constexpr std::uint32_t side_bit = 1u << side_position;
constexpr std::uint32_t generation_position = side_position + 1;
constexpr std::uint32_t generation_mask =
    ~(side_bit | (parent_mask << parent_position) | move_mask);
constexpr std::uint32_t max_generation = (0xFFFF'FFFFu >> generation_position) - 1;

static_assert(coords::state_count <= parent_mask, "a state no longer fits the parent field");
static_assert(solve_moves.size() < root_move, "a move index would collide with the root marker");

/// The working set of one solve, kept alive between solves.
///
/// Allocating and zeroing 14.7 MB per solve would cost far more than the search
/// itself, so the mark array is allocated once and *aged* rather than cleared: a
/// solve bumps the generation counter and marks left by earlier solves no longer
/// match, so they read as unvisited. Six bits give 63 solves between real
/// clears, which amortises the one memset that is left to nothing.
class Scratch {
  public:
    /// Begin a solve; returns the tag this solve's marks carry.
    [[nodiscard]] std::uint32_t stamp() {
        if (generation_ >= max_generation) {
            std::ranges::fill(marks_, 0u);
            generation_ = 0;
        }
        ++generation_;
        return generation_ << generation_position;
    }

    [[nodiscard]] std::uint32_t* marks() { return marks_.data(); }
    [[nodiscard]] std::vector<std::uint32_t>& buffer(std::size_t which) { return buffers_[which]; }

  private:
    std::vector<std::uint32_t> marks_ = std::vector<std::uint32_t>(coords::state_count, 0u);
    std::array<std::vector<std::uint32_t>, 3> buffers_{};
    std::uint32_t generation_ = 0;
};

/// One `Scratch` per thread. A plain function-local `static` would be a data
/// race the moment two threads solved at once -- this file has no way to know
/// that they do not -- and an argument would force the 14.7 MB buffer into the
/// public signature of `solve_bfs`, which the CLI contract fixes. The thread
/// -local indirection is paid once per solve, not once per node.
[[nodiscard]] Scratch& scratch() {
    static thread_local Scratch instance;
    return instance;
}

/// Moves along one frontier's path, from its root to the meeting point.
struct Trail {
    std::array<std::uint8_t, max_depth + 1> moves{};
    std::size_t size = 0;
};

/// Walk the marks back to this frontier's root, yielding the moves in forward
/// order: the sequence that turns that root into `node`.
[[nodiscard]] Trail trail(const std::uint32_t* marks, std::uint32_t node) {
    Trail out;
    for (;;) {
        const std::uint32_t mark = marks[node];
        const std::uint32_t move = mark & move_mask;
        if (move == root_move) {
            break;
        }
        out.moves[out.size++] = static_cast<std::uint8_t>(move);
        node = (mark >> parent_position) & parent_mask;
    }
    std::ranges::reverse(out.moves.begin(), out.moves.begin() + static_cast<std::ptrdiff_t>(out.size));
    return out;
}

}  // namespace

Result<Moves> solve_bfs(const State& state) {
    const Result<std::uint32_t> start = coords::encode(state);
    if (!start) {
        return std::unexpected(start.error());
    }
    if (*start == coords::solved_index) {
        return Moves{};
    }

    Scratch& work = scratch();
    const std::uint32_t generation = work.stamp();
    std::uint32_t* const marks = work.marks();
    const coords::MoveTables& tables = coords::move_tables();

    marks[*start] = generation | root_move;
    marks[coords::solved_index] = generation | side_bit | root_move;

    std::vector<std::uint32_t>& forward = work.buffer(0);
    std::vector<std::uint32_t>& backward = work.buffer(1);
    std::vector<std::uint32_t>& following = work.buffer(2);
    forward.assign(1, *start);
    backward.assign(1, coords::solved_index);

    for (int depth = 0; depth < max_depth; ++depth) {
        // Always grow the cheaper side, which keeps both halves shallow.
        const bool from_goal = backward.size() < forward.size();
        std::vector<std::uint32_t>& growing = from_goal ? backward : forward;
        const std::uint32_t side = from_goal ? side_bit : 0u;

        following.clear();
        bool met = false;
        std::uint32_t meeting = 0;
        std::uint32_t from = 0;
        std::size_t by = 0;

        for (std::size_t i = 0; i < growing.size(); ++i) {
            // Every edge touches one random word of a 14.7 MB array, so the
            // loop stalls on memory rather than on arithmetic. Naming a later
            // entry's six marks now overlaps those misses with this entry's
            // work; the move tables the addresses come from stay in cache.
            if (i + prefetch_distance < growing.size()) {
                const std::uint32_t ahead = growing[i + prefetch_distance];
                const std::uint32_t next_quotient = ahead / coords::orientation_count;
                const std::uint32_t next_remainder = ahead % coords::orientation_count;
                const auto& next_wide = tables.permutation_wide[next_quotient];
                const auto& next_orientation = tables.orientation[next_remainder];
                for (std::size_t move = 0; move < solve_moves.size(); ++move) {
                    prefetch(marks + next_wide[move] + next_orientation[move]);
                }
            }

            const std::uint32_t node = growing[i];
            const std::uint32_t quotient = node / coords::orientation_count;
            const std::uint32_t remainder = node % coords::orientation_count;

            const auto& wide = tables.permutation_wide[quotient];
            const auto& orientation = tables.orientation[remainder];
            for (std::size_t move = 0; move < solve_moves.size(); ++move) {
                const std::uint32_t child = wide[move] + orientation[move];
                const std::uint32_t mark = marks[child];
                if ((mark & generation_mask) == generation) {
                    if ((mark & side_bit) == side) {
                        continue;  // this side has already been here
                    }
                    meeting = child;
                    from = node;
                    by = move;
                    met = true;
                    break;
                }
                marks[child] = generation | side | (node << parent_position) |
                               static_cast<std::uint32_t>(move);
                following.push_back(child);
            }
            if (met) {
                break;
            }
        }

        if (met) {
            // `reached` runs from the growing side's root to the meeting point,
            // `opposite` from the other root to the same place. Whichever half
            // ends at the goal is the one that gets undone.
            Trail reached = trail(marks, from);
            reached.moves[reached.size++] = static_cast<std::uint8_t>(by);
            const Trail opposite = trail(marks, meeting);

            Moves solution;
            solution.reserve(reached.size + opposite.size);
            const auto append_forward = [&solution](const Trail& path) {
                for (std::size_t i = 0; i < path.size; ++i) {
                    solution.push_back(solve_move_chars[path.moves[i]]);
                }
            };
            const auto append_inverted = [&solution](const Trail& path) {
                for (std::size_t i = path.size; i-- > 0;) {
                    // Inverses sit at adjacent indices in `solve_moves`.
                    solution.push_back(solve_move_chars[path.moves[i] ^ 1u]);
                }
            };
            if (from_goal) {
                append_forward(opposite);
                append_inverted(reached);
            } else {
                append_forward(reached);
                append_inverted(opposite);
            }
            return solution;
        }

        growing.swap(following);
        if (forward.empty() || backward.empty()) {
            break;
        }
    }

    return fail(ErrorKind::unsolvable, std::format("no solution within {} moves", max_depth));
}

}  // namespace rubik
