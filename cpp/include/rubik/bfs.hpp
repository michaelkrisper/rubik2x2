/// Bidirectional breadth-first search: the solver that needs no setup.
///
/// Two frontiers grow a level at a time -- one from the scramble, one from the
/// goal -- and the search stops where they touch. God's number for a pocket
/// cube is 14 quarter turns and the level sizes peak at 1,350,852 states at
/// depth 11, so a one-sided search would have to walk most of the space.
/// Meeting in the middle needs depth 7 from each side, where only 33,058 states
/// live.
///
/// Expanding by whole levels is what makes the result optimal: when the
/// frontiers meet, every shorter pairing has already been tried, so the first
/// meeting point is a shortest solution -- no heuristic, no tie-breaking.
#pragma once

#include "rubik/cube.hpp"
#include "rubik/error.hpp"

namespace rubik {

/// God's number for the pocket cube in the quarter-turn metric.
inline constexpr int max_depth = 14;

/// A shortest move sequence solving `state`; empty if it is already solved.
[[nodiscard]] Result<Moves> solve_bfs(const State& state);

}  // namespace rubik
