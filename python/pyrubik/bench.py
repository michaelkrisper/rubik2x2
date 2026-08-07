"""Compare the solvers, setup cost included.

Per-solve time alone flatters the lookup table, because it hides the exhaustive
search that produced it. What decides which algorithm to reach for is total time
for the workload you have -- so this reports setup, per-solve, and the
break-even point where paying for the table starts to win.

The scrambles come from a file shared by every language implementation in this
repository, so the numbers compare like with like. See ../../SPEC.md section 6.
"""

import json
import os
import platform
import sys
import time
import tracemalloc
from collections.abc import Callable, Sequence
from dataclasses import asdict, dataclass, field
from pathlib import Path

from . import coords
from .cube import SOLVED, State, apply_moves, is_solved, parse, scramble
from .ida import PatternDatabases
from .solvers import Algorithm, Solver, make
from .table import DistanceTable, numpy

#: An antipodal position: 14 quarter turns from solved, the hardest there is.
HARDEST = "wppryygrygrwgprbbbwpbwyg"

#: Scrambles shared with the other language implementations.
SCRAMBLES = Path(__file__).resolve().parents[2] / "bench" / "scrambles.txt"


@dataclass(frozen=True, slots=True)
class Measurement:
    """What one algorithm cost on one workload. Field names are fixed by the spec."""

    name: str
    setup_ms: float
    avg_ms: float
    worst_ms: float
    peak_mb: float
    optimal: bool
    workers: int | None = field(default=None)

    def total_ms(self, solves: int) -> float:
        """Setup plus ``solves`` solves -- the number that actually decides."""
        return self.setup_ms + solves * self.avg_ms


def _measure(function: Callable[[], object], repeats: int = 1) -> float:
    started = time.perf_counter()
    for _ in range(repeats):
        function()
    return (time.perf_counter() - started) / repeats * 1000


def _peak_megabytes(function: Callable[[], object]) -> float:
    tracemalloc.start()
    function()
    peak = tracemalloc.get_traced_memory()[1]
    tracemalloc.stop()
    return peak / 1e6


def load_scrambles(path: Path | None = None, limit: int | None = None) -> list[State]:
    """Read the shared scramble file, falling back to generated ones."""
    path = path or SCRAMBLES
    if path.exists():
        states = [parse(line) for line in path.read_text().split() if line]
    else:  # pragma: no cover - only when run outside the repository
        import random

        states = [scramble(SOLVED, 14, random.Random(seed)) for seed in range(200)]
    return states[:limit] if limit else states


def _evaluate(
    name: str,
    solve: Solver,
    setup_ms: float,
    states: Sequence[State],
    hardest: State,
    reference: Solver,
    repeats: int,
    workers: int | None = None,
) -> Measurement:
    average = _measure(lambda: [solve(state) for state in states], repeats) / len(states)
    worst = _measure(lambda: solve(hardest), repeats)
    peak = _peak_megabytes(lambda: solve(hardest))
    optimal = all(
        len(solve(state)) == len(reference(state)) and is_solved(apply_moves(state, solve(state)))
        for state in states[:25]
    )
    return Measurement(name, setup_ms, average, worst, peak, optimal, workers)


def measure_all(states: Sequence[State]) -> list[Measurement]:
    """Time every algorithm end to end, setup included."""
    hardest = parse(HARDEST)
    coords.move_tables()  # shared by all of them; not any one algorithm's setup
    reference = DistanceTable.build().solve

    results = [
        _evaluate("bfs", make(Algorithm.BFS), 0.0, states, hardest, reference, 1),
        _evaluate(
            "ida",
            make(Algorithm.IDA),
            _measure(PatternDatabases.build),
            states, hardest, reference, 1,
        ),
    ]

    table_setup = _measure(DistanceTable.build)
    table = DistanceTable.build()
    results.append(
        _evaluate("table", table.solve, table_setup, states, hardest, reference, 20)
    )

    workers = os.process_cpu_count() or 1
    parallel_setup = _measure(lambda: DistanceTable.build_parallel(workers))
    results.append(
        _evaluate(
            "table-par", table.solve, parallel_setup, states, hardest, reference, 20, workers
        )
    )
    return results


def as_json(results: Sequence[Measurement]) -> str:
    """The machine-readable report tools/report.py aggregates."""
    gil = "GIL" if getattr(sys, "_is_gil_enabled", lambda: True)() else "free-threaded"
    return json.dumps(
        {
            "language": "Python",
            "version": platform.python_version(),
            "build": f"{platform.python_implementation()}, {gil}"
            + (", numpy table build" if numpy is not None else ", pure Python table build"),
            "algorithms": [
                {key: value for key, value in asdict(result).items() if value is not None}
                for result in results
            ],
        },
        indent=2,
    )


def _break_even(fast: Measurement, slow: Measurement) -> int | None:
    """Solves after which ``fast`` overtakes ``slow``, setup included."""
    gain = slow.avg_ms - fast.avg_ms
    if gain <= 0:
        return None
    return int((fast.setup_ms - slow.setup_ms) / gain) + 1


def run(samples: int | None = None, as_json_only: bool = False, path: Path | None = None) -> None:
    """Run the comparison and print a report."""
    states = load_scrambles(path, samples)
    results = measure_all(states)

    if as_json_only:
        print(as_json(results))
        return

    print(f"pyrubik benchmark -- {len(states)} scrambles plus one antipodal position")
    print(f"{coords.N_STATES:,} reachable states, maximum distance 14 quarter turns")
    print(f"numpy: {'yes' if numpy is not None else 'no'}\n")

    header = (
        f"{'algorithm':<11}{'setup ms':>11}{'avg ms':>10}"
        f"{'worst ms':>11}{'peak MB':>10}{'optimal':>9}"
    )
    print(header)
    print("-" * len(header))
    for result in results:
        print(
            f"{result.name:<11}{result.setup_ms:11.1f}{result.avg_ms:10.3f}"
            f"{result.worst_ms:11.3f}{result.peak_mb:10.1f}"
            f"{'yes' if result.optimal else 'NO':>9}"
        )

    print("\ntotal seconds for a workload (setup + solves)")
    counts = (1, 10, 100, 1_000, 10_000)
    print(f"{'algorithm':<11}" + "".join(f"{count:>12,}" for count in counts))
    print("-" * (11 + 12 * len(counts)))
    for result in results:
        print(
            f"{result.name:<11}"
            + "".join(f"{result.total_ms(count) / 1000:>12.2f}" for count in counts)
        )

    by_name = {result.name: result for result in results}
    crossover = _break_even(by_name["table"], by_name["bfs"])
    reach = max(PatternDatabases.build().reach)
    print(
        f"\nThe table overtakes bidirectional search at about {crossover:,} solves."
        "\nBelow that, building it costs more than it saves; above it, nothing"
        "\nelse comes close. IDA* is dominated everywhere: its pattern databases"
        f"\nreach only distance {reach} of 14, too loose to prune a puzzle this small."
    )
    speedup = by_name["table"].setup_ms / by_name["table-par"].setup_ms
    print(
        f"\nThreading the table build gives {speedup:.2f}x on {by_name['table-par'].workers}"
        " cores -- see the README for why\nthat number is below 1 on both CPython builds."
    )
