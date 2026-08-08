# rubik2x2 — x86-64 assembly

Optimal 2x2x2 solver written in hand-written x86-64 assembly, GNU `as` with
Intel syntax. No compiler is involved in producing `./rubik2x2`: `as` assembles
every instruction and `gcc` appears only as the linker driver, so that the C
runtime start-up files and libc come along.

## Build, run, test

```sh
make                # ./rubik2x2
make test           # builds and runs the test suite
make bench          # human-readable benchmark against ../bench/scrambles.txt
make clean
```

```sh
./rubik2x2 solve wppryygrygrwgprbbbwpbwyg [--algorithm bfs|ida|table]
./rubik2x2 scramble [-n N] [--seed S]
./rubik2x2 table [--workers N]               # build the table, print the histogram
./rubik2x2 bench [--json] [--scrambles FILE] [-n N] [--workers N]
```

`solve` writes nothing but the move sequence to stdout, so it pipes. A state
argument of `-` reads the state from stdin. Unsolvable input exits non-zero with
the specific reason on stderr.

## Layout

| File | What it owns |
|---|---|
| `src/rubik.inc` | every shared constant: sizes, status codes, struct offsets, row shifts |
| `src/cube.s` | the 24-sticker model: move tables, parsing, rendering, rotations, the RNG |
| `src/coords.s` | ranking, encode/decode, and the per-move coordinate tables built at start-up |
| `src/bfs.s` | bidirectional breadth-first search |
| `src/ida.s` | IDA\* with the two pattern databases |
| `src/table.s` | the full distance table, serial and parallel builds |
| `src/bench.s` | the benchmark and its JSON report |
| `src/cli.s` | argument parsing; knows nothing about how a cube is solved |
| `tests/test_rubik.c` | everything SPEC.md section 4 requires (see [Tests](#tests)) |

The layering the spec asks for is enforced by what each file references:
`cube.s` names no coordinate symbol, and the three solvers name no facelet.

## Conventions

**Calling convention.** System V AMD64, unmodified, so every routine is
callable from C and the test suite does exactly that. Arguments in
`rdi rsi rdx rcx r8 r9`, doubles in `xmm0..`, integer and pointer results in
`rax`, doubles in `xmm0`. `rbx rbp r12-r15` are callee-saved and every routine
that touches them pushes them. Before any `call` into libc the stack is
16-byte aligned; variadic calls set `al` to the number of vector registers used
(`xor eax, eax` for the integer-only cases, `mov eax, 4` for the benchmark's
`%f` rows).

**A cube is a pointer to 24 bytes**, one colour index per facelet, never a
value. `cube_apply_move(dst, src, move)` requires that `dst` and `src` do not
alias; callers that want in-place turns copy through a 24-byte scratch, which
is three `mov`s of 8 bytes.

**Status in `eax`.** `0` is success everywhere. `coords_encode` returns
`CE_CORNER/PIECE/REPEAT/TWIST` (1..4); the solvers return
`SV_INVALID/NOSOL/NOMEM` (1..3). Both have a `*_status_message` that turns the
code into the sentence the CLI prints.

**A `solution` is 48 bytes**: a 32-byte NUL-terminated move string at offset 0,
its length at 32, and the bytes the algorithm held live at 40 (`peak_mb`).
Offsets live in `rubik.inc` as `SOL_MOVES/SOL_LEN/SOL_PEAK`, and the test
suite `_Static_assert`s a matching C struct against them.

**Memory.** Everything of fixed size is `.bss` — the coordinate tables
(415 KB), the two pattern databases, the parallel context. Only the two big
arrays are dynamic: `bfs_prepare()` `calloc`s the 14.7 MB mark array once for
the life of the process, and `table_create()` `calloc`s 3.7 MB per table.
Tables are addressed by shifting rather than multiplying: rows of six moves are
padded to eight, so `perm_wide[q]` is at `q << 5` and `orient_child[r]` at
`r << 5`, and a child index is `perm_wide[q][m] + orient_child[r][m]` — one
add, because the permutation table is pre-multiplied by 729.

**Idempotent set-up.** `coords_init`, `ida_prepare` and `cube_rotations` each
guard on a ready flag and derive their tables from the sticker model. Nothing
in this repository is a pasted magic table except the twelve move permutations
and the three rotation generators, which a human can check by hand.

## Assembly versus libc

Every algorithm is assembly: the sticker model, ranking, encoding, all three
solvers, both table builds, the benchmark's timing and reporting logic, and
argument parsing. Nothing is delegated to a library routine that would do the
interesting part.

libc is used for the things a freestanding program would have to reimplement
against the kernel, and for nothing else:

| Purpose | Symbols |
|---|---|
| allocation | `calloc`, `free` |
| bulk memory | `memset`, `memcmp` |
| formatted output | `printf`, `dprintf`, `puts` |
| files | `fopen`, `fgets`, `fclose` |
| strings and numbers in `argv` | `strcmp`, `strncmp`, `strtol` |
| time | `clock_gettime`, `time` |
| CPU count | `sysconf` |
| threads | `pthread_create/join`, `pthread_barrier_*`, `pthread_mutex_*` |

`printf` is the one that could be argued about. Writing a `%.4f` formatter in
assembly would add a few hundred lines that have nothing to do with solving a
cube, and every other implementation in this repository formats its benchmark
output with its language's standard formatter.

## Why the design is shaped this way

The reasoning behind the fixed corner, the split encoding, the bidirectional
search and the table's break-even is the same in every implementation and is
written out at the top of each `.s` file. In brief:

**The fixed corner.** Solutions use only `l L t T f F`; those six never touch
facelets 11, 13 and 23. Holding that corner still removes the 24-fold whole-cube
rotation symmetry, which is what cuts the space to 3,674,160 states.

**Why the encoding splits in two.** 7 pieces in 7 slots (5040) crossed with
their twists (3⁶ = 729, the seventh forced). A move acts on the two halves
*independently* — hence the single add above, and hence IDA\*'s heuristic comes
free: each coordinate alone is a genuine quotient of the state graph, so its
exact distance is admissible.

**Why `bfs` marks a flat array.** A state is already a dense integer below
3,674,160, so hashing it would be work spent deriving an index from something
that is one. One `uint32` per state holds the arriving move, the parent, the
frontier and a 6-bit generation stamp, so a solve bumps a counter instead of
clearing 14.7 MB. Six bits give 62 solves between real clears.

**The parallel table build** uses the spec's compare-exchange option, not
per-worker buffers: on this problem the merge *is* the work, since every one of
the 22 million edges has to be tested against the visited array anyway.
A worker claims a state with one `lock cmpxchg` byte-sized from 0 to the level
marker, guarded by a plain load — about 85% of edges lead somewhere already
visited and never execute a locked instruction. Queue slots are claimed 512 at
a time and frontier chunks 4096 at a time with `lock xadd`, so the two hot
shared words are touched once per block rather than once per state. Threads are
created once and meet at a `pthread_barrier_t` twice per level.

On x86-64 the serial and parallel builds run the identical inner loop apart
from the exchange, because a relaxed byte load or store *is* a plain `mov`.
The test suite asserts the two produce byte-identical tables.

## What was tricky

**`as` has no C preprocessor.** `.equ`, `.include` and `.macro` are the whole
toolkit: no conditional compilation, no string interpolation, no header to
carry a type. The version strings the benchmark reports (`"language"`, `"version"`,
`"build"`) therefore cannot be baked into a source file — the Makefile
generates `build/version.s` with `printf` and assembles that. It is the only
generated source, and it contains nothing but two `.asciz` strings.

**Dividing by 729.** Unpacking an index into `(q, r)` happens on the hot path
of every solver and both table builds, and `div` is 20-40 cycles. `rubik.inc`
carries `MAGIC729 = floor(2^32 / 729) + 1`, so `q = (index * MAGIC729) >> 32`
is one `imul` and one `shr`. The reciprocal is only exact over a bounded range:
the error term is 356 and the largest index 3,674,159, and 356 × 3,674,159 <
2^32, so it is exact for every index this program can hold. That bound is the
kind of thing a comment has to carry, because nothing in the code states it.

**Register pressure in `ida_descend`.** The search wanted seven live values in
six callee-saved registers. The deepening limit does not change inside one
pass, so it lives in `.bss` as `ida_limit1` rather than in the frame or an
argument — which is what lets the recursion run with no stack traffic beyond
the pushes, at roughly fifteen instructions per child.

**Opaque libc structures have no header to ask.** `pthread_barrier_t` and
`pthread_mutex_t` are 32 and 40 bytes on glibc/x86-64, and assembly cannot
`sizeof` them. The parallel context reserves 64 bytes for each at fixed offsets
(`PC_BARRIER`, `PC_MUTEX`) with the cache-line padding doing double duty as
headroom. It is the one place in the program that depends on a detail of the
platform's libc rather than on the ISA or the ABI.

**Stack frames are hand-laid.** There is no compiler to allocate slots, so
every frame with more than a couple of locals names its offsets as `.equ`
constants at the top of the file (`F_BUFA`, `F_MEET`, `F_STAMP`, … in `bfs.s`).
Doing this by hand once per function is fine; doing it implicitly is how frames
overlap.

**`.note.GNU-stack`.** Every file ends with
`.section .note.GNU-stack,"",@progbits`. Without it the linker assumes the
object might want an executable stack and marks the binary accordingly, with a
warning on modern binutils. A compiler emits this for you.

## Tests

`make test` builds and runs `tests/test_rubik.c`: **16,487 checks in 12
groups**, no framework and no dependency.

**The runner is C; everything it tests is assembly.** The spec requires the
*algorithms* to be in assembly, not the harness, and a C harness can print what
it expected next to what it got when a check fails — an assembly one would have
to spell that out by hand for every assertion. It calls the assembly routines
from outside through the ordinary System V ABI, which also means the test suite
independently verifies that the ABI is respected: if a routine clobbered a
callee-saved register, this is where it would show. There are no headers in
this project — the assembly is the interface — so the test file declares every
symbol itself, and `_Static_assert`s its `solution_t` against the `SOL_*`
offsets in `rubik.inc`. The Makefile compiles that one file, and only that
file, with `gcc`; nothing compiled reaches `./rubik2x2`.

What is covered:

- the **OEIS A079762** histogram from the full table build, exactly — the
  strongest single check available, since no incorrect move table and no
  incorrect encoding can reproduce those fifteen numbers
- move algebra: every move has order exactly 4 (and is not the identity, and
  does not square to it), a move and its inverse cancel facelet by facelet on
  24 distinct markers, three turns equal the inverse, opposite faces commute
- exactly facelets 11, 13 and 23 are fixed by the six solve moves, they are
  slot 7, and every facelet belongs to exactly one slot
- parsing: the three error codes are distinguished, whitespace is trimmed, and
  100 random scrambles round-trip through their text form
- `cube_invert` cancels 50 random 8-move sequences
- the whole-cube rotation group has 24 distinct elements, each a permutation
- the encoding round-trips both ways (2000 indices including both ends, 500
  upright scrambles), every solved cube encodes to 0 in all 24 orientations,
  and every decoded state has four stickers of each colour
- the compact move tables agree with the sticker model on every move of 300
  random scrambles, `perm_child` agrees with `perm_wide`, and each move is a
  bijection on both coordinates
- all four impossible arrangements are rejected **with their own status code**:
  an impossible fixed corner (`CE_CORNER`), a slot holding no valid piece
  (`CE_PIECE`), the same piece in two slots (`CE_REPEAT`), and twists that do
  not sum to a multiple of three (`CE_TWIST`) — plus the near miss that keeps
  the last one honest: three corners twisted alike sum to 0 mod 3, are
  reachable, and must be accepted *and solved*
- all three algorithms return equal-length solutions, each equal to the table's
  distance, drawn only from the six solve moves, and every solution actually
  solves the cube it was given — on 26 scrambles and again on a stride through
  the whole index space
- the antipodal position `wppryygrygrwgprbbbwpbwyg` needs exactly 14 moves,
  from all three algorithms
- all 24 orientations of a scramble need the same number of moves and each is
  actually solved (they encode to *different* indices — see below)
- the parallel build produces a byte-identical table for 2, 4 and 8 workers
- both pattern databases reach every coordinate value and stop short of 14,
  which is the documented reason IDA\* does not win here

### Two things the encoding does *not* promise

**The 24 orientations do not share an index.** Every *solved* cube encodes to 0
however it is held, and every orientation of a scramble is solved correctly —
but rotating the cube puts a different physical piece in the fixed slot, so the
permutation coordinate genuinely changes. What is invariant is the distance.
It follows that `coords_decode` inverts `coords_encode` only for cubes held the
standard way up, since decoding always produces one.

**An illegal arrangement will not announce itself later.** The index space is
smaller than the space of sticker arrangements: six twists are stored and the
seventh derived, and the permutation is ranked as a bijection. One corner
turned in place would normalise into some *other*, perfectly legal state, and
the solver would return a confident solution leaving the cube scrambled. That
is why `to_cubies` checks all four conditions before ranking.

## Against the C implementation

Both measured on the same machine — Intel i5-4258U, 2 physical cores with
hyper-threading, `powersave` governor — with both implementations optimised.
C is `-O3 -march=native -flto`; the assembly has no equivalent knob, which is
part of the point.

| | assembly | C | |
|---|---|---|---|
| `bfs` avg | 0.091 ms | 0.078 ms | C 1.17× faster |
| `ida` avg | 0.094 ms | 0.080 ms | C 1.18× faster |
| `table` setup, serial | 143 ms | 144 ms | even |
| `table-par` setup, 4 workers | 84.6 ms | 86 ms | even |
| binary as built | 47 KB | 53 KB | assembly 12% smaller |
| binary stripped | 27 KB | 48 KB | assembly 1.8× smaller |

Worst case, assembly: `bfs` 1.15 ms and `ida` 10.3 ms, both on the antipodal
position. All four entries report `optimal: true`.

**The honest reading: writing it by hand did not make it faster.** GCC is
about 17% ahead on both search algorithms and level on the table build, which
is the result you should expect and the one worth reporting.

Where the two are level — the table builds — the inner loop is memory-latency
bound on a randomly-accessed 3.7 MB array that does not fit in this chip's 3 MB
L3. The CPU is waiting, and it does not matter who emitted the `mov`. Both
implementations also converged on the same algorithmic choices (reciprocal
multiply for the divide, rows padded so addressing is a shift, the
pre-multiplied permutation table), so there is no instruction-selection gap
left to exploit: the C source was already written against the machine.

Where GCC is ahead — the two search algorithms — it is winning on the things a
human does not do by hand across thousands of lines: it unrolls and software-
pipelines the child-expansion loops, schedules loads earlier than is readable
to write, and reallocates registers per basic block instead of per function.
The assembly hand-unrolls the six children in `bfs.s` and `table.s` with a
`.macro`, which is where most of the gap would otherwise be, but it keeps one
fixed register assignment for a whole routine because tracking a per-block one
by hand is how you introduce bugs. `-march=native` also lets GCC use
instructions this code deliberately does not (it targets baseline x86-64 plus
`popcnt`, which `rank_perm` uses).

The binary is smaller for an uninteresting reason: 16.7 KB of `.text`, because
there is no inlining and no unrolling beyond the six children, and nothing is
duplicated for `-flto` to specialise. The gap widens to 1.8× once both are
stripped, since the assembly's `-g` output is the bulkier half of its file.
Small and slower is the trade here, not a win.

What hand-writing did buy is that the cost model is visible. `MAGIC729`'s
exactness bound, the 6-bit generation stamp, and the register budget in
`ida_descend` are all decisions in the source rather than the compiler's
private opinion about them. That is worth something for reading, and nothing
for the benchmark.

## Notes

- `--workers` defaults to 1 for the `table` command and to one per online CPU
  for `table-par` in the benchmark, matching the C implementation.
- The optional `show` subcommand (drawing a cube as a net) is not implemented
  here; the four commands SPEC.md section 5 requires all are.
- `bfs`'s 14.7 MB mark array is allocated once and intentionally never freed:
  it lives for the process, so the benchmark's per-solve cost does not include
  it. Every other allocation is balanced.
