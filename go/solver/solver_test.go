package solver_test

import (
	"errors"
	"math/rand/v2"
	"sync"
	"testing"

	"github.com/michaelkrisper/rubik2x2/go/coords"
	"github.com/michaelkrisper/rubik2x2/go/cube"
	"github.com/michaelkrisper/rubik2x2/go/solver"
)

// hardest is an antipodal position: 14 quarter turns from solved.
const hardest = "wppryygrygrwgprbbbwpbwyg"

// oeis is A079762, the published number of pocket cube positions at each
// distance in the quarter-turn metric.
var oeis = []int{
	1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508,
	930588, 1350852, 782536, 90280, 276,
}

func scrambles(n, moves int, seed uint64) []cube.State {
	rng := rand.New(rand.NewPCG(seed, 0))
	states := make([]cube.State, n)
	for i := range states {
		s := cube.Solved
		for range moves {
			s, _ = cube.Apply(s, cube.SolveMoves[rng.IntN(len(cube.SolveMoves))])
		}
		states[i] = s
	}
	return states
}

// table is built at most once for the whole package: it is both the reference
// for optimality and one of the algorithms under test.
var table = sync.OnceValue(solver.NewTable)

// TestHistogramMatchesOEIS is the single strongest check available: no
// incorrect move table or encoding could reproduce this distribution. It builds
// the whole 3.7 M state table, so -short skips it.
func TestHistogramMatchesOEIS(t *testing.T) {
	if testing.Short() {
		t.Skip("builds the full distance table; run without -short")
	}
	got := table().Histogram()
	if len(got) != len(oeis) {
		t.Fatalf("maximum distance %d, want %d", len(got)-1, len(oeis)-1)
	}
	total := 0
	for depth, count := range got {
		if count != oeis[depth] {
			t.Errorf("depth %d: %d states, want %d", depth, count, oeis[depth])
		}
		total += count
	}
	if total != coords.NStates {
		t.Errorf("histogram sums to %d, want %d", total, coords.NStates)
	}
}

func TestParallelBuildMatchesSerial(t *testing.T) {
	if testing.Short() {
		t.Skip("builds the full distance table; run without -short")
	}
	for _, workers := range []int{1, 3, 8} {
		parallel := solver.NewTableParallel(workers)
		for index := range int32(coords.NStates) {
			if got, want := parallel.Distance(index), table().Distance(index); got != want {
				t.Fatalf("workers=%d: distance at %d = %d, want %d", workers, index, got, want)
			}
		}
	}
}

// TestAlgorithmsAgree is the cross-check the specification asks for: all three
// must return a solution of the same, optimal length, and it must actually
// solve the cube.
func TestAlgorithmsAgree(t *testing.T) {
	databases := solver.NewPatternDB()
	algorithms := []struct {
		name  string
		solve solver.Solver
	}{
		{"bfs", solver.BFS},
		{"ida", databases.Solve},
		{"table", table().Solve},
	}

	states := scrambles(40, 14, 11)
	antipodal, err := cube.Parse(hardest)
	if err != nil {
		t.Fatal(err)
	}
	states = append(states, antipodal, cube.Solved)

	for _, state := range states {
		index, err := coords.Encode(state)
		if err != nil {
			t.Fatalf("Encode(%v): %v", state, err)
		}
		want := table().Distance(index)
		for _, algorithm := range algorithms {
			solution, err := algorithm.solve(state)
			if err != nil {
				t.Fatalf("%s solving %v: %v", algorithm.name, state, err)
			}
			if len(solution) != want {
				t.Errorf("%s solved %v in %d moves (%q), optimum is %d",
					algorithm.name, state, len(solution), solution, want)
			}
			solved, err := cube.ApplyMoves(state, solution)
			if err != nil {
				t.Fatalf("%s produced %q: %v", algorithm.name, solution, err)
			}
			if !solved.IsSolved() {
				t.Errorf("%s solution %q does not solve %v", algorithm.name, solution, state)
			}
		}
	}
}

func TestAntipodalNeedsFourteenMoves(t *testing.T) {
	state, err := cube.Parse(hardest)
	if err != nil {
		t.Fatal(err)
	}
	index, err := coords.Encode(state)
	if err != nil {
		t.Fatal(err)
	}
	if got := table().Distance(index); got != solver.MaxDepth {
		t.Errorf("table distance = %d, want %d", got, solver.MaxDepth)
	}
	for name, solve := range map[string]solver.Solver{
		"bfs":   solver.BFS,
		"ida":   solver.NewPatternDB().Solve,
		"table": table().Solve,
	} {
		solution, err := solve(state)
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if len(solution) != solver.MaxDepth {
			t.Errorf("%s returned %d moves (%q), want %d",
				name, len(solution), solution, solver.MaxDepth)
		}
	}
}

// TestOrientationDoesNotChangeTheAnswer holds a scramble every one of the 24
// ways round. The encoded index differs each time — it is a different labelling
// of the same position — but the cube is the same cube, so the solution must be
// the same length and must still solve it.
func TestOrientationDoesNotChangeTheAnswer(t *testing.T) {
	for _, state := range scrambles(5, 14, 21) {
		index, err := coords.Encode(state)
		if err != nil {
			t.Fatal(err)
		}
		want := table().Distance(index)
		for _, rotated := range cube.Orientations(state) {
			solution, err := table().Solve(rotated)
			if err != nil {
				t.Fatalf("solving %v: %v", rotated, err)
			}
			if len(solution) != want {
				t.Errorf("%v needs %d moves, its rotation %v needs %d",
					state, want, rotated, len(solution))
			}
			solved, err := cube.ApplyMoves(rotated, solution)
			if err != nil || !solved.IsSolved() {
				t.Errorf("%q does not solve %v", solution, rotated)
			}
		}
	}
}

func TestSolvedCubeNeedsNoMoves(t *testing.T) {
	for name, solve := range map[string]solver.Solver{
		"bfs":   solver.BFS,
		"ida":   solver.NewPatternDB().Solve,
		"table": table().Solve,
	} {
		solution, err := solve(cube.Solved)
		if err != nil || solution != "" {
			t.Errorf("%s on a solved cube returned %q, %v", name, solution, err)
		}
	}
}

func TestSolversRejectUnsolvable(t *testing.T) {
	twisted := cube.Solved
	slot := coords.Slots[0]
	twisted[slot[0]], twisted[slot[1]], twisted[slot[2]] =
		cube.Solved[slot[1]], cube.Solved[slot[2]], cube.Solved[slot[0]]

	for name, solve := range map[string]solver.Solver{
		"bfs":   solver.BFS,
		"ida":   solver.NewPatternDB().Solve,
		"table": table().Solve,
	} {
		if _, err := solve(twisted); !errors.Is(err, cube.ErrUnsolvable) {
			t.Errorf("%s error = %v, want ErrUnsolvable", name, err)
		}
	}
}

// TestPatternDatabaseReach documents why IDA* does badly here: the bound tops
// out well short of the 14 moves a hard position really needs.
func TestPatternDatabaseReach(t *testing.T) {
	permutation, orientation := solver.NewPatternDB().Reach()
	if permutation < 1 || orientation < 1 {
		t.Fatalf("reach = (%d, %d), the databases look empty", permutation, orientation)
	}
	if max(permutation, orientation) >= solver.MaxDepth {
		t.Errorf("reach = (%d, %d); a bound that tight would be surprising",
			permutation, orientation)
	}
	if got := solver.NewPatternDB().Bound(coords.SolvedIndex); got != 0 {
		t.Errorf("bound at the goal = %d, want 0", got)
	}
}
