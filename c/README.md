# rubik2x2 — C

Optimal 2x2x2 solver in C23. No dependencies beyond the C standard library and
POSIX threads.

## Build, run, test

```sh
make                # ./rubik2x2, -O3 -march=native -flto, warning-clean under -Wall -Wextra -Werror
make test           # builds and runs the test suite
make bench          # human-readable benchmark against ../bench/scrambles.txt
make clean
```

`-std=c23` is strict ISO, so the Makefile also passes `-D_POSIX_C_SOURCE=200809L`
to bring back the POSIX declarations the build needs (`pthread_barrier_*`,
`clock_gettime`, `isatty`, `sysconf`) without dropping to a `-std=gnu*` dialect.

```sh
./rubik2x2 solve wppryygrygrwgprbbbwpbwyg [--algorithm bfs|ida|table]
./rubik2x2 scramble [-n N] [--seed S]
./rubik2x2 show <state>                      # draw the cube as an unfolded net
./rubik2x2 table [--workers N]               # build the table, print the histogram
./rubik2x2 bench [--json] [--scrambles FILE] [-n N] [--workers N]
```

`solve` writes nothing but the move sequence to stdout, so it pipes. A state
argument of `-` reads the state from stdin instead.

## Layout

| File | What it owns |
|---|---|
| `src/cube.{h,c}` | the 24-sticker model: move tables, parsing, rotations, scrambling |
| `src/coords.{h,c}` | the compact encoding and the per-move coordinate tables |
| `src/bfs.{h,c}` | bidirectional breadth-first search |
| `src/ida.{h,c}` | IDA\* with the two pattern databases |
| `src/table.{h,c}` | the full distance table, serial and parallel builds |
| `src/display.{h,c}` | drawing a cube for a terminal |
| `src/bench.{h,c}` | the benchmark and its JSON report |
| `src/cli.c` | argument parsing; knows nothing about how a cube is solved |
| `tests/test_rubik.c` | everything SPEC.md section 4 requires |

Nothing is a hidden global. `coords_create()`, `table_create()` and
`ida_databases_create()` hand back owned pointers with matching `_destroy`
functions, so a test can hold several tables at once and the CLI frees
everything on every path. The one exception is the 24-element whole-cube
rotation group in `cube.c`, which is derived from three generators once under
`pthread_once` and then only read.

## Why the design is shaped this way

**The fixed corner.** The solvers use only `l L t T f F`. A fourth face would be
redundant — turning a face equals turning the opposite face plus a whole-cube
rotation — and these six never touch facelets 11, 13 and 23. Holding that one
corner still removes the 24-fold rotation symmetry, which is what cuts the state
space to 3,674,160.

**Why the encoding splits in two.** With a corner held still, a state is
7 pieces in 7 slots (7! = 5040) plus their twists (3⁶ = 729, since the twists
sum to a multiple of 3). A move acts on those two halves *independently*, so it
is two array lookups and an add rather than a 24-byte shuffle. The permutation
table is pre-multiplied by 729 so a child costs a single addition — worth having
in a loop that runs 22 million times during the table build.

That independence is also why IDA\* gets its heuristic free: each coordinate
alone is a genuine quotient of the state graph, so its exact distance is an
admissible bound.

**Why the table wins only above its break-even.** A full distance table answers
any scramble in a handful of lookups, but it costs an exhaustive search of all
3.67 M states up front. `rubik2x2 bench` prints total time for workloads of 1 to
10,000 solves next to the per-solve figures, because per-solve time alone hides
the setup entirely and would make the table look free.

**Memory reporting.** `peak_mb` is the memory each algorithm actually holds
while solving, reported by the solver itself rather than sampled from the OS:
the 7.3 MB mark array and its level buffers for `bfs`, the two pattern
databases for `ida`, the 3.67 MB byte array for `table`. Sampling RSS would not
work here — glibc never returns the peak to the kernel, so a delta measured
after the first algorithm would be zero for every later one.

**Why `bfs` is not a hash set.** A cube state is *already* a dense integer below
3,674,160, so hashing it is work spent to derive an index from something that is
one. `bfs.c` marks visited states in a flat `uint16_t` array subscripted by the
state itself: one load says whether a state has been seen, by which frontier and
how deep, and one store records a child. The array is 7.3 MB and lives for the
process, because a solve cannot afford to allocate or clear it — so a mark also
carries a **generation stamp**, and a solve bumps a counter instead of zeroing
7.3 MB. Everything an earlier solve left behind reads as unvisited.

That stamp is why a mark stores a *distance* and not a parent state. A parent
needs 22 bits, which forces a 32-bit mark and leaves only 6 for the stamp, so
every 63rd solve would have to clear the array for real — measured at a fifth of
the average solve, all of it `memset`. A distance needs 4 bits, which fits the
whole mark in 16 — depth, frontier bit and an 11-bit stamp — halving the array's
cache footprint and putting the clear at once in 2,046 solves. The path is
rebuilt without parents because breadth-first
marks are exact: from a node at distance `d`, a neighbour at `d - 1` under the
same stamp and frontier is a parent, and there are only six neighbours to try.
A few dozen lookups once per solve buy back a store on every one of ~150,000
edges — and the store that remains is a constant for the whole level.

Against the two open-addressed sets and node pools this replaced: **0.212 →
0.075 ms** average and **3.79 → 1.09 ms** on the antipodal position, best of
five runs each. The cost is `peak_mb`, 1.4 → 8.9, which is the honest trade:
the search now rents the whole state space instead of paying per node.

Tried and reverted: 2 MB pages for the mark array (`aligned_alloc` +
`madvise(MADV_HUGEPAGE)`). It measured slightly *worse* than 4 KB pages here and
would have added a Linux-only path to an otherwise POSIX file.

## The parallel table build

`table-par` is the same breadth-first search over all 3,674,160 states, built by
`table_create_parallel()`. The search is level-synchronous, so within a level
the frontier splits cleanly across workers; the threads are created once and
meet at a `pthread_barrier_t` twice per level (open the level, close it), which
costs nothing next to the 15 levels of work between them.

**The visited array: atomic compare-exchange, not per-worker buffers.**
The distance array is `_Atomic uint8_t`, and a worker claims a state with one
relaxed `atomic_compare_exchange_strong` from 0 to the level marker. Whoever
wins owns the state and queues it; everyone else moves on. A plain relaxed load
in front of the exchange keeps the locked instruction off the roughly 85% of
edges that lead somewhere already visited.

The alternative the spec offers — per-worker output buffers merged at the level
boundary — was rejected because on this problem the merge *is* the work. Every
one of the 22 million edges has to be tested against the visited array and
deduplicated; deferring that to a single-threaded merge pass would leave almost
nothing to parallelise. With the compare-exchange the marking is done in
parallel and the only serial step per level is a barrier.

Queue slots are still claimed in blocks (512 states per `atomic_fetch_add` on
the shared tail, frontier chunks of 4096 per `atomic_fetch_add` on the cursor),
so the two hot atomics are touched once per block rather than once per state.

Declaring the array `_Atomic` costs the serial build nothing: on x86-64 a
relaxed byte load or store is a plain `mov`, so `table_create()` and
`table_create_parallel()` run the identical inner loop apart from the exchange.
The test suite asserts the two builds produce byte-identical tables.

**What it measured.** On the development machine — an Intel i5-4258U, 2 physical
cores with hyper-threading, 4 logical CPUs, `powersave` governor — the build
takes **153 ms** serial and **85 ms** with 4 workers: **1.8×**. That is close to
the ceiling here. The inner loop is a random-access read of a 3.7 MB array that
does not fit in this chip's 3 MB L3, so it is memory-latency bound, and the two
physical cores share one memory path; hyper-threading buys a little by hiding
latency, not by adding throughput. Expect better scaling on a machine with more
real cores.

Both builds are timed three times, alternating, and the fastest run of each is
reported. A single sample is not usable on a laptop: whichever build ran first
would pay for faulting in 18 MB of fresh pages while the second reused them —
a "speedup" for `table-par` that had nothing to do with threads — and turbo and
thermal state move the absolute numbers by a factor of two between neighbouring
runs anyway. Benchmark on an otherwise idle machine.

Solving a single cube is not parallelised, per the spec: a solve is
microseconds to milliseconds and its frontiers are small.

## Tests

`make test` runs `tests/test_rubik.c`, a plain C harness (no framework, no
dependency), covering everything in SPEC.md section 4:

- the **OEIS A079762** histogram, exactly — the strongest single check
  available, since no incorrect move table or encoding can reproduce it
- every move has order 4, a move and its inverse cancel, opposite faces commute
- exactly facelets 11, 13 and 23 are fixed by the six solve moves
- the whole-cube rotation group has 24 distinct elements
- the encoding round-trips both ways and agrees with the sticker model on every
  move
- all four unsolvable arrangements are rejected: an impossible fixed corner, a
  slot holding no valid piece, the same piece in two slots, and twists that do
  not sum to a multiple of three — plus the near miss, three corners twisted
  alike, which *is* reachable and must be accepted
- all three algorithms return equal-length solutions, each equal to the table's
  distance, and every solution actually solves its cube
- the antipodal position `wppryygrygrwgprbbbwpbwyg` needs exactly 14 moves
- the parallel build produces a byte-identical table for 2, 4 and 8 workers

### Two things the encoding does *not* promise

**The 24 orientations do not share an index.** Every *solved* cube encodes to 0
whatever way it is held, and every orientation of a scramble is solved
correctly — but rotating the cube puts a different physical piece in the fixed
slot, so the permutation coordinate genuinely changes. What is invariant is the
distance, and that is what the tests check: all 24 orientations of a scramble
need the same number of moves, and each returned solution solves the cube it was
given. It also follows that `coords_decode` inverts `coords_encode` only for
cubes held in the standard orientation, since decoding always produces one.

**An illegal arrangement will not announce itself later.** The index is smaller
than the space of sticker arrangements: only six twists are stored and the
seventh is derived, and the permutation is ranked as a bijection. A single
corner turned in place therefore normalises into some *other*, perfectly legal
state, and the solver returns a confident solution that leaves the cube
scrambled. That is why `to_cubies()` checks all four conditions before ranking
rather than trusting the caller.

## Memory checking

Neither `valgrind` nor the GCC sanitizer runtimes are installed on the
development machine, so leaks were checked with an `LD_PRELOAD` allocation
accountant instead. Every subcommand and the test suite balance exactly: the
only blocks live at exit are glibc's own stdout buffer and, for the threaded
paths, libpthread's internal allocations.

`bfs.c` is the one module that keeps state between calls, so it has
`bfs_release()` to hand the mark array and level buffers back. The CLI, the
benchmark and the test suite all call it before returning, which is what keeps
that ledger balanced.
