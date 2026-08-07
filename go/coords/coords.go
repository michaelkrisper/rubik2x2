// Package coords is the compact state encoding every solver runs on.
//
// Package cube models a state as 24 stickers, which is what a human reads but
// not what a search wants. A pocket cube with one corner held still is
// described by far less: which of the 7 free corner pieces sits in each of the
// 7 free slots (7! = 5040 arrangements) and how each is twisted (3^7, minus the
// constraint that the twists sum to a multiple of 3, so 3^6 = 729). That is
// 5040 * 729 = 3,674,160 states — exactly what an exhaustive search over
// stickers finds.
//
// The payoff is that a move acts on the two halves independently:
//
//	new_permutation[slot] = old_permutation[source[slot]]
//	new_orientation[slot] = old_orientation[source[slot]] + twist[slot]  (mod 3)
//
// Neither half depends on the other, so a move is two lookups in tables of 5040
// and 729 entries and a whole state is a single int32 below [NStates]. That
// dense bounded index is what makes the full distance table possible at all,
// and what hands IDA* its pattern databases for free.
package coords

import (
	"fmt"

	"github.com/michaelkrisper/rubik2x2/go/cube"
)

// Sizes of the two coordinates and of the space they span.
const (
	// Free is the number of corner slots that actually move.
	Free = 7
	// NOrientations is 3^(Free-1): slot 6's twist is implied by the other six.
	NOrientations = 729
	// NPermutations is Free!.
	NPermutations = 5040
	// NStates is the whole reachable state space.
	NStates = NPermutations * NOrientations
	// SolvedIndex is the identity permutation with no twists, which ranks 0.
	SolvedIndex int32 = 0
)

// Slots lists each corner slot as its three facelets, in a consistent
// rotational sense so that "twist" means the same thing at every slot. The
// order is (x-face, y-face, z-face) where sign(x)*sign(y)*sign(z) == +1 and
// (x-face, z-face, y-face) otherwise, which makes all eight triples agree in
// handedness. Slot 7 is the corner the solve moves never touch.
var Slots = [8][3]int{
	{16, 0, 14},  // left-up-back
	{21, 15, 1},  // right-up-back
	{17, 4, 2},   // left-up-front
	{20, 3, 5},   // right-up-front
	{19, 8, 6},   // left-down-front
	{22, 7, 9},   // right-down-front
	{18, 12, 10}, // left-down-back
	{23, 11, 13}, // right-down-back — fixed
}

// Pieces holds the canonical colour triple of each corner piece, read off a
// solved cube. A piece is identified by what its home slot shows when solved.
type Pieces [8][3]uint8

// PiecesOf reads the eight piece colour triples off a solved cube.
func PiecesOf(goal cube.State) Pieces {
	var pieces Pieces
	for slot, facelets := range Slots {
		for i, facelet := range facelets {
			pieces[slot][i] = goal[facelet]
		}
	}
	return pieces
}

// Canonical is [PiecesOf] applied to the standard solved cube.
var Canonical = PiecesOf(cube.Solved)

// A cubie is what a slot holds: which piece, and how far it is rotated.
type cubie struct{ piece, twist uint8 }

// goal is one of the 24 solved cubes, with a reverse index from a colour triple
// to the (piece, twist) that shows it.
type goal struct {
	state  cube.State
	pieces Pieces
	cubies map[[3]uint8]cubie
}

// goals is keyed by the fixed corner's three colours. A cube may be handed over
// in any of the 24 orientations; that corner identifies which rotation of the
// solved cube the scramble is aiming at, so a cube held any which way still
// encodes correctly and the goal index is always 0.
var goals = buildGoals()

func buildGoals() map[[3]uint8]*goal {
	out := make(map[[3]uint8]*goal, 24)
	for _, state := range cube.Orientations(cube.Solved) {
		pieces := PiecesOf(state)
		cubies := make(map[[3]uint8]cubie, 24)
		for piece, canonical := range pieces {
			for twist := range 3 {
				cubies[rotate(canonical, twist)] = cubie{uint8(piece), uint8(twist)}
			}
		}
		key := [3]uint8{state[cube.FixedCorner[0]], state[cube.FixedCorner[1]], state[cube.FixedCorner[2]]}
		out[key] = &goal{state: state, pieces: pieces, cubies: cubies}
	}
	return out
}

// rotate turns a colour triple left by twist positions.
func rotate(colours [3]uint8, twist int) [3]uint8 {
	return [3]uint8{colours[twist%3], colours[(twist+1)%3], colours[(twist+2)%3]}
}

func goalFor(s cube.State) (*goal, error) {
	key := [3]uint8{s[cube.FixedCorner[0]], s[cube.FixedCorner[1]], s[cube.FixedCorner[2]]}
	g, ok := goals[key]
	if !ok {
		return nil, fmt.Errorf("%w: fixed-corner colours %q occur on no solved cube",
			cube.ErrUnsolvable, colourString(key))
	}
	return g, nil
}

func colourString(colours [3]uint8) string {
	return string([]byte{cube.ColorChars[colours[0]], cube.ColorChars[colours[1]], cube.ColorChars[colours[2]]})
}

// GoalFor returns the solved cube whose fixed corner matches s's fixed corner —
// the orientation this state is to be solved into.
func GoalFor(s cube.State) (cube.State, error) {
	g, err := goalFor(s)
	if err != nil {
		return cube.State{}, err
	}
	return g.state, nil
}

// Cubies splits a sticker state into (piece per slot, twist per slot) for the
// seven free slots, reading the goal orientation off the fixed corner.
func Cubies(s cube.State) (perm, orient [Free]uint8, err error) {
	g, err := goalFor(s)
	if err != nil {
		return perm, orient, err
	}
	return g.cubies7(s)
}

func (g *goal) cubies7(s cube.State) (perm, orient [Free]uint8, err error) {
	var seen uint8
	var twistSum int
	for slot := range Free {
		facelets := Slots[slot]
		colours := [3]uint8{s[facelets[0]], s[facelets[1]], s[facelets[2]]}
		c, ok := g.cubies[colours]
		if !ok || c.piece >= Free {
			return perm, orient, fmt.Errorf("%w: facelets %v show %q, which is no free corner",
				cube.ErrUnsolvable, facelets, colourString(colours))
		}
		if seen&(1<<c.piece) != 0 {
			return perm, orient, fmt.Errorf("%w: corner %q occurs twice",
				cube.ErrUnsolvable, colourString(colours))
		}
		seen |= 1 << c.piece
		perm[slot] = c.piece
		orient[slot] = c.twist
		twistSum += int(c.twist)
	}
	// With the eighth corner held in place its twist is 0, so the other seven
	// must cancel out; anything else is a corner twisted in the hand.
	if twistSum%3 != 0 {
		return perm, orient, fmt.Errorf("%w: corner twists sum to %d, not a multiple of 3",
			cube.ErrUnsolvable, twistSum)
	}
	return perm, orient, nil
}

// FromCubies rebuilds a sticker state from pieces and twists. Inverse of
// [Cubies] for states in the canonical solved orientation.
func FromCubies(perm, orient [Free]uint8, pieces Pieces) cube.State {
	var s cube.State
	for slot := range Free {
		colours := rotate(pieces[perm[slot]], int(orient[slot]))
		for i, facelet := range Slots[slot] {
			s[facelet] = colours[i]
		}
	}
	for i, facelet := range Slots[Free] {
		s[facelet] = pieces[Free][i]
	}
	return s
}

var factorial = [Free + 1]int32{1, 1, 2, 6, 24, 120, 720, 5040}

// rankPermutation is the Lehmer code: any bijection onto 0..5039 would do, as
// long as the identity ranks 0.
func rankPermutation(p [Free]uint8) int32 {
	var rank int32
	for i := range Free {
		var smaller int32
		for j := i + 1; j < Free; j++ {
			if p[j] < p[i] {
				smaller++
			}
		}
		rank += smaller * factorial[Free-1-i]
	}
	return rank
}

func unrankPermutation(rank int32) [Free]uint8 {
	var available [Free]uint8
	for i := range Free {
		available[i] = uint8(i)
	}
	var p [Free]uint8
	left := Free
	for i := range Free {
		f := factorial[Free-1-i]
		k := rank / f
		rank %= f
		p[i] = available[k]
		copy(available[k:left-1], available[k+1:left])
		left--
	}
	return p
}

func rankOrientation(orient [Free]uint8) int32 {
	var rank int32
	for i := Free - 2; i >= 0; i-- {
		rank = rank*3 + int32(orient[i])
	}
	return rank
}

func unrankOrientation(rank int32) [Free]uint8 {
	var orient [Free]uint8
	sum := 0
	for i := range Free - 1 {
		orient[i] = uint8(rank % 3)
		sum += int(orient[i])
		rank /= 3
	}
	orient[Free-1] = uint8((3 - sum%3) % 3) // twists sum to 0 mod 3
	return orient
}

// Encode maps a sticker state to its dense index below [NStates]. All 24 ways
// of holding the same scramble encode to the same index, and a solved cube in
// any orientation encodes to [SolvedIndex].
func Encode(s cube.State) (int32, error) {
	perm, orient, err := Cubies(s)
	if err != nil {
		return 0, err
	}
	return rankPermutation(perm)*NOrientations + rankOrientation(orient), nil
}

// Decode is the inverse of [Encode], in the canonical solved orientation.
func Decode(index int32) cube.State {
	return FromCubies(
		unrankPermutation(index/NOrientations),
		unrankOrientation(index%NOrientations),
		Canonical,
	)
}

// A MoveTable is one solve move's action on the two coordinates, as lookups.
type MoveTable struct {
	// Move is the move's letter.
	Move byte
	// Perm maps a permutation rank to a permutation rank.
	Perm []int32
	// Wide is Perm pre-multiplied by [NOrientations], so that a child index is
	// Wide[index/NOrientations] + Orient[index%NOrientations] — one add rather
	// than a multiply and an add, in a loop that runs tens of millions of times.
	Wide []int32
	// Orient maps an orientation rank to an orientation rank.
	Orient []int32
}

// MoveTables holds one table per move in [cube.SolveMoves], in that order, so
// that index^1 is the move that undoes index. Built at startup in a couple of
// milliseconds; every solver shares them.
var MoveTables = buildMoveTables()

func buildMoveTables() []MoveTable {
	var identity [Free]uint8
	for i := range identity {
		identity[i] = uint8(i)
	}
	solved := FromCubies(identity, [Free]uint8{}, Canonical)
	canonicalGoal := goals[[3]uint8{
		cube.Solved[cube.FixedCorner[0]],
		cube.Solved[cube.FixedCorner[1]],
		cube.Solved[cube.FixedCorner[2]],
	}]

	tables := make([]MoveTable, len(cube.SolveMoves))
	for m := range len(cube.SolveMoves) {
		move := cube.SolveMoves[m]
		turned, err := cube.Apply(solved, move)
		if err != nil {
			panic(err) // the solve moves are compiled in; a failure here is a bug
		}
		// On the identity state the permutation *is* the slot-to-slot mapping
		// and the orientation *is* the twist each slot picks up.
		source, twist, err := canonicalGoal.cubies7(turned)
		if err != nil {
			panic(err)
		}

		table := MoveTable{
			Move:   move,
			Perm:   make([]int32, NPermutations),
			Wide:   make([]int32, NPermutations),
			Orient: make([]int32, NOrientations),
		}
		for rank := range int32(NPermutations) {
			old := unrankPermutation(rank)
			var next [Free]uint8
			for slot := range Free {
				next[slot] = old[source[slot]]
			}
			table.Perm[rank] = rankPermutation(next)
			table.Wide[rank] = table.Perm[rank] * NOrientations
		}
		for rank := range int32(NOrientations) {
			old := unrankOrientation(rank)
			var next [Free]uint8
			for slot := range Free {
				next[slot] = (old[source[slot]] + twist[slot]) % 3
			}
			table.Orient[rank] = rankOrientation(next)
		}
		tables[m] = table
	}
	return tables
}

// Apply turns one face of an encoded state. Solvers inline this; it exists for
// readability and for the tests that check it against the sticker model.
func Apply(index int32, move byte) (int32, error) {
	for _, table := range MoveTables {
		if table.Move == move {
			return table.Wide[index/NOrientations] + table.Orient[index%NOrientations], nil
		}
	}
	return 0, fmt.Errorf("move %q is not one of the solve moves %s", string(move), cube.SolveMoves)
}
