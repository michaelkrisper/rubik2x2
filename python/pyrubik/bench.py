"""Compare the three solvers, setup cost included.

Per-solve time alone flatters the lookup table, because it hides the exhaustive
search that produced it. What actually decides which algorithm to reach for is
total time for the workload you have -- so this reports setup, per-solve, and
the break-even point where paying for the table starts to win.
"""

import random
import time
import tracemalloc
from collections.abc import Callable, Sequence
from dataclasses import dataclass

from . import coords
from .cube import SOLVED, State, apply_moves, is_solved, parse, scramble
from .ida import PatternDatabases
from .solvers import Algorithm, Solver, make
from .table import DistanceTable, numpy

#: An antipodal position: 14 quarter turns from solved, the hardest there is.
HARDEST = "wppryygrygrwgprbbbwpbwyg"

#: What each algorithm must precompute before its first solve. Only the
#: benchmark cares, so it lives here rather than in the library.
SETUP: dict[Algorithm, Callable[[], object]] = {
    Algorithm.BFS: lambda: None,
    Algorithm.IDA: PatternDatabases.build,
    Algorithm.TABLE: DistanceTable.build,
}


@dataclass(frozen=True, slots=True)
class Result:
    """What one algorithm cost on one workload."""

    algorithm: Algorithm
    setup_ms: float
    average_ms: float
    worst_ms: float
    peak_mb: float
    optimal: bool

    def total_ms(self, solves: int) -> float:
        """Setup plus ``solves`` solves -- the number that actually decides."""
        return self.setup_ms + solves * self.average_ms


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


def _repeats_for(algorithm: Algorithm) -> int:
    """Fast solvers need repetition before the clock says anything useful."""
    return 20 if algorithm is Algorithm.TABLE else 1


def evaluate(
    algorithm: Algorithm,
    states: Sequence[State],
    hardest: State,
    reference: Solver,
) -> Result:
    """Time one algorithm end to end, setup included."""
    setup_ms = _measure(SETUP[algorithm])
    table = DistanceTable.build() if algorithm is Algorithm.TABLE else None
    solve = make(algorithm, table)
    repeats = _repeats_for(algorithm)

    average = _measure(lambda: [solve(state) for state in states], repeats) / len(states)
    worst = _measure(lambda: solve(hardest), repeats)
    peak = _peak_megabytes(lambda: solve(hardest))
    optimal = all(
        len(solve(state)) == len(reference(state)) and is_solved(apply_moves(state, solve(state)))
        for state in states[:25]
    )
    return Result(algorithm, setup_ms, average, worst, peak, optimal)


def _break_even(fast: Result, slow: Result) -> int | None:
    """Solves after which ``fast`` overtakes ``slow``, setup included."""
    gain = slow.average_ms - fast.average_ms
    if gain <= 0:
        return None
    return int((fast.setup_ms - slow.setup_ms) / gain) + 1


def run(samples: int = 200) -> list[Result]:
    """Run the comparison and print a report. Returns the raw results."""
    hardest = parse(HARDEST)
    states = [scramble(SOLVED, 14, random.Random(seed)) for seed in range(samples)]

    print(f"pyrubik benchmark -- {samples} random scrambles plus one antipodal position")
    print(f"{coords.N_STATES:,} reachable states, maximum distance 14 quarter turns")
    print(f"numpy: {'yes' if numpy is not None else 'no (pure-Python table build)'}\n")

    move_tables_ms = _measure(coords.move_tables)  # shared by all three, built once
    print(f"shared move tables: {move_tables_ms:.0f} ms\n")

    reference = DistanceTable.build().solve
    results = [evaluate(algorithm, states, hardest, reference) for algorithm in Algorithm]

    header = (
        f"{'algorithm':<8}{'setup ms':>11}{'avg ms':>10}"
        f"{'worst ms':>11}{'peak MB':>10}{'optimal':>9}"
    )
    print(header)
    print("-" * len(header))
    for result in results:
        print(
            f"{result.algorithm:<8}{result.setup_ms:11.1f}{result.average_ms:10.3f}"
            f"{result.worst_ms:11.3f}{result.peak_mb:10.1f}"
            f"{'yes' if result.optimal else 'NO':>9}"
        )

    print("\ntotal time for a workload (setup + solves)")
    counts = (1, 10, 100, 1_000, 10_000)
    print(f"{'algorithm':<8}" + "".join(f"{count:>12,}" for count in counts))
    print("-" * (8 + 12 * len(counts)))
    for result in results:
        row = "".join(f"{result.total_ms(count) / 1000:>12.2f}" for count in counts)
        print(f"{result.algorithm:<8}{row}")
    print(f"{'':8}" + "".join(f"{'seconds':>12}" for _ in counts))

    by_name = {result.algorithm: result for result in results}
    crossover = _break_even(by_name[Algorithm.TABLE], by_name[Algorithm.BFS])
    reach = max(PatternDatabases.build().reach)
    if crossover is not None:
        print(
            f"\nThe table overtakes bidirectional search at about {crossover:,} solves."
            "\nBelow that, building it costs more than it saves; above it, nothing"
            "\nelse comes close. IDA* is dominated everywhere: its pattern databases"
            f"\nreach only distance {reach} of 14, too loose to prune a puzzle this small."
        )
    return results
