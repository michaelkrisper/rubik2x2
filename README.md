# rubik2x2

**Optimal solvers for the 2x2x2 Rubik's Cube (Pocket Cube, Mini Cube)** — the
same three algorithms implemented once per language, so the algorithmic and the
language-level differences can be told apart.

Every solver here returns a *shortest* solution, never merely a working one.

| Implementation | | |
|---|---|---|
| [`c/`](c/) | C23 | gcc, pthreads |
| [`cpp/`](cpp/) | C++23 | g++, `std::jthread` |
| [`csharp/`](csharp/) | .NET 10 | threads around a `Barrier` |
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
[`bench/scrambles.txt`](bench/scrambles.txt) — the same file, not each language's
own random generator — measured one at a time so nothing competes for cores.
Machine: Intel i5-4258U, 2 physical cores, 4 threads, 3 MiB L3, Fedora 44.

```sh
python3 tools/measure.py            # builds and benchmarks each, sequentially
python3 tools/report.py             # renders the tables below
```

<!-- BENCHMARK:BEGIN -->

| Language | Version | Table build | Per solve | Break-even | Threaded build |
|---|---|---|---|---|---|
| C | gcc 16.1.1 | 151 ms | 0.0004 ms | 673 solves | 1.72× (4 threads) |
| C++ | C++23 (GCC 16.1.1) | 194 ms | 0.00049 ms | 642 solves | 1.99× (4 threads) |
| Rust | 1.97.1 | 228 ms | 0.0006 ms | 707 solves | 1.86× (4 threads) |
| Go | 1.26.5-X:nodwarf5 | 229 ms | 0.001 ms | 305 solves | 1.26× (4 threads) |
| C# | 10.0.10 | 239 ms | 0.00075 ms | 239 solves | 1.31× (4 threads) |
| JavaScript | 24.18.0 | 360 ms | 0.002 ms | 431 solves | 0.77× (4 threads) |
| Python | 3.14.6 | 4,686 ms | 0.057 ms | 1,630 solves | 0.59× (4 threads) |

### What it cost to get there

| Language | Build from clean | Artifact | Source files | Source lines | Test lines |
|---|---|---|---|---|---|
| C | 2.0 s | 56 KB | 17 | 2,126 | 426 |
| C++ | 31.6 s | 257 KB | 9 | 1,339 | 491 |
| Rust | 11.0 s | 601 KB | 13 | 1,269 | 366 |
| Go | 9.9 s | 2.1 MB | 15 | 2,221 | 805 |
| C# | 3.0 s | 224 KB | 30 | 2,103 | 408 |
| JavaScript | — | 131 KB | 11 | 1,716 | 694 |
| Python | — | 59 KB | 11 | 1,256 | 528 |

### What it costs just to start

| Language | Startup | Resident at startup | vs. one table lookup |
|---|---|---|---|
| C | 1.5 ms | 1.6 MB | 3,786× |
| C++ | 3.0 ms | 4.0 MB | 6,033× |
| Rust | 1.9 ms | 2.2 MB | 3,157× |
| Go | 8.5 ms | 2.9 MB | 8,528× |
| C# | 51.4 ms | 26.9 MB | 68,579× |
| JavaScript | 192 ms | 56.2 MB | 96,113× |
| Python | 236 ms | 33.1 MB | 4,118× |

### Every measurement

**Setup — one-time precomputation, milliseconds**

| Language | bfs | ida | table | table-par |
|---|---|---|---|---|
| C | 0 | 0.12 | 151 | 87.5 |
| C++ | 0 | 0.119 | 194 | 97.4 |
| Rust | 0 | 0.1 | 228 | 123 |
| Go | 0 | 0.24 | 229 | 182 |
| C# | 0 | 0.544 | 239 | 183 |
| JavaScript | 0 | 7.7 | 360 | 466 |
| Python | 0 | 3.5 | 4,686 | 7,936 |

**Average per solve, milliseconds (200 scrambles)**

| Language | bfs | ida | table | table-par |
|---|---|---|---|---|
| C | 0.225 | 0.124 | 0.0004 | 0.0004 |
| C++ | 0.302 | 0.161 | 0.00049 | 0.00049 |
| Rust | 0.324 | 0.189 | 0.0006 | 0.0006 |
| Go | 0.753 | 0.3 | 0.001 | 0.001 |
| C# | 1.0 | 0.297 | 0.00075 | 0.00075 |
| JavaScript | 0.838 | 0.887 | 0.002 | 0.002 |
| Python | 2.9 | 9.9 | 0.057 | 0.048 |

**Worst case — the antipodal position, milliseconds**

| Language | bfs | ida | table | table-par |
|---|---|---|---|---|
| C | 3.8 | 13.2 | 0.0003 | 0.0003 |
| C++ | 6.6 | 18.0 | 0.00025 | 0.00025 |
| Rust | 4.5 | 21.6 | 0.0004 | 0.0004 |
| Go | 11.7 | 33.3 | 0.001 | 0.001 |
| C# | 6.4 | 28.8 | 0.00048 | 0.00048 |
| JavaScript | 16.9 | 96.3 | 0.001 | 0.001 |
| Python | 46.9 | 1,124 | 0.07 | 0.043 |

**Peak memory while solving, MB**

| Language | bfs | ida | table | table-par |
|---|---|---|---|---|
| C | 1.4 | 0.01 | 3.7 | 3.7 |
| C++ | 3.9 | 1.4e-05 | 3.7 | 3.7 |
| Rust | 1.8 | 0 | 3.7 | 3.7 |
| Go | 2.7 | 0.006 | 3.7 | 3.7 |
| C# | 6.8 | 0.006 | 3.7 | 3.7 |
| JavaScript | 6.8 | 1.2 | 3.7 | 3.7 |
| Python | 9.4 | 0.0084 | 3.7 | 3.7 |

Builds: C C23, -O3 -march=native -flto; C++ g++ -std=c++23 -O3 -march=native -flto; Rust rustc 1.97.1, release, opt-level=3, lto=fat, codegen-units=1; Go gc, linux/amd64, optimised; C# .NET 10.0.10, Release, TieredPGO, x64; JavaScript Node.js v24.18.0, V8 13.6.233.17-node.50, JIT; Python CPython 3.14.6, GIL, pure Python

<!-- BENCHMARK:END -->

### Reading the numbers

**Startup can cost more than the entire problem.** This is the result that most
changes what you would actually build. A table lookup solves any cube in
0.4 microseconds in C — but starting the C binary takes 1.5 ms, roughly four
thousand solves' worth. Starting Node takes 192 ms and 56 MB, which is ninety-six
thousand solves. For a command-line tool invoked once, the runtime you chose
dominates so completely that the algorithm barely registers; for a long-running
service, it is free and the per-solve column is the only one that matters. The
same seven programs rank almost inversely under those two readings.

**The spread between languages is not one number.** Building the table spans 31×
(C 151 ms to Python 4.7 s), a single table lookup spans 142×, but bidirectional
search spans only 13× and the *artifact* spans 38× the other way (C 56 KB to Go
2.1 MB). Which language is "fast" depends entirely on which of these you are
paying for.

**Threading has a ceiling here, and it is the memory system.** C++ reaches 1.99×,
Rust 1.86×, C 1.72× — all close to the 2 physical cores, none beyond it despite 4
hardware threads. The table build is memory-latency bound: 3.7 MB does not fit in
3 MiB of L3, so the second thread per core waits on the same cache misses. Go's
implementation shows this most directly — it measured a 460 KB claim bitmap
against a 14.7 MB atomic array and found the larger one *slower than serial*.

Three things hold in every language:

- **The table only pays off above its break-even.** Below it, building costs more
  than it saves. Per-solve figures alone would hide this completely, which is
  exactly why setup time is measured and reported next to them.
- **The worst case separates the algorithms far more than the average does.**
  Table lookup is flat whatever you throw at it; IDA\* degrades by one to two
  orders of magnitude on hard positions. An average alone understates that badly,
  and tail latency is usually what you actually feel.
- **IDA\*'s heuristic is too weak here, in every language.** Not a flaw in Korf's
  method but a mismatch: pattern databases pay off when a puzzle has more pieces
  than you can afford to track and you keep only some. A pocket cube is nothing
  *but* the pieces the databases already cover, so both bounds stop at distance 8
  while real positions reach 14 — far too loose to prune well.

And one thing that does **not** hold everywhere, which is the reason for running
this in six languages rather than reasoning about it in one:

> **Whether IDA\* or bidirectional BFS is faster on average depends on the
> language.** In Python, BFS wins comfortably. In C, C++, Go and JavaScript,
> IDA\* wins — sometimes by 2×.

Same algorithms, same inputs, opposite ranking. The reason is what each language
makes expensive. Bidirectional BFS lives or dies on a hash map holding tens of
thousands of visited states; IDA\* is a recursive descent over two flat arrays
and allocates nothing. In CPython the map operations run at C speed while every
recursive call is interpreted, so BFS wins. Once the interpreter overhead is
gone, the allocation and hashing dominate instead and the ranking inverts.

Worth remembering before porting a benchmark result across a language boundary —
IDA\* still loses badly on the worst case everywhere, so the average would have
been a misleading thing to port either way.

### Does threading help?

Only the table build is worth parallelising — a single solve is milliseconds and
its frontiers are small. The build is level-synchronous, so each level's frontier
splits cleanly across workers, and races on the shared visited array are benign:
two workers reaching the same state write the same value.

The compiled languages get real speedup from that, up to the ~2× the hardware
allows. Python does not, on either interpreter: under the GIL the inner loop is
pure bytecode so threads merely take turns, and on the free-threaded 3.14 build
the shared array turns into contention instead of parallelism — that build is
already 26% slower single-threaded, and adding threads makes it worse still.
JavaScript also lands below 1.0×, where worker startup costs more than it saves
on a build that only takes a third of a second.

Two implementation choices mattered more than the language:

- **Claim states through a small bitmap, not a wide atomic array.** One bit per
  state is 460 KB and stays in cache; one atomic word per state is 14.7 MB and
  does not.
- **A single solve is not worth parallelising.** It is milliseconds of work over
  small frontiers; the coordination costs more than the search.

The per-language READMEs give the numbers and the scheme each one chose.

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
