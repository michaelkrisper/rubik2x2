# rubik2x2

**Optimal solvers for the 2x2x2 Rubik's Cube (Pocket Cube, Mini Cube)** — the
same three algorithms implemented once per language, so the algorithmic and the
language-level differences can be told apart.

Every solver here returns a *shortest* solution, never merely a working one.

| Implementation | | |
|---|---|---|
| [`asm/`](asm/) | x86-64 | hand-written, GNU as, pthreads |
| [`c/`](c/) | C23 | gcc, pthreads |
| [`cpp/`](cpp/) | C++23 | g++, `std::jthread` |
| [`csharp/`](csharp/) | .NET 10 | threads around a `Barrier` |
| [`go/`](go/) | Go 1.26 | goroutines |
| [`javascript/`](javascript/) | Node 24 | `worker_threads` |
| [`python/`](python/) | Python 3.14 | reference implementation, numpy |

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
| C (clang) | clang version 22.1.8 | 132 ms | 0.0004 ms | 1,771 solves | 1.80× (4 threads) |
| Assembly | 2.46.1-1.fc44 | 132 ms | 0.0004 ms | 1,639 solves | 1.40× (4 threads) |
| C++ (clang) | clang version 22.1.8 | 134 ms | 0.0005 ms | 1,813 solves | 1.68× (4 threads) |
| C++ | C++23 (GCC 16.1.1) | 135 ms | 0.00045 ms | 1,843 solves | 1.66× (4 threads) |
| C | gcc 16.1.1 | 138 ms | 0.0004 ms | 2,103 solves | 1.70× (4 threads) |
| Rust | 1.97.1 | 168 ms | 0.0006 ms | 1,273 solves | 1.64× (4 threads) |
| Go | 1.26.5-X:nodwarf5 | 204 ms | 0.001 ms | 1,270 solves | 1.27× (4 threads) |
| C# | 10.0.10 | 338 ms | 0.00075 ms | 1,476 solves | 1.22× (4 threads) |
| JavaScript | 24.18.0 | 428 ms | 0.002 ms | 2,196 solves | 0.74× (4 threads) |
| Python | 3.14.6 | 723 ms | 0.039 ms | 284 solves | 1.93× (4 threads) |

### What it cost to get there

| Language | Build from clean | Artifact | Source files | Source lines | Test lines |
|---|---|---|---|---|---|
| C (clang) | 1.5 s | 60 KB | 17 | 2,164 | 427 |
| Assembly | 0.1 s | 46 KB | 8 | 3,346 | 644 |
| C++ (clang) | 36.0 s | 261 KB | 9 | 1,367 | 492 |
| C++ | 33.6 s | 261 KB | 9 | 1,367 | 492 |
| C | 1.8 s | 52 KB | 17 | 2,164 | 427 |
| Rust | 10.9 s | 597 KB | 13 | 1,311 | 366 |
| Go | 9.7 s | 2.1 MB | 16 | 2,331 | 866 |
| C# | 8.1 s | 226 KB | 30 | 2,214 | 408 |
| JavaScript | — | 151 KB | 11 | 1,944 | 694 |
| Python | — | 62 KB | 11 | 1,328 | 528 |

### What it costs just to start

| Language | Startup | Resident at startup | vs. one table lookup |
|---|---|---|---|
| C (clang) | 1.5 ms | 1.6 MB | 3,691× |
| Assembly | 1.8 ms | 1.5 MB | 4,535× |
| C++ (clang) | 3.0 ms | 3.9 MB | 6,017× |
| C++ | 2.8 ms | 3.9 MB | 6,229× |
| C | 1.6 ms | 1.6 MB | 3,895× |
| Rust | 1.9 ms | 2.2 MB | 3,091× |
| Go | 8.2 ms | 2.8 MB | 8,169× |
| C# | 52.0 ms | 26.9 MB | 69,466× |
| JavaScript | 180 ms | 56.6 MB | 90,153× |
| Python | 122 ms | 19.5 MB | 3,088× |

### Every measurement

**Setup — one-time precomputation, milliseconds**

| Language | bfs | ida | table | table-par |
|---|---|---|---|---|
| C (clang) | 0 | 0.108 | 132 | 73.4 |
| Assembly | 0 | 0.129 | 132 | 94.4 |
| C++ (clang) | 0 | 0.087 | 134 | 79.6 |
| C++ | 0 | 0.111 | 135 | 81.6 |
| C | 0 | 0.12 | 138 | 81.1 |
| Rust | 0 | 0.1 | 168 | 102 |
| Go | 0 | 0.213 | 204 | 161 |
| C# | 0 | 0.595 | 338 | 277 |
| JavaScript | 0 | 7.1 | 428 | 580 |
| Python | 0 | 3.9 | 723 | 375 |

**Average per solve, milliseconds (200 scrambles)**

| Language | bfs | ida | table | table-par |
|---|---|---|---|---|
| C (clang) | 0.075 | 0.086 | 0.0004 | 0.0004 |
| Assembly | 0.081 | 0.091 | 0.0004 | 0.0004 |
| C++ (clang) | 0.074 | 0.076 | 0.0005 | 0.0005 |
| C++ | 0.074 | 0.082 | 0.00045 | 0.00045 |
| C | 0.066 | 0.08 | 0.0004 | 0.0004 |
| Rust | 0.132 | 0.182 | 0.0006 | 0.0006 |
| Go | 0.162 | 0.297 | 0.001 | 0.001 |
| C# | 0.23 | 0.266 | 0.00075 | 0.00075 |
| JavaScript | 0.197 | 0.281 | 0.002 | 0.002 |
| Python | 2.6 | 9.4 | 0.039 | 0.039 |

**Worst case — the antipodal position, milliseconds**

| Language | bfs | ida | table | table-par |
|---|---|---|---|---|
| C (clang) | 1.1 | 9.4 | 0.0002 | 0.0002 |
| Assembly | 0.969 | 10.0 | 0.0002 | 0.0002 |
| C++ (clang) | 0.826 | 8.5 | 0.00023 | 0.00023 |
| C++ | 0.847 | 9.0 | 0.00026 | 0.00026 |
| C | 0.941 | 8.4 | 0.0002 | 0.0002 |
| Rust | 1.5 | 20.4 | 0.0004 | 0.0004 |
| Go | 2.1 | 33.5 | 0.001 | 0.001 |
| C# | 2.1 | 29.2 | 0.00051 | 0.00051 |
| JavaScript | 2.5 | 30.4 | 0.001 | 0.001 |
| Python | 42.2 | 1,027 | 0.042 | 0.042 |

**Peak memory while solving, MB**

| Language | bfs | ida | table | table-par |
|---|---|---|---|---|
| C (clang) | 15.3 | 0.01 | 3.7 | 3.7 |
| Assembly | 16.3 | 0.01 | 3.7 | 3.7 |
| C++ (clang) | 15.2 | 0 | 3.7 | 3.7 |
| C++ | 15.2 | 0 | 3.7 | 3.7 |
| C | 15.3 | 0.01 | 3.7 | 3.7 |
| Rust | 15.3 | 0 | 3.7 | 3.7 |
| Go | 15.7 | 0.006 | 3.7 | 3.7 |
| C# | 5.6e-05 | 0.006 | 3.7 | 3.7 |
| JavaScript | 15.3 | 0.07 | 3.7 | 3.7 |
| Python | 9.4 | 0.0085 | 3.7 | 3.7 |

Builds: C (clang) C23, -O3 -march=native -flto; Assembly GNU as 2.46.1-1.fc44, hand-written x86-64, System V ABI; C++ (clang) clanclang++ -std=c++23 -O3 -march=native -flto; C++ g++ -std=c++23 -O3 -march=native -flto; C C23, -O3 -march=native -flto; Rust rustc 1.97.1, release, opt-level=3, lto=fat, codegen-units=1; Go gc, linux/amd64, optimised; C# .NET 10.0.10, Release, TieredPGO, x64; JavaScript Node.js v24.18.0, V8 13.6.233.17-node.50, JIT; Python CPython 3.14.6, GIL, numpy

<!-- BENCHMARK:END -->

### Reading the numbers

**Startup can cost more than the entire problem.** This is the result that most
changes what you would actually build. A table lookup solves any cube in
0.4 microseconds in C — but starting the C binary takes 1.6 ms, roughly four
thousand solves' worth. Starting Node takes 180 ms and 57 MB, which is ninety
thousand solves. For a command-line tool invoked once, the runtime you chose
dominates so completely that the algorithm barely registers; for a long-running
service it is free, and the per-solve column is the only one that matters. The
same ten programs rank almost inversely under those two readings.

**The spread between languages is not one number.** Building the table spans 6×
(C/clang 123 ms to Python 723 ms), a single table lookup spans 97×, bidirectional
search spans 39×, and the *artifact* spans 46× the other way (assembly 46 KB to
Go 2.1 MB). Which language is "fast" depends entirely on which of these you are
paying for.

**The data structure mattered more than the language.** Every implementation
first used its language's natural hash map for the search frontier, and the
spread was 13× — Python 2.9 ms against C 0.21 ms. Replacing that with a flat
array indexed by the state itself (a cube state is already a dense integer below
3,674,160, so hashing it was pure overhead) was worth 4.7× in C#, 4.5× in
JavaScript, 4.4× in Go, 3.9× in C++, 2.7× in C and 2.1× in Rust. Afterwards the
five compiled languages sit within 3.5× of each other, and C, C++ and hand-written
assembly are within 20%. Most of what looked like a language gap was one wrong
container.

**Threading has a ceiling here, and it is the memory system.** The compiled
implementations reach 1.4–1.75× on 4 hardware threads — close to the 2 physical
cores, never beyond. The table build is memory-latency bound: 3.7 MB does not fit
in 3 MiB of L3, so the second thread on a core waits on the same misses. Go's
implementation shows it most directly: it measured a 460 KB claim bitmap against
a 14.7 MB atomic array and found the larger one *slower than serial*.

**Choosing the compiler is worth about as much as a week of tuning.** Same C
source, same flags: clang builds the table in 123 ms where gcc needs 138 ms, an
11% gap for changing one word on the command line. It cuts both ways — gcc's
binary is 13% smaller — but it is the cheapest 11% in the table.

Three things hold in every language:

- **Bidirectional BFS beats IDA\* everywhere**, on the average and by far on the
  worst case. Not a flaw in Korf's method but a mismatch: pattern databases pay
  off when a puzzle has more pieces than you can afford to track and you keep
  only some. A pocket cube is nothing *but* the pieces the databases already
  cover, so both bounds stop at distance 8 while real positions reach 14 — too
  loose to prune well.
- **The table only pays off above its break-even**, which is now 1,300–2,200
  solves. Below that, building it costs more than it saves. Per-solve figures
  alone would hide this completely, which is exactly why setup is measured beside
  them.
- **The worst case separates the algorithms far more than the average does.**
  Table lookup is flat whatever you throw at it; IDA\* degrades by two orders of
  magnitude on hard positions. Tail latency is usually what you actually feel.

That first point is a correction worth stating plainly, because this repository
got it wrong at first. Before the optimization pass, IDA\* beat bidirectional BFS
in C, C++, Go and JavaScript, and lost only in Python — which looked like a
genuine language-dependent inversion, and was written up as one. It was not. It
was an artifact of a slow BFS: the hash map cost more than the search. With that
removed, BFS wins in all ten variants and the algorithmic ranking is the same
everywhere.

**The general lesson: a benchmark comparing languages mostly measures whichever
implementation is least optimized.** The honest conclusion only appeared after
every implementation had had the same idea applied to it.

### Does threading help?

Only the table build is worth parallelising — a single solve is milliseconds and
its frontiers are small. The build is level-synchronous, so each level's frontier
splits cleanly across workers, and races on the shared visited array are benign:
two workers reaching the same state write the same value.

The compiled languages get real speedup from that, up to the ~1.75× this hardware
allows. The two ends of the range are both instructive.

**Python is fastest of all at 1.93×** — which is not a typo, and not a defence of
the GIL. It is a demonstration of what the GIL actually locks: bytecode, not
threads. numpy's ufuncs release it and spend their time in compiled loops, so
four genuinely run at once. The same decomposition in *pure* Python manages
0.79×, and on the free-threaded 3.14 build 0.59× — removing the GIL made it
worse, because that interpreter starts 26% slower single-threaded and the shared
array becomes contention rather than parallelism.

**JavaScript is the only one below 1.0×** (0.74×), where spawning workers costs
more than it saves on a build that takes less than half a second.

Two implementation choices mattered more than the language:

- **Claim states through a small bitmap, not a wide atomic array.** One bit per
  state is 460 KB and stays in cache; one atomic word per state is 14.7 MB and
  does not.
- **A single solve is not worth parallelising.** It is milliseconds of work over
  small frontiers; the coordination costs more than the search.

The per-language READMEs give the numbers and the scheme each one chose.

### Hand-written assembly against the compiler

The assembly implementation exists to ask what the abstractions cost. Both ends
of the answer are worth having, and [`asm/README.md`](asm/README.md) works
through the disassembly line by line.

It **wins on size** — 46 KB against C's 52 KB, and 27 KB against 48 KB stripped —
and **ties everywhere the work is memory-bound**: the table builds are level with
C's, because both are waiting on the same cache misses and no amount of
instruction selection changes that.

It **loses by about 20% on the searches**, and the reason is not what you would
guess. Per child the assembly executes *fewer* instructions than GCC does, twelve
against fifteen. What it cannot match is a design choice: C stores a *depth* in
each mark and finds the parent afterwards by looking for a neighbour one level
shallower, which makes its inner-loop store a loop-invariant constant. The
assembly stores the *parent and the move*, so it rebuilds that value on every one
of ~150,000 edges. The compiler did not out-code the assembly; the C author
out-designed it.

Two of GCC's shapes were worth copying back into the assembly (out-of-line cold
paths, a hoisted bounds check, together 86 → 82 µs). One was not: reusing a
computed address register is 2% *slower* by hand, because `[base + index*4]` is
folded into the load and the store for free.

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
