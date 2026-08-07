package bench_test

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/michaelkrisper/rubik2x2/go/bench"
	"github.com/michaelkrisper/rubik2x2/go/cube"
)

// scramblesPath is the shared workload every language implementation measures.
var scramblesPath = filepath.Join("..", "..", "bench", "scrambles.txt")

func TestLoadScrambles(t *testing.T) {
	states, err := bench.LoadScrambles(scramblesPath)
	if err != nil {
		t.Fatalf("LoadScrambles: %v", err)
	}
	if len(states) != 200 {
		t.Errorf("read %d scrambles, want 200", len(states))
	}
	for i, state := range states {
		if state.IsSolved() {
			t.Errorf("scramble %d is already solved", i)
		}
	}
}

func TestLoadScramblesRejectsGarbage(t *testing.T) {
	path := filepath.Join(t.TempDir(), "bad.txt")
	if err := writeFile(path, "yyyyrrrrwwwwppppbbbbgggg\nnot a cube\n"); err != nil {
		t.Fatal(err)
	}
	if _, err := bench.LoadScrambles(path); err == nil {
		t.Error("LoadScrambles should reject a malformed line")
	}
}

func TestHardestIsParseable(t *testing.T) {
	if _, err := cube.Parse(bench.Hardest); err != nil {
		t.Fatalf("Hardest does not parse: %v", err)
	}
}

// TestRunJSON checks the wire format the comparison table is built from.
func TestRunJSON(t *testing.T) {
	if testing.Short() {
		t.Skip("builds the distance table several times; run without -short")
	}
	var out bytes.Buffer
	err := bench.Run(&out, bench.Options{
		Scrambles: scramblesPath,
		Samples:   5,
		Workers:   2,
		JSON:      true,
	})
	if err != nil {
		t.Fatalf("Run: %v", err)
	}

	var report bench.Report
	if err := json.Unmarshal(out.Bytes(), &report); err != nil {
		t.Fatalf("output is not valid JSON: %v\n%s", err, out.String())
	}
	if report.Language != "Go" || report.Version == "" || report.Build == "" {
		t.Errorf("incomplete header: %+v", report)
	}

	want := []string{"bfs", "ida", "table", "table-par"}
	if len(report.Algorithms) != len(want) {
		t.Fatalf("got %d algorithms, want %d", len(report.Algorithms), len(want))
	}
	for i, result := range report.Algorithms {
		if result.Name != want[i] {
			t.Errorf("algorithm %d is %q, want %q", i, result.Name, want[i])
		}
		if !result.Optimal {
			t.Errorf("%s did not return optimal solutions", result.Name)
		}
		if result.AvgMS <= 0 || result.WorstMS <= 0 {
			t.Errorf("%s reported no time: %+v", result.Name, result)
		}
		if result.SetupMS < 0 || result.PeakMB < 0 {
			t.Errorf("%s reported a negative measurement: %+v", result.Name, result)
		}
	}
	if got := report.Algorithms[3].Workers; got != 2 {
		t.Errorf("table-par reported %d workers, want 2", got)
	}
	if report.Algorithms[0].SetupMS > 1 {
		t.Errorf("bfs needs no setup, but reported %v ms", report.Algorithms[0].SetupMS)
	}
}

func TestRunText(t *testing.T) {
	if testing.Short() {
		t.Skip("builds the distance table several times; run without -short")
	}
	var out bytes.Buffer
	err := bench.Run(&out, bench.Options{Scrambles: scramblesPath, Samples: 3, JSON: false})
	if err != nil {
		t.Fatalf("Run: %v", err)
	}
	for _, want := range []string{"bfs", "ida", "table", "table-par", "setup ms"} {
		if !strings.Contains(out.String(), want) {
			t.Errorf("report is missing %q:\n%s", want, out.String())
		}
	}
}

func TestTotalMS(t *testing.T) {
	result := bench.Result{SetupMS: 100, AvgMS: 0.5}
	if got := result.TotalMS(200); got != 200 {
		t.Errorf("TotalMS(200) = %v, want 200", got)
	}
}

func writeFile(path, content string) error {
	return os.WriteFile(path, []byte(content), 0o644)
}
