package cube_test

import (
	"math/rand/v2"
	"strings"
	"testing"

	"github.com/michaelkrisper/rubik2x2/go/cube"
)

// probe is a state whose 24 "colours" are the facelet indices themselves. It is
// not a legal cube, but Apply only permutes, so turning the probe hands back the
// move's permutation table: probe'[i] == PERM[i]. That makes the algebra of the
// tables directly testable without exporting them.
func probe() cube.State {
	var s cube.State
	for i := range s {
		s[i] = uint8(i)
	}
	return s
}

func apply(t *testing.T, s cube.State, moves string) cube.State {
	t.Helper()
	out, err := cube.ApplyMoves(s, moves)
	if err != nil {
		t.Fatalf("ApplyMoves(%q): %v", moves, err)
	}
	return out
}

func TestMovesHaveOrderFour(t *testing.T) {
	for i := range len(cube.MoveNames) {
		move := string(cube.MoveNames[i])
		t.Run(move, func(t *testing.T) {
			s := probe()
			for turn := 1; turn <= 4; turn++ {
				s = apply(t, s, move)
				if got, want := s == probe(), turn == 4; got != want {
					t.Fatalf("after %d turns of %q: identity = %v, want %v", turn, move, got, want)
				}
			}
		})
	}
}

func TestMoveAndInverseCancel(t *testing.T) {
	for i := range len(cube.MoveNames) {
		move := cube.MoveNames[i : i+1]
		t.Run(move, func(t *testing.T) {
			if got := apply(t, probe(), move+cube.Invert(move)); got != probe() {
				t.Errorf("%q then its inverse changed the cube", move)
			}
		})
	}
}

func TestOppositeFacesCommute(t *testing.T) {
	// The two turns of an opposite pair touch disjoint halves of the cube.
	pairs := []struct{ a, b string }{
		{"l", "r"}, {"l", "R"}, {"L", "r"}, {"L", "R"},
		{"t", "b"}, {"t", "B"}, {"T", "b"}, {"T", "B"},
		{"f", "a"}, {"f", "A"}, {"F", "a"}, {"F", "A"},
	}
	for _, pair := range pairs {
		t.Run(pair.a+pair.b, func(t *testing.T) {
			if apply(t, probe(), pair.a+pair.b) != apply(t, probe(), pair.b+pair.a) {
				t.Errorf("%q and %q do not commute", pair.a, pair.b)
			}
		})
	}
}

func TestSolveMovesFixExactlyTheOneCorner(t *testing.T) {
	fixed := map[int]bool{}
	for i := range 24 {
		fixed[i] = true
	}
	for i := range len(cube.SolveMoves) {
		turned := apply(t, probe(), cube.SolveMoves[i:i+1])
		for facelet := range 24 {
			if int(turned[facelet]) != facelet {
				delete(fixed, facelet)
			}
		}
	}
	want := map[int]bool{11: true, 13: true, 23: true}
	if len(fixed) != len(want) {
		t.Fatalf("solve moves fix %v, want %v", fixed, want)
	}
	for facelet := range want {
		if !fixed[facelet] {
			t.Errorf("facelet %d should be fixed by every solve move", facelet)
		}
	}
}

func TestInvert(t *testing.T) {
	tests := []struct{ in, want string }{
		{"", ""},
		{"l", "L"},
		{"lFtL", "lTfL"},
		{"aAbB", "bBaA"},
	}
	for _, test := range tests {
		if got := cube.Invert(test.in); got != test.want {
			t.Errorf("Invert(%q) = %q, want %q", test.in, got, test.want)
		}
	}
}

func TestParseRoundTrip(t *testing.T) {
	text := cube.Solved.String()
	if text != "yyyyrrrrwwwwppppbbbbgggg" {
		t.Fatalf("Solved.String() = %q", text)
	}
	got, err := cube.Parse(text)
	if err != nil {
		t.Fatalf("Parse: %v", err)
	}
	if got != cube.Solved {
		t.Errorf("Parse(%q) = %v, want the solved cube", text, got)
	}
	if _, err := cube.Parse("  " + strings.ToUpper(text) + "\n"); err != nil {
		t.Errorf("Parse should accept surrounding space and upper case: %v", err)
	}
}

func TestParseRejects(t *testing.T) {
	tests := []struct{ name, in string }{
		{"empty", ""},
		{"too short", "yyyyrrrrwwwwppppbbbbggg"},
		{"too long", "yyyyrrrrwwwwppppbbbbggggg"},
		{"unknown letter", "xyyyrrrrwwwwppppbbbbgggg"},
		{"wrong counts", "yyyyyrrrwwwwppppbbbbgggg"},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if _, err := cube.Parse(test.in); err == nil {
				t.Errorf("Parse(%q) should have failed", test.in)
			}
		})
	}
}

func TestApplyRejectsUnknownMove(t *testing.T) {
	if _, err := cube.Apply(cube.Solved, 'x'); err == nil {
		t.Error("Apply should reject an unknown move letter")
	}
	if _, err := cube.ApplyMoves(cube.Solved, "lFx"); err == nil {
		t.Error("ApplyMoves should reject an unknown move letter")
	}
}

func TestIsSolved(t *testing.T) {
	if !cube.Solved.IsSolved() {
		t.Error("the solved cube is not solved")
	}
	for _, rotated := range cube.Orientations(cube.Solved) {
		if !rotated.IsSolved() {
			t.Errorf("rotating a solved cube unsolved it: %v", rotated)
		}
	}
	if apply(t, cube.Solved, "l").IsSolved() {
		t.Error("one quarter turn should leave the cube unsolved")
	}
}

func TestOrientationsAreDistinct(t *testing.T) {
	seen := map[cube.State]bool{}
	for _, s := range cube.Orientations(apply(t, cube.Solved, "lFtL")) {
		seen[s] = true
	}
	if len(seen) != 24 {
		t.Errorf("got %d distinct orientations, want 24", len(seen))
	}
}

func TestScrambleIsReproducible(t *testing.T) {
	first := cube.Scramble(cube.Solved, 12, rand.New(rand.NewPCG(7, 0)))
	second := cube.Scramble(cube.Solved, 12, rand.New(rand.NewPCG(7, 0)))
	if first != second {
		t.Errorf("same seed gave %v and %v", first, second)
	}
	if first == cube.Solved {
		t.Error("a 12-move scramble should not be solved")
	}
}
