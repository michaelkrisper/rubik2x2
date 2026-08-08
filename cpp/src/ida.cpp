#include "rubik/ida.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <span>
#include <vector>

#include "rubik/bfs.hpp"

namespace rubik {
namespace {

constexpr std::uint8_t unreached = 0xFF;

/// Exact distance-to-solved within one coordinate, by breadth-first search over
/// its few thousand nodes. `successors[node]` is that node's row of six.
template <std::size_t Size, typename Table>
std::array<std::uint8_t, Size> project(const Table& successors) {
    std::array<std::uint8_t, Size> distances{};
    distances.fill(unreached);
    distances[0] = 0;

    std::vector<std::uint16_t> level{0};
    for (std::uint8_t depth = 1; !level.empty(); ++depth) {
        std::vector<std::uint16_t> following;
        for (std::uint16_t node : level) {
            for (const auto child : successors[node]) {
                if (distances[child] == unreached) {
                    distances[child] = depth;
                    following.push_back(child);
                }
            }
        }
        level = std::move(following);
    }
    return distances;
}

/// The path under construction: at most 14 moves, so a fixed array with a
/// length beats a `std::vector` whose capacity check runs at every node.
using Path = std::array<char, max_depth>;

/// One depth-first pass of IDA*, carrying the state as its two *coordinates*
/// rather than as the combined index.
///
/// Both move tables and both pattern databases are subscripted by a coordinate,
/// never by the combined index, so keeping the pair split removes the divide and
/// the multiply that recombining them would need -- and this loop is the whole
/// program. `remaining` is `limit - depth`, which turns the bound test into a
/// comparison against a value already in a register.
///
/// Children are bounded and rejected *here*, in the parent, so a node that
/// cannot fit the limit costs a table lookup rather than a call.
bool descend(std::uint32_t quotient, std::uint32_t remainder, int remaining, int depth, Path& path,
             std::size_t last, const PatternDatabases& databases,
             const coords::MoveTables& tables) {
    if (quotient == 0 && remainder == 0) {
        return true;
    }
    if (remaining == 0) {
        return false;
    }

    const auto& children_quotient = tables.permutation[quotient];
    const auto& children_remainder = tables.orientation[remainder];
    for (std::size_t move = 0; move < solve_moves.size(); ++move) {
        // `solve_moves` pairs each turn with its inverse at adjacent indices,
        // so `move ^ 1` is the turn that would undo the previous one.
        if ((move ^ 1u) == last) {
            continue;
        }
        const std::uint32_t child_quotient = children_quotient[move];
        const std::uint32_t child_remainder = children_remainder[move];
        const int bound = std::max<int>(databases.permutation[child_quotient],
                                        databases.orientation[child_remainder]);
        if (bound >= remaining) {
            continue;
        }
        path[static_cast<std::size_t>(depth)] = to_char(solve_moves[move]);
        if (descend(child_quotient, child_remainder, remaining - 1, depth + 1, path, move, databases,
                    tables)) {
            return true;
        }
    }
    return false;
}

}  // namespace

PatternDatabases PatternDatabases::build() {
    const coords::MoveTables& tables = coords::move_tables();

    return PatternDatabases{project<coords::permutation_count>(tables.permutation),
                            project<coords::orientation_count>(tables.orientation)};
}

const PatternDatabases& PatternDatabases::shared() {
    static const PatternDatabases databases = build();
    return databases;
}

std::pair<int, int> PatternDatabases::reach() const {
    return {*std::ranges::max_element(permutation), *std::ranges::max_element(orientation)};
}

Result<Moves> solve_ida(const State& state) {
    return solve_ida(state, PatternDatabases::shared());
}

Result<Moves> solve_ida(const State& state, const PatternDatabases& databases) {
    const Result<std::uint32_t> start = coords::encode(state);
    if (!start) {
        return std::unexpected(start.error());
    }
    if (*start == coords::solved_index) {
        return Moves{};
    }

    const coords::MoveTables& tables = coords::move_tables();
    const std::uint32_t quotient = *start / coords::orientation_count;
    const std::uint32_t remainder = *start % coords::orientation_count;

    Path path{};
    for (int limit = databases.bound(*start); limit <= max_depth; ++limit) {
        if (descend(quotient, remainder, limit, 0, path, solve_moves.size(), databases, tables)) {
            return Moves{path.begin(), path.begin() + limit};
        }
    }

    return fail(ErrorKind::unsolvable, std::format("no solution within {} moves", max_depth));
}

}  // namespace rubik
