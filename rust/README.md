# rubik2x2 — Rust

Optimal solvers for the 2x2x2 Rubik's cube: three algorithms that all return a
*shortest* move sequence, plus the exhaustive distance table that proves they
do. Rust 2024, stable toolchain, **no dependencies** — standard library only,
including the parallel table build.

## Build, run, test

```sh
cargo build --release                      # tuned: opt-level 3, fat LTO, one codegen unit
cargo test                                 # 30 tests, ~5 s, OEIS check included
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

MEASUREMENT_PLACEHOLDER

## Benchmark

`bench` follows the protocol in `../SPEC.md`: the 200 shared scrambles from
`bench/scrambles.txt` (found automatically if the binary is run from the
repository), plus the antipodal position `wppryygrygrwgprbbbwpbwyg` as the worst
case. Every figure is repeated until it has run for at least 250 ms, so a
3-microsecond table lookup still gets a meaningful clock reading; setup is
timed three times and the fastest kept.

BENCHMARK_PLACEHOLDER

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
- **The table's break-even is much later than in a slow language.** Building it
  costs the same milliseconds either way, but each search it replaces is now
  cheap, so it takes far more solves to pay the build off. Per-solve numbers
  alone would hide that entirely, which is why setup is measured too.

[oeis]: https://oeis.org/A079762
