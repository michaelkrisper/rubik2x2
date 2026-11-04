# rubik2x2 — Go

An optimal solver for the 2x2x2 Rubik's Cube (pocket cube), in three algorithms.
See [`../SPEC.md`](../SPEC.md) for the contract every language implementation in
this repository follows.

Go 1.27, standard library only.

## Build, run, test

```sh
go build -o rubik2x2 ./cmd/rubik2x2     # or: go install ./cmd/rubik2x2
go test ./...                           # everything, including the OEIS check
go test -short ./...                    # skips the full-table checks (~5 s faster)
go vet ./... && gofmt -l .
```

```sh
./rubik2x2 solve wppryygrygrwgprbbbwpbwyg              # prints only the moves
./rubik2x2 solve wppryygrygrwgprbbbwpbwyg --algorithm table
./rubik2x2 scramble -n 12 --seed 42
./rubik2x2 show wppryygrygrwgprbbbwpbwyg
./rubik2x2 table                                       # histogram of all distances
./rubik2x2 bench --json --scrambles ../bench/scrambles.txt
```

A state is 24 colour letters (`rwbypg`) in side order UP FRONT DOWN BACK LEFT
RIGHT, each face read row-major; `-` reads it from stdin. `solve` writes nothing
but the move sequence, so it pipes.

## Layout

| package        | what it is                                                        |
| -------------- | ----------------------------------------------------------------- |
| `cube`         | the readable model: 24 stickers, 12 moves, parse/render/scramble   |
| `coords`       | the compact encoding the solvers run on, and the move tables       |
| `solver`       | `BFS`, `PatternDB` (IDA\*), `Table` (God's algorithm)              |
| `display`      | the unfolded-net drawing, colour detection                         |
| `bench`        | the measurement protocol and its JSON output                       |
| `cmd/rubik2x2` | the CLI                                                            |

The layering is the point: `cube` knows nothing about search, `coords` knows
nothing about how a cube is drawn, and every layer is checked against the one
below it.

## Why the encoding splits in two

The six moves the solvers use never touch one corner, and holding one corner
still is exactly what removes the 24-fold whole-cube-rotation symmetry. What is
left is 7 free pieces in 7 free slots (7! = 5040) and their twists
(3⁷ constrained to sum to 0 mod 3, so 3⁶ = 729) — 3,674,160 states, which is the
whole puzzle.

A move acts on those two halves *independently*, so it is two array lookups
rather than a rearrangement of 24 bytes, and a state is a single `int32`. That
is what makes the full distance table possible at all, and it hands IDA\* its
two pattern databases for free: each coordinate on its own is a genuine quotient
of the state graph, so its exact distance is an admissible bound.

The move tables are built during package initialisation — about two
milliseconds, 35k entries — so `coords.Encode` is safe to call without any
setup dance. Every solver shares them, which is why `bfs` genuinely reports zero
setup.

## Which algorithm wins, and when

`table` costs a full breadth-first sweep of the state space up front and then
answers in microseconds; `bfs` costs nothing up front and a few milliseconds per
solve. So the table wins above its break-even and loses below it — the benchmark
prints where that crossover sits for the machine it ran on.

IDA\* is the interesting one, and Go changes its verdict. In the Python
reference it loses to `bfs` outright; here it wins on the *average* — roughly
0.39 ms against 0.85 ms — because Go's recursive depth-first descent over two
byte arrays is very cheap, while `bfs` pays for hashing every one of tens of
thousands of frontier states. What it cannot fix is the worst case: 36 ms on the
antipodal position, ninety times its own average.

That gap is structural. Both pattern databases top out at distance 8 while real
positions reach 14, so near the antipodes the bound has nothing left to prune
with and the search degenerates to brute force. Pattern databases pay off when a
puzzle has more pieces than you can afford to track and you keep only some of
them; a pocket cube is nothing but the pieces the databases already cover. If
you care about the tail rather than the mean — and for a solver you usually do —
`bfs` is still the one to reach for below the table's break-even.

## Benchmark

`rubik2x2 bench --json --scrambles ../bench/scrambles.txt` reads the 200 shared
scrambles — a file, not a generator, so every language measures the same work —
and emits the schema in the spec. On the development machine (i5-4258U, 4
hardware threads):

```json
{
  "language": "Go",
  "version": "1.26.5",
  "build": "gc, linux/amd64, optimised",
  "algorithms": [
    {"name": "bfs",       "setup_ms": 0.0,   "avg_ms": 0.831, "worst_ms": 13.611, "peak_mb": 2.726, "optimal": true},
    {"name": "ida",       "setup_ms": 0.238, "avg_ms": 0.314, "worst_ms": 34.145, "peak_mb": 0.006, "optimal": true},
    {"name": "table",     "setup_ms": 349.9, "avg_ms": 0.002, "worst_ms": 0.001,  "peak_mb": 3.678, "optimal": true},
    {"name": "table-par", "setup_ms": 262.4, "avg_ms": 0.002, "worst_ms": 0.001,  "peak_mb": 3.678, "optimal": true, "workers": 4}
  ]
}
```

Two notes on how those are taken, because they change what the numbers mean:

- Every timing is the **best of three rounds**, not the mean. The noise here is
  one-sided — a preemption, a collection or a throttling CPU can only make a
  round slower — so the minimum is both more reproducible and the figure the
  machine is actually capable of. The two table builds are additionally timed in
  alternation, so neither is charged for the heat the other left behind.
- `peak_mb` is best effort and deliberately explicit: the heap the algorithm
  still holds when its setup is done, plus everything one worst-case solve
  allocates. That charges `table` for the 3.7 MB it keeps, which is the honest
  comparison against `bfs`, whose cost is entirely transient.

## The parallel table build

`table-par` is the same breadth-first sweep as `table`, split across goroutines.
The search is level-synchronous, so within a level the frontier divides cleanly;
the hazard is the shared visited state, where two goroutines can reach the same
state in the same level.

The spec offers two ways out — an atomic compare-and-exchange per state, or
per-worker output buffers merged at the level boundary. This implementation uses
**both, split by what each is good at**:

- **Claiming** is a lock-free `CompareAndSwap` on a *bitmap*, one bit per state,
  held in a `[]atomic.Uint32`. The goroutine that sets the bit owns the state.
- **Publishing** is per-worker: the owner alone writes the distance byte and
  appends the state to its own next-level buffer, and the buffers are
  concatenated at the barrier.

That split is what makes it race-free without a lock — the distance array is
written exactly once per state, by its owner, and read by nobody until the
barrier — and `go test -race` confirms it.

The bitmap matters more than it looks. Go has no atomic operation on a byte, so
the obvious move is to widen the distance array to `[]atomic.Uint32` and swap on
it directly. That was measured, and it is **slower than the serial build**: the
visited set is what all 22 million edges touch at random, and at 14.7 MB it
falls out of cache, where the 460 KB bitmap stays resident. Pure per-worker
buffering with no atomics at all was measured too, and is slower again, because
it hands the check-and-set for eight million candidate edges — roughly half the
work at the widest level — back to a single thread.

### What it measured

On the machine this was developed on — an Intel i5-4258U, 2 physical cores, 4
hardware threads, 3 MB L3 — the numbers are, best of eight interleaved runs:

| build                              | time   |
| ---------------------------------- | ------ |
| serial                             | 271 ms |
| parallel, bitmap CAS, 4 goroutines | 218 ms |
| parallel, `[]atomic.Uint32`        | 292 ms |
| parallel, buffers merged serially  | 634 ms |

`rubik2x2 bench` reports the same thing end to end, 290 ms against 243 ms, and
times the two builds in alternation so that neither is charged for the heat the
other left behind.

So about **1.2x on two cores**, not the 2x the core count suggests. The build is
memory-latency bound rather than compute bound: 22 million random accesses into
a 3.7 MB array, on two cores sharing one memory controller and 3 MB of L3.
Adding threads adds cache pressure roughly as fast as it adds parallelism. On a
machine with more cores and more last-level cache the gap should widen; that is
what `rubik2x2 bench` reports for whichever machine it runs on.

Solving a single cube is deliberately *not* parallelised. A solve is
milliseconds and the frontiers are small, so the goroutine and synchronisation
overhead would dominate.

## Tests

`go test ./...` covers everything the spec requires:

- the move tables' algebra: every move has order four, a move and its inverse
  cancel, opposite faces commute, and exactly facelets 11, 13 and 23 survive
  every solve move (checked by turning a probe state whose "colours" are the
  facelet indices, which hands the permutation table straight back);
- the encoding round-trips both ways, agrees with the sticker model move for
  move, and rejects the two kinds of impossible cube — a fixed corner no solved
  cube shows, and a lone twisted corner;
- all three algorithms return solutions of the same, optimal length, and every
  solution actually solves its cube, including the antipodal position at exactly
  14 moves;
- the parallel table build agrees with the serial one, state for state;
- **the distance histogram equals [OEIS A079762][oeis]** — the single strongest
  check available, since no incorrect move table or encoding could reproduce it.

The histogram check and the other full-table checks build 3.7 M states, so they
are guarded by `testing.Short()`: `go test -short ./...` skips them, plain
`go test ./...` runs them. The spec makes the histogram mandatory, so short mode
is for the inner development loop only — not for verifying a change.

[oeis]: https://oeis.org/A079762
