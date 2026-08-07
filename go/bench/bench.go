// Package bench compares the solvers on one fixed workload.
//
// Per-solve time alone flatters the lookup table, because it hides the
// exhaustive search that produced it. What actually decides which algorithm to
// reach for is total time for the workload you have — so this reports setup,
// per-solve and the break-even point where paying for the table starts to win.
//
// The scrambles come from a file rather than a random generator, so every
// language implementation in this repository measures exactly the same work.
package bench

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"math/rand/v2"
	"os"
	"runtime"
	"runtime/debug"
	"strings"
	"text/tabwriter"
	"time"

	"github.com/michaelkrisper/rubik2x2/go/coords"
	"github.com/michaelkrisper/rubik2x2/go/cube"
	"github.com/michaelkrisper/rubik2x2/go/solver"
)

// Hardest is an antipodal position: 14 quarter turns from solved, the worst
// case there is.
const Hardest = "wppryygrygrwgprbbbwpbwyg"

// Options configures a benchmark run.
type Options struct {
	// Scrambles is the path to a file of state strings, one per line. Empty
	// means generate random scrambles instead, which no cross-language
	// comparison should rely on.
	Scrambles string
	// Samples caps how many scrambles to use; <= 0 means all of them.
	Samples int
	// Workers is the goroutine count for the parallel table build; <= 0 means
	// runtime.NumCPU.
	Workers int
	// JSON prints exactly one JSON object and nothing else.
	JSON bool
}

// Result is one algorithm's line in the report. The JSON field names, types and
// units are fixed by the specification; tools/report.py aggregates them.
type Result struct {
	Name    string  `json:"name"`
	SetupMS float64 `json:"setup_ms"`
	AvgMS   float64 `json:"avg_ms"`
	WorstMS float64 `json:"worst_ms"`
	PeakMB  float64 `json:"peak_mb"`
	Optimal bool    `json:"optimal"`
	Workers int     `json:"workers,omitempty"`
}

// Report is the whole benchmark, as emitted by --json.
type Report struct {
	Language   string   `json:"language"`
	Version    string   `json:"version"`
	Build      string   `json:"build"`
	Algorithms []Result `json:"algorithms"`
}

// TotalMS is setup plus n solves — the number that actually decides.
func (r Result) TotalMS(n int) float64 { return r.SetupMS + float64(n)*r.AvgMS }

// sink keeps solutions alive so the compiler cannot optimise a solve away.
var sink string

// LoadScrambles reads state strings, one per line, ignoring blank lines.
func LoadScrambles(path string) ([]cube.State, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	var states []cube.State
	scanner := bufio.NewScanner(file)
	for line := 1; scanner.Scan(); line++ {
		text := strings.TrimSpace(scanner.Text())
		if text == "" {
			continue
		}
		state, err := cube.Parse(text)
		if err != nil {
			return nil, fmt.Errorf("%s:%d: %w", path, line, err)
		}
		states = append(states, state)
	}
	return states, scanner.Err()
}

// randomScrambles is the fallback when no scramble file is given.
func randomScrambles(n int) []cube.State {
	states := make([]cube.State, n)
	for i := range states {
		states[i] = cube.Scramble(cube.Solved, 14, rand.New(rand.NewPCG(uint64(i), 0)))
	}
	return states
}

// measure times f, repeating it until the total is long enough to mean
// something, and returns the mean in milliseconds.
func measure(f func()) float64 {
	start := time.Now()
	f()
	first := time.Since(start)

	repeats := 1
	if first < 50*time.Millisecond {
		repeats = min(1000, int(50*time.Millisecond/max(first, time.Microsecond))+1)
	}
	start = time.Now()
	for range repeats {
		f()
	}
	return float64(time.Since(start).Nanoseconds()) / 1e6 / float64(repeats)
}

// peakMB is the additional heap f holds, best effort: with the collector off,
// heap growth across f is everything f allocated and therefore an upper bound
// on its peak.
func peakMB(f func()) float64 {
	defer debug.SetGCPercent(debug.SetGCPercent(-1))
	runtime.GC()
	var before, after runtime.MemStats
	runtime.ReadMemStats(&before)
	f()
	runtime.ReadMemStats(&after)
	runtime.KeepAlive(sink)
	if after.HeapAlloc < before.HeapAlloc {
		return 0
	}
	return float64(after.HeapAlloc-before.HeapAlloc) / 1e6
}

// evaluate times one algorithm end to end, setup included. setup returns the
// solver it prepared, so its cost is attributed where it belongs.
func evaluate(name string, setup func() solver.Solver, states []cube.State,
	hardest cube.State, table *solver.Table) Result {

	setupMS := measure(func() { setup() })
	solve := setup()

	avgMS := measure(func() {
		for _, state := range states {
			sink, _ = solve(state)
		}
	}) / float64(len(states))
	worstMS := measure(func() { sink, _ = solve(hardest) })
	peak := peakMB(func() {
		solve := setup()
		sink, _ = solve(hardest)
	})

	optimal := true
	for _, state := range append(states, hardest) {
		solution, err := solve(state)
		if err != nil {
			optimal = false
			break
		}
		index, err := coords.Encode(state)
		if err != nil || len(solution) != table.Distance(index) {
			optimal = false
			break
		}
		if solved, err := cube.ApplyMoves(state, solution); err != nil || !solved.IsSolved() {
			optimal = false
			break
		}
	}
	return Result{Name: name, SetupMS: setupMS, AvgMS: avgMS, WorstMS: worstMS,
		PeakMB: peak, Optimal: optimal}
}

func round(value float64, places int) float64 {
	scale := 1.0
	for range places {
		scale *= 10
	}
	rounded := float64(int64(value*scale + 0.5))
	if value < 0 {
		rounded = float64(int64(value*scale - 0.5))
	}
	return rounded / scale
}

// Run measures every algorithm and writes the report to out.
func Run(out io.Writer, opts Options) error {
	hardest, err := cube.Parse(Hardest)
	if err != nil {
		return err
	}

	var states []cube.State
	if opts.Scrambles != "" {
		if states, err = LoadScrambles(opts.Scrambles); err != nil {
			return err
		}
	} else {
		samples := opts.Samples
		if samples <= 0 {
			samples = 200
		}
		states = randomScrambles(samples)
	}
	if opts.Samples > 0 && opts.Samples < len(states) {
		states = states[:opts.Samples]
	}
	if len(states) == 0 {
		return fmt.Errorf("no scrambles to benchmark")
	}

	workers := opts.Workers
	if workers <= 0 {
		workers = runtime.NumCPU()
	}
	reference := solver.NewTable()

	results := []Result{
		evaluate("bfs", func() solver.Solver { return solver.BFS }, states, hardest, reference),
		evaluate("ida", func() solver.Solver { return solver.NewPatternDB().Solve },
			states, hardest, reference),
		evaluate("table", func() solver.Solver { return solver.NewTable().Solve },
			states, hardest, reference),
	}

	// Only the build is parallelised, so table-par shares every per-solve figure
	// with table by construction; measuring them twice would only add noise.
	parallel := results[len(results)-1]
	parallel.Name = "table-par"
	parallel.Workers = workers
	parallel.SetupMS = measure(func() { solver.NewTableParallel(workers) })
	results = append(results, parallel)

	for i := range results {
		results[i].SetupMS = round(results[i].SetupMS, 3)
		results[i].AvgMS = round(results[i].AvgMS, 3)
		results[i].WorstMS = round(results[i].WorstMS, 3)
		results[i].PeakMB = round(results[i].PeakMB, 3)
	}

	if opts.JSON {
		encoder := json.NewEncoder(out)
		encoder.SetIndent("", "  ")
		return encoder.Encode(Report{
			Language:   "Go",
			Version:    strings.TrimPrefix(runtime.Version(), "go"),
			Build:      fmt.Sprintf("%s, %s/%s, optimised", runtime.Compiler, runtime.GOOS, runtime.GOARCH),
			Algorithms: results,
		})
	}
	writeText(out, opts, states, results, workers)
	return nil
}

func writeText(out io.Writer, opts Options, states []cube.State, results []Result, workers int) {
	source := opts.Scrambles
	if source == "" {
		source = "generated"
	}
	fmt.Fprintf(out, "rubik2x2 benchmark — %d scrambles from %s, plus the antipodal position\n",
		len(states), source)
	fmt.Fprintf(out, "%d reachable states, maximum distance %d quarter turns\n",
		coords.NStates, solver.MaxDepth)
	fmt.Fprintf(out, "%s on %s/%s, %d workers for table-par\n\n",
		runtime.Version(), runtime.GOOS, runtime.GOARCH, workers)

	w := tabwriter.NewWriter(out, 0, 0, 2, ' ', tabwriter.AlignRight)
	fmt.Fprintln(w, "algorithm\tsetup ms\tavg ms\tworst ms\tpeak MB\toptimal\t")
	for _, r := range results {
		fmt.Fprintf(w, "%s\t%.1f\t%.3f\t%.3f\t%.1f\t%s\t\n",
			r.Name, r.SetupMS, r.AvgMS, r.WorstMS, r.PeakMB, yesNo(r.Optimal))
	}
	w.Flush()

	fmt.Fprintln(out, "\ntotal seconds for a workload (setup + solves)")
	counts := []int{1, 10, 100, 1000, 10000}
	w = tabwriter.NewWriter(out, 0, 0, 2, ' ', tabwriter.AlignRight)
	fmt.Fprint(w, "algorithm\t")
	for _, n := range counts {
		fmt.Fprintf(w, "%d\t", n)
	}
	fmt.Fprintln(w)
	for _, r := range results {
		fmt.Fprintf(w, "%s\t", r.Name)
		for _, n := range counts {
			fmt.Fprintf(w, "%.2f\t", r.TotalMS(n)/1000)
		}
		fmt.Fprintln(w)
	}
	w.Flush()

	byName := map[string]Result{}
	for _, r := range results {
		byName[r.Name] = r
	}
	if crossover, ok := breakEven(byName["table"], byName["bfs"]); ok {
		permutation, orientation := solver.NewPatternDB().Reach()
		fmt.Fprintf(out,
			"\nThe table overtakes bidirectional search at about %d solves.\n"+
				"Below that, building it costs more than it saves; above it, nothing else\n"+
				"comes close. IDA* is dominated everywhere: its pattern databases reach only\n"+
				"distance %d of %d, too loose to prune a puzzle this small.\n",
			crossover, max(permutation, orientation), solver.MaxDepth)
	}
}

// breakEven is the number of solves after which fast overtakes slow, setup
// included.
func breakEven(fast, slow Result) (int, bool) {
	gain := slow.AvgMS - fast.AvgMS
	if gain <= 0 {
		return 0, false
	}
	return int((fast.SetupMS-slow.SetupMS)/gain) + 1, true
}

func yesNo(ok bool) string {
	if ok {
		return "yes"
	}
	return "NO"
}
