#include "rubik/table.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <utility>
#include <vector>

#include "rubik/coords.hpp"

namespace rubik {
namespace {

/// Expand one breadth-first level.
///
/// `Atomic` decides how the shared distance array is claimed. On one thread a
/// plain read-then-write is correct and fastest; on several, two workers can
/// reach the same state in the same level, so the byte is claimed with a
/// compare-and-exchange and only the winner queues the state. That keeps the
/// frontier free of duplicates without any locking, and it is why the parallel
/// build produces exactly the same table as the serial one.
template <bool Atomic>
void expand(std::span<const std::uint32_t> nodes, std::vector<std::uint32_t>& out,
            std::uint8_t* distances, std::uint8_t marker, const coords::MoveTables& tables) {
    for (std::uint32_t node : nodes) {
        const std::uint32_t quotient = node / coords::orientation_count;
        const std::uint32_t remainder = node % coords::orientation_count;
        for (const coords::MoveTable& table : tables) {
            const std::uint32_t child =
                table.permutation_wide[quotient] + table.orientation[remainder];
            if constexpr (Atomic) {
                std::atomic_ref<std::uint8_t> cell(distances[child]);
                // The cheap relaxed load skips the great majority of children,
                // which are already claimed; only the survivors pay for a CAS.
                if (cell.load(std::memory_order_relaxed) != 0) {
                    continue;
                }
                std::uint8_t expected = 0;
                if (!cell.compare_exchange_strong(expected, marker, std::memory_order_relaxed)) {
                    continue;
                }
            } else {
                if (distances[child] != 0) {
                    continue;
                }
                distances[child] = marker;
            }
            out.push_back(child);
        }
    }
}

/// Below this the thread hand-off costs more than the level does; the first six
/// levels of the search hold fewer than a thousand states between them.
constexpr std::size_t parallel_threshold = 4096;

std::vector<std::uint8_t> search(unsigned workers) {
    const coords::MoveTables& tables = coords::move_tables();

    std::vector<std::uint8_t> distances(coords::state_count, 0);
    distances[coords::solved_index] = 1;

    std::vector<std::uint32_t> level{coords::solved_index};

    for (std::uint8_t depth = 1; !level.empty(); ++depth) {
        const std::uint8_t marker = static_cast<std::uint8_t>(depth + 1);
        std::vector<std::vector<std::uint32_t>> chunks(workers);

        if (workers == 1 || level.size() < parallel_threshold) {
            expand<false>(level, chunks[0], distances.data(), marker, tables);
        } else {
            const std::size_t span = (level.size() + workers - 1) / workers;
            std::vector<std::jthread> threads;
            threads.reserve(workers);
            for (unsigned worker = 0; worker < workers; ++worker) {
                const std::size_t begin = std::min(worker * span, level.size());
                const std::size_t end = std::min(begin + span, level.size());
                threads.emplace_back([&, worker, begin, end] {
                    // A thread-local buffer, published once at the end: sharing
                    // the outer vector's headers would bounce a cache line
                    // between workers on every push.
                    std::vector<std::uint32_t> found;
                    found.reserve((end - begin) * 2);
                    expand<true>(std::span{level}.subspan(begin, end - begin), found,
                                 distances.data(), marker, tables);
                    chunks[worker] = std::move(found);
                });
            }
            threads.clear();  // std::jthread joins on destruction
        }

        // Per-worker buffers merged at the level boundary: no worker ever reads
        // another's output, so the only shared state is the distance array.
        std::size_t total = 0;
        for (const std::vector<std::uint32_t>& chunk : chunks) {
            total += chunk.size();
        }
        std::vector<std::uint32_t> following;
        following.reserve(total);
        for (const std::vector<std::uint32_t>& chunk : chunks) {
            following.insert(following.end(), chunk.begin(), chunk.end());
        }
        level = std::move(following);
    }
    return distances;
}

}  // namespace

unsigned DistanceTable::default_workers() {
    const unsigned reported = std::thread::hardware_concurrency();
    return reported == 0 ? 1u : reported;
}

DistanceTable DistanceTable::build() { return DistanceTable{search(1)}; }

DistanceTable DistanceTable::build_parallel(unsigned workers) {
    return DistanceTable{search(std::max(1u, workers))};
}

std::vector<std::uint64_t> DistanceTable::histogram() const {
    const std::uint8_t deepest = *std::ranges::max_element(distances_);
    std::vector<std::uint64_t> counts(deepest, 0);
    for (std::uint8_t stored : distances_) {
        if (stored != 0) {
            ++counts[stored - 1];
        }
    }
    return counts;
}

Result<Moves> DistanceTable::solve(const State& state) const {
    const Result<std::uint32_t> start = coords::encode(state);
    if (!start) {
        return std::unexpected(start.error());
    }

    const coords::MoveTables& tables = coords::move_tables();
    std::uint32_t index = *start;
    int remaining = distance(index);

    Moves path;
    path.reserve(static_cast<std::size_t>(remaining));
    while (remaining > 0) {
        const std::uint32_t quotient = index / coords::orientation_count;
        const std::uint32_t remainder = index % coords::orientation_count;
        bool stepped = false;
        for (std::size_t move = 0; move < tables.size() && !stepped; ++move) {
            const std::uint32_t child =
                tables[move].permutation_wide[quotient] + tables[move].orientation[remainder];
            if (distance(child) == remaining - 1) {
                path.push_back(to_char(solve_moves[move]));
                index = child;
                --remaining;
                stepped = true;
            }
        }
        if (!stepped) {
            return fail(ErrorKind::unsolvable, "distance table is inconsistent");
        }
    }
    return path;
}

}  // namespace rubik
