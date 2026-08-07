// Package display draws a cube for a terminal.
//
// Deliberately separate from package cube: the model is 24 bytes and knows
// nothing about how anyone looks at it.
//
// The picture is the standard unfolded net — UP above FRONT, the four side
// faces in a band, DOWN below — so it matches the order a state string is
// written in. BACK is drawn as seen from behind, which is what makes the band
// read as one continuous strip around the cube.
//
// Colour is used when the terminal really supports it and letters otherwise,
// per NO_COLOR/FORCE_COLOR and whether output is a terminal at all. Coloured
// facelets are two rows tall so they come out roughly square, since terminal
// cells are about twice as tall as they are wide; the letter fallback uses one
// row, where squareness would only cost readability.
package display

import (
	"fmt"
	"os"
	"strings"

	"github.com/michaelkrisper/rubik2x2/go/cube"
)

// A Color says how much colour the output may use.
type Color int

// The colour modes, in increasing richness.
const (
	NoColor Color = iota
	ANSI256
	TrueColor
)

// palette is the face colour RGB, close to a real cube.
var palette = map[byte][3]int{
	'r': {196, 30, 58},
	'w': {245, 245, 245},
	'b': {0, 81, 186},
	'y': {255, 213, 0},
	'p': {137, 66, 175},
	'g': {0, 158, 96},
}

// band lists the side faces left to right.
var band = [4]struct {
	name string
	side int
}{
	{"LEFT", cube.Left}, {"FRONT", cube.Front}, {"RIGHT", cube.Right}, {"BACK", cube.Back},
}

const (
	cellWidth = 4
	faceWidth = cellWidth * 2
	gap       = "  "
	reset     = "\x1b[0m"
	dim       = "\x1b[2m"
)

// Best is the richest mode this terminal understands, ignoring where output
// goes.
func Best() Color {
	if os.Getenv("TERM") == "dumb" {
		return NoColor
	}
	switch strings.ToLower(os.Getenv("COLORTERM")) {
	case "truecolor", "24bit":
		return TrueColor
	}
	return ANSI256
}

// Detect is what to actually use, honouring NO_COLOR, FORCE_COLOR and piping.
func Detect(out *os.File) Color {
	if os.Getenv("NO_COLOR") != "" {
		return NoColor
	}
	if os.Getenv("FORCE_COLOR") != "" {
		return Best()
	}
	info, err := out.Stat()
	if err != nil || info.Mode()&os.ModeCharDevice == 0 {
		return NoColor
	}
	return Best()
}

// cellHeight is the rows per facelet: two for colour blocks, one for letters.
func (c Color) cellHeight() int {
	if c == NoColor {
		return 1
	}
	return 2
}

// ansi256 is the nearest index in the xterm 6x6x6 colour cube.
func ansi256(rgb [3]int) int {
	return 16 + 36*(rgb[0]*5/255) + 6*(rgb[1]*5/255) + (rgb[2]*5/255)
}

// paint renders one facelet row, always cellWidth columns wide.
func paint(char byte, colour Color) string {
	if colour == NoColor {
		return center(strings.ToUpper(string(char)), cellWidth)
	}
	rgb := palette[char]
	background := fmt.Sprintf("\x1b[48;5;%dm", ansi256(rgb))
	if colour == TrueColor {
		background = fmt.Sprintf("\x1b[48;2;%d;%d;%dm", rgb[0], rgb[1], rgb[2])
	}
	return background + strings.Repeat(" ", cellWidth) + reset
}

func center(text string, width int) string {
	if len(text) >= width {
		return text
	}
	left := (width - len(text)) / 2
	return strings.Repeat(" ", left) + text + strings.Repeat(" ", width-len(text)-left)
}

// face renders one face as text rows.
func face(s cube.State, side int, colour Color) []string {
	base := side * 4
	order := [2][2]int{{0, 1}, {2, 3}}
	if side == cube.Back {
		order = [2][2]int{{3, 2}, {1, 0}}
	}
	var rows []string
	for _, pair := range order {
		row := paint(cube.ColorChars[s[base+pair[0]]], colour) +
			paint(cube.ColorChars[s[base+pair[1]]], colour)
		for range colour.cellHeight() {
			rows = append(rows, row)
		}
	}
	return rows
}

func label(text string, colour Color) string {
	centered := center(text, faceWidth)
	if colour == NoColor {
		return centered
	}
	return dim + centered + reset
}

// Render draws the cube as an unfolded net.
func Render(s cube.State, colour Color) string {
	// UP and DOWN sit over FRONT, the second face in the band.
	indent := strings.Repeat(" ", faceWidth+len(gap))
	faces := make([][]string, len(band))
	names := make([]string, len(band))
	for i, entry := range band {
		faces[i] = face(s, entry.side, colour)
		names[i] = label(entry.name, colour)
	}

	lines := []string{indent + label("UP", colour)}
	for _, row := range face(s, cube.Up, colour) {
		lines = append(lines, indent+row)
	}
	lines = append(lines, "", strings.Join(names, gap))
	for row := range faces[0] {
		parts := make([]string, len(faces))
		for i := range faces {
			parts[i] = faces[i][row]
		}
		lines = append(lines, strings.Join(parts, gap))
	}
	lines = append(lines, "", indent+label("DOWN", colour))
	for _, row := range face(s, cube.Down, colour) {
		lines = append(lines, indent+row)
	}
	return strings.Join(lines, "\n")
}
