package solver

import (
	"fmt"
	"runtime"
	"sync"
	"sync/atomic"

	"github.com/michaelkrisper/rubik2x2/go/coords"
	"github.com/michaelkrisper/rubik2x2/go/cube"
)

// A Table is God's algorithm: the exact distance to solved of every one of the
// 3,674,160 reachable states, one byte each.
//
// Distances are stored as distance + 1, because the build needs 0 to mean "not
// yet reached". With the table in hand there is nothing left to search, only a
// table to read: at a state d moves from solved, exactly one neighbour is d-1
// away — step to it and repeat. A solve costs d lookups, constant in the
// difficulty of the scramble and optimal by construction.
//
// The table is built in memory and never cached to disk, so it always matches
// the code that produced it. Building costs one exhaustive breadth-first search
// over 22 million edges: instant solves, paid for up front.
type Table struct {
	distances []uint8
}

// NewTable builds the distance table with a single-threaded breadth-first
// search from the solved state.
//
// The six moves are unrolled and their lookup tables lifted into locals. That
// is not decoration: this loop runs 22 million times per build, and leaving the
// slice headers behind a struct field, to be reloaded per edge, measured 25%
// slower.
func NewTable() *Table {
	t := coords.MoveTables
	w0, o0 := t[0].Wide, t[0].Orient
	w1, o1 := t[1].Wide, t[1].Orient
	w2, o2 := t[2].Wide, t[2].Orient
	w3, o3 := t[3].Wide, t[3].Orient
	w4, o4 := t[4].Wide, t[4].Orient
	w5, o5 := t[5].Wide, t[5].Orient

	distances := make([]uint8, coords.NStates)
	distances[coords.SolvedIndex] = 1
	level := []int32{coords.SolvedIndex}

	for depth := uint8(1); len(level) > 0; depth++ {
		marker := depth + 1
		next := make([]int32, 0, len(level)*3)
		for _, node := range level {
			q, r := node/coords.NOrientations, node%coords.NOrientations
			if c := w0[q] + o0[r]; distances[c] == 0 {
				distances[c] = marker
				next = append(next, c)
			}
			if c := w1[q] + o1[r]; distances[c] == 0 {
				distances[c] = marker
				next = append(next, c)
			}
			if c := w2[q] + o2[r]; distances[c] == 0 {
				distances[c] = marker
				next = append(next, c)
			}
			if c := w3[q] + o3[r]; distances[c] == 0 {
				distances[c] = marker
				next = append(next, c)
			}
			if c := w4[q] + o4[r]; distances[c] == 0 {
				distances[c] = marker
				next = append(next, c)
			}
			if c := w5[q] + o5[r]; distances[c] == 0 {
				distances[c] = marker
				next = append(next, c)
			}
		}
		level = next
	}
	return &Table{distances}
}

// parallelCutoff is the frontier size below which spreading a level over
// goroutines costs more than it saves. The first five levels are tiny.
const parallelCutoff = 8192

// NewTableParallel builds the same table as [NewTable] with workers goroutines;
// workers <= 0 means [runtime.NumCPU]. The result is identical — breadth-first
// distances do not depend on the order within a level.
//
// The search is level-synchronous, so within a level the frontier splits
// cleanly across workers. The hazard is the shared visited state, where two
// workers can reach the same state at once, and this resolves it by claiming:
// a separate bitmap, one bit per state, is set with a lock-free
// compare-and-swap, and the worker that wins the bit owns the state — it alone
// writes the distance byte and keeps the state in its own next-level buffer.
// The buffers are concatenated at the barrier, in whatever order they finished.
//
// Go has no atomic operation on a byte, which rules out swapping directly on
// the distance array. Widening that array to []atomic.Uint32 would work but
// measured *slower* than the serial build: the visited set is what every one of
// the 22 million edges touches at random, and at 14.7 MB it falls out of cache.
// The bitmap is 460 KB, small enough to stay resident, and it is the only thing
// contended — the distance array is then written exactly once per state by its
// owner, so no lock and no race. See the README for what that measured.
func NewTableParallel(workers int) *Table {
	if workers <= 0 {
		workers = runtime.NumCPU()
	}
	t := coords.MoveTables
	w0, o0 := t[0].Wide, t[0].Orient
	w1, o1 := t[1].Wide, t[1].Orient
	w2, o2 := t[2].Wide, t[2].Orient
	w3, o3 := t[3].Wide, t[3].Orient
	w4, o4 := t[4].Wide, t[4].Orient
	w5, o5 := t[5].Wide, t[5].Orient

	distances := make([]uint8, coords.NStates)
	seen := make([]atomic.Uint32, (coords.NStates+31)/32)
	// claim reports whether this caller is the one that discovered the state.
	claim := func(state int32) bool {
		word, bit := &seen[state>>5], uint32(1)<<(uint32(state)&31)
		for {
			current := word.Load()
			if current&bit != 0 {
				return false
			}
			if word.CompareAndSwap(current, current|bit) {
				return true
			}
		}
	}

	claim(coords.SolvedIndex)
	distances[coords.SolvedIndex] = 1
	level := []int32{coords.SolvedIndex}
	buffers := make([][]int32, workers)

	for depth := uint8(1); len(level) > 0; depth++ {
		marker := depth + 1
		parts := max(1, min(workers, len(level)/parallelCutoff))

		var wg sync.WaitGroup
		for part := range parts {
			lo, hi := len(level)*part/parts, len(level)*(part+1)/parts
			wg.Add(1)
			go func() {
				defer wg.Done()
				buffer := buffers[part][:0] // reused across levels
				for _, node := range level[lo:hi] {
					q, r := node/coords.NOrientations, node%coords.NOrientations
					if c := w0[q] + o0[r]; claim(c) {
						distances[c] = marker
						buffer = append(buffer, c)
					}
					if c := w1[q] + o1[r]; claim(c) {
						distances[c] = marker
						buffer = append(buffer, c)
					}
					if c := w2[q] + o2[r]; claim(c) {
						distances[c] = marker
						buffer = append(buffer, c)
					}
					if c := w3[q] + o3[r]; claim(c) {
						distances[c] = marker
						buffer = append(buffer, c)
					}
					if c := w4[q] + o4[r]; claim(c) {
						distances[c] = marker
						buffer = append(buffer, c)
					}
					if c := w5[q] + o5[r]; claim(c) {
						distances[c] = marker
						buffer = append(buffer, c)
					}
				}
				buffers[part] = buffer
			}()
		}
		wg.Wait()

		total := 0
		for _, buffer := range buffers[:parts] {
			total += len(buffer)
		}
		next := make([]int32, 0, total)
		for _, buffer := range buffers[:parts] {
			next = append(next, buffer...)
		}
		level = next
	}
	return &Table{distances}
}

// Len is the number of states in the table.
func (t *Table) Len() int { return len(t.distances) }

// Distance returns the quarter turns from an encoded state to solved.
func (t *Table) Distance(index int32) int { return int(t.distances[index]) - 1 }

// Histogram counts states per distance, index 0 being the solved state. It must
// equal OEIS A079762; the tests assert exactly that, and no incorrect move
// table or encoding could reproduce it.
func (t *Table) Histogram() []int {
	counts := make([]int, 0, MaxDepth+1)
	for _, d := range t.distances {
		if d == 0 {
			continue // never reached; impossible for a complete build
		}
		for int(d) > len(counts) {
			counts = append(counts, 0)
		}
		counts[d-1]++
	}
	return counts
}

// Solve walks downhill: at distance d exactly one neighbour is at d-1, so each
// move is one lookup per candidate turn.
func (t *Table) Solve(s cube.State) (string, error) {
	index, err := coords.Encode(s)
	if err != nil {
		return "", err
	}
	remaining := t.distances[index]
	path := make([]byte, 0, MaxDepth)
	for remaining > 1 {
		q, r := index/coords.NOrientations, index%coords.NOrientations
		stepped := false
		for _, table := range coords.MoveTables {
			child := table.Wide[q] + table.Orient[r]
			if t.distances[child] == remaining-1 {
				path = append(path, table.Move)
				index = child
				remaining--
				stepped = true
				break
			}
		}
		if !stepped {
			return "", fmt.Errorf("distance table is inconsistent at index %d", index)
		}
	}
	return string(path), nil
}
