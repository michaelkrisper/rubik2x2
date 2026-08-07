package main

import (
	"bytes"
	"strings"
	"testing"

	"github.com/michaelkrisper/rubik2x2/go/cube"
)

const antipodal = "wppryygrygrwgprbbbwpbwyg"

func TestSolvePrintsOnlyTheMoves(t *testing.T) {
	for _, algorithm := range []string{"bfs", "ida", "table"} {
		t.Run(algorithm, func(t *testing.T) {
			if algorithm == "table" && testing.Short() {
				t.Skip("builds the full distance table; run without -short")
			}
			var out bytes.Buffer
			if err := run([]string{"solve", "--algorithm", algorithm, antipodal}, &out); err != nil {
				t.Fatalf("run: %v", err)
			}
			solution := strings.TrimSpace(out.String())
			if len(solution) != 14 {
				t.Fatalf("got %q (%d moves), want 14", solution, len(solution))
			}
			state, err := cube.Parse(antipodal)
			if err != nil {
				t.Fatal(err)
			}
			solved, err := cube.ApplyMoves(state, solution)
			if err != nil || !solved.IsSolved() {
				t.Errorf("%q does not solve the antipodal position", solution)
			}
		})
	}
}

func TestCommandErrors(t *testing.T) {
	tests := []struct {
		name string
		args []string
	}{
		{"unknown command", []string{"frobnicate"}},
		{"missing state", []string{"solve"}},
		{"bad state", []string{"solve", "nonsense"}},
		{"unknown algorithm", []string{"solve", antipodal, "--algorithm", "magic"}},
		{"unknown flag", []string{"scramble", "--nope"}},
		{"bad seed", []string{"scramble", "--seed", "abc"}},
		{"missing scramble file", []string{"bench", "--scrambles", "/nonexistent/file"}},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if err := run(test.args, new(bytes.Buffer)); err == nil {
				t.Errorf("run(%q) should have failed", test.args)
			}
		})
	}
}

func TestScrambleIsReproducible(t *testing.T) {
	var first, second bytes.Buffer
	for _, out := range []*bytes.Buffer{&first, &second} {
		if err := run([]string{"scramble", "-n", "10", "--seed", "42"}, out); err != nil {
			t.Fatalf("run: %v", err)
		}
	}
	if first.String() != second.String() {
		t.Errorf("same seed gave %q and %q", first.String(), second.String())
	}
	if _, err := cube.Parse(first.String()); err != nil {
		t.Errorf("scramble printed something unparseable: %v", err)
	}
}

func TestShow(t *testing.T) {
	var out bytes.Buffer
	if err := run([]string{"show", "--no-color", cube.Solved.String()}, &out); err != nil {
		t.Fatalf("run: %v", err)
	}
	if !strings.Contains(out.String(), "FRONT") {
		t.Errorf("show did not draw a net:\n%s", out.String())
	}
}

func TestUsage(t *testing.T) {
	var out bytes.Buffer
	if err := run(nil, &out); err != nil {
		t.Fatalf("run: %v", err)
	}
	if !strings.Contains(out.String(), "rubik2x2 solve") {
		t.Errorf("no usage printed:\n%s", out.String())
	}
}

func TestTablePrintsTheHistogram(t *testing.T) {
	if testing.Short() {
		t.Skip("builds the full distance table; run without -short")
	}
	var out bytes.Buffer
	if err := run([]string{"table"}, &out); err != nil {
		t.Fatalf("run: %v", err)
	}
	const oeis = "1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508, " +
		"930588, 1350852, 782536, 90280, 276"
	if !strings.Contains(out.String(), oeis) {
		t.Errorf("histogram line missing:\n%s", out.String())
	}
}
