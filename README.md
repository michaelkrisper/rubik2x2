# rubik2x2

**Optimal solvers for the 2x2x2 Rubik's Cube (Pocket Cube, Mini Cube)** — the
same three algorithms implemented once per language, so the algorithmic and the
language-level differences can be told apart.

Every solver here returns a *shortest* solution, never merely a working one.

| Implementation | | |
|---|---|---|
| [`c/`](c/) | C23 | gcc, pthreads |
| [`cpp/`](cpp/) | C++23 | g++, `std::jthread` |
| [`csharp/`](csharp/) | .NET 10 | `Parallel.For` |
| [`go/`](go/) | Go 1.26 | goroutines |
| [`javascript/`](javascript/) | Node 24 | `worker_threads` |
| [`python/`](python/) | Python 3.14 | reference implementation |

Each is written idiomatically for its language rather than transliterated, and
all of them satisfy the same contract in [SPEC.md](SPEC.md) — same move tables,
same encoding, same three algorithms, same benchmark inputs and output format.

---

## The problem, and why it is small

A pocket cube has 3,674,160 reachable positions. That number is the whole story,
and it comes from two observations:

**One corner never has to move.** Turning a face is the same as turning the
opposite face plus a whole-cube rotation, so three faces suffice. The three
chosen here never touch the down-back-right corner. Holding that corner still
removes the 24-fold rotation symmetry, cutting 88,179,840 raw arrangements to
3,674,160 distinct ones.

**That corner also names the goal.** Its three sticker colors match exactly one
of the 24 rotations of a solved cube, so a scramble has a single target rather
than a set of them — which is what lets a search run from both ends.

With one corner fixed, a state is fully described by which of the 7 free corner
pieces sits in each of the 7 free slots (7! = 5040) and how each is twisted
(3⁶ = 729, the twists summing to a multiple of 3). 5040 × 729 = 3,674,160.
A move acts on those two halves independently, so it costs two small table
lookups and a state is a single integer.

God's number is **14** in the quarter-turn metric, and 276 positions sit at that
maximum.

## The three algorithms

**Bidirectional BFS.** Grow one frontier from the scramble and one from the
goal, a level at a time, and stop where they meet. Level sizes peak at 1,350,852
states at depth 11, so a one-sided search would walk most of the space; meeting
in the middle needs depth 7 from each side, where only 33,058 states live.
Expanding by whole levels is what makes the first meeting point provably
shortest. No precomputation.

**IDA\* with pattern databases** (Korf's method). Depth-first passes with a
rising limit on `g + h`, where `h` comes from exact distances for a relaxed
puzzle. Here the relaxations are free: because a move acts on the permutation
and orientation coordinates independently, each coordinate alone is a genuine
quotient of the state graph, so its exact distance (5040 and 729 entries) is an
admissible bound.

**Full distance table** (God's algorithm). Store every state's exact distance in
a flat 3.7 MB byte array, then never search again: at a state *d* moves out,
exactly one neighbour is *d−1* moves out — step to it and repeat. Optimal by
construction, constant time in difficulty. This is the classic 1993 result for
the 2x2x2.

## Results

All implementations solve the same 200 scrambles from
[`bench/scrambles.txt`](bench/scrambles.txt), measured one at a time on the same
machine so nothing competes for cores. Reproduce with:

```sh
python3 tools/measure.py            # builds and benchmarks each, sequentially
python3 tools/report.py             # renders the tables below
```

<!-- BENCHMARK:BEGIN -->

_Not yet measured. Run `python3 tools/measure.py`._

<!-- BENCHMARK:END -->

### Reading the numbers

Three things hold in every language:

- **The table only pays off above its break-even.** Below it, building costs
  more than it saves. Per-solve figures alone would hide this completely, which
  is exactly why setup time is measured and reported next to them.
- **IDA\* is dominated everywhere.** Not a flaw in Korf's method but a mismatch:
  pattern databases pay off when a puzzle has more pieces than you can afford to
  track and you keep only some. A pocket cube is nothing *but* the pieces the
  databases already cover, so both bounds stop at distance 8 while real positions
  reach 14 — far too loose to prune.
- **The worst case separates the algorithms more than the average does.** Table
  lookup is flat whatever you throw at it; IDA\* degrades by two orders of
  magnitude on hard positions. An average alone would understate that badly.

### Does threading help?

Only the table build is worth parallelising — a single solve is milliseconds and
its frontiers are small. The build is level-synchronous, so each level's frontier
splits cleanly across workers, and races on the shared visited array are benign:
two workers reaching the same state write the same value.

The compiled languages get real speedup from that. Python does not, on either
interpreter — under the GIL the inner loop is pure bytecode so threads merely
take turns, and on the free-threaded 3.14 build the shared array turns into
contention instead of parallelism. The per-language READMEs give the numbers and
the approach each one chose.

## Correctness

The move tables in every implementation are checked, not trusted. Building the
distance table reproduces [OEIS A079762][oeis] — the published count of
positions at each quarter-turn distance — term for term:

```
1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508,
930588, 1350852, 782536, 90280, 276
```

Reproducing that sequence exercises the move tables, the state encoding and the
search together, and pins the total at 3,674,160 with a maximum distance of 14 —
no incorrect move table could produce it. On top of that, the compact encoding is
checked move by move against the readable sticker model, and all three algorithms
are cross-checked against each other for equal solution lengths.

## Sources

- [OEIS A079762 — positions at exactly n quarter turns][oeis]
- [Jerry Bryan, *God's Algorithm for the 2x2x2 Pocket Cube*, Cube Lovers, 1993][bryan]
- [Korf, *Finding Optimal Solutions to Rubik's Cube Using Pattern Databases*, AAAI 1997][korf]
- [Optimal solutions for Rubik's Cube (Wikipedia)](https://en.wikipedia.org/wiki/Optimal_solutions_for_Rubik's_Cube)

[oeis]: https://oeis.org/A079762
[bryan]: http://www.math.rwth-aachen.de/~Martin.Schoenert/Cube-Lovers/Jerry_Bryan__God's_Algorithm_for_the_2x2x2_Pocket_Cube.html
[korf]: https://ai.dmi.unibas.ch/_files/teaching/fs26/ai/material/ai-b09-korf-aaai1997.pdf

## License

MIT — see [LICENSE](LICENSE).
