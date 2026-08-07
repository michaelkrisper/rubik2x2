package solver

import (
	"fmt"

	"github.com/michaelkrisper/rubik2x2/go/coords"
	"github.com/michaelkrisper/rubik2x2/go/cube"
)

// crumb is how a node was first reached: from which node, by which move index.
// Storing the trail instead of a path string keeps the frontier maps small and
// allocation-free per node; the path is rebuilt once, at the meeting point.
type crumb struct {
	parent int32
	move   int8
}

// BFS solves by bidirectional breadth-first search, with no precomputation.
//
// Two frontiers grow a level at a time — one from the scramble, one from the
// goal — and the search stops where they touch. Level sizes peak at 1,350,852
// states at depth 11, so a one-sided search would have to walk most of the
// space; meeting in the middle needs depth 7 from each side, where only 33,058
// states live.
//
// Expanding by whole levels is what makes the result optimal: when the
// frontiers meet, every shorter pairing has already been tried, so the first
// meeting point is a shortest solution — no heuristic, no tie-breaking.
func BFS(s cube.State) (string, error) {
	start, err := coords.Encode(s)
	if err != nil {
		return "", err
	}
	if start == coords.SolvedIndex {
		return "", nil
	}

	tables := coords.MoveTables
	forward := map[int32]crumb{start: {-1, -1}}
	backward := map[int32]crumb{coords.SolvedIndex: {-1, -1}}
	forwardLevel := []int32{start}
	backwardLevel := []int32{coords.SolvedIndex}

	for range MaxDepth {
		// Always grow the cheaper side, which keeps both halves shallow.
		fromGoal := len(backwardLevel) < len(forwardLevel)
		level, seen, other := forwardLevel, forward, backward
		if fromGoal {
			level, seen, other = backwardLevel, backward, forward
		}

		next := make([]int32, 0, len(level)*3)
		meeting := int32(-1)
		for _, node := range level {
			q, r := node/coords.NOrientations, node%coords.NOrientations
			for m := range tables {
				child := tables[m].Wide[q] + tables[m].Orient[r]
				if _, ok := seen[child]; ok {
					continue
				}
				seen[child] = crumb{node, int8(m)}
				if _, ok := other[child]; ok {
					meeting = child
					break
				}
				next = append(next, child)
			}
			if meeting >= 0 {
				break
			}
		}

		if meeting >= 0 {
			return trail(forward, meeting) + cube.Invert(trail(backward, meeting)), nil
		}
		if fromGoal {
			backwardLevel = next
		} else {
			forwardLevel = next
		}
		if len(forwardLevel) == 0 || len(backwardLevel) == 0 {
			break
		}
	}
	return "", fmt.Errorf("%w: no solution within %d moves", cube.ErrUnsolvable, MaxDepth)
}

// trail walks the crumbs back to the root and returns the moves in forward
// order: the sequence that turns the root into node.
func trail(seen map[int32]crumb, node int32) string {
	var moves []byte
	for c := seen[node]; c.move >= 0; c = seen[node] {
		moves = append(moves, cube.SolveMoves[c.move])
		node = c.parent
	}
	for i, j := 0, len(moves)-1; i < j; i, j = i+1, j-1 {
		moves[i], moves[j] = moves[j], moves[i]
	}
	return string(moves)
}
