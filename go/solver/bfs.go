package solver

import (
	"fmt"
	"sync"
	"sync/atomic"

	"github.com/michaelkrisper/rubik2x2/go/coords"
	"github.com/michaelkrisper/rubik2x2/go/cube"
)

// How a visited state is recorded in the mark table, packed into one uint16:
//
//	bits  0..3   its distance from this frontier's root
//	bit   4      which frontier claimed it
//	bits  5..15  generation stamp
//
// A cube state is already a dense integer below coords.NStates, so the
// visited set can be a flat array indexed by the state itself — no hashing, no
// collision probing, no allocation per node. Profiling the previous map-based
// version put ~72% of BFS time in runtime.memhash32 and the map probe loop, all
// of it spent hashing a number that was already a perfect index.
//
// The mark records a distance, not a parent state: a parent needs 22 bits and
// forces the word to 32, while a distance fits in 4 and halves the table to
// 7.3 MB — the win is cache footprint, one random word per edge. Distances are
// enough to rebuild the path because breadth-first marks are exact: from a node
// at distance d, exactly the neighbours at d-1 under the same tag are parents,
// and trail finds one by trying each move's inverse — a few dozen lookups once
// per solve, against a saved store on every edge.
//
// The generation stamp is what makes reusing that array cheap: a solve bumps
// the generation rather than clearing 7.3 MB, so marks left by earlier solves
// read as unvisited. Eleven bits give 2,046 solves between real clears.
const (
	depthBits = 4
	depthMask = 1<<depthBits - 1
	sidePos   = depthBits
	sideBit   = uint16(1) << sidePos
	genPos    = sidePos + 1
	genMask   = ^(sideBit | uint16(depthMask))
	maxGen    = ^uint16(0)>>genPos - 1
)

// scratch is the working set of one solve, kept alive between solves so the
// 7.3 MB mark table is allocated once. Pooled rather than package-level, so
// concurrent callers get one each instead of contending on a lock.
type scratch struct {
	marks      []uint16
	generation uint16
	buffers    [3][]int32
}

var scratchPool = sync.Pool{
	New: func() any { return &scratch{marks: newMarks(coords.NStates)} },
}

// mappedBytes totals what the huge-page mark mappings hold. mmap'd memory is
// invisible to runtime.MemStats, so the benchmark reads it here instead.
var mappedBytes atomic.Int64

// MappedBytes reports the bytes held by mmap-backed mark tables, zero on
// platforms where newMarks falls back to the heap.
func MappedBytes() int64 { return mappedBytes.Load() }

// stamp begins a solve and returns the tag its marks carry. Clearing happens
// only when the counter wraps.
func (s *scratch) stamp() uint16 {
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

	marks[start] = generation
	marks[coords.SolvedIndex] = generation | sideBit

	forward := append(work.buffers[0][:0], start)
	backward := append(work.buffers[1][:0], coords.SolvedIndex)
	next := work.buffers[2][:0]

	// The two sides advance independently — each round grows only one of
	// them — so each carries its own level depth, which is what the marks
	// record.
	forwardDepth, backwardDepth := uint16(0), uint16(0)

	solution, solved := "", false
	for range MaxDepth {
		// Always grow the cheaper side, which keeps both halves shallow.
		fromGoal := len(backward) < len(forward)
		level, side, levelDepth := forward, uint16(0), forwardDepth
		if fromGoal {
			level, side, levelDepth = backward, sideBit, backwardDepth
		}
		// Every state reached this round gets the same mark, so the store
		// in the inner loop is a constant.
		reached := generation | side | (levelDepth + 1)

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
				marks[child] = reached
				next = append(next, child)
			}
			if meeting >= 0 {
				break
			}
		}

		if meeting >= 0 {
			// One trail runs from each root to the meeting point; the
			// move that joined them belongs to whichever side was
			// expanding.
			here, there := generation|side, generation|(side^sideBit)
			reached := trail(marks, from, here) + string(cube.SolveMoves[by])
			opposite := trail(marks, meeting, there)
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
			backwardDepth++
		} else {
			forward, next = next, forward
			forwardDepth++
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

// trail returns the moves from node's frontier root down to node, in
// root-first order.
//
// tag is the generation and side bit the frontier's marks carry. A neighbour
// one step shallower under the same tag is a parent by construction, and the
// move reaching node from it is the inverse of the one that got there —
// adjacent indices, which is why the six solve moves are ordered as they are.
func trail(marks []uint16, node int32, tag uint16) string {
	tables := coords.MoveTables
	var moves []byte
	for depth := marks[node] & depthMask; depth > 0; depth-- {
		parentMark := tag | (depth - 1)
		q, r := node/coords.NOrientations, node%coords.NOrientations
		for m := range tables {
			back := tables[m^1].Wide[q] + tables[m^1].Orient[r]
			if marks[back] == parentMark {
				moves = append(moves, cube.SolveMoves[m])
				node = back
				break
			}
		}
	}
	for i, j := 0, len(moves)-1; i < j; i, j = i+1, j-1 {
		moves[i], moves[j] = moves[j], moves[i]
	}
	return string(moves)
}
