# rubik2x2

**Optimal solvers for the 2x2x2 Rubik's Cube (Pocket Cube, Mini Cube)** — the
same three algorithms implemented once per language, so the algorithmic and the
language-level differences can be told apart.

Every solver here returns a *shortest* solution, never merely a working one.

| Implementation | Status |
|---|---|
| [`python/`](python/) | complete — library, CLI, benchmark, 250 tests |
| `csharp/` | planned |
| `rust/` | planned |

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

## Measured (Python 3.14, one core)

```
algorithm   setup ms    avg ms   worst ms   peak MB  optimal
------------------------------------------------------------
bfs             0.0     3.099     44.825       9.4      yes
ida             3.5    11.413   1238.035       0.0      yes
table         846.4     0.051      0.058       0.0      yes

total time for a workload (setup + solves), seconds
algorithm           1          10         100       1,000      10,000
--------------------------------------------------------------------
bfs             0.00        0.03        0.31        3.10       30.99
ida             0.01        0.12        1.14       11.42      114.13
table           0.85        0.85        0.85        0.90        1.35
```

Three things worth reading off that table:

- **The table wins only above ~278 solves.** Below the break-even, building it
  costs more than it saves. Per-solve numbers alone would hide this entirely,
  which is why setup is measured too.
- **IDA\* is dominated everywhere** — 4× slower than plain search on average and
  28× in the worst case. Not a flaw in Korf's method but a mismatch: pattern
  databases pay off when a puzzle has more pieces than you can track and you
  keep only some. A pocket cube is nothing *but* the pieces the databases
  already cover, so both bounds stop at distance 8 while real positions reach
  14 — far too loose to prune.
- **Worst case separates them more than the average.** The table is flat at
  0.06 ms whatever you throw at it; IDA\* degrades by a factor of a hundred on
  hard positions.

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
