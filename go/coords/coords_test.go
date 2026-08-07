package coords_test

import (
	"errors"
	"math/rand/v2"
	"testing"

	"github.com/michaelkrisper/rubik2x2/go/coords"
	"github.com/michaelkrisper/rubik2x2/go/cube"
)

// scrambled returns a state reachable with the six solve moves, which is the
// family the encoding is defined on: it leaves the fixed corner alone.
func scrambled(t *testing.T, rng *rand.Rand, moves int) cube.State {
	t.Helper()
	s := cube.Solved
	for range moves {
		var err error
		if s, err = cube.Apply(s, cube.SolveMoves[rng.IntN(len(cube.SolveMoves))]); err != nil {
			t.Fatal(err)
		}
	}
	return s
}

func TestSolvedEncodesToZero(t *testing.T) {
	for _, rotated := range cube.Orientations(cube.Solved) {
		index, err := coords.Encode(rotated)
		if err != nil {
			t.Fatalf("Encode(%v): %v", rotated, err)
		}
		if index != coords.SolvedIndex {
			t.Errorf("Encode(%v) = %d, want %d", rotated, index, coords.SolvedIndex)
		}
	}
}

func TestEncodeDecodeRoundTrip(t *testing.T) {
	rng := rand.New(rand.NewPCG(1, 2))
	for range 500 {
		state := scrambled(t, rng, 14)
		index, err := coords.Encode(state)
		if err != nil {
			t.Fatalf("Encode(%v): %v", state, err)
		}
		if index < 0 || index >= coords.NStates {
			t.Fatalf("Encode(%v) = %d, out of range", state, index)
		}
		if got := coords.Decode(index); got != state {
			t.Fatalf("Decode(Encode(%v)) = %v", state, got)
		}
	}
}

func TestDecodeEncodeRoundTrip(t *testing.T) {
	rng := rand.New(rand.NewPCG(3, 4))
	for range 500 {
		index := rng.Int32N(coords.NStates)
		state := coords.Decode(index)
		got, err := coords.Encode(state)
		if err != nil {
			t.Fatalf("Encode(Decode(%d)): %v", index, err)
		}
		if got != index {
			t.Fatalf("Encode(Decode(%d)) = %d", index, got)
		}
	}
}

// TestAnyOrientationEncodes is the point of looking the goal up from the fixed
// corner. A cube may be handed over in any of the 24 ways; each is a different
// labelling of the same physical position, so the indices differ — what must
// hold is that every one of them encodes at all, against the solved cube it is
// actually aiming at. (That the *distance* is the same in every orientation is
// checked where a distance table exists, in package solver.)
func TestAnyOrientationEncodes(t *testing.T) {
	rng := rand.New(rand.NewPCG(5, 6))
	for range 50 {
		state := scrambled(t, rng, 11)
		for _, rotated := range cube.Orientations(state) {
			index, err := coords.Encode(rotated)
			if err != nil {
				t.Fatalf("Encode(%v): %v", rotated, err)
			}
			if index < 0 || index >= coords.NStates {
				t.Fatalf("Encode(%v) = %d, out of range", rotated, index)
			}
			goal, err := coords.GoalFor(rotated)
			if err != nil {
				t.Fatal(err)
			}
			if !goal.IsSolved() {
				t.Errorf("GoalFor(%v) = %v, which is not solved", rotated, goal)
			}
		}
	}
}

// TestMoveTablesMatchTheStickerModel is the check that keeps the two layers
// honest: turning a face and then encoding must equal encoding and then looking
// the move up.
func TestMoveTablesMatchTheStickerModel(t *testing.T) {
	rng := rand.New(rand.NewPCG(7, 8))
	for range 200 {
		state := scrambled(t, rng, 9)
		index, err := coords.Encode(state)
		if err != nil {
			t.Fatal(err)
		}
		for i := range len(cube.SolveMoves) {
			move := cube.SolveMoves[i]
			turned, err := cube.Apply(state, move)
			if err != nil {
				t.Fatal(err)
			}
			want, err := coords.Encode(turned)
			if err != nil {
				t.Fatal(err)
			}
			got, err := coords.Apply(index, move)
			if err != nil {
				t.Fatal(err)
			}
			if got != want {
				t.Fatalf("move %q on %v: table gave %d, stickers gave %d",
					string(move), state, got, want)
			}
		}
	}
}

func TestMoveTablesArePairedWithTheirInverses(t *testing.T) {
	for i := range len(cube.SolveMoves) {
		if got, want := coords.MoveTables[i].Move, cube.SolveMoves[i]; got != want {
			t.Fatalf("MoveTables[%d].Move = %q, want %q", i, string(got), string(want))
		}
	}
	// The IDA* pruning relies on index^1 being the undoing move.
	rng := rand.New(rand.NewPCG(9, 10))
	for range 100 {
		index := rng.Int32N(coords.NStates)
		for i := range coords.MoveTables {
			once, err := coords.Apply(index, coords.MoveTables[i].Move)
			if err != nil {
				t.Fatal(err)
			}
			back, err := coords.Apply(once, coords.MoveTables[i^1].Move)
			if err != nil {
				t.Fatal(err)
			}
			if back != index {
				t.Fatalf("moves %d and %d are not inverses", i, i^1)
			}
		}
	}
}

func TestEncodeRejectsUnsolvable(t *testing.T) {
	twisted := cube.Solved
	// Rotate the stickers of one free corner in place: colours still count out,
	// but no sequence of turns produces it.
	slot := coords.Slots[0]
	twisted[slot[0]], twisted[slot[1]], twisted[slot[2]] =
		cube.Solved[slot[1]], cube.Solved[slot[2]], cube.Solved[slot[0]]

	mirrored := cube.Solved
	// Swap two of the fixed corner's stickers. The colours are still those of a
	// real corner, but in the mirrored order, which no rotation of a solved cube
	// ever shows at that slot.
	mirrored[cube.FixedCorner[1]], mirrored[cube.FixedCorner[2]] =
		mirrored[cube.FixedCorner[2]], mirrored[cube.FixedCorner[1]]

	tests := []struct {
		name  string
		state cube.State
	}{
		{"twisted corner", twisted},
		{"impossible fixed corner", mirrored},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if _, err := coords.Encode(test.state); !errors.Is(err, cube.ErrUnsolvable) {
				t.Errorf("Encode(%v) error = %v, want ErrUnsolvable", test.state, err)
			}
		})
	}
}

func TestStateSpaceSize(t *testing.T) {
	if coords.NStates != 3674160 {
		t.Errorf("NStates = %d, want 3674160", coords.NStates)
	}
	if coords.NPermutations*coords.NOrientations != coords.NStates {
		t.Error("NStates must be NPermutations * NOrientations")
	}
}
