package solver

import (
	"fmt"
	"sync"

	"github.com/michaelkrisper/rubik2x2/go/coords"
	"github.com/michaelkrisper/rubik2x2/go/cube"
)

// How a visited state is recorded in the mark table, packed into one uint32:
//
//	bits  0..2   the move that arrived here (rootMove for a frontier root)
//	bits  3..24  the state it arrived from (22 bits; 3,674,160 < 2^22)
//	bit   25     which frontier claimed it
//	bits 26..31  generation stamp
//
// A cube state is already a dense integer below coords.NStates, so the
// visited set can be a flat array indexed by the state itself — no hashing, no
// collision probing, no allocation per node. Profiling the previous map-based
// version put ~72% of BFS time in runtime.memhash32 and the map probe loop, all
// of it spent hashing a number that was already a perfect index.
//
// The generation stamp is what makes reusing that array cheap: a solve bumps the
// generation rather than clearing 14.7 MB, so marks left by earlier solves read
// as unvisited. Six bits give 63 solves between real clears.
const (
	moveBits   = 3
	moveMask   = 1<<moveBits - 1
	rootMove   = moveMask // no real move index reaches 7
	parentPos  = moveBits
	parentBits = 22
	parentMask = 1<<parentBits - 1
	sidePos    = parentPos + parentBits
	sideBit    = uint32(1) << sidePos
	genPos     = sidePos + 1
	genMask    = ^(sideBit | uint32(parentMask)<<parentPos | moveMask)
	maxGen     = ^uint32(0)>>genPos - 1
)

// scratch is the working set of one solve, kept alive between solves so the
// 14.7 MB mark table is allocated once. Pooled rather than package-level, so
// concurrent callers get one each instead of contending on a lock.
type scratch struct {
	marks      []uint32
	generation uint32
	buffers    [3][]int32
}

var scratchPool = sync.Pool{
	New: func() any { return &scratch{marks: make([]uint32, coords.NStates)} },
}

// stamp begins a solve and returns the tag its marks carry. Clearing happens
// only when the counter wraps.
func (s *scratch) stamp() uint32 {
	if s.generation >= maxGen {
		clear(s.marks)
		s.generation = 0
	}
	s.generation++
	return s.generation << genPos
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

	work := scratchPool.Get().(*scratch)
	marks, generation := work.marks, work.stamp()
	tables := coords.MoveTables

	marks[start] = generation | rootMove
	marks[coords.SolvedIndex] = generation | sideBit | rootMove

	forward := append(work.buffers[0][:0], start)
	backward := append(work.buffers[1][:0], coords.SolvedIndex)
	next := work.buffers[2][:0]

	solution, solved := "", false
	for range MaxDepth {
		// Always grow the cheaper side, which keeps both halves shallow.
		fromGoal := len(backward) < len(forward)
		level, side := forward, uint32(0)
		if fromGoal {
			level, side = backward, sideBit
		}

		next = next[:0]
		meeting, from, by := int32(-1), int32(0), 0

		for _, node := range level {
			q, r := node/coords.NOrientations, node%coords.NOrientations
			for m := range tables {
				child := tables[m].Wide[q] + tables[m].Orient[r]
				mark := marks[child]
				if mark&genMask == generation {
					if mark&sideBit == side {
						continue // this side has already been here
					}
					meeting, from, by = child, node, m
					break
				}
				marks[child] = generation | side | uint32(node)<<parentPos | uint32(m)
				next = append(next, child)
			}
			if meeting >= 0 {
				break
			}
		}

		if meeting >= 0 {
			reached := trail(marks, from) + string(cube.SolveMoves[by])
			opposite := trail(marks, meeting)
			if fromGoal {
				solution = opposite + cube.Invert(reached)
			} else {
				solution = reached + cube.Invert(opposite)
			}
			solved = true
			break
		}

		if fromGoal {
			backward, next = next, backward
		} else {
			forward, next = next, forward
		}
		if len(forward) == 0 || len(backward) == 0 {
			break
		}
	}

	work.buffers[0], work.buffers[1], work.buffers[2] = forward, backward, next
	scratchPool.Put(work)

	if solved {
		return solution, nil
	}
	return "", fmt.Errorf("%w: no solution within %d moves", cube.ErrUnsolvable, MaxDepth)
}

// trail walks the marks back to its frontier's root and returns the moves in
// forward order: the sequence that turns that root into node.
func trail(marks []uint32, node int32) string {
	var moves []byte
	for {
		mark := marks[node]
		move := mark & moveMask
		if move == rootMove {
			break
		}
		moves = append(moves, cube.SolveMoves[move])
		node = int32(mark >> parentPos & parentMask)
	}
	for i, j := 0, len(moves)-1; i < j; i, j = i+1, j-1 {
		moves[i], moves[j] = moves[j], moves[i]
	}
	return string(moves)
}
