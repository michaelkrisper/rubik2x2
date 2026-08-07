# pyrubik

The Python implementation of [rubik2x2](../README.md): an optimal 2x2x2 cube
solver with three interchangeable algorithms. Pure Python 3.14, no required
dependencies.

## Install

```sh
pip install .              # or: pip install ".[fast]" for the numpy table build
```

Or run from a checkout with `python -m pyrubik`.

## Command line

```sh
pyrubik                                   # interactive shell, starting solved
pyrubik shell rgrggpyybbppwwbrwwgpyyrb    # shell, starting from a state
pyrubik solve rgrggpyybbppwwbrwwgpyyrb    # print a shortest solution and exit
pyrubik solve <state> --algorithm ida     # pick bfs (default), ida or table
pyrubik scramble -n 8 --seed 42           # print a scrambled state
pyrubik table                             # build the distance table, show its shape
pyrubik bench                             # compare all three algorithms
```

`solve` prints nothing but the move sequence, so it pipes:

```sh
pyrubik solve "$(pyrubik scramble -n 11)"
```

The interactive shell builds the distance table on a background thread while you
work: solves before it lands use search, and become lookups afterwards.

### State strings

24 color letters in side order **UP FRONT DOWN BACK LEFT RIGHT**, each face read
row-major:

```
-------
|1. 2.|
|3. 4.|
-------
```

Colors are `r`ed, `w`hite, `b`lue, `y`ellow, `p`urple, `g`reen. Solved is
`yyyyrrrrwwwwppppbbbbgggg`.

### Moves

Lowercase and uppercase are inverse quarter turns of the same face, and chain
as `lFtL`:

| | | |
|---|---|---|
| `l` / `L` | left column | down / up |
| `r` / `R` | right column | down / up |
| `t` / `T` | top layer | left / right |
| `b` / `B` | bottom layer | left / right |
| `f` / `F` | front face | cw / ccw |
| `a` / `A` | back face | cw / ccw |

Patterns worth trying: `ttlfTlFlFTLL` (cube in a cube), `lfllffTFttlt`,
`lftfLfLfftLT`, `lltfLfLtFLTT`.

### Display

The cube is drawn as an unfolded net. Colored blocks are used when the terminal
supports them, letters otherwise; `NO_COLOR`, `FORCE_COLOR`, `TERM=dumb`,
`COLORTERM` and whether output is a pipe are all honoured, and `--color` /
`--no-color` override the lot.

```
          UP
         Y  Y
         Y  Y

 LEFT   FRONT   RIGHT    BACK
 B  B    R  R    G  G    P  P
 B  B    R  R    G  G    P  P

         DOWN
         W  W
         W  W
```

## Library

```python
import pyrubik

state = pyrubik.parse("rgrggpyybbppwwbrwwgpyyrb")
solution = pyrubik.solve(state)                    # bidirectional search
assert pyrubik.is_solved(pyrubik.apply_moves(state, solution))
```

`pyrubik.solve` needs no setup. To solve many cubes, build the table once:

```python
from pyrubik import DistanceTable

table = DistanceTable.build()        # ~0.9 s with numpy, ~5 s without
table.solve(state)                   # ~0.05 ms, whatever the scramble
```

All three algorithms share one interface:

```python
from pyrubik import Algorithm, make

for algorithm in Algorithm:
    solver = make(algorithm, table)
    print(algorithm, algorithm.description, len(solver(state)))
```

## Architecture

Layered strictly, no cycles — every module imports only from those above it.

| Module | Responsibility |
|---|---|
| `cube.py` | The model: 24 stickers, 12 moves, parsing, scrambling. Nothing else. |
| `coords.py` | The compact encoding the solvers run on, plus the move tables. |
| `search.py` | Bidirectional BFS. |
| `table.py` | `DistanceTable` (God's algorithm) and a background builder. |
| `ida.py` | `PatternDatabases` and IDA\*. |
| `solvers.py` | The three behind one `Solver` protocol. |
| `display.py` | Terminal rendering and color detection. |
| `bench.py` | The comparison, setup costs included. |
| `cli.py` | Argument parsing and the interactive shell. |

Two deliberate choices worth naming. The model is 24 readable bytes while the
solvers run on a dense integer index — keeping both, with the encoding verified
against the model, buys clarity where you read and speed where it runs. And no
module holds mutable global state: a `DistanceTable` is an object its caller
owns, which is why the tests need no reset hooks.

## Does threading help?

No — and the measurement is worth keeping. The distance-table build is the only
part worth parallelising: 22 million edges, level-synchronous, so each level's
frontier splits cleanly across workers. Races on the shared visited array are
benign, since two workers reaching the same state write the same value.

Measured on 4 cores, building the full table:

| Interpreter | serial | 4 threads | speedup |
|---|---|---|---|
| CPython 3.14 (GIL) | 7.45 s | 9.45 s | 0.79× |
| CPython 3.14t (free-threaded) | 9.38 s | 15.99 s | 0.59× |

Under the GIL this is expected: the inner loop is pure bytecode, so threads take
turns and only add handover cost. The free-threaded result is the interesting
one — removing the GIL did not help. That build starts 26% slower single-threaded,
and the shared `bytearray` that every worker writes into turns into contention
rather than parallelism. Free-threading pays off for work that is coarse-grained
and mostly independent; this loop is neither.

The compiled implementations in this repository do get real speedup from the
same decomposition, which is the point of comparing them.

## Development

```sh
python -m venv .venv && .venv/bin/pip install pytest ruff numpy
.venv/bin/pytest              # fast suite
.venv/bin/pytest -m slow      # also builds the full table and checks it
.venv/bin/ruff check .
```

Correctness rests on three layers rather than on any single assertion:

- `test_cube.py` pins the algebra the move tables must obey — every move has
  order four, inverses cancel, opposite faces commute, and exactly the intended
  corner stays fixed.
- `test_coords.py` checks the compact encoding against the sticker model move by
  move, so the fast representation can never drift from the readable one.
- `test_table.py` searches all 3,674,160 states and checks the distance
  distribution against OEIS A079762 term for term. That one test exercises the
  move tables, the encoding and the search together, against published fact.

## License

MIT — see [../LICENSE](../LICENSE).
