// Command rubik2x2 solves the 2x2x2 Rubik's Cube (pocket cube) optimally.
//
// Usage:
//
//	rubik2x2 solve <state> [--algorithm bfs|ida|table]   print a shortest solution
//	rubik2x2 scramble [-n N] [--seed S]                  print a scrambled state
//	rubik2x2 show <state>                                draw the cube as a net
//	rubik2x2 table                                       build the table, print the histogram
//	rubik2x2 bench [--json] [--scrambles FILE] [-n N]    compare the algorithms
//
// A state is 24 colour letters (rwbypg) in side order UP FRONT DOWN BACK LEFT
// RIGHT, each face read row-major; "-" reads it from stdin. solve prints
// nothing but the move sequence, so it pipes.
package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"math/rand/v2"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/michaelkrisper/rubik2x2/go/bench"
	"github.com/michaelkrisper/rubik2x2/go/coords"
	"github.com/michaelkrisper/rubik2x2/go/cube"
	"github.com/michaelkrisper/rubik2x2/go/display"
	"github.com/michaelkrisper/rubik2x2/go/solver"
)

const usage = `rubik2x2 — optimal 2x2x2 Rubik's Cube solver

  rubik2x2 solve <state> [--algorithm bfs|ida|table]   print a shortest solution
  rubik2x2 scramble [-n N] [--seed S]                  print a scrambled state
  rubik2x2 show <state> [--color|--no-color]           draw the cube as a net
  rubik2x2 table                                       build the table, print the histogram
  rubik2x2 bench [--json] [--scrambles FILE] [-n N]    compare the algorithms

A state is 24 colour letters (` + cube.ColorChars + `) in side order
UP FRONT DOWN BACK LEFT RIGHT, each face read row-major; "-" reads stdin.
`

func main() {
	if err := run(os.Args[1:], os.Stdout); err != nil {
		fmt.Fprintln(os.Stderr, "rubik2x2:", err)
		os.Exit(1)
	}
}

func run(args []string, out io.Writer) error {
	if len(args) == 0 {
		fmt.Fprint(out, usage)
		return nil
	}
	switch command := args[0]; command {
	case "solve":
		return solveCommand(args[1:], out)
	case "scramble":
		return scrambleCommand(args[1:], out)
	case "show":
		return showCommand(args[1:], out)
	case "table":
		return tableCommand(args[1:], out)
	case "bench":
		return benchCommand(args[1:], out)
	case "-h", "--help", "help":
		fmt.Fprint(out, usage)
		return nil
	default:
		return fmt.Errorf("unknown command %q (try --help)", command)
	}
}

// flagSet builds a FlagSet that reports errors instead of exiting, so that main
// stays the only place that decides the exit status.
func flagSet(name string) *flag.FlagSet {
	set := flag.NewFlagSet(name, flag.ContinueOnError)
	set.SetOutput(io.Discard)
	return set
}

// parse reads args into set, allowing flags to appear after the positional
// argument — which is where the documented usage puts them, and where the flag
// package would otherwise stop looking. It returns the positional arguments.
func parse(set *flag.FlagSet, args []string) ([]string, error) {
	var positional []string
	for len(args) > 0 {
		if err := set.Parse(args); err != nil {
			return nil, err
		}
		if args = set.Args(); len(args) > 0 {
			positional = append(positional, args[0])
			args = args[1:]
		}
	}
	return positional, nil
}

// readState takes the state from the first positional argument, reading stdin
// for "-".
func readState(positional []string) (cube.State, error) {
	var text string
	if len(positional) > 0 {
		text = positional[0]
	}
	if text == "" {
		return cube.State{}, errors.New("missing state (24 colour letters, or - for stdin)")
	}
	if text == "-" {
		raw, err := io.ReadAll(os.Stdin)
		if err != nil {
			return cube.State{}, err
		}
		text = string(raw)
	}
	return cube.Parse(text)
}

func solveCommand(args []string, out io.Writer) error {
	set := flagSet("solve")
	algorithm := set.String("algorithm", "bfs", "bfs, ida or table")
	positional, err := parse(set, args)
	if err != nil {
		return err
	}
	state, err := readState(positional)
	if err != nil {
		return err
	}

	var solve solver.Solver
	switch *algorithm {
	case "bfs":
		solve = solver.BFS
	case "ida":
		solve = solver.NewPatternDB().Solve
	case "table":
		solve = solver.NewTable().Solve
	default:
		return fmt.Errorf("unknown algorithm %q (bfs, ida or table)", *algorithm)
	}

	solution, err := solve(state)
	if err != nil {
		return err
	}
	fmt.Fprintln(out, solution)
	return nil
}

func scrambleCommand(args []string, out io.Writer) error {
	set := flagSet("scramble")
	count := set.Int("n", 8, "number of random moves")
	seed := set.String("seed", "", "seed for reproducible output")
	if err := set.Parse(args); err != nil {
		return err
	}
	var rng *rand.Rand
	if *seed != "" {
		value, err := strconv.ParseUint(*seed, 10, 64)
		if err != nil {
			return fmt.Errorf("invalid seed %q: %w", *seed, err)
		}
		rng = rand.New(rand.NewPCG(value, 0))
	}
	fmt.Fprintln(out, cube.Scramble(cube.Solved, *count, rng))
	return nil
}

func showCommand(args []string, out io.Writer) error {
	set := flagSet("show")
	coloured := set.Bool("color", false, "force coloured output")
	plain := set.Bool("no-color", false, "plain letters only")
	positional, err := parse(set, args)
	if err != nil {
		return err
	}
	state, err := readState(positional)
	if err != nil {
		return err
	}
	mode := display.Detect(os.Stdout)
	switch {
	case *plain:
		mode = display.NoColor
	case *coloured:
		mode = display.Best()
	}
	fmt.Fprintln(out, display.Render(state, mode))
	return nil
}

func tableCommand(args []string, out io.Writer) error {
	set := flagSet("table")
	parallel := set.Bool("parallel", false, "build with goroutines")
	workers := set.Int("workers", 0, "goroutines for -parallel (0 = NumCPU)")
	if err := set.Parse(args); err != nil {
		return err
	}

	fmt.Fprintf(out, "Searching all %d states...\n", coords.NStates)
	started := time.Now()
	table := solver.NewTable()
	how := "serial"
	if *parallel {
		table = solver.NewTableParallel(*workers)
		how = "parallel"
	}
	elapsed := time.Since(started)

	histogram := table.Histogram()
	total := 0
	for depth, count := range histogram {
		total += count
		fmt.Fprintf(out, "  depth %2d: %9d states\n", depth, count)
	}
	fmt.Fprintf(out, "\nBuilt in %.2f s (%s); %d states, %.1f MB resident.\n",
		elapsed.Seconds(), how, total, float64(table.Len())/1e6)
	fmt.Fprintln(out, join(histogram))
	fmt.Fprintln(out, "This matches OEIS A079762, the published distance distribution.")
	return nil
}

func join(counts []int) string {
	parts := make([]string, len(counts))
	for i, count := range counts {
		parts[i] = strconv.Itoa(count)
	}
	return strings.Join(parts, ", ")
}

func benchCommand(args []string, out io.Writer) error {
	set := flagSet("bench")
	asJSON := set.Bool("json", false, "print one JSON object and nothing else")
	scrambles := set.String("scrambles", "", "file of state strings, one per line")
	samples := set.Int("n", 0, "use at most N scrambles (0 = all)")
	workers := set.Int("workers", 0, "goroutines for table-par (0 = NumCPU)")
	if err := set.Parse(args); err != nil {
		return err
	}
	return bench.Run(out, bench.Options{
		Scrambles: *scrambles,
		Samples:   *samples,
		Workers:   *workers,
		JSON:      *asJSON,
	})
}
