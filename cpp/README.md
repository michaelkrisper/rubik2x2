# rubik2x2 — C++23

Optimal solver for the 2x2x2 Rubik's cube (pocket cube), implementing
[`SPEC.md`](../SPEC.md): three algorithms over the same 3,674,160-state model,
plus a parallel build of the distance table.

Standard library only — no external dependencies, including for the tests.

## Build

```sh
make            # builds ./rubik2x2
make test       # builds and runs ./rubik2x2_tests
make clean
```

Requires a C++23 compiler; developed against GCC 16. The build is
`-std=c++23 -O3 -march=native -flto` and is clean under `-Wall -Wextra -Werror`.

CMake is not installed on the reference machine, so this uses a plain Makefile.
It is a three-line change to port: one library of objects, one executable, one
test binary.

## Run

```sh
./rubik2x2 solve wppryygrygrwgprbbbwpbwyg                    # 14 moves, the antipodal position
./rubik2x2 solve <state> --algorithm ida                     # bfs (default), ida or table
./rubik2x2 solve -                                           # read the state from stdin
./rubik2x2 scramble -n 12 --seed 7
./rubik2x2 show <state>                                      # draw the unfolded net
./rubik2x2 table [--workers N]                               # build the table, print its histogram
./rubik2x2 bench [--json] [--scrambles FILE] [-n N]
```

`solve` prints nothing but the move sequence, so it pipes:

```sh
./rubik2x2 scramble -n 14 | ./rubik2x2 solve -
```

A state is 24 colour letters (`rwbypg`) in side order UP FRONT DOWN BACK LEFT
RIGHT, each face row-major. Moves are quarter turns; lowercase and uppercase are
inverses: `l/L` `r/R` `t/T` `b/B` `f/F` `a/A`.

Exit codes: 0 success, 1 the cube cannot be solved, 2 bad input or usage.

## Test

```sh
make test
```

One binary, a few dozen cases, about a second. It covers everything in section 4
of the spec:

- the distance table's histogram equals **OEIS A079762** — the single strongest
  check available, since no incorrect move table or encoding can reproduce it;
- every move has order 4, a move and its inverse cancel, opposite faces commute
  (and adjacent ones do not);
- exactly facelets 11, 13, 23 are fixed by the six solve moves;
- the encoding round-trips, in both directions, and agrees with the sticker
  model move by move over a long random walk;
- the three algorithms return equal-length solutions, matching the table's
  distances, and every solution actually solves its cube;
- the antipodal position needs exactly 14 moves;
- the parallel table build produces a byte-identical table at 1, 2 and 4
  workers.

The harness is [`tests/harness.hpp`](tests/harness.hpp) and its 40-line
implementation: `TEST(name) { ... }` registers a case, and `CHECK` / `CHECK_EQ`
/ `CHECK_NE` fail the case they are in while letting the rest run.

## Layout

| Path | What it holds |
|---|---|
| `include/rubik/cube.hpp`, `src/cube.cpp` | the readable 24-sticker model: moves, rotations, parsing |
| `include/rubik/coords.hpp`, `src/coords.cpp` | the compact index the solvers run on, and the move tables |
| `include/rubik/bfs.hpp`, `src/bfs.cpp` | bidirectional breadth-first search |
| `include/rubik/ida.hpp`, `src/ida.cpp` | IDA\* with the two pattern databases |
| `include/rubik/table.hpp`, `src/table.cpp` | the full distance table, serial and parallel builds |
| `include/rubik/display.hpp`, `src/display.cpp` | drawing a cube for a terminal |
| `include/rubik/bench.hpp`, `src/bench.cpp` | the benchmark and its JSON/text reports |
| `include/rubik/memtrack.hpp`, `src/memtrack.cpp` | a counting `operator new`, so `peak_mb` is measured |
| `src/main.cpp` | the command line |

The layering the spec asks for holds: `cube` knows nothing about searching or
drawing, `coords` knows nothing about which algorithm consumes its indices, and
`display` is never on any solver's path.

## Why the design looks like this

**One corner never moves.** Turning a face equals turning the opposite face plus
a whole-cube rotation, so three faces suffice, and the three chosen here
(`l/L`, `t/T`, `f/F`) never touch the down-back-right corner. Holding that corner
still removes the 24-fold rotation symmetry.

**That corner also names the goal.** Its three sticker colours match exactly one
of the 24 rotations of a solved cube, so a scramble has a single target rather
than a set of them — which is what lets a search run from both ends. The 24
goals and their piece colourings are computed at compile time (`consteval`), so
there is no static-initialisation order to worry about and no cost at startup.

**The encoding splits in two** because a move acts on the two halves
independently: which piece sits in each slot (7! = 5040) and how each is twisted
(3⁶ = 729, the twists summing to a multiple of 3). A move is then two lookups,
a state is one integer below 3,674,160, and both halves are, separately, exact
quotients of the state graph — which is where IDA\* gets its pattern databases
for free.

**The table wins only above its break-even.** It costs a quarter of a second to
build and then answers in a microsecond; bidirectional search costs nothing up
front and about a millisecond per solve. `bench` prints where the lines cross.

A note on what is *not* invariant: the encoded index is **not** the same for all
24 ways of holding one scramble. A whole-cube rotation relabels the slots and
the pieces together, which conjugates the permutation coordinate, so the index
moves. What is invariant — and what actually matters — is that every orientation
encodes successfully, that a solved cube always lands on index 0, and that the
solution length is the same however the cube is held. All three are tested.

## The parallel table build

`table-par` is the same breadth-first search over all 3,674,160 states, with
each level's frontier split across `std::jthread` workers. The search is
level-synchronous, so within a level the work is embarrassingly parallel; the
one hazard is the shared distance array, where two workers can reach the same
state at the same moment.

Both mechanisms the spec offers are used, because they solve different halves of
the problem:

- **Per-worker output buffers, merged at the level boundary.** Each thread
  collects the states it discovered into a `std::vector` of its own, on its own
  stack, and the buffers are concatenated once the level ends. No worker ever
  reads another's output. An earlier version had the workers push into slots of
  a shared `vector<vector<...>>`; the vector *headers* are 24 bytes apart, so
  every `push_back` bounced one cache line between cores.
- **An atomic compare-and-exchange per state,** via `std::atomic_ref` over the
  ordinary `std::vector<std::uint8_t>` the table already is. A relaxed load
  rejects the great majority of children (already claimed) before any CAS is
  attempted, and only the thread that wins the exchange queues the state. That
  is what keeps the merged frontier duplicate-free, and it is why the parallel
  build produces a byte-for-byte identical table — which the tests check at 1, 2
  and 4 workers.

`std::atomic_ref` is what makes this cheap: the table stays a plain byte vector,
usable at full speed by every reader afterwards, and only the build phase views
it atomically. Relaxed ordering is enough — the level barrier (joining the
threads) provides all the synchronisation the algorithm needs, and within a
level the only thing that must be exact is that one worker claims each state.

Levels below 4096 states are expanded on the calling thread; the first six
levels of the search hold fewer than a thousand states between them, and the
thread hand-off would cost more than the work.

### What it measured

On the reference machine — Intel i5-4258U, 2 physical cores / 4 threads, 3 MiB
L3 — best of eight runs of `./rubik2x2 table --workers N`:

| workers | build | speedup |
|---|---|---|
| 1 | 0.237 s | 1.00× |
| 2 | 0.139 s | 1.71× |
| 3 | 0.143 s | 1.66× |
| 4 | 0.118 s | 2.01× |

1.7× on the two physical cores, and 2.0× once both hyperthreads are used. That
is the expected shape for this loop: the inner step is a random access into a
3.5 MiB array that does not fit in a 3 MiB L3, so the search is
memory-latency-bound rather than compute-bound. That is exactly the case where
hyperthreading still buys something — a stalled thread leaves the core free —
but it also caps how far real cores can take it. Three workers on four hardware
threads land badly (two on one core, one on the other) and lose to two, which is
scheduling, not the algorithm.

The whole build is only 0.2 s, so the fixed costs are visible too: fourteen
thread launches and joins, and the concatenation of the per-worker buffers at
each level boundary. Bigger machines with more real cores and more last-level
cache should do considerably better; this one is honest about being small.

Solving a single cube is deliberately **not** parallelised, as the spec asks: a
solve is under a millisecond and the frontiers are far too small to pay for a
thread.

## How `peak_mb` is measured

C++ has no `tracemalloc`, and peak RSS never falls, so an algorithm measured
second would inherit the first one's high-water mark. `src/memtrack.cpp`
replaces the global `operator new`/`operator delete` with a pair that keeps a
running total of live bytes, which is the closest honest analogue. The figure
reported is what the algorithm still holds after its setup — the distance table,
the pattern databases — plus the high-water mark of one hard solve. Scratch
space the setup allocates and frees again is not counted, since it is not held
while solving.

Two caveats, stated rather than hidden: the counter sees heap allocations only,
so IDA\*'s two pattern databases (5,769 bytes, held in automatic storage as
`std::array`) do not appear in its figure; and allocator overhead is not
counted, only the sizes the program asked for.

## The optimization pass

Two changes, both measured on an idle machine and both kept because they paid.

**The frontier hash maps became one flat array** (`src/bfs.cpp`). A cube state is
already a dense index below 3,674,160, so hashing it to look it up was pure
overhead. The visited set is now one `uint32_t` per state packing the arriving
move, the parent, which frontier claimed it and a generation stamp — so a solve
bumps a counter instead of clearing 14.7 MB, and each edge costs one array read
and one write rather than a lookup, a second lookup and an insert.

| | before | after | |
|---|---|---|---|
| bfs, average | 0.302 ms | **0.077 ms** | 3.9× |
| bfs, worst case | 6.6 ms | **1.0 ms** | 6.6× |

The same change is in every other compiled implementation here, and it is the
single largest win the project found: 4.7× in C#, 4.5× in JavaScript, 4.4× in Go,
2.7× in C, 2.1× in Rust.

**Prefetching the frontier** (`include/rubik/prefetch.hpp`). Both hot loops walk
a frontier and touch one random word of a multi-megabyte array per edge. That
array does not fit in this machine's 3 MiB L3, so nearly every touch misses and
the loop is bound by memory latency rather than by arithmetic. Naming the
addresses a few entries ahead lets those misses overlap instead of stalling one
at a time; it also took the table build from 194 ms to 140 ms.

That both wins came from memory behaviour rather than from instruction count is
the theme of the whole comparison — see the root README.

## Benchmark

```sh
./rubik2x2 bench --scrambles ../bench/scrambles.txt          # human-readable report
./rubik2x2 bench --json --scrambles ../bench/scrambles.txt   # one JSON object, per the spec
```

With no `--scrambles`, `bench/scrambles.txt` is looked for relative to the
working directory, its parent, and its grandparent, so running from either `cpp/`
or the repository root works. Fast algorithms are repeated until the measured
window is long enough to mean something (a table solve is about two
microseconds, so it runs some hundred thousand times).
