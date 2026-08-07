// Package solver holds the three optimal solvers: bidirectional breadth-first
// search, IDA* with pattern databases, and the full distance table.
//
// All three return a shortest solution. God's number for the pocket cube is 14
// in the quarter-turn metric, so no solution is ever longer than that. What
// separates them is where the work goes: bfs pays nothing up front and a few
// milliseconds per solve, the table pays a full sweep of the state space up
// front and then answers in microseconds, and IDA* sits in between — see the
// benchmark and the README for where each wins.
package solver

import (
	"github.com/michaelkrisper/rubik2x2/go/cube"
)

// MaxDepth is God's number for the pocket cube in the quarter-turn metric.
const MaxDepth = 14

// A Solver returns a shortest move sequence solving a state, or the empty
// string if it is already solved.
type Solver func(cube.State) (string, error)
