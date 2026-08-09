#include "rubik/bfs.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <vector>

#if defined(__linux__)
#include <sys/mman.h>

#include <new>

#include "rubik/memtrack.hpp"
#endif

#include "rubik/coords.hpp"
#include "rubik/prefetch.hpp"

namespace rubik {
namespace {

/// How a visited state is recorded, packed into one `std::uint16_t`:
///
///     bits  0..3   its distance from this frontier's root
///     bit   4      which frontier claimed it
///     bits  5..15  generation stamp
///
/// A cube state is already a dense index below `coords::state_count`, so the
/// visited set is a flat array subscripted by the state itself rather than a
/// hash map: no hashing, no probing, and one array write per edge instead of a
/// lookup in one map, a lookup in the other and an insert.
///
/// The mark stores a *distance* rather than a parent state: a parent needs 22
/// bits and forces the mark to 32, doubling an array whose whole cost is cache
/// footprint. Distances are enough to rebuild the path because breadth-first
/// marks are exact: from a node at distance d, exactly the neighbours at d-1 on
/// the same side are parents, and there are only six neighbours to try.
constexpr std::uint16_t depth_bits = 4;
constexpr std::uint16_t depth_mask = (1u << depth_bits) - 1;
constexpr std::uint16_t side_position = depth_bits;
constexpr std::uint16_t side_bit = 1u << side_position;
constexpr std::uint16_t generation_position = side_position + 1;
constexpr std::uint16_t generation_mask =
    static_cast<std::uint16_t>(~(side_bit | depth_mask));
constexpr std::uint32_t max_generation = (0xFFFFu >> generation_position) - 1;

static_assert(max_depth <= depth_mask, "a frontier depth no longer fits the mark");

#if defined(__linux__)
/// Backing store for the mark array: a 2 MB-aligned anonymous mapping asking
/// the kernel for transparent huge pages.
///
/// The array is 7.3 MB of *random* access -- about 1,800 ordinary 4 KB pages,
/// more than the TLB holds -- so nearly every cache miss also paid a page
/// walk. Backed by 2 MB pages the whole array is four TLB entries. mmap gives
/// no alignment promise, so the mapping is over-sized by one huge page, the
/// unaligned head and whatever tail is left are unmapped, and only then is the
/// kernel advised: with THP in `madvise` mode the huge pages arrive at first
/// fault, exactly when the zeroed mapping is touched. `operator new` never
/// sees this block, so its size is reported to `memtrack` by hand.
class MarkStore {
  public:
    MarkStore() {
        void* raw = ::mmap(nullptr, mapped_bytes + huge_page, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (raw == MAP_FAILED) {
            throw std::bad_alloc{};
        }
        const auto base = reinterpret_cast<std::uintptr_t>(raw);
        const std::uintptr_t aligned = (base + huge_page - 1) & ~(huge_page - 1);
        const std::size_t head = aligned - base;
        if (head != 0) {
            ::munmap(raw, head);
        }
        ::munmap(reinterpret_cast<void*>(aligned + mapped_bytes), huge_page - head);
        ::madvise(reinterpret_cast<void*>(aligned), mapped_bytes, MADV_HUGEPAGE);  // best effort
        marks_ = reinterpret_cast<std::uint16_t*>(aligned);
        memtrack::note_mapped(mapped_bytes);
    }
    ~MarkStore() {
        memtrack::forget_mapped(mapped_bytes);
        ::munmap(marks_, mapped_bytes);
    }
    MarkStore(const MarkStore&) = delete;
    MarkStore& operator=(const MarkStore&) = delete;

    [[nodiscard]] std::uint16_t* data() { return marks_; }

  private:
    static constexpr std::size_t huge_page = std::size_t{2} << 20;
    static constexpr std::size_t mapped_bytes =
        (coords::state_count * sizeof(std::uint16_t) + huge_page - 1) & ~(huge_page - 1);
    std::uint16_t* marks_;
};
#endif

/// The working set of one solve, kept alive between solves.
///
/// Allocating and zeroing 7.3 MB per solve would cost far more than the search
/// itself, so the mark array is allocated once and *aged* rather than cleared: a
/// solve bumps the generation counter and marks left by earlier solves no longer
/// match, so they read as unvisited. Eleven bits give 2,046 solves between real
/// clears, which amortises the one fill that is left to nothing.
class Scratch {
  public:
    /// Begin a solve; returns the tag this solve's marks carry.
    [[nodiscard]] std::uint16_t stamp() {
        if (generation_ >= max_generation) {
            std::fill_n(marks_.data(), coords::state_count, std::uint16_t{0});
            generation_ = 0;
        }
        ++generation_;
        return static_cast<std::uint16_t>(generation_ << generation_position);
    }

    [[nodiscard]] std::uint16_t* marks() { return marks_.data(); }
    [[nodiscard]] std::vector<std::uint32_t>& buffer(std::size_t which) { return buffers_[which]; }

  private:
#if defined(__linux__)
    MarkStore marks_;
#else
    // Anywhere the huge-page mapping above is not portable, a plain zeroed
    // vector: correct, just paying a page walk per TLB miss.
    std::vector<std::uint16_t> marks_ = std::vector<std::uint16_t>(coords::state_count, 0u);
#endif
    std::array<std::vector<std::uint32_t>, 3> buffers_{};
    std::uint32_t generation_ = 0;
};

/// One `Scratch` per thread. A plain function-local `static` would be a data
/// race the moment two threads solved at once -- this file has no way to know
/// that they do not -- and an argument would force the 7.3 MB buffer into the
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
///
/// `tag` is the generation and side bit the frontier's marks carry. A neighbour
/// one step shallower under the same tag is a parent by construction, and the
/// move reaching `node` from it is the inverse of the one that got there --
/// adjacent indices in `solve_moves`.
[[nodiscard]] Trail trail(const std::uint16_t* marks, std::uint32_t node, std::uint16_t tag) {
    Trail out;
    for (std::uint32_t depth = marks[node] & depth_mask; depth > 0; --depth) {
        const std::uint16_t parent_mark = static_cast<std::uint16_t>(tag | (depth - 1));
        for (std::size_t move = 0; move < solve_moves.size(); ++move) {
            const std::uint32_t back = coords::apply(node, move ^ 1u);
            if (marks[back] == parent_mark) {
                out.moves[out.size++] = static_cast<std::uint8_t>(move);
                node = back;
                break;
            }
        }
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
    const std::uint16_t generation = work.stamp();
    std::uint16_t* const marks = work.marks();
    const coords::MoveTables& tables = coords::move_tables();

    marks[*start] = generation;
    marks[coords::solved_index] = generation | side_bit;

    std::vector<std::uint32_t>& forward = work.buffer(0);
    std::vector<std::uint32_t>& backward = work.buffer(1);
    std::vector<std::uint32_t>& following = work.buffer(2);
    forward.assign(1, *start);
    backward.assign(1, coords::solved_index);

    // The two sides advance independently -- each round grows only one of them
    // -- so each carries its own level depth, which is what the marks record.
    std::uint32_t forward_depth = 0;
    std::uint32_t backward_depth = 0;

    for (int round = 0; round < max_depth; ++round) {
        // Always grow the cheaper side, which keeps both halves shallow.
        const bool from_goal = backward.size() < forward.size();
        std::vector<std::uint32_t>& growing = from_goal ? backward : forward;
        std::uint32_t& growing_depth = from_goal ? backward_depth : forward_depth;
        const std::uint16_t side = from_goal ? side_bit : std::uint16_t{0};
        // Every state reached this round gets the same mark, so the store in
        // the inner loop is a constant.
        const std::uint16_t reached =
            static_cast<std::uint16_t>(generation | side | (growing_depth + 1));

        following.clear();
        bool met = false;
        std::uint32_t meeting = 0;
        std::uint32_t from = 0;
        std::size_t by = 0;

        for (std::size_t i = 0; i < growing.size(); ++i) {
            // Every edge touches one random word of a 7.3 MB array, so the
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
                const std::uint16_t mark = marks[child];
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
                marks[child] = reached;
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
            const std::uint16_t here = generation | side;
            const std::uint16_t there = generation | (side ^ side_bit);
            Trail reached_trail = trail(marks, from, here);
            reached_trail.moves[reached_trail.size++] = static_cast<std::uint8_t>(by);
            const Trail opposite = trail(marks, meeting, there);

            Moves solution;
            solution.reserve(reached_trail.size + opposite.size);
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
                append_inverted(reached_trail);
            } else {
                append_forward(reached_trail);
                append_inverted(opposite);
            }
            return solution;
        }

        growing.swap(following);
        ++growing_depth;
        if (forward.empty() || backward.empty()) {
            break;
        }
    }

    return fail(ErrorKind::unsolvable, std::format("no solution within {} moves", max_depth));
}

}  // namespace rubik
