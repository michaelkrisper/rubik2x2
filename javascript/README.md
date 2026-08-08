# rubik2x2 — JavaScript

An optimal solver for the 2×2×2 Rubik's Cube (pocket cube), in modern
JavaScript for Node.js 24. Three algorithms, all of them returning a *shortest*
solution, plus the full God's-algorithm distance table.

No dependencies — not even development ones. Tests run on `node:test`.

## Running it

```sh
node src/cli.js solve wppryygrygrwgprbbbwpbwyg            # 14 moves, the antipode
node src/cli.js solve <state> --algorithm ida             # bfs (default) | ida | table
node src/cli.js scramble -n 12 --seed 7                   # a reproducible state string
node src/cli.js show <state>                              # the unfolded net, in colour
node src/cli.js table                                     # build the table, print the histogram
node src/cli.js bench --json                              # the shared 200-scramble benchmark
```

`solve` writes nothing to stdout but the move sequence, so it pipes:

```sh
node src/cli.js scramble | node src/cli.js solve -
```

Installing the package (`npm link`, or `npm i -g .`) puts the same program on
`PATH` as `rubik2x2`.

## Testing

```sh
node --test          # or: npm test
```

Around 80 assertions covering the move algebra, the encoding, the three solvers
and the CLI. The one that matters most is in `test/table.test.js`: the distance
histogram must equal [OEIS A079762][oeis]

```
1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508, 930588, 1350852, 782536, 90280, 276
```

which no wrong permutation table, slot list or ranking function could reproduce.
The suite takes about half a minute, most of it building distance tables.

## Layout

```
src/cube.js           24 stickers and twelve permutations — the readable model
src/coords.js         the dense 3,674,160-state index the solvers run on
src/search.js         bfs   — bidirectional breadth-first search
src/ida.js            ida   — Korf's IDA* with two pattern databases
src/table.js          table — God's algorithm, serial and parallel builds
src/table-worker.js   one worker of the parallel build
src/solvers.js        the three algorithms behind one interface
src/display.js        the unfolded net, in colour or letters
src/bench.js          the shared benchmark protocol
src/cli.js            argument parsing and the subcommands
```

Each module explains *why* it exists in its header comment. In short:

- **One corner is held still.** The six solve moves `l L t T f F` never touch
  the down-back-right corner, which removes the 24-fold whole-cube-rotation
  symmetry and makes the state space exactly 3,674,160 rather than 24× that.
- **The index splits in two.** A move acts on *which piece sits where* (7! =
  5040) and on *how each piece is twisted* (3⁶ = 729) independently, so a move
  is two array lookups, each coordinate alone is an admissible heuristic for
  IDA\*, and the whole state is one integer small enough to index a 3.7 MB byte
  array.
- **Impossible cubes are rejected, not solved.** Because the rank keeps six of
  the seven twists and derives the seventh, an unvalidated encoding would map an
  unreachable arrangement onto some *other*, legal state and hand back a
  confident sequence that leaves the cube unsolved. `toCubies` therefore checks
  all four conditions of `SPEC.md` §2 — an impossible fixed corner, a slot whose
  colours are no corner of this cube, a piece in two slots, and twists that do
  not sum to a multiple of three — and `solve` exits 1 with
  `No solution: …` on stderr.
- **The table wins only above its break-even.** It costs one exhaustive search
  up front and then answers in microseconds; below roughly a thousand solves,
  searching each cube from scratch is cheaper. `node src/cli.js bench` prints
  the crossover for the machine it runs on.

TypedArrays carry everything hot: `Uint8Array` for states and distances,
`Int32Array` for the move tables and the breadth-first queue. The queue is a
single `Int32Array(3_674_160)` holding every state in visit order, with level
boundaries as indices into it, so the build never grows an array.

## `bfs` without a Map

`search.js` used to keep a `Map` per frontier. A `Map` is the wrong shape here:
a cube state is *already* a dense integer below 3,674,160, so hashing it is work
spent deriving an index from something that is one, and ~90,000 entries per
solve is ~7 MB of garbage handed to the collector on every call.

It is now a module-level `Uint16Array(3_674_160)` subscripted by the state
itself. One load says whether a state has been seen, by which frontier and how
deep; one store records a child. Each mark packs a distance in bits 0–3, the
frontier in bit 4, and a **generation stamp** in bits 5–15. The stamp is what
makes reuse cheap: 7.3 MB is far too much to clear per solve, so a solve bumps
a counter and every mark an earlier solve left behind reads as unvisited; the
real clear happens once per 2,046 solves. Sixteen-bit marks halve the array
against a 32-bit layout, and the cache footprint is the point — every edge of
the search loads one random word of it.

Storing a *distance* rather than a parent state is what lets a mark fit in 16
bits at all — a parent needs 22. Paths are rebuilt without parents because
breadth-first marks are exact: from a node at distance `d`, a neighbour at
`d - 1` under the same stamp and frontier is a parent, and only six to try.
That is a few dozen lookups once per solve, against a store saved on each of
~150,000 edges; the store that remains is a constant for the whole level.

The six move tables are hoisted into locals and the expansion unrolled against
them, the same shape `ida.js` uses. This is not cosmetic — the readable
`tables[position].permutation[q]` form measured **0.34 ms** average against
**0.20 ms** unrolled, because V8 cannot keep the indexed load monomorphic.

Against the two-`Map` version: **0.864 → 0.196 ms** average and
**16.9 → 3.0 ms** on the antipodal position, best of five runs each. `peak_mb`
goes from 6.8 to 15.29 and stops being per-solve garbage — the search now rents
the whole state space once instead of paying for it on every call.

## The parallel table build (`table-par`)

`DistanceTable.buildParallel()` splits the same breadth-first search across
`node:worker_threads`. Breadth-first search is level-synchronous, so within a
level the frontier is independent work; the main thread owns the level
structure and hands each worker a contiguous slice of the current frontier.

Two shared structures live in `SharedArrayBuffer`s: the 3.7 MB visited/distance
array and the 14.7 MB queue.

**The hazard** is that two workers can reach the same state inside one level.
This implementation resolves it with an atomic compare-and-exchange per
candidate state:

```js
if (distances[c] === 0 && Atomics.compareExchange(distances, c, 0, marker) === 0) found[count++] = c;
```

The plain read in front is what makes this affordable — about 85% of the 22
million edges lead somewhere already visited and never touch an atomic. Exactly
one worker can win the exchange, so exactly one enqueues the state, and the
result is byte-for-byte identical to the serial build (asserted in
`test/table.test.js`).

Newly claimed states go into a per-worker buffer and are appended to the shared
queue at the level boundary, after a single `Atomics.add` reserves a contiguous
block — one atomic per worker per level instead of one per state. Level
barriers use `postMessage`/reply rather than `Atomics.wait`: there are only 15
levels, so the round trips are noise, and the code stays readable.

### What it measured: no speedup here

On the development machine — an Intel i5-4258U, **2 physical cores** (4 logical),
`powersave` governor — the parallel build never beat the serial one. Best of
three runs, warm process:

| build     | total  | BFS phase (excludes worker startup) |
| --------- | ------ | ----------------------------------- |
| serial    | 344 ms | —                                   |
| 1 worker  | 544 ms | 480 ms                              |
| 2 workers | 523 ms | 441 ms                              |
| 4 workers | 479 ms | 387 ms                              |
| 8 workers | 680 ms | 445 ms                              |

Read the second column first: the parallel search *does* speed up with more
workers (480 → 387 ms, ~1.24×), but it starts from a much worse baseline than
the serial loop and never catches up. Three costs, none of which parallelism can
pay back here:

1. **Shared-memory overhead, ~40%.** One worker doing all the work — every
   compare-and-exchange executed, no contention possible — takes 480 ms against
   the serial loop's 344 ms. A locked instruction per newly discovered state is
   3.7 million of them, and the per-worker buffer adds a second pass over every
   state before it reaches the shared queue.
2. **Worker startup, 65–100 ms** for one to four workers (measured separately) —
   a fifth of the entire serial build, spent before any search happens, because
   each thread is a fresh V8 isolate that must load and JIT the module.
3. **Two cores, and a memory-bound loop.** A control experiment — workers doing
   nothing but a tight arithmetic loop, no shared memory at all — gave 896 ms
   for one worker, 1029 ms for two (1.7× throughput) and 2183 ms for four
   (1.6×): hyper-threading adds nothing, so there are ~1.7 cores' worth of
   compute available. The build's inner loop is a random read-modify-write into
   a 3.7 MB array against a 3 MB shared L3, so the two threads compete for cache
   and memory rather than for arithmetic units, and it realises only 1.24× of
   that 1.7×.

So this is a measured non-result rather than a limitation of the language:
`SharedArrayBuffer` plus `Atomics` gives JavaScript genuine shared-memory
parallelism, and the implementation is correct — the parallel table is
byte-identical to the serial one. On a machine with more cores and more L3 the
same code should pay off across the ~3.4 million states in levels 9–13, where
all the work is. It does not pay off on two Haswell cores. Note also that
benchmark numbers on a loaded laptop swing by a factor of three run to run, and
under load `table-par` sometimes "wins" simply by claiming more scheduler
attention; the table above was taken on an idle machine.

## Benchmark output

`node src/cli.js bench --json` prints exactly one JSON object in the format
`SPEC.md` §6 fixes, with entries for `bfs`, `ida`, `table` and `table-par`.
Notes on how the numbers are produced:

- `setup_ms` is the algorithm's own precomputation, timed on its first call in a
  cold process. Building the shared move tables (~10 ms) is charged to none of
  them, since all four need it and `bfs`'s setup is defined to be zero.
- `avg_ms` and `worst_ms` repeat the workload until at least a second has
  passed, so a 5 µs table lookup is measured rather than the clock.
- `peak_mb` is best effort: what the algorithm keeps resident (its pattern
  databases, its distance table, or `bfs`'s 7.3 MB mark array) plus the heap
  growth of one hard solve, after forcing a collection. It is read *after* the
  algorithm has run, because `bfs` allocates its marks on the first solve and
  then keeps them.
- `table-par` reports its own `setup_ms` and inherits `table`'s solve figures:
  only the build is parallelised, and the two builds produce the same bytes.

[oeis]: https://oeis.org/A079762
