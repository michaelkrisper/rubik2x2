package display_test

import (
	"strings"
	"testing"

	"github.com/michaelkrisper/rubik2x2/go/cube"
	"github.com/michaelkrisper/rubik2x2/go/display"
)

func TestRenderPlain(t *testing.T) {
	got := display.Render(cube.Solved, display.NoColor)
	if strings.Contains(got, "\x1b") {
		t.Error("NoColor output must contain no escape sequences")
	}
	for _, name := range []string{"UP", "DOWN", "LEFT", "FRONT", "RIGHT", "BACK"} {
		if !strings.Contains(got, name) {
			t.Errorf("rendering is missing the %s label:\n%s", name, got)
		}
	}
	// Each colour has four facelets, drawn as one upper-case letter each. Only
	// the facelet rows count; the face labels are words made of letters too.
	counts := map[rune]int{}
	for _, line := range strings.Split(got, "\n") {
		if strings.TrimLeft(line, " RWBYPG") != "" {
			continue // a label row
		}
		for _, char := range line {
			if char != ' ' {
				counts[char]++
			}
		}
	}
	for _, char := range strings.ToUpper(cube.ColorChars) {
		if counts[char] != 4 {
			t.Errorf("colour %q appears %d times, want 4:\n%s", string(char), counts[char], got)
		}
	}
}

func TestRenderColored(t *testing.T) {
	for _, mode := range []display.Color{display.ANSI256, display.TrueColor} {
		got := display.Render(cube.Solved, mode)
		if !strings.Contains(got, "\x1b[48;") {
			t.Errorf("mode %v produced no background colour", mode)
		}
		// Colour blocks are two rows tall, so the picture is taller than the
		// letter fallback.
		plain := strings.Count(display.Render(cube.Solved, display.NoColor), "\n")
		if strings.Count(got, "\n") <= plain {
			t.Errorf("mode %v should draw taller cells than NoColor", mode)
		}
	}
}

func TestRenderTracksTheState(t *testing.T) {
	turned, err := cube.ApplyMoves(cube.Solved, "lFt")
	if err != nil {
		t.Fatal(err)
	}
	if display.Render(turned, display.NoColor) == display.Render(cube.Solved, display.NoColor) {
		t.Error("turning the cube did not change the picture")
	}
}

func TestBestHonoursTerm(t *testing.T) {
	t.Setenv("TERM", "dumb")
	if got := display.Best(); got != display.NoColor {
		t.Errorf("TERM=dumb gave %v, want NoColor", got)
	}
	t.Setenv("TERM", "xterm-256color")
	t.Setenv("COLORTERM", "truecolor")
	if got := display.Best(); got != display.TrueColor {
		t.Errorf("COLORTERM=truecolor gave %v, want TrueColor", got)
	}
	t.Setenv("COLORTERM", "")
	if got := display.Best(); got != display.ANSI256 {
		t.Errorf("plain terminal gave %v, want ANSI256", got)
	}
}
