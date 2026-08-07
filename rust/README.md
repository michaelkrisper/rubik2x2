# rubik2x2 — Rust

Optimal solvers for the 2x2x2 Rubik's cube: three algorithms that all return a
*shortest* move sequence, plus the exhaustive distance table that proves they
do. Rust 2024, stable toolchain, **no dependencies** — standard library only,
including the parallel table build.

## Build, run, test

```sh
cargo build --release                      # tuned: opt-level 3, fat LTO, one codegen unit
cargo test                                 # 35 tests, ~5 s, OEIS check included
cargo clippy --release --all-targets -- -D warnings
```

```sh
rubik2x2 solve <state> [--algorithm bfs|ida|table]   # default bfs; prints the moves only
rubik2x2 scramble [-n N] [--seed S]                  # prints a state string
rubik2x2 table                                       # builds the table, prints the histogram
rubik2x2 bench [--json] [--scrambles FILE] [-n N]    # benchmark
```

A state is 24 colour letters (`rwbypg`) in side order UP FRONT DOWN BACK LEFT
RIGHT, each face read row-major; `-` reads it from stdin instead. `solve` prints
nothing but the move sequence, so it pipes:

```sh
$ ./target/release/rubik2x2 solve wppryygrygrwgprbbbwpbwyg
tlFllFtLTLLTLL
$ ./target/release/rubik2x2 scramble -n 14 --seed 1 | ./target/release/rubik2x2 solve -
```

`table` prints the histogram on stdout and its commentary on stderr, so
`rubik2x2 table 2>/dev/null` gives the bare sequence:

```
1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508, 930588, 1350852, 782536, 90280, 276
```

That is [OEIS A079762][oeis] — the published count of positions at each
quarter-turn distance. Reproducing it exercises the move tables, the encoding
and the search at once, and no incorrect implementation can fake it, so
`tests/table.rs` asserts it term for term. It runs as part of the ordinary
`cargo test` (about a second, thanks to `[profile.test] opt-level = 1`) and is
**not** `#[ignore]`d; `cargo test --release` is faster still.

## Layout

| file | layer |
|---|---|
| `src/cube.rs` | the readable model: 24 stickers, twelve permutation tables, parse/format |
| `src/coords.rs` | the compact model: one `u32` per state, two lookup tables per move |
| `src/display.rs` | drawing an unfolded net; neither model knows about it |
| `src/bfs.rs` `src/ida.rs` `src/table.rs` | the three algorithms |
| `src/bench.rs` `src/main.rs` | measurement and CLI |
| `src/error.rs` `src/hash.rs` `src/rng.rs` `src/memory.rs` | one error type, an integer hasher, SplitMix64, allocation accounting |

Why the split: a search that allocates a fresh 24-byte array per move would
spend its life copying stickers. Holding the down-back-right corner still — the
one the six solve moves never touch — removes the 24-fold whole-cube-rotation
symmetry and leaves 7 pieces in 7 slots (7! = 5040) each twisted one of three
ways (3⁶ = 729, the twists summing to a multiple of three). 5040 × 729 =
3,674,160 states, so a state is an integer and a move is two array lookups. The
sticker model stays around as the thing the tests check the compact one against.

### Impossible cubes

Four conditions are rejected, and only the first belongs to the parser:

1. not 24 letters from `rwbypg`, or a colour that does not appear four times;
2. three facelets that form no corner of this cube (`NoCornerAt`);
3. one piece in two slots, so the arrangement is no permutation (`RepeatedPiece`);
4. twists that do not sum to a multiple of three (`TwistedCorner`).

3 and 4 live in `coords::to_cubies`, before anything is ranked, because ranking
is lossy on purpose: only six twists are stored and the seventh is derived, so
an impossible cube does not fail the ranker — it *normalises* into some other,
legal cube, and the solver then answers confidently about the wrong puzzle. The
classic case is a solved cube with one corner twisted in place
(`pyyyrrrrwwwwppbpybbbgggg`), which looks almost solved and is unreachable.
Three corners twisted the *same* way do sum to zero mod three, are genuinely
reachable, and are accepted — `tests/coords.rs` and `tests/solvers.rs` pin both
sides of that line, and assert the specific error variant rather than merely
that something failed.

The fixed corner does double duty: its three colours match exactly one of the 24
rotations of a solved cube, so it names the goal a scramble is aiming at and the
goal index is always 0 however the cube is held. Note what that does *not* mean —
two rotations of the same scramble encode to *different* indices (the rotation is
conjugated into the permutation) and get different, equally short, solutions. The
distance is what is preserved, and `tests/solvers.rs` pins exactly that.

## The parallel table build

`DistanceTable::build_parallel` is the `table-par` row of the benchmark. The
breadth-first search is level-synchronous, so within a level the frontier can be
split freely; the only shared mutable state is the 3.7 MB visited array, where
two workers can reach the same state at the same instant.

**Chosen: one `AtomicU8::compare_exchange` per candidate state**, over a
`Vec<AtomicU8>`, with each worker collecting the states it won into its own
`Vec` and the level being the concatenation of those after `join`. Why this and
not per-worker buffers merged at the level boundary:

- The claim *is* the deduplication. A merge-based scheme still has to
  deduplicate — two workers can queue the same state — so it needs a second
  pass over the level and its own visited marking; the CAS collapses both into
  one instruction on a byte the worker was going to touch anyway.
- Contention is negligible in practice. By the time a level is large enough to
  be worth splitting, the overwhelming majority of children are already visited,
  and those are settled by a plain relaxed *load* — the exchange only runs on a
  genuine first visit, of which there are 3.6 million across the whole build.
- `Relaxed` ordering suffices: a compare-exchange is atomic on its own location
  whatever the ordering, so no state is ever claimed twice, and joining the
  workers at the end of each level is what publishes one level's writes to the
  next. No fences, no `unsafe`.

The result is deterministic — byte-for-byte identical to the single-threaded
build, which `tests/table.rs` asserts and the benchmark re-checks before it
reuses the serial per-solve figures. Only the order of states *within* a level
varies, and nothing reads that.

Levels below 4096 states run on the calling thread; the first six levels hold
694 states between them, less than the cost of spawning.

### What it measured

On a 2-core / 4-thread Haswell laptop (4 MB L3), best of three builds each:

| build | setup |
|---|---|
| `table` (one thread) | 245 ms |
| `table-par` (4 workers) | 120 ms |

**2x on four workers, and that is close to the ceiling here.**
`available_parallelism()` reports four hyperthreads on *two* physical cores, and
the search is memory-latency bound rather than compute bound — 22 million
effectively random accesses into a 3.7 MB array that only just fits in L3 — so
the second thread on a core adds much less than the second core did. Repeated
runs land between 1.3x and 2.0x depending on what else the machine is doing;
the figures above were taken with it otherwise idle.

Two things that turned out to matter more than the synchronisation scheme:

- **Cache residency dominates.** Building the table with *another* 3.7 MB table
  already resident measures nearly twice as slow (245 ms → 450 ms), which is why
  `bench` reduces its reference table to the 201 distances it actually needs
  before timing anything.
- **Hoisting the move tables into local slices** ahead of the loop is worth
  about a fifth of the build time on its own — the same 22 million edges, one
  less indirection each.

## Benchmark

`bench` follows the protocol in `../SPEC.md`: the 200 shared scrambles from
`bench/scrambles.txt` (found automatically if the binary is run from the
repository), plus the antipodal position `wppryygrygrwgprbbbwpbwyg` as the worst
case. Every figure is repeated until it has run for at least 250 ms, so a
sub-microsecond table lookup still gets a meaningful clock reading; setup is
timed three times and the fastest kept.

```
algorithm    setup ms    avg ms   worst ms   peak MB  optimal
-------------------------------------------------------------
bfs               0.0    0.3408     4.9186       1.8      yes
ida               0.1    0.1981    23.3485       0.0      yes
table           245.2    0.0007     0.0004       3.7      yes
table-par       120.4    0.0007     0.0004       3.7      yes
```

```json
{
  "language": "Rust",
  "version": "1.97.1",
  "build": "rustc 1.97.1, release, opt-level=3, lto=fat, codegen-units=1",
  "algorithms": [
    {"name": "bfs", "setup_ms": 0.0, "avg_ms": 0.3408, "worst_ms": 4.9186, "peak_mb": 1.8, "optimal": true},
    {"name": "ida", "setup_ms": 0.1, "avg_ms": 0.1981, "worst_ms": 23.3485, "peak_mb": 0.0, "optimal": true},
    {"name": "table", "setup_ms": 245.2, "avg_ms": 0.0007, "worst_ms": 0.0004, "peak_mb": 3.7, "optimal": true},
    {"name": "table-par", "setup_ms": 120.4, "avg_ms": 0.0007, "worst_ms": 0.0004, "peak_mb": 3.7, "optimal": true, "workers": 4}
  ]
}
```

`peak_mb` is real, not an estimate: the binary installs a counting global
allocator (`src/memory.rs`) and reports the high-water mark from the moment
setup finished, minus what was already live — so the 3.7 MB the distance table
*keeps* counts, and the harness's own memory does not. That is the one place
this crate uses `unsafe`, because `GlobalAlloc` is an unsafe trait; every method
forwards straight to `System`.

Two findings worth reading off:

- **IDA\* beats bidirectional search here**, which it does not in the Python
  reference. Its inner loop is a handful of array lookups with no allocation at
  all, while bidirectional BFS pays for two hash maps; in a language where the
  constant factors are this small, the algorithm with the tighter loop wins on
  average even though it explores far more nodes. On the worst case the ordering
  flips back, sharply — the pattern databases reach only distance 8 of 14, so a
  14-move position makes IDA\* enumerate millions of nodes.
- **The table's break-even is much later than in a slow language.** It overtakes
  bidirectional search at about 720 solves here, against roughly 280 for the
  Python reference: building it costs the same milliseconds either way, but each
  search it replaces is now cheap, so it takes far more solves to pay the build
  off. Per-solve numbers alone would hide that entirely, which is why setup is
  measured too. `bench` without `--json` prints that crossover and the
  total-time-per-workload table.

[oeis]: https://oeis.org/A079762
