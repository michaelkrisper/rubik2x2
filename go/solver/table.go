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
func NewTable() *Table {
	tables := coords.MoveTables
	distances := make([]uint8, coords.NStates)
	distances[coords.SolvedIndex] = 1
	level := []int32{coords.SolvedIndex}

	for depth := uint8(1); len(level) > 0; depth++ {
		marker := depth + 1
		next := make([]int32, 0, len(level)*3)
		for _, node := range level {
			q, r := node/coords.NOrientations, node%coords.NOrientations
			for m := range tables {
				child := tables[m].Wide[q] + tables[m].Orient[r]
				if distances[child] == 0 {
					distances[child] = marker
					next = append(next, child)
				}
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
// cleanly across workers; the hazard is the shared visited array, where two
// workers can reach the same state at once. This resolves it with a lock-free
// compare-and-swap per newly discovered state: the worker that wins the swap
// owns the state and puts it in its own next-level buffer, and the buffers are
// concatenated at the level boundary. Go has no atomic operation on a byte, so
// the build works in a []atomic.Uint32 — 14.7 MB instead of 3.7 MB, narrowed to
// bytes once at the end. That transient 4x is the price of keeping the
// claim-and-publish step parallel; the alternative, per-worker candidate
// buffers merged serially, keeps the byte array but hands roughly half the work
// (the check-and-set for eight million candidate edges at the widest level)
// back to a single thread.
func NewTableParallel(workers int) *Table {
	if workers <= 0 {
		workers = runtime.NumCPU()
	}
	tables := coords.MoveTables
	claimed := make([]atomic.Uint32, coords.NStates)
	claimed[coords.SolvedIndex].Store(1)
	level := []int32{coords.SolvedIndex}

	for depth := uint32(1); len(level) > 0; depth++ {
		marker := depth + 1
		parts := max(1, min(workers, len(level)/parallelCutoff))
		buffers := make([][]int32, parts)

		var wg sync.WaitGroup
		for part := range parts {
			lo, hi := len(level)*part/parts, len(level)*(part+1)/parts
			wg.Add(1)
			go func() {
				defer wg.Done()
				buffer := make([]int32, 0, (hi-lo)*3)
				for _, node := range level[lo:hi] {
					q, r := node/coords.NOrientations, node%coords.NOrientations
					for m := range tables {
						child := tables[m].Wide[q] + tables[m].Orient[r]
						// The plain load filters out the overwhelming majority;
						// only a genuinely new state reaches the swap.
						if claimed[child].Load() == 0 && claimed[child].CompareAndSwap(0, marker) {
							buffer = append(buffer, child)
						}
					}
				}
				buffers[part] = buffer
			}()
		}
		wg.Wait()

		total := 0
		for _, buffer := range buffers {
			total += len(buffer)
		}
		next := make([]int32, 0, total)
		for _, buffer := range buffers {
			next = append(next, buffer...)
		}
		level = next
	}

	distances := make([]uint8, coords.NStates)
	for i := range distances {
		distances[i] = uint8(claimed[i].Load())
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
