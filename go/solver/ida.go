package solver

import (
	"fmt"

	"github.com/michaelkrisper/rubik2x2/go/coords"
	"github.com/michaelkrisper/rubik2x2/go/cube"
)

// A PatternDB is Korf's pattern database pair: the exact distance to solved
// within each of the two coordinates on its own.
//
// A move acts on the permutation coordinate and the orientation coordinate
// independently, so each coordinate alone is a genuine quotient of the state
// graph — solving the cube necessarily solves both projections. Their exact
// distances (5040 and 729 entries, computed in well under a millisecond) are
// therefore admissible lower bounds, and the larger of the two is the better
// one.
//
// This is here for comparison, not because it wins. On a 2x2x2 it loses to
// plain bidirectional search, and measurably so: both databases top out at
// distance 8 while real positions reach 14, leaving the bound too loose to
// prune well. Pattern databases pay off when a puzzle has more pieces than you
// can afford to track and you keep only some of them; a pocket cube is nothing
// but the pieces the databases already cover.
type PatternDB struct {
	perm   []uint8 // distance + 1, indexed by permutation rank
	orient []uint8 // distance + 1, indexed by orientation rank
}

// NewPatternDB builds both databases by breadth-first search over the
// projections.
func NewPatternDB() *PatternDB {
	perm := make([][]int32, len(coords.MoveTables))
	orient := make([][]int32, len(coords.MoveTables))
	for i, table := range coords.MoveTables {
		perm[i], orient[i] = table.Perm, table.Orient
	}
	return &PatternDB{
		perm:   project(perm, coords.NPermutations),
		orient: project(orient, coords.NOrientations),
	}
}

// project is breadth-first search inside one coordinate. It returns
// distance + 1, so that 0 still means "not reached".
func project(tables [][]int32, size int) []uint8 {
	distances := make([]uint8, size)
	distances[0] = 1
	level := []int32{0}
	for depth := uint8(1); len(level) > 0; depth++ {
		var next []int32
		for _, node := range level {
			for _, table := range tables {
				if child := table[node]; distances[child] == 0 {
					distances[child] = depth + 1
					next = append(next, child)
				}
			}
		}
		level = next
	}
	return distances
}

// Bound is an admissible lower bound on the moves left from an encoded state.
func (d *PatternDB) Bound(index int32) int {
	p := d.perm[index/coords.NOrientations]
	o := d.orient[index%coords.NOrientations]
	return int(max(p, o)) - 1
}

// Reach is the largest bound each database can ever return — the measure of how
// much pruning power there is to be had.
func (d *PatternDB) Reach() (permutation, orientation int) {
	var p, o uint8
	for _, v := range d.perm {
		p = max(p, v)
	}
	for _, v := range d.orient {
		o = max(o, v)
	}
	return int(p) - 1, int(o) - 1
}

// Solve runs iterative-deepening A*: depth-first passes with a rising limit on
// g + h, where h is [PatternDB.Bound]. Optimal, but see [PatternDB].
func (d *PatternDB) Solve(s cube.State) (string, error) {
	index, err := coords.Encode(s)
	if err != nil {
		return "", err
	}
	if index == coords.SolvedIndex {
		return "", nil
	}
	path := make([]byte, 0, MaxDepth+1)
	for limit := d.Bound(index); limit <= MaxDepth; limit++ {
		if found, ok := d.descend(index, 0, limit, path, -1); ok {
			return string(found), nil
		}
	}
	return "", fmt.Errorf("%w: no solution within %d moves", cube.ErrUnsolvable, MaxDepth)
}

func (d *PatternDB) descend(index int32, depth, limit int, path []byte, last int) ([]byte, bool) {
	q, r := index/coords.NOrientations, index%coords.NOrientations
	if depth+d.Bound(index) > limit {
		return path, false
	}
	if index == coords.SolvedIndex {
		return path, true
	}
	for m, table := range coords.MoveTables {
		// cube.SolveMoves pairs each turn with its inverse at adjacent indices,
		// so m^1 is the move that would undo the previous one.
		if m^1 == last {
			continue
		}
		child := table.Wide[q] + table.Orient[r]
		if found, ok := d.descend(child, depth+1, limit, append(path, table.Move), m); ok {
			return found, true
		}
	}
	return path, false
}
