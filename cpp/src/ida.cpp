#include "rubik/ida.hpp"

#include <cstddef>
#include <format>
#include <span>
#include <vector>

#include "rubik/bfs.hpp"

namespace rubik {
namespace {

constexpr std::uint8_t unreached = 0xFF;

/// Exact distance-to-solved within one coordinate, by breadth-first search over
/// its few thousand nodes.
template <std::size_t Size, typename Table>
std::array<std::uint8_t, Size> project(const Table& successors) {
    std::array<std::uint8_t, Size> distances{};
    distances.fill(unreached);
    distances[0] = 0;

    std::vector<std::uint16_t> level{0};
    for (std::uint8_t depth = 1; !level.empty(); ++depth) {
        std::vector<std::uint16_t> following;
        for (std::uint16_t node : level) {
            for (const auto& table : successors) {
                const std::uint16_t child = table[node];
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

/// One depth-first pass of IDA*. Returns true once the goal is inside `limit`.
bool descend(std::uint32_t index, int depth, int limit, std::vector<char>& path, std::size_t last,
             const PatternDatabases& databases, const coords::MoveTables& tables) {
    if (depth + databases.bound(index) > limit) {
        return false;
    }
    if (index == coords::solved_index) {
        return true;
    }
    const std::uint32_t quotient = index / coords::orientation_count;
    const std::uint32_t remainder = index % coords::orientation_count;

    for (std::size_t move = 0; move < tables.size(); ++move) {
        // `solve_moves` pairs each turn with its inverse at adjacent indices,
        // so `move ^ 1` is the turn that would undo the previous one.
        if ((move ^ 1u) == last) {
            continue;
        }
        const std::uint32_t child =
            tables[move].permutation_wide[quotient] + tables[move].orientation[remainder];
        path.push_back(to_char(solve_moves[move]));
        if (descend(child, depth + 1, limit, path, move, databases, tables)) {
            return true;
        }
        path.pop_back();
    }
    return false;
}

}  // namespace

PatternDatabases PatternDatabases::build() {
    const coords::MoveTables& tables = coords::move_tables();

    std::array<std::span<const std::uint16_t>, solve_moves.size()> permutation_moves{};
    std::array<std::span<const std::uint16_t>, solve_moves.size()> orientation_moves{};
    for (std::size_t move = 0; move < tables.size(); ++move) {
        permutation_moves[move] = tables[move].permutation;
        orientation_moves[move] = tables[move].orientation;
    }

    return PatternDatabases{project<coords::permutation_count>(permutation_moves),
                            project<coords::orientation_count>(orientation_moves)};
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

    std::vector<char> path;
    path.reserve(max_depth);
    for (int limit = databases.bound(*start); limit <= max_depth; ++limit) {
        path.clear();
        if (descend(*start, 0, limit, path, solve_moves.size(), databases, tables)) {
            return Moves{path.begin(), path.end()};
        }
    }

    return fail(ErrorKind::unsolvable, std::format("no solution within {} moves", max_depth));
}

}  // namespace rubik
