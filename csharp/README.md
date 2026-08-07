# rubik2x2 — C#

An optimal solver for the 2x2x2 Rubik's Cube (Pocket Cube) on .NET 10, following
[`SPEC.md`](../SPEC.md): the same three algorithms, the same state model, the same
benchmark protocol as every other implementation in this repository.

```
csharp/
  src/Rubik2x2.Core/     the library
    CubeState.cs           24 stickers — the readable model
    Moves.cs               the twelve quarter turns, as facelet permutations
    CubeSymmetry.cs        the 24 ways of holding a cube
    Coordinates.cs         the dense index the solvers run on
    CoordinateMoveTables.cs  a move as two array lookups
    Solvers/               BidirectionalSearch, IdaStarSolver + PatternDatabases,
                           DistanceTable + ParallelFrontier, Algorithm
    CubeRenderer.cs        the unfolded net, for terminals
    Benchmarking/          the measurement protocol and its JSON shape
  src/Rubik2x2.Cli/      the `rubik2x2` executable
  tests/Rubik2x2.Tests/  xunit
```

The three layers are kept apart on purpose. `CubeState` is 24 bytes a human can
read; `Coordinates` is the integer below 3,674,160 a search wants; `CubeRenderer`
is the only code that knows what a cube looks like. Nothing in the first layer
knows about the second, and nothing in either knows about the third.

## Requirements

.NET SDK 10 or later. No NuGet packages beyond the framework and xunit, which is
what `dotnet new xunit` gives you.

## Build, run, test

```sh
cd csharp
dotnet build -c Release            # zero warnings; warnings are errors here
dotnet test -c Release             # 45 tests, including the OEIS histogram check
```

Run the CLI straight from the source tree:

```sh
dotnet run -c Release --project src/Rubik2x2.Cli -- solve wppryygrygrwgprbbbwpbwyg
```

or publish it once and use the binary, which starts about 60 ms faster:

```sh
dotnet publish src/Rubik2x2.Cli -c Release -o out
./out/rubik2x2 solve wppryygrygrwgprbbbwpbwyg --algorithm table
```

`dotnet publish -c Release -o publish` on the solution works too, and is what
`tools/measure.py` runs; the test project is marked unpublishable so only the
executable and the library land there (about 230 KB).

## The command line

```
rubik2x2 solve <state> [--algorithm bfs|ida|table]   # prints only the moves, so it pipes
rubik2x2 scramble [-n N] [--seed S]                  # prints a state string
rubik2x2 table                                       # builds the table, prints the histogram
rubik2x2 bench [--json] [--scrambles FILE] [-n N]    # the benchmark of SPEC.md section 6
rubik2x2 show <state> [--color | --no-color]         # draws the unfolded net
```

A state is 24 colour letters (`rwbypg`) in side order UP FRONT DOWN BACK LEFT
RIGHT, each side row-major; `-` reads it from stdin. The solved cube is
`yyyyrrrrwwwwppppbbbbgggg`. Solutions use `l/L`, `t/T` and `f/F` — lowercase and
uppercase are inverse quarter turns.

```sh
./out/rubik2x2 table
1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508, 930588, 1350852, 782536, 90280, 276
3,674,160 states, maximum distance 14, built in 406 ms (3.7 MB)

./out/rubik2x2 scramble --seed 7 -n 12 | ./out/rubik2x2 solve -
```

## Why it is shaped this way

**One corner never moves.** The six solve moves (`l L t T f F`) leave the
down-back-right corner alone, which removes the 24-fold rotation symmetry and
makes the goal a single state instead of a set of 24. That corner's own colours
say which rotation of the solved cube a scramble is aiming at, so a cube handed
over in any orientation still encodes correctly.

**The encoding splits in two.** With that corner held still, a state is which of
the 7 free pieces sits in each of the 7 free slots (7! = 5040) and how each is
twisted (3⁶ = 729, the twists summing to a multiple of three). A move acts on
those halves independently, so applying one is two array reads rather than 24
sticker moves — and each half alone is a genuine quotient of the state graph,
which is where IDA\* gets its pattern databases for free.

**The table wins only above its break-even.** Building it is one exhaustive sweep
over 22 million edges; after that a solve is about 500 ns — a handful of byte
reads. On the machine below that is 250–600 ms of setup against roughly 1 ms per
bidirectional search, so the table pays for itself at a few hundred cubes and is
the wrong tool for one. `rubik2x2 bench` prints that crossover.

## The parallel build

`table-par` is the same distance table built by `DistanceTable.BuildParallel`.
Only the build is parallel; solving a single cube is milliseconds of work on a
small frontier and is deliberately left alone, as the spec requires.

Breadth-first search is level-synchronous, so within a level the frontier is a
list of independent nodes that can be sliced across workers and merged at the
barrier. Two decisions were open, and both were measured:

**Claiming a state.** .NET has no atomic operation on a single `byte`, so the
distance array cannot be the point of synchronisation. Rather than widen it to
`int` (14 MB instead of 3.7 MB), the workers claim states in a side bitset of
`uint` words with `Interlocked.CompareExchange`: whoever sets the bit owns the
state and is the only thread to write its distance byte and append it to its own
output buffer. The bitset costs 460 KB, the common case — a state already visited
— stays a plain read with no bus traffic, and the result is deterministic: the
parallel table is byte-for-byte the sequential one, which the test suite asserts
over all 3,674,160 states. The alternative, per-worker buffers with no claim at
all, would also produce correct distances but would let two workers queue the same
state, so the frontier — and the work — would depend on timing.

**Owning the threads.** The first version used `Parallel.For` over the frontier
slices. It worked, but the figure was not repeatable: whole levels take a few
milliseconds and the entire build a few hundred, which is the same timescale as
the thread pool's ramp-up, so the build measured anywhere between 1.0x and 2.0x
the sequential one depending on how warm the pool was. Dedicated threads around a
`Barrier` — one per worker, alive for the whole search, with the merge in the
barrier's post-phase action — removed that variance at the cost of about
`workers` thread starts per build.

Measured on an Intel i5-4258U (2 cores, 4 threads, 2.4 GHz), best of six builds
each, alternating so that thermal drift hits every configuration equally:

| build | best | median |
|---|---|---|
| sequential | 244 ms | 267 ms |
| parallel, 1 worker | 322 ms | 349 ms |
| parallel, 2 workers | 181 ms | 205 ms |
| parallel, 4 workers | 162 ms | 195 ms |

So the atomic claim and the barrier cost about 30% against the sequential build,
two workers pay that back and more, and the fourth thread adds almost nothing —
the two extra threads are hyperthreads on the same two cores, and the search is
memory-latency bound anyway: every edge is a random read into a 3.7 MB array.
On this dual-core laptop the honest number is **about 1.5x**, and the benchmark's
`table-par` entry drops further towards 1.3x because by then the machine has been
under load for ten seconds and is clocking down. On a machine with more real cores
the same code should scale further, but this repository reports what it measured.

## Measurement notes

`rubik2x2 bench --json` emits exactly the object `SPEC.md` section 6 defines.
Two fields need a word on how they are read here:

- `setup_ms` excludes the coordinate move tables (a few milliseconds), which all
  three algorithms share and which the reference implementation also builds before
  its clock starts. It is a fresh build of what the algorithm alone needs: nothing
  for `bfs`, the two pattern databases for `ida`, the distance table for `table`.
- `peak_mb` is "additional memory held while solving", best effort: what the
  algorithm keeps resident (3.7 MB of table, 5.8 KB of pattern database, nothing
  for `bfs`) plus what one solve of the antipodal position allocates, from
  `GC.GetAllocatedBytesForCurrentThread`. The runtime has no cheap true peak-RSS
  probe, and for these three the allocation total is the interesting half.

Timings are wall clock via `Stopwatch`, each measurement repeated until it has run
for about 250 ms, after a warm-up pass so tiered compilation has settled and after
a full GC so no measurement pays for the previous one's garbage.

## Tests

`dotnet test` runs everything SPEC.md section 4 asks for:

- the distance histogram equals OEIS A079762 — 1, 6, 27, 120, 534, 2256, 8969,
  33058, 114149, 360508, 930588, 1350852, 782536, 90280, 276 — which no incorrect
  move table or encoding can reproduce;
- the parallel build produces exactly the same table;
- every move has order four, a move and its inverse cancel, opposite faces commute;
- exactly facelets 11, 13 and 23 are fixed by the six solve moves;
- the encoding round-trips both ways, and the ranks are bijections;
- all three algorithms return equal-length solutions, every solution actually
  solves its cube, and the antipodal position needs exactly 14 moves;
- unreachable cubes — a single twisted corner, an impossible corner colouring —
  are rejected rather than silently solved.
