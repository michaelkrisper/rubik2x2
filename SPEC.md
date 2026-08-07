# Implementation specification

Every language implementation in this repository solves the same problem with
the same three algorithms and reports its results in the same format, so that
differences in the numbers are differences between *languages* and not between
programs.

This document is the contract. Implement it idiomatically and optimally for your
language — do not transliterate another implementation.

---

## 1. The model

### State

A cube state is 24 sticker color ids, values 0..5, indexed
`side * 4 + row * 2 + column` where the sides are in this order:

```
0 UP   1 FRONT   2 DOWN   3 BACK   4 LEFT   5 RIGHT
```

Within a side the stickers are row-major:

```
-------
|0  1 |
|2  3 |
-------
```

Colors, by id: `0 r`ed, `1 w`hite, `2 b`lue, `3 y`ellow, `4 p`urple, `5 g`reen.

The solved cube is `yyyyrrrrwwwwppppbbbbgggg`, i.e. ids
`3 3 3 3 0 0 0 0 1 1 1 1 4 4 4 4 2 2 2 2 5 5 5 5`.

A **state string** is those 24 color letters. Parsing must reject anything that
is not 24 letters from `rwbypg` with each colour appearing exactly 4 times.

### Moves

Lowercase and uppercase are inverse quarter turns of one face. Applied as
`new[i] = old[PERM[i]]`.

```
l  12  1 14  3  0  5  2  7  4  9  6 11  8 13 10 15 18 16 19 17 20 21 22 23
L   4  1  6  3  8  5 10  7 12  9 14 11  0 13  2 15 17 19 16 18 20 21 22 23
r   0 13  2 15  4  1  6  3  8  5 10  7 12  9 14 11 16 17 18 19 21 23 20 22
R   0  5  2  7  4  9  6 11  8 13 10 15 12  1 14  3 16 17 18 19 22 20 23 21
t   2  0  3  1 20 21  6  7  8  9 10 11 12 13 17 16  4  5 18 19 15 14 22 23
T   1  3  0  2 16 17  6  7  8  9 10 11 12 13 21 20 15 14 18 19  4  5 22 23
b   0  1  2  3  4  5 22 23  9 11  8 10 19 18 14 15 16 17  6  7 20 21 13 12
B   0  1  2  3  4  5 18 19 10  8 11  9 23 22 14 15 16 17 13 12 20 21  6  7
f   0  1 19 17  6  4  7  5 22 20 10 11 12 13 14 15 16  8 18  9  2 21  3 23
F   0  1 20 22  5  7  4  6 17 19 10 11 12 13 14 15 16  3 18  2  9 21  8 23
a  18 16  2  3  4  5  6  7  8  9 23 21 13 15 12 14 10 17 11 19 20  0 22  1
A  21 23  2  3  4  5  6  7  8  9 16 18 14 12 15 13  1 17  0 19 20 11 22 10
```

Move strings chain: `"lFtL"` applies `l`, then `F`, then `t`, then `L`.
The inverse of a sequence is the reversed sequence with each letter's case
flipped.

### The three solve moves

Solvers use only `l L t T f F`, **in that order** — inverses at adjacent
indices, which the IDA\* pruning relies on. A fourth face would be redundant:
turning a face equals turning the opposite face plus a whole-cube rotation.

These six moves never touch facelets **11, 13, 23** — the down-back-right
corner. That fact drives everything else.

---

## 2. The encoding

Holding one corner still removes the 24-fold whole-cube-rotation symmetry, so a
state is fully described by:

- which of the 7 free corner **pieces** sits in each of the 7 free **slots** —
  7! = 5040 arrangements;
- how each is **twisted** — 3⁷ constrained so the twists sum to 0 mod 3, hence
  3⁶ = 729.

Total: 5040 × 729 = **3,674,160** states. Encode as
`index = permutation_rank * 729 + orientation_rank`, so the solved state is 0.

### Slots

Each slot is its three facelets, listed so that "twist" means the same thing at
every slot (all eight triples share a handedness):

```
slot 0  left-up-back      16  0 14
slot 1  right-up-back     21 15  1
slot 2  left-up-front     17  4  2
slot 3  right-up-front    20  3  5
slot 4  left-down-front   19  8  6
slot 5  right-down-front  22  7  9
slot 6  left-down-back    18 12 10
slot 7  right-down-back   23 11 13   <- fixed, not part of the index
```

Slot 7's facelets appear here in the same rotational order as every other slot,
which is what reading its piece requires. Elsewhere the same three facelets are
referred to as `11, 13, 23` — ascending, because there they are only a key for
looking up which solved orientation a cube is in, and order is irrelevant. Same
corner, two uses.

A piece is identified by the colour triple its slot shows on a **solved** cube.
For a state, read the three colours at a slot and find the (piece, twist) whose
canonical triple rotated left by `twist` matches.

### Ranking

- **Permutation rank**: any bijection from the 5040 permutations of 0..6 to
  0..5039 works, as long as the identity permutation ranks 0. Lehmer code is the
  obvious choice.
- **Orientation rank**: `twist[0] + 3*twist[1] + ... + 3^5*twist[5]` — six terms,
  slots 0 to 5, giving 3⁶ = 729 values. Slot 6's twist is not stored; it is
  recovered as `(-(twist[0] + ... + twist[5])) mod 3`.

### Goal orientation

A cube may be handed over in any of the 24 orientations. The fixed corner's
three sticker colours match exactly one rotation of the solved cube — look that
up (24 entries, precomputed) and derive the piece colour triples from **it**,
not from the canonical solved state. Encoding is then correct for any
orientation, and the goal index is always 0.

Note what this does *not* say: the index is **not** rotation-invariant. Rotating
a scramble conjugates which piece counts as which, so the same physical cube held
two ways generally yields two different indices. What is invariant — and what
tests should assert — is that every orientation encodes, aims at index 0, and
solves in the same number of moves.

### Rejecting the impossible

Four conditions must be rejected as unsolvable. The first two are visible in the
stickers; the last two are not, and both were missed by the first draft of the
reference implementation until two independent implementations caught them:

1. a fixed-corner colour triple that no solved orientation has;
2. a slot whose three colours form no valid piece;
3. the same piece appearing in more than one slot;
4. twists that do not sum to a multiple of three.

Condition 4 matters more than it looks. Only six twists are stored and the
seventh is derived, so a single twisted corner — physically impossible — encodes
silently as some *other*, perfectly legal cube. The solver then returns a
sequence that solves that one instead, with no error and no warning. Check the
sum before ranking.

### Move tables

Because a move acts on the two coordinates independently:

```
new_permutation[slot] = old_permutation[source[slot]]
new_orientation[slot] = old_orientation[source[slot]] + twist[slot]   (mod 3)
```

derive `source` and `twist` per move by applying it to the identity state, then
build two tables per move: 5040 entries mapping permutation rank to permutation
rank, and 729 entries mapping orientation rank to orientation rank. Applying a
move is then two lookups. Pre-multiplying the permutation table by 729 turns the
child computation into a single addition.

---

## 3. The algorithms

All three must return a **shortest** solution. God's number is 14 in the
quarter-turn metric.

### `bfs` — bidirectional breadth-first search

Grow one frontier from the scramble and one from index 0, one level at a time,
always expanding the smaller frontier. Stop at the first meeting point; because
both sides expand by whole levels, it is provably shortest. The solution is the
path from the start plus the inverse of the path from the goal. No
precomputation.

### `ida` — iterative-deepening A\* with pattern databases

Depth-first passes with a rising limit on `g + h`. Since a move acts on each
coordinate independently, each coordinate alone is a genuine quotient of the
state graph, so its exact distance is an admissible bound. Build both by
breadth-first search over 5040 and over 729 nodes; `h = max(of the two) `.
Prune the move that would undo the previous one (`index ^ 1`).

### `table` — full distance table (God's algorithm)

Breadth-first from index 0 over all 3,674,160 states, storing each state's
distance in one byte. Solve by walking downhill: at distance `d`, exactly one
neighbour is at `d - 1`.

Store `distance + 1` so that 0 can mean "not yet reached" during the build.

---

## 4. Required verification

The distance table's histogram **must** equal [OEIS A079762][oeis]:

```
1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508,
930588, 1350852, 782536, 90280, 276
```

Summing to 3,674,160 with a maximum distance of 14. No incorrect move table or
encoding can reproduce this, so it is the single strongest check available —
every implementation must assert it in its test suite.

Also required in tests:

- each move has order 4; a move and its inverse cancel; opposite faces commute
- exactly facelets 11, 13, 23 are fixed by the six solve moves
- the encoding round-trips: `decode(encode(s)) == s` for a cube in the canonical
  orientation (for any other, `decode` returns the canonical representative, so
  compare against the state's own goal)
- all four impossible arrangements above are rejected
- the three algorithms return **equal-length** solutions on the same inputs
- every returned solution actually solves its cube
- the antipodal position `wppryygrygrwgprbbbwpbwyg` needs exactly 14 moves

---

## 5. Command line interface

Each implementation provides an executable supporting:

```
<prog> solve <state> [--algorithm bfs|ida|table]    # default bfs; prints the moves only
<prog> scramble [-n N] [--seed S]                   # default n=8; prints a state string
<prog> table [--workers N]                          # builds the table, prints the histogram
<prog> bench [--json] [--scrambles FILE] [-n N]     # benchmark
```

`--workers` defaults to one per online CPU. Unsolvable input exits non-zero with
a message on stderr.

`solve` prints nothing but the move sequence on stdout, so it pipes.

## 6. Benchmark protocol

Read scrambles from `bench/scrambles.txt` (one state string per line, 200 of
them) — **not** from a random generator, so every language measures the same
work. The antipodal position `wppryygrygrwgprbbbwpbwyg` is the "worst case".

For each of the three algorithms measure:

- `setup_ms` — one-time precomputation before the first solve (0 for bfs)
- `avg_ms` — mean wall time per solve over all scrambles
- `worst_ms` — wall time solving the antipodal position
- `peak_mb` — high-water mark of memory live from the end of setup onwards, minus
  what was live before setup. Retained precomputation counts, so `table` shows
  its ~3.7 MB. Measure it for real where the language allows (an allocator hook,
  `tracemalloc`, an allocation counter); resident-set deltas do not work, since
  allocators do not return freed pages and every algorithm after the first reads
  as zero. Say in the implementation's README how it was measured — these figures
  compare within a language far better than across languages.
- `optimal` — whether all solution lengths match the table's distances

Repeat fast algorithms enough times to measure them meaningfully.

**Measure `table` and `table-par` setup as the best of at least three
alternating samples.** Timed once each in sequence, whichever runs first pays to
fault in ~18 MB of fresh pages and the second inherits warm pages for free — an
artifact worth roughly 1.5× that points the same way in every language, which
would make the parallel-speedup column look uniformly better than reality.
Alternate them and take the best of each.

### Parallelism

Report a fourth algorithm entry named **`table-par`**: the same distance table,
built with the language's native concurrency (threads, tasks, goroutines,
workers — whatever is idiomatic). Only `setup_ms` differs; per-solve figures are
identical to `table`, since only the build is parallelised.

The table build is the one place where parallelism can pay: the breadth-first
search is level-synchronous, so within a level the frontier can be split across
workers. The hazard is the shared visited array — two workers can reach the same
state at once. Resolve it in whatever way the language makes natural (an atomic
compare-and-exchange per state, or per-worker output buffers merged at the level
boundary) and say in the implementation's README which you chose and why.

Solving a single cube is *not* worth parallelising — a solve is milliseconds and
the frontiers are small. Do not do it.

If the language cannot benefit (a global interpreter lock, for instance), report
the measurement anyway and state the reason. A measured non-result is a result.

With `--json`, print exactly one JSON object on stdout and nothing else:

```json
{
  "language": "Python",
  "version": "3.14.6",
  "build": "CPython, -O2 equivalent",
  "algorithms": [
    {"name": "bfs",   "setup_ms": 0.0,   "avg_ms": 3.099, "worst_ms": 44.825,
     "peak_mb": 9.4, "optimal": true},
    {"name": "ida",   "setup_ms": 3.5,   "avg_ms": 11.413, "worst_ms": 1238.0,
     "peak_mb": 0.1, "optimal": true},
    {"name": "table", "setup_ms": 846.4, "avg_ms": 0.051, "worst_ms": 0.058,
     "peak_mb": 3.7, "optimal": true},
    {"name": "table-par", "setup_ms": 300.0, "avg_ms": 0.051, "worst_ms": 0.058,
     "peak_mb": 3.7, "optimal": true, "workers": 8}
  ]
}
```

Field names, types and units are fixed. `tools/report.py` aggregates these into
the comparison table in the README.

---

## 7. Layout and quality

```
<language>/
  README.md          how to build, run and test this implementation
  ...                idiomatic project layout for the language
```

Requirements:

- **Idiomatic and modern.** Use the language's current standard and its natural
  idioms. Release/optimised build settings.
- **No external dependencies** beyond the standard library and the language's
  standard test framework — with one deliberate exception, `numpy` in Python.
  The rule exists so that every implementation is measured doing the work
  itself, not calling out to someone else's solver. It is not meant to forbid a
  language's ordinary way of touching arrays. Every implementation here already
  leans on compiled machinery it did not write: a JavaScript `Uint32Array` is
  C++ inside V8, C# `Span<T>` is runtime intrinsics, and Python's `dict` and
  `bytearray` are C. Numeric array work in Python means numpy; excluding it
  while counting CPython's own C internals would be an arbitrary line that
  measures Python at a disadvantage no Python programmer would accept. The
  pure-Python build is kept and reported alongside, so the cost of the
  interpreter stays visible.
- **Layered.** Keep the readable 24-sticker model separate from the compact
  index the solvers use, and keep display separate from both.
- **Tested.** The checks in section 4, in the language's usual test framework,
  runnable with one command documented in the implementation's README.
- **Documented.** Explain *why* — the fixed corner, why the encoding splits in
  two, why the table wins only above its break-even. Not what each line does.

[oeis]: https://oeis.org/A079762
