package solver_test

import (
	"path/filepath"
	"testing"

	"github.com/michaelkrisper/rubik2x2/go/bench"
	"github.com/michaelkrisper/rubik2x2/go/cube"
	"github.com/michaelkrisper/rubik2x2/go/solver"
)

// The shared cross-language workload; the antipodal position is appended so the
// worst case is always part of what a run measures.
func benchStates(b *testing.B) []cube.State {
	b.Helper()
	states, err := bench.LoadScrambles(filepath.Join("..", "..", "bench", "scrambles.txt"))
	if err != nil {
		b.Fatalf("LoadScrambles: %v", err)
	}
	hardest, err := cube.Parse(bench.Hardest)
	if err != nil {
		b.Fatal(err)
	}
	return append(states, hardest)
}

var benchSink string

func BenchmarkBFS(b *testing.B) {
	states := benchStates(b)
	b.ResetTimer()
	for i := 0; b.Loop(); i++ {
		benchSink, _ = solver.BFS(states[i%len(states)])
	}
}

func BenchmarkBFSHardest(b *testing.B) {
	hardest, _ := cube.Parse(bench.Hardest)
	for b.Loop() {
		benchSink, _ = solver.BFS(hardest)
	}
}

func BenchmarkIDA(b *testing.B) {
	states := benchStates(b)
	db := solver.NewPatternDB()
	b.ResetTimer()
	for i := 0; b.Loop(); i++ {
		benchSink, _ = db.Solve(states[i%len(states)])
	}
}

func BenchmarkIDAHardest(b *testing.B) {
	hardest, _ := cube.Parse(bench.Hardest)
	db := solver.NewPatternDB()
	b.ResetTimer()
	for b.Loop() {
		benchSink, _ = db.Solve(hardest)
	}
}

var tableSink *solver.Table

func BenchmarkTableBuild(b *testing.B) {
	for b.Loop() {
		tableSink = solver.NewTable()
	}
}

func BenchmarkTableBuildParallel(b *testing.B) {
	for b.Loop() {
		tableSink = solver.NewTableParallel(0)
	}
}
