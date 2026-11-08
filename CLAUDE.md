# CLAUDE.md

One optimal 2x2x2 Rubik's cube (Pocket Cube) solver, implemented eight times (`asm`,
`c`, `cpp`, `csharp`, `go`, `javascript`, `python`, `rust`) to compare languages on
identical work. Each subdirectory is a self-contained project with its own README
documenting that implementation's design decisions and measurements. Nothing is
deployed; the product is the code plus the benchmark tables in `README.md`.

`SPEC.md` is the contract every implementation must satisfy: state encoding, move
tables, the three algorithms (`bfs` bidirectional BFS, `ida` IDA* with pattern
databases, `table` full distance table), the CLI, the benchmark protocol and JSON
schema. Read SPEC.md before changing any implementation: a change in one language
usually has to be ported to all eight.

Every implementation exposes the same CLI: `solve <state> [--algorithm bfs|ida|table]`,
`scramble [-n N] [--seed S]`, `table [--workers N]`, `bench [--json] [--scrambles FILE]
[-n N]`. `--workers` defaults to one per online CPU.

## Commands

Run from the implementation's directory; these are exactly what `ci.yml` runs.

| dir | build | test | run |
| --- | --- | --- | --- |
| `asm` | `make` | `make test` | `./rubik2x2` |
| `c` | `make` (`make CC=clang`) | `make test` | `./rubik2x2` |
| `cpp` | `make` | `make test` | `./rubik2x2` |
| `rust` | `cargo build --release` | `cargo test` | `./target/release/rubik2x2` |
| `go` | `go build -o rubik2x2 ./cmd/rubik2x2` | `go test ./...` | `./rubik2x2` |
| `csharp` | `dotnet publish -c Release -o publish` | `dotnet test -c Release` | `./publish/rubik2x2` |
| `javascript` | (none) | `node --test` | `node src/cli.js` |
| `python` | (none) | `pytest`, then `pytest -m slow` | `python -m pyrubik` |
| `tools` (repo root) | | `pytest -q tools/tests` | |

- Lint gates in CI: `cargo fmt --check`, `cargo clippy --release --all-targets -- -D
  warnings` (rust); `test -z "$(gofmt -l .)"`, `go vet ./...` (go); `ruff check .`
  (python); `ruff check tools`, `ruff format --check tools/tests` (tools). C, C++ and
  asm build with `-Wall -Wextra -Werror`; C# treats warnings as errors.
- Every CI job ends with `<prog> table` and `<prog> bench --json --scrambles
  ../bench/scrambles.txt`; the `agree` job passes only when all languages did.

Toolchains, most newer than a distro default:
- C needs gcc 14+ (`-std=c23`) or clang. C++ needs `-std=c++23` with `std::expected`,
  `std::jthread` and `std::atomic_ref`. The runner's clang has no standard library
  with all three, so CI builds C++ with g++-14 only; the clang C++ build
  (`make CXX=clang++`) is measured locally with a newer clang. `make
  STDLIB=-stdlib=libc++` selects libc++.
- Rust >= 1.85 (edition 2024), std only, zero dependencies by design. Go 1.27
  (`go/go.mod`). Node >= 24 (CI: 26). Python >= 3.14 (CI: 3.15), no runtime
  dependencies. .NET 10 (`net10.0`, solution in the new `Rubik2x2.slnx` format).
- `asm` is assembled with GNU `as`; `gcc` is only the linker driver, the C compiler
  is used solely for the test harness.
- `ARCH` defaults to `-march=native` in the C and C++ makefiles. CI overrides it with
  `ARCH=-march=x86-64-v2` because the runner's advertised feature set trips
  `-Werror`. Do the same for any build meant to be portable.
- Python's optional `numpy` (`fast` extra) only accelerates the table build; CI runs
  the python job with and without it and results must be identical. `pytest` skips
  the slow suite by default (`addopts = -m 'not slow'`); the full-table OEIS check
  needs `pytest -m slow`.

## Correctness invariant

Every test suite must assert that the distance-table histogram equals OEIS A079762
(`1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508, 930588, 1350852, 782536,
90280, 276`, sum 3,674,160, max distance 14). No wrong move table or encoding can
reproduce it, so it is the strongest check available. SPEC.md section 4 lists the
other required assertions (move order 4, encoding round-trip, rejection of the four
impossible arrangements, all three algorithms returning equal-length solutions).

## Benchmark harness

`tools/measure.py` builds each target from clean, times build/startup/artifact
size/source lines, then runs its `bench --json`; results land in
`bench/results/<target>.json` (gitignored). `tools/report.py --update-readme`
rewrites the README tables between `<!-- BENCHMARK:BEGIN -->` and
`<!-- BENCHMARK:END -->`: never hand-edit that section.

- `python3 tools/measure.py` (everything), `python3 tools/measure.py rust c` (subset),
  `--skip-build` (reuse artifacts).
- It measures `c-clang` and `cpp-clang` as separate targets from the same source, and
  runs Python as `python/.venv/bin/python`: create that venv with numpy first, or the
  python target fails.
- Everything runs strictly sequentially; two builds or benchmarks at once make the
  numbers fiction. Never benchmark while builds, tests or other agents run.
- Scrambles come from `bench/scrambles.txt` (200 fixed states), never a generator.
  The antipodal position `wppryygrygrwgprbbbwpbwyg` is the worst case (14 moves).
- `.github/workflows/bench.yml` is `workflow_dispatch` only, runs the whole matrix on
  one runner profile with portable `ARCH` and uploads `bench/results/` as an
  artifact. The README numbers come from local runs.
- `tools/measure.py` does not clear `PR_SET_THP_DISABLE`. Claude Code sessions run
  with it set, so `MADV_HUGEPAGE` silently yields no huge pages and THP-backed
  targets measure slow. Wrap the harness in an exec wrapper that calls
  `prctl(41, 0, 0, 0, 0)` first when measuring from a Claude session; GitHub runners
  need nothing.

## Performance gotchas

The recent work is all memory layout, mirrored across languages:
- The mark/visited array is 16 bits per state, not 32.
- It is backed by transparent huge pages in asm, C, C++, Rust and Go.
  `madvise(MADV_HUGEPAGE)` must reach a fresh, untouched, 2 MB-aligned anonymous
  mapping: THP picks the page size at first fault, so anything that writes first
  (an allocator zeroing an over-aligned block) locks in 4 KB pages. Rust therefore
  issues raw `mmap`/`madvise` syscalls via inline asm instead of `alloc_zeroed`
  (gain 14-18 % vs 1 %); C made the same mistake first. C# and JavaScript cannot do
  this without leaving the language.
- `table` setup timings must be the best of at least three alternating
  `table`/`table-par` samples: whichever runs first pays to fault in ~18 MB of fresh
  pages and the second inherits them warm, an artifact worth ~1.5x.

## Workflow

- Every change starts with a GitHub issue. Branch `<issue>-<slug>` from `main`,
  open a PR with `Closes #<issue>`. Never push to `main` directly.
- PRs auto-merge (squash) once CI and the "Linked ticket" check pass; drafts are not merged.
- Versions exist only as git tags `vX.Y.Z`. Every merge to `main` tags the next patch;
  never edit version numbers in files. Minor/major releases: Actions -> Release.
- Nothing is deployed.
- Details: CONTRIBUTING.md.
