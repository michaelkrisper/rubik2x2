// Package cube models the 2x2x2 pocket cube as 24 stickers.
//
// A [State] is 24 sticker colour ids, one per facelet, ordered side by side:
// UP, FRONT, DOWN, BACK, LEFT, RIGHT. Within a side the four stickers are
// row-major
//
//	-------
//	|0  1 |
//	|2  3 |
//	-------
//
// so the flat index is side*4 + row*2 + column.
//
// Every move is a permutation table applied as new[i] = old[PERM[i]]. The
// tables are checked rather than trusted: the tests pin the algebra they must
// obey (each move has order four, inverses cancel, opposite faces commute), and
// building the distance table reproduces OEIS A079762 exactly, which no wrong
// table could do.
//
// This package is the readable model and nothing else — no search, no display.
// Package coords re-encodes states for the solvers; package display draws them.
package cube

import (
	"errors"
	"fmt"
	"math/rand/v2"
	"strings"
)

// A State is one cube: 24 sticker colour ids in the range 0..5.
type State [24]uint8

// The six sides, in the order a state string lists them.
const (
	Up = iota
	Front
	Down
	Back
	Left
	Right
)

// ColorChars maps a colour id to its letter: red, white, blue, yellow, purple,
// green. This puzzle uses purple where a standard cube has orange.
const ColorChars = "rwbypg"

// Colour ids.
const (
	Red = iota
	White
	Blue
	Yellow
	Purple
	Green
)

// Solved is the cube in the standard orientation: yellow up, red front.
var Solved = State{
	Yellow, Yellow, Yellow, Yellow,
	Red, Red, Red, Red,
	White, White, White, White,
	Purple, Purple, Purple, Purple,
	Blue, Blue, Blue, Blue,
	Green, Green, Green, Green,
}

// ErrUnsolvable marks a sticker arrangement that no sequence of turns can reach
// from a solved cube. Wrap it with %w; test for it with [errors.Is].
var ErrUnsolvable = errors.New("unsolvable cube")

// MoveNames lists every move letter. Lowercase and uppercase are inverse
// quarter turns of one face:
//
//	l/L  left column down/up      r/R  right column down/up
//	t/T  top layer left/right     b/B  bottom layer left/right
//	f/F  front face cw/ccw        a/A  back face cw/ccw
//
// Iterating this string is the one deterministic order over all moves.
const MoveNames = "lLrRtTbBfFaA"

// SolveMoves are the six quarter-turn generators every solver uses. A fourth
// face would be redundant: turning a face equals turning the opposite face plus
// a whole-cube rotation. Inverses sit at adjacent indices, which the IDA*
// pruning relies on, so keep them paired.
const SolveMoves = "lLtTfF"

// FixedCorner names the facelets that [SolveMoves] never touch — the
// down-back-right corner. Holding one corner still is what collapses the
// 24-fold whole-cube-rotation symmetry, and that is what the compact encoding
// in package coords is built on.
var FixedCorner = [3]int{11, 13, 23}

// A permutation of the 24 facelets.
type permutation = [24]uint8

var movePerms = map[byte]permutation{
	'l': {12, 1, 14, 3, 0, 5, 2, 7, 4, 9, 6, 11, 8, 13, 10, 15, 18, 16, 19, 17, 20, 21, 22, 23},
	'L': {4, 1, 6, 3, 8, 5, 10, 7, 12, 9, 14, 11, 0, 13, 2, 15, 17, 19, 16, 18, 20, 21, 22, 23},
	'r': {0, 13, 2, 15, 4, 1, 6, 3, 8, 5, 10, 7, 12, 9, 14, 11, 16, 17, 18, 19, 21, 23, 20, 22},
	'R': {0, 5, 2, 7, 4, 9, 6, 11, 8, 13, 10, 15, 12, 1, 14, 3, 16, 17, 18, 19, 22, 20, 23, 21},
	't': {2, 0, 3, 1, 20, 21, 6, 7, 8, 9, 10, 11, 12, 13, 17, 16, 4, 5, 18, 19, 15, 14, 22, 23},
	'T': {1, 3, 0, 2, 16, 17, 6, 7, 8, 9, 10, 11, 12, 13, 21, 20, 15, 14, 18, 19, 4, 5, 22, 23},
	'b': {0, 1, 2, 3, 4, 5, 22, 23, 9, 11, 8, 10, 19, 18, 14, 15, 16, 17, 6, 7, 20, 21, 13, 12},
	'B': {0, 1, 2, 3, 4, 5, 18, 19, 10, 8, 11, 9, 23, 22, 14, 15, 16, 17, 13, 12, 20, 21, 6, 7},
	'f': {0, 1, 19, 17, 6, 4, 7, 5, 22, 20, 10, 11, 12, 13, 14, 15, 16, 8, 18, 9, 2, 21, 3, 23},
	'F': {0, 1, 20, 22, 5, 7, 4, 6, 17, 19, 10, 11, 12, 13, 14, 15, 16, 3, 18, 2, 9, 21, 8, 23},
	'a': {18, 16, 2, 3, 4, 5, 6, 7, 8, 9, 23, 21, 13, 15, 12, 14, 10, 17, 11, 19, 20, 0, 22, 1},
	'A': {21, 23, 2, 3, 4, 5, 6, 7, 8, 9, 16, 18, 14, 12, 15, 13, 1, 17, 0, 19, 20, 11, 22, 10},
}

// rotationGenerators generate the 24 whole-cube rotations.
var rotationGenerators = [3]permutation{
	{12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 18, 16, 19, 17, 21, 23, 20, 22},
	{2, 0, 3, 1, 20, 21, 22, 23, 9, 11, 8, 10, 19, 18, 17, 16, 4, 5, 6, 7, 15, 14, 13, 12},
	{18, 16, 19, 17, 6, 4, 7, 5, 22, 20, 23, 21, 13, 15, 12, 14, 10, 8, 11, 9, 2, 0, 3, 1},
}

// Rotations holds the 24 ways of holding a cube, as facelet permutations.
var Rotations = rotationGroup()

func rotationGroup() [24]permutation {
	var identity permutation
	for i := range identity {
		identity[i] = uint8(i)
	}
	seen := map[permutation]bool{identity: true}
	frontier := []permutation{identity}
	group := []permutation{identity}
	for len(frontier) > 0 {
		current := frontier[len(frontier)-1]
		frontier = frontier[:len(frontier)-1]
		for _, generator := range rotationGenerators {
			var composed permutation
			for i, index := range generator {
				composed[i] = current[index]
			}
			if !seen[composed] {
				seen[composed] = true
				frontier = append(frontier, composed)
				group = append(group, composed)
			}
		}
	}
	return [24]permutation(group)
}

func permute(s State, p *permutation) State {
	var out State
	for i := range out {
		out[i] = s[p[i]]
	}
	return out
}

// Apply turns one face. It reports an error for an unknown move letter.
func Apply(s State, move byte) (State, error) {
	p, ok := movePerms[move]
	if !ok {
		return s, fmt.Errorf("unknown move %q (moves are %s)", string(move), MoveNames)
	}
	return permute(s, &p), nil
}

// ApplyMoves turns faces in sequence: "lFtL" applies l, then F, then t, then L.
func ApplyMoves(s State, moves string) (State, error) {
	for i := range len(moves) {
		var err error
		if s, err = Apply(s, moves[i]); err != nil {
			return s, fmt.Errorf("move %d: %w", i, err)
		}
	}
	return s, nil
}

// Invert undoes a sequence: reversed, with every letter's case flipped.
func Invert(moves string) string {
	out := make([]byte, len(moves))
	for i := range len(moves) {
		out[len(moves)-1-i] = swapCase(moves[i])
	}
	return string(out)
}

func swapCase(c byte) byte {
	switch {
	case c >= 'a' && c <= 'z':
		return c - 'a' + 'A'
	case c >= 'A' && c <= 'Z':
		return c - 'A' + 'a'
	}
	return c
}

// Orientations returns all 24 whole-cube rotations of s.
func Orientations(s State) [24]State {
	var out [24]State
	for i := range Rotations {
		out[i] = permute(s, &Rotations[i])
	}
	return out
}

// IsSolved reports whether every face shows a single colour.
func (s State) IsSolved() bool {
	for side := 0; side < 24; side += 4 {
		if s[side] != s[side+1] || s[side] != s[side+2] || s[side] != s[side+3] {
			return false
		}
	}
	return true
}

// String renders the state as its 24 colour letters. Inverse of [Parse].
func (s State) String() string {
	var b strings.Builder
	b.Grow(24)
	for _, colour := range s {
		b.WriteByte(ColorChars[colour])
	}
	return b.String()
}

// Parse reads 24 colour letters in side order UP FRONT DOWN BACK LEFT RIGHT.
// It rejects anything that is not 24 letters from "rwbypg" with each colour
// appearing exactly four times.
func Parse(text string) (State, error) {
	var s State
	text = strings.ToLower(strings.TrimSpace(text))
	if len(text) != 24 {
		return s, fmt.Errorf("expected 24 colour letters, got %d", len(text))
	}
	var counts [6]int
	for i := range len(text) {
		colour := strings.IndexByte(ColorChars, text[i])
		if colour < 0 {
			return s, fmt.Errorf("invalid colour letter %q at position %d (allowed: %s)",
				string(text[i]), i, ColorChars)
		}
		s[i] = uint8(colour)
		counts[colour]++
	}
	for colour, count := range counts {
		if count != 4 {
			return s, fmt.Errorf("colour %q appears %d times, expected 4",
				string(ColorChars[colour]), count)
		}
	}
	return s, nil
}

// Scramble applies count random moves, each reaching a state not seen before,
// so a short scramble really is short. A nil rng means a non-deterministic one.
func Scramble(s State, count int, rng *rand.Rand) State {
	if rng == nil {
		rng = rand.New(rand.NewPCG(rand.Uint64(), rand.Uint64()))
	}
	seen := map[State]bool{s: true}
	for count > 0 {
		candidate, _ := Apply(s, MoveNames[rng.IntN(len(MoveNames))])
		if !seen[candidate] {
			seen[candidate] = true
			s = candidate
			count--
		}
	}
	return s
}
