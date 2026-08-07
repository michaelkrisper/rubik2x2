/// Korf's IDA* with pattern databases -- the textbook optimal cube solver.
///
/// Iterative-deepening A* runs depth-first passes with a rising limit on
/// `g + h`, where `h` is a lower bound on the moves still needed. Korf's
/// contribution was to get `h` from *pattern databases*: exact distances for a
/// relaxed version of the puzzle, precomputed once.
///
/// Here the relaxations fall out of the encoding for free. A move acts on the
/// permutation coordinate and the orientation coordinate independently, so each
/// coordinate on its own is a genuine quotient of the state graph -- solving
/// the cube necessarily solves both projections. Their exact distances (5040
/// and 729 entries, computed in microseconds) are therefore admissible, and the
/// larger of the two is the better bound.
///
/// This is here for comparison, not because it wins. On a 2x2x2 it loses to
/// plain bidirectional search: both databases top out at distance 8 while real
/// positions reach 14, leaving the bound far too loose to prune. Pattern
/// databases pay off when a puzzle has more pieces than you can afford to track
/// and you keep only some of them; a pocket cube is nothing *but* the pieces
/// the databases already cover.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include "rubik/coords.hpp"
#include "rubik/cube.hpp"
#include "rubik/error.hpp"

namespace rubik {

/// Exact distances for the two coordinate projections of the cube.
class PatternDatabases {
  public:
    [[nodiscard]] static PatternDatabases build();

    /// Built once and reused; the databases are the same on every call.
    [[nodiscard]] static const PatternDatabases& shared();

    /// Admissible lower bound on the moves left from an encoded state.
    [[nodiscard]] int bound(std::uint32_t index) const {
        return std::max<int>(permutation[index / coords::orientation_count],
                             orientation[index % coords::orientation_count]);
    }

    /// The largest bound each database can ever return.
    [[nodiscard]] std::pair<int, int> reach() const;

    std::array<std::uint8_t, coords::permutation_count> permutation{};
    std::array<std::uint8_t, coords::orientation_count> orientation{};
};

/// A shortest move sequence solving `state`; empty if it is already solved.
[[nodiscard]] Result<Moves> solve_ida(const State& state, const PatternDatabases& databases);

/// The same, using the process-wide databases (built on first use).
[[nodiscard]] Result<Moves> solve_ida(const State& state);

}  // namespace rubik
