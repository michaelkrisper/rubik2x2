#!/usr/bin/env python3
"""Measure every implementation the same way, one at a time.

Builds each language from clean, times it, sizes the artifact, counts the source,
then runs its benchmark. Everything runs **sequentially** -- two builds or two
benchmarks at once would compete for the same cores and the numbers would be
fiction.

    python3 tools/measure.py                # everything
    python3 tools/measure.py rust c         # only these
    python3 tools/measure.py --skip-build   # reuse existing artifacts

Writes bench/results/<language>.json, which tools/report.py turns into the
comparison tables in the README. Standard library only.
"""

import argparse
import json
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "bench" / "results"
SCRAMBLES = ROOT / "bench" / "scrambles.txt"


@dataclass(frozen=True)
class Target:
    """How to build, size, count and run one implementation."""

    name: str
    directory: str
    #: Removed before timing a build, so "build time" means from clean.
    clean: list[str] = field(default_factory=list)
    build: list[str] = field(default_factory=list)
    #: Artifact whose size is reported, relative to the directory.
    artifact: str | None = None
    #: Extra artifacts summed into the size (runtime assemblies, for instance).
    artifact_glob: str | None = None
    run: list[str] = field(default_factory=list)
    source_globs: tuple[str, ...] = ()
    test_globs: tuple[str, ...] = ()
    comment_prefixes: tuple[str, ...] = ("//",)

    @property
    def path(self) -> Path:
        return ROOT / self.directory


TARGETS = [
    Target(
        name="asm",
        directory="asm",
        clean=["make", "clean"],
        build=["make"],
        artifact="rubik2x2",
        run=["./rubik2x2"],
        source_globs=("src/*.s", "src/*.inc"),
        test_globs=("tests/*.c", "tests/*.s", "tests/*.h"),
        comment_prefixes=("#", "//", "/*", "*"),
    ),
    Target(
        name="c",
        directory="c",
        clean=["make", "clean"],
        build=["make"],
        artifact="rubik2x2",
        run=["./rubik2x2"],
        source_globs=("*.c", "*.h", "src/*.c", "src/*.h", "include/*.h"),
        test_globs=("test*.c", "tests/*.c", "tests/*.h"),
    ),
    Target(
        name="cpp",
        directory="cpp",
        clean=["make", "clean"],
        build=["make"],
        artifact="rubik2x2",
        run=["./rubik2x2"],
        source_globs=("*.cpp", "*.hpp", "src/*.cpp", "src/*.hpp", "include/*.hpp"),
        test_globs=("test*.cpp", "tests/*.cpp", "tests/*.hpp"),
    ),
    Target(
        name="rust",
        directory="rust",
        clean=["cargo", "clean"],
        build=["cargo", "build", "--release"],
        artifact="target/release/rubik2x2",
        run=["./target/release/rubik2x2"],
        source_globs=("src/*.rs", "src/**/*.rs"),
        test_globs=("tests/*.rs",),
    ),
    Target(
        name="go",
        directory="go",
        clean=["go", "clean", "-cache"],
        build=["go", "build", "-ldflags=-s -w", "-o", "rubik2x2", "./cmd/rubik2x2"],
        artifact="rubik2x2",
        run=["./rubik2x2"],
        source_globs=("**/*.go",),
        test_globs=("**/*_test.go",),
    ),
    Target(
        name="csharp",
        directory="csharp",
        clean=["dotnet", "clean", "-c", "Release"],
        build=["dotnet", "publish", "-c", "Release", "-o", "publish"],
        artifact="publish/rubik2x2",
        artifact_glob="publish/*",
        run=["./publish/rubik2x2"],
        source_globs=("**/*.cs",),
        test_globs=("**/*Tests*.cs", "**/*Test.cs"),
    ),
    Target(
        name="javascript",
        directory="javascript",
        run=["node", "src/cli.js"],
        source_globs=("src/*.js", "src/**/*.js"),
        test_globs=("test/*.js", "tests/*.js", "**/*.test.js"),
    ),
    Target(
        name="python",
        directory="python",
        run=[".venv/bin/python", "-m", "pyrubik"],
        source_globs=("pyrubik/*.py",),
        test_globs=("tests/*.py",),
        comment_prefixes=("#",),
    ),
]


def _count(target: Target, globs: tuple[str, ...]) -> tuple[int, int]:
    """(files, non-blank non-comment lines) for the matching sources."""
    seen: set[Path] = set()
    for pattern in globs:
        seen.update(path for path in target.path.glob(pattern) if path.is_file())
    lines = 0
    for path in seen:
        for line in path.read_text(errors="replace").splitlines():
            stripped = line.strip()
            if stripped and not stripped.startswith(target.comment_prefixes):
                lines += 1
    return len(seen), lines


def _size(target: Target) -> int:
    if target.artifact_glob:
        return sum(p.stat().st_size for p in target.path.glob(target.artifact_glob) if p.is_file())
    if target.artifact and (target.path / target.artifact).exists():
        return (target.path / target.artifact).stat().st_size
    # Interpreted: the source that ships is the artifact.
    return sum(
        path.stat().st_size
        for pattern in target.source_globs
        for path in target.path.glob(pattern)
        if path.is_file()
    )


def _run(command: list[str], cwd: Path, capture: bool = False) -> subprocess.CompletedProcess:
    return subprocess.run(command, cwd=cwd, text=True, capture_output=capture, check=False)


def _startup(target: Target, rounds: int = 7) -> tuple[float | None, int | None]:
    """(fastest wall time in ms, peak resident KB) for the cheapest real command.

    This is the cost of starting the thing at all: process creation, loading the
    runtime, JIT warm-up, and whatever the implementation does eagerly at start.
    It is charged to no algorithm, but you pay it on every invocation, and for
    the managed runtimes it dwarfs a table lookup by five orders of magnitude.

    ``scramble`` is used because it is the least work any of them can be asked
    to do while still parsing arguments and producing output.
    """
    if not shutil.which("/usr/bin/time"):  # pragma: no cover - depends on the box
        return None, None
    # /usr/bin/time reports elapsed seconds to two decimals, which rounds every
    # compiled binary to 0.0. Take the wall time here instead and let it report
    # only peak RSS, which it is the easy way to get.
    command = ["/usr/bin/time", "-f", "%M", *target.run, "scramble", "-n", "1", "--seed", "1"]
    times: list[float] = []
    resident: list[int] = []
    for _ in range(rounds):
        started = time.perf_counter()
        completed = _run(command, target.path, capture=True)
        times.append((time.perf_counter() - started) * 1000)
        if completed.returncode != 0:
            return None, None
        try:  # /usr/bin/time writes its line last, on stderr
            resident.append(int(completed.stderr.strip().splitlines()[-1]))
        except (IndexError, ValueError):
            return None, None
    return min(times), sorted(resident)[len(resident) // 2]


def measure(target: Target, skip_build: bool) -> dict | None:
    if not target.path.is_dir():
        print(f"  {target.name}: not implemented yet, skipping")
        return None

    build_ms: float | None = None
    if target.build and not skip_build:
        if target.clean:
            _run(target.clean, target.path, capture=True)
        print(f"  building {target.name}...", flush=True)
        started = time.perf_counter()
        completed = _run(target.build, target.path, capture=True)
        build_ms = (time.perf_counter() - started) * 1000
        if completed.returncode != 0:
            print(f"  {target.name}: BUILD FAILED\n{completed.stderr[-2000:]}")
            return None

    print(f"  benchmarking {target.name}...", flush=True)
    completed = _run(
        [*target.run, "bench", "--json", "--scrambles", str(SCRAMBLES)],
        target.path,
        capture=True,
    )
    if completed.returncode != 0:
        print(f"  {target.name}: BENCH FAILED\n{completed.stderr[-2000:]}")
        return None
    try:
        report = json.loads(completed.stdout[completed.stdout.index("{"):])
    except (ValueError, json.JSONDecodeError) as error:
        print(f"  {target.name}: unreadable JSON ({error})")
        return None

    print(f"  timing {target.name} startup...", flush=True)
    startup_ms, startup_rss_kb = _startup(target)

    source_files, source_lines = _count(target, target.source_globs)
    test_files, test_lines = _count(target, target.test_globs)
    report |= {
        "build_ms": build_ms,
        "startup_ms": startup_ms,
        "startup_rss_kb": startup_rss_kb,
        "artifact_bytes": _size(target),
        "source_files": source_files,
        "source_lines": source_lines,
        "test_files": test_files,
        "test_lines": test_lines,
    }
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / f"{target.name}.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("only", nargs="*", help="languages to measure (default: all)")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    if not SCRAMBLES.exists():
        sys.exit(f"missing {SCRAMBLES}")

    targets = [t for t in TARGETS if not args.only or t.name in args.only]
    print(f"Measuring {len(targets)} implementation(s), one at a time.\n")
    done = 0
    for target in targets:
        print(f"{target.name}:")
        if measure(target, args.skip_build) is not None:
            done += 1
    print(f"\n{done}/{len(targets)} measured -> {RESULTS}")
    print("Run tools/report.py --update-readme to refresh the README tables.")
    return 0 if done else 1


if __name__ == "__main__":
    raise SystemExit(main())
