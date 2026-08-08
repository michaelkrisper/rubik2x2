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
	"math"
	"math/rand/v2"
	"os"
	"runtime"
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

// rounds is how many times a measurement is repeated before the best is taken.
const rounds = 3

// measure times f and returns the best per-call mean, in milliseconds.
//
// It first calibrates: a call short enough to be lost in timer noise is
// repeated until a round lasts long enough to mean something. It then takes the
// *minimum* over several rounds rather than the average, because the noise here
// is one-sided — a scheduler preemption, a garbage collection or a laptop CPU
// throttling can only ever make a round slower. The minimum is both the more
// reproducible figure and the one the machine is actually capable of.
func measure(f func()) float64 {
	start := time.Now()
	f()
	first := time.Since(start)

	repeats := 1
	if first < 50*time.Millisecond {
		repeats = min(1000, int(50*time.Millisecond/max(first, time.Microsecond))+1)
	}

	best := math.Inf(1)
	for range rounds {
		start = time.Now()
		for range repeats {
			f()
		}
		best = min(best, float64(time.Since(start).Nanoseconds())/1e6/float64(repeats))
	}
	return best
}

// peakMB is best effort, and deliberately explicit about what it counts: the
// heap the algorithm still holds once its setup is done, plus everything one
// solve of the worst case allocates. None of the three frees anything in the
// middle of a solve, so the sum stands in fairly for the peak — and it charges
// the table for the 3.7 MB it keeps, which is the honest comparison.
func peakMB(setup func() solver.Solver, state cube.State) float64 {
	runtime.GC()
	var before, after runtime.MemStats
	runtime.ReadMemStats(&before)

	solve := setup()
	runtime.GC()
	runtime.ReadMemStats(&after)
	retained := int64(after.HeapAlloc) - int64(before.HeapAlloc)

	allocated := after.TotalAlloc
	sink, _ = solve(state)
	runtime.ReadMemStats(&after)
	runtime.KeepAlive(solve)

	return float64(max(0, retained)+int64(after.TotalAlloc-allocated)) / 1e6
}

// measureBoth times two functions in alternation and returns the best round of
// each. Interleaving is what makes the two comparable: run one after the other
// and the second inherits whatever thermal or cache state the first left
// behind, which on a laptop is worth more than the difference being measured.
func measureBoth(f, g func()) (float64, float64) {
	bestF, bestG := math.Inf(1), math.Inf(1)
	for range rounds {
		start := time.Now()
		f()
		bestF = min(bestF, float64(time.Since(start).Nanoseconds())/1e6)
		start = time.Now()
		g()
		bestG = min(bestG, float64(time.Since(start).Nanoseconds())/1e6)
	}
	return bestF, bestG
}

// evaluate times one algorithm's solves. setupMS is passed in rather than
// measured here, so that the two table builds can be timed against each other.
func evaluate(name string, setupMS float64, setup func() solver.Solver,
	states []cube.State, hardest cube.State, table *solver.Table) Result {

	solve := setup()

	avgMS := measure(func() {
		for _, state := range states {
			sink, _ = solve(state)
		}
	}) / float64(len(states))
	worstMS := measure(func() { sink, _ = solve(hardest) })
	peak := peakMB(setup, hardest)

	optimal := true
	check := func(state cube.State) bool {
		solution, err := solve(state)
		if err != nil {
			return false
		}
		index, err := coords.Encode(state)
		if err != nil || len(solution) != table.Distance(index) {
			return false
		}
		solved, err := cube.ApplyMoves(state, solution)
		return err == nil && solved.IsSolved()
	}
	for _, state := range states {
		if !check(state) {
			optimal = false
			break
		}
	}
	optimal = optimal && check(hardest)

	return Result{Name: name, SetupMS: setupMS, AvgMS: avgMS, WorstMS: worstMS,
		PeakMB: peak, Optimal: optimal}
}

// significant rounds to n significant digits rather than n decimal places.
//
// Fixed decimals are wrong for this report: the figures it carries span seven
// orders of magnitude, from a sub-microsecond table lookup to a multi-second
// table build. Three decimal places flattened every lookup faster than 0.5 us
// to zero, which on a quick machine is all of them.
func significant(value float64, digits int) float64 {
	if value == 0 || math.IsNaN(value) || math.IsInf(value, 0) {
		return value
	}
	magnitude := math.Ceil(math.Log10(math.Abs(value)))
	scale := math.Pow(10, float64(digits)-magnitude)
	return math.Round(value*scale) / scale
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

	// The two table builds are timed against each other, since the whole point
	// of table-par is the difference between them. bfs has no precomputation at
	// all: the move tables it uses are built during package initialisation and
	// shared by all three algorithms.
	serialSetup, parallelSetup := measureBoth(
		func() { solver.NewTable() },
		func() { solver.NewTableParallel(workers) },
	)

	results := []Result{
		evaluate("bfs", 0, func() solver.Solver { return solver.BFS },
			states, hardest, reference),
		evaluate("ida", measure(func() { solver.NewPatternDB() }),
			func() solver.Solver { return solver.NewPatternDB().Solve },
			states, hardest, reference),
		evaluate("table", serialSetup, func() solver.Solver { return solver.NewTable().Solve },
			states, hardest, reference),
	}

	// Only the build is parallelised, so table-par shares every per-solve figure
	// with table by construction; measuring them twice would only add noise.
	parallel := results[len(results)-1]
	parallel.Name = "table-par"
	parallel.Workers = workers
	parallel.SetupMS = parallelSetup
	results = append(results, parallel)

	for i := range results {
		results[i].SetupMS = significant(results[i].SetupMS, 4)
		results[i].AvgMS = significant(results[i].AvgMS, 4)
		results[i].WorstMS = significant(results[i].WorstMS, 4)
		results[i].PeakMB = significant(results[i].PeakMB, 4)
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
		fmt.Fprintf(out,
			"\nThe table overtakes bidirectional search at about %d solves. Below that,\n"+
				"building it costs more than it saves; above it, nothing else comes close.\n",
			crossover)
	}
	permutation, orientation := solver.NewPatternDB().Reach()
	ida := byName["ida"]
	fmt.Fprintf(out,
		"IDA* averages %.3f ms but takes %.0f ms on the antipodal position — %.0fx its\n"+
			"own average. Its pattern databases reach only distance %d of %d, so near the\n"+
			"antipodes the bound has nothing left to prune with.\n",
		ida.AvgMS, ida.WorstMS, ida.WorstMS/max(ida.AvgMS, 1e-9),
		max(permutation, orientation), solver.MaxDepth)
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
