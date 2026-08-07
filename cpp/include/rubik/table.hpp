/// God's algorithm: the exact distance of every reachable state.
///
/// The pocket cube has 3,674,160 positions -- few enough to hold every one's
/// exact distance to solved in a flat byte array of 3.7 MB. Then there is
/// nothing left to search, only a table to read:
///
///     at a state *d* moves from solved, exactly one neighbour is *d-1* away;
///     step to it, repeat.
///
/// A solve costs *d* lookups: constant time in the difficulty of the scramble,
/// and optimal by construction. This is the classic result for the 2x2x2 --
/// Jerry Bryan computed the same distribution in 1993, and it is recorded as
/// OEIS A079762, which `histogram()` reproduces exactly.
///
/// Distances are stored as `distance + 1` so that 0 can mean "not yet reached"
/// during the build, which saves a second array.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "rubik/cube.hpp"
#include "rubik/error.hpp"

namespace rubik {

class DistanceTable {
  public:
    /// Breadth-first over the whole space on one thread.
    [[nodiscard]] static DistanceTable build();

    /// The same search, with each level's frontier split across `workers`.
    /// Produces a byte-for-byte identical table.
    [[nodiscard]] static DistanceTable build_parallel(unsigned workers = default_workers());

    /// Threads to use when none is asked for: what the machine reports, at
    /// least one.
    [[nodiscard]] static unsigned default_workers();

    /// Quarter turns from the encoded state `index` to solved.
    [[nodiscard]] int distance(std::uint32_t index) const {
        return distances_[index] - 1;
    }

    /// States per distance, index 0 being the solved state.
    [[nodiscard]] std::vector<std::uint64_t> histogram() const;

    /// Walk downhill: one table lookup per candidate move, per step.
    [[nodiscard]] Result<Moves> solve(const State& state) const;

    [[nodiscard]] std::span<const std::uint8_t> data() const { return distances_; }

  private:
    explicit DistanceTable(std::vector<std::uint8_t> distances)
        : distances_(std::move(distances)) {}

    std::vector<std::uint8_t> distances_;
};

}  // namespace rubik
