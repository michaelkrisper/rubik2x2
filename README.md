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
| C++ (clang) | clang version 22.1.8 | 132 ms | 0.00051 ms | 2,901 solves | 1.74× (4 threads) |
| Assembly | 2.46.1-1.fc44 | 133 ms | 0.0004 ms | 2,958 solves | 1.53× (4 threads) |
| C++ | C++23 (GCC 16.1.1) | 136 ms | 0.00046 ms | 2,852 solves | 1.62× (4 threads) |
| C (clang) | clang version 22.1.8 | 137 ms | 0.0005 ms | 2,529 solves | 1.73× (4 threads) |
| Rust | 1.97.1 | 162 ms | 0.0006 ms | 2,403 solves | 1.60× (4 threads) |
| C | gcc 16.1.1 | 169 ms | 0.0004 ms | 3,202 solves | 1.88× (4 threads) |
| Go | 1.26.5-X:nodwarf5 | 205 ms | 0.001 ms | 1,591 solves | 1.27× (4 threads) |
| C# | 10.0.10 | 273 ms | 0.00071 ms | 1,541 solves | 1.10× (4 threads) |
| JavaScript | 24.18.0 | 405 ms | 0.002 ms | 2,110 solves | 0.66× (4 threads) |
| Python | 3.14.6 | 740 ms | 0.04 ms | 270 solves | 2.03× (4 threads) |

### What it cost to get there

| Language | Build from clean | Artifact | Source files | Source lines | Test lines |
|---|---|---|---|---|---|
| C++ (clang) | 34.8 s | 261 KB | 9 | 1,371 | 492 |
| Assembly | 0.1 s | 47 KB | 8 | 3,427 | 644 |
| C++ | 33.4 s | 261 KB | 9 | 1,371 | 492 |
| C (clang) | 1.6 s | 60 KB | 17 | 2,175 | 427 |
| Rust | 10.5 s | 598 KB | 13 | 1,333 | 366 |
| C | 1.9 s | 52 KB | 17 | 2,175 | 427 |
| Go | 9.4 s | 2.1 MB | 16 | 2,333 | 866 |
| C# | — | 229 KB | 30 | 2,247 | 408 |
| JavaScript | — | 151 KB | 11 | 1,946 | 694 |
| Python | — | 62 KB | 11 | 1,328 | 528 |

### What it costs just to start

| Language | Startup | Resident at startup | vs. one table lookup |
|---|---|---|---|
| C++ (clang) | 2.8 ms | 3.9 MB | 5,497× |
| Assembly | 2.3 ms | 1.5 MB | 5,674× |
| C++ | 2.9 ms | 3.9 MB | 6,388× |
| C (clang) | 1.8 ms | 1.6 MB | 3,681× |
| Rust | 1.8 ms | 2.2 MB | 3,048× |
| C | 1.6 ms | 1.6 MB | 3,953× |
| Go | 8.1 ms | 2.8 MB | 7,971× |
| C# | 59.7 ms | 27.0 MB | 84,031× |
| JavaScript | 181 ms | 56.6 MB | 90,627× |
| Python | 120 ms | 19.4 MB | 2,977× |

### Every measurement

**Setup — one-time precomputation, milliseconds**

| Language | bfs | ida | table | table-par |
|---|---|---|---|---|
| C++ (clang) | 0 | 0.071 | 132 | 75.8 |
| Assembly | 0 | 0.136 | 133 | 86.8 |
| C++ | 0 | 0.109 | 136 | 83.6 |
| C (clang) | 0 | 0.111 | 137 | 79.1 |
| Rust | 0 | 0.1 | 162 | 101 |
| C | 0 | 0.121 | 169 | 90.1 |
| Go | 0 | 0.217 | 205 | 162 |
| C# | 0 | 4.1 | 273 | 248 |
| JavaScript | 0 | 6.0 | 405 | 616 |
| Python | 0 | 3.4 | 740 | 364 |

**Average per solve, milliseconds (200 scrambles)**

| Language | bfs | ida | table | table-par |
|---|---|---|---|---|
| C++ (clang) | 0.046 | 0.076 | 0.00051 | 0.00051 |
| Assembly | 0.045 | 0.093 | 0.0004 | 0.0004 |
| C++ | 0.048 | 0.082 | 0.00046 | 0.00046 |
| C (clang) | 0.055 | 0.088 | 0.0005 | 0.0005 |
| Rust | 0.068 | 0.18 | 0.0006 | 0.0006 |
| C | 0.053 | 0.081 | 0.0004 | 0.0004 |
| Go | 0.13 | 0.3 | 0.001 | 0.001 |
| C# | 0.178 | 0.271 | 0.00071 | 0.00071 |
| JavaScript | 0.194 | 0.288 | 0.002 | 0.002 |
| Python | 2.8 | 9.1 | 0.04 | 0.04 |

**Worst case — the antipodal position, milliseconds**

| Language | bfs | ida | table | table-par |
|---|---|---|---|---|
| C++ (clang) | 0.66 | 8.4 | 0.00022 | 0.00022 |
| Assembly | 0.632 | 10.1 | 0.0002 | 0.0002 |
| C++ | 0.711 | 9.0 | 0.00026 | 0.00026 |
| C (clang) | 0.685 | 9.7 | 0.0003 | 0.0003 |
| Rust | 0.972 | 19.9 | 0.0004 | 0.0004 |
| C | 0.713 | 8.6 | 0.0003 | 0.0003 |
| Go | 2.0 | 33.6 | 0.00088 | 0.00088 |
| C# | 1.5 | 29.5 | 0.00052 | 0.00052 |
| JavaScript | 2.9 | 30.3 | 0.001 | 0.001 |
| Python | 41.1 | 1,005 | 0.042 | 0.042 |

**Peak memory while solving, MB**

| Language | bfs | ida | table | table-par |
|---|---|---|---|---|
| C++ (clang) | 7.9 | 0 | 3.7 | 3.7 |
| Assembly | 8.9 | 0.01 | 3.7 | 3.7 |
| C++ | 7.9 | 0 | 3.7 | 3.7 |
| C (clang) | 7.9 | 0.01 | 3.7 | 3.7 |
| Rust | 7.9 | 0 | 3.7 | 3.7 |
| C | 7.9 | 0.01 | 3.7 | 3.7 |
| Go | 8.3 | 0.0062 | 3.7 | 3.7 |
| C# | 8.0 | 0.006 | 3.7 | 3.7 |
| JavaScript | 7.9 | 0.07 | 3.7 | 3.7 |
| Python | 9.4 | 0.0085 | 3.7 | 3.7 |

Builds: C++ (clang) clanclang++ -std=c++23 -O3 -march=native -flto; Assembly GNU as 2.46.1-1.fc44, hand-written x86-64, System V ABI; C++ g++ -std=c++23 -O3 -march=native -flto; C (clang) C23, -O3 -march=native -flto; Rust rustc 1.97.1, release, opt-level=3, lto=fat, codegen-units=1; C C23, -O3 -march=native -flto; Go gc, linux/amd64, optimised; C# .NET 10.0.10, Release, TieredPGO, x64; JavaScript Node.js v24.18.0, V8 13.6.233.17-node.50, JIT; Python CPython 3.14.6, GIL, numpy

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
(C++/clang 132 ms to Python 740 ms), a single table lookup spans 100×,
bidirectional search spans 62×, and the *artifact* spans 45× the other way
(assembly 47 KB to Go 2.1 MB). Which language is "fast" depends entirely on
which of these you are paying for.

**Two optimizations mattered more than the choice of language, and both were
about memory rather than instructions.**

*The container.* Every implementation first reached for its language's natural
hash map for the search frontier, and the spread across languages was 13×.
Replacing it with a flat array indexed by the state itself — a cube state is
already a dense integer below 3,674,160, so hashing it was pure overhead — was
worth 4.7× in C#, 4.5× in JavaScript, 4.4× in Go, 3.9× in C++, 2.7× in C and
2.1× in Rust.

*The prefetch.* Profiling then put ~69% of what remained in a single
instruction: the mark load, one random word of a 14.7 MB array per edge, on a
machine with 3 MiB of last-level cache. The loop was not computing, it was
waiting. Naming those addresses six frontier entries ahead was worth another 27%
in C#, 22% in Rust, 17% in the assembly and 13% in C — nothing changed except
when the misses were allowed to start.

*The width.* If the array is what you are waiting on, make it smaller. Nothing in
a mark needed 32 bits: a depth takes 4, the frontier side 1, leaving 11 for the
generation stamp — 2047 solves between clears, and a 7.3 MB clear at that
interval is 0.3 µs per solve. Storing a depth rather than a parent is what buys
the room; the path is recovered afterwards by stepping to whichever neighbour is
one level shallower, forty-odd lookups against 150,000 edges made cheaper. It
also makes the inner-loop store loop-invariant. Halving the array was worth
**40% in the assembly** — which is what finally put hand-written code ahead of
the compiler — around 35% in C++, roughly 20% in Rust, JavaScript and C#, and
16% in C. Go, with no prefetch keeping the narrower loads in flight, gained
only the memory. Every implementation now carries the same 16-bit scheme, so
the rows above compare like with like.

Together these collapsed the compiled spread from 13× to about 4×, and
assembly, C++ and C now sit within roughly 20% of each other. Almost everything
that looked like a language gap was one wrong container, one missing hint, and
one over-wide word.

Go and JavaScript are the exceptions to the second point, for a reason worth
recording: neither exposes a prefetch. Go's standard library has no intrinsic,
and JavaScript has no concept of one. That is a real difference between
languages — not in how fast their code runs, but in whether the hardware is
reachable at all.

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

It **wins on size** — 47 KB against C's 52 KB, and 27 KB against 48 KB stripped —
**ties C on the table build**, and is now **faster than C on the search**
(0.045 ms against 0.053), the fastest of the ten variants measured — though with
the 16-bit mark ported everywhere, clang's C++ sits within measurement noise of
it at 0.046.

Getting there is the part worth reading, because the first answer was wrong.
Disassembling both inner loops turned up three differences from GCC's output.
Two were worth copying: cold paths moved out of line so the common case takes no
branch, and the bounds check hoisted from six times per node to one in a
register (together 86 → 82 µs). One was not: reusing a computed address register,
which GCC does, is 2% *slower* by hand, because `[base + index*4]` is folded into
the load and the store for free.

That left a stubborn ~19%, and the analysis at the time concluded it was
architectural — C stores a *depth* and finds the parent afterwards, making its
inner-loop store loop-invariant, where the assembly stores *parent and move* and
rebuilds the value on every edge. The write-up said the compiler had not
out-coded the assembly, the C author had out-designed it.

That was wrong twice over, and the profiler said so plainly once asked the right
question. 69% of the time was the mark load, and the assembly's search — unlike
its own table build — was not prefetching. Adding it closed most of the gap in
one change, 87 → 72 µs. Then the deeper point: if the wait is on that array, the
array is the thing to change. Narrowing the mark from 32 to 16 bits took it from
73 to 44 µs and put it ahead of C.

Which retires the earlier conclusion entirely. Storing a parent instead of a
depth *was* the problem — not because it made a store non-invariant, as first
claimed, but because it spent 22 bits that the cache could not afford. The fix
was the one the analysis had described and dismissed as too expensive.

The honest summary is duller than either wrong version: hand-written assembly is
competitive here and can win, and both times it looked beaten it was missing an
optimization rather than losing to the compiler's code generation.

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
