"""God's algorithm: the exact distance of every reachable state.

The pocket cube has 3,674,160 positions -- few enough to hold every one's exact
distance to solved in a flat byte array of 3.7 MB. Then there is nothing left to
search, only a table to read:

    at a state *d* moves from solved, exactly one neighbour is *d-1* away;
    step to it, repeat.

A solve costs *d* lookups: constant time in the difficulty of the scramble, and
optimal by construction. This is the classic result for the 2x2x2 -- Jerry Bryan
computed the same distribution in 1993, and it is recorded as OEIS A079762,
which :meth:`DistanceTable.histogram` reproduces exactly.

The table is built in memory and never cached to disk, so it always matches the
code that produced it. Building costs one exhaustive breadth-first search over
22 million edges, which is the whole trade: instant solves, paid for up front.
:class:`BackgroundTable` moves that cost off the calling thread.
"""

import os
from array import array
from collections.abc import Callable, Sequence
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass
from itertools import repeat
from typing import Self

from . import coords
from .cube import Moves, State, UnsolvableError

try:  # optional accelerator; the pure-Python build below is the reference
    import numpy
except ImportError:  # pragma: no cover - depends on the environment
    numpy = None

#: Called with (depth, states found at that depth) once per level.
type Progress = Callable[[int, int], None]


def _build_python(progress: Progress | None) -> bytearray:
    """Breadth-first over the whole space. Returns distance + 1 per state.

    The loop is unrolled over the six moves and the permutation tables come
    pre-multiplied by the row width, because this runs 22 million times and at
    that point a multiply and a tuple unpack are the cost.

    The frontier is an ``array('i')`` rather than a list: at its widest it holds
    1.35 million entries, and boxed Python ints would cost ~50 MB where packed
    32-bit values cost 5 MB.
    """
    width = coords.N_ORIENTATIONS
    (_, p0, o0), (_, p1, o1), (_, p2, o2), (_, p3, o3), (_, p4, o4), (_, p5, o5) = (
        coords.wide_tables()
    )

    distances = bytearray(coords.N_STATES)
    distances[coords.SOLVED_INDEX] = 1
    level = array("i", [coords.SOLVED_INDEX])
    depth = 0

    while level:
        if progress is not None:
            progress(depth, len(level))
        depth += 1
        marker = depth + 1
        following = array("i")
        append = following.append
        for node in level:
            q, r = divmod(node, width)
            if not distances[c := p0[q] + o0[r]]:
                distances[c] = marker
                append(c)
            if not distances[c := p1[q] + o1[r]]:
                distances[c] = marker
                append(c)
            if not distances[c := p2[q] + o2[r]]:
                distances[c] = marker
                append(c)
            if not distances[c := p3[q] + o3[r]]:
                distances[c] = marker
                append(c)
            if not distances[c := p4[q] + o4[r]]:
                distances[c] = marker
                append(c)
            if not distances[c := p5[q] + o5[r]]:
                distances[c] = marker
                append(c)
        level = following

    return distances


def _build_numpy(progress: Progress | None) -> bytearray:
    """The same search, one array operation per level instead of per edge."""
    tables = coords.move_tables()
    width = coords.N_ORIENTATIONS
    permutation = numpy.array([table for _, table, _ in tables], dtype=numpy.int32)
    orientation = numpy.array([table for _, _, table in tables], dtype=numpy.int32)

    distances = numpy.zeros(coords.N_STATES, dtype=numpy.uint8)
    distances[coords.SOLVED_INDEX] = 1
    level = numpy.array([coords.SOLVED_INDEX], dtype=numpy.int64)
    depth = 0

    while level.size:
        if progress is not None:
            progress(depth, int(level.size))
        depth += 1
        marker = depth + 1
        rows, columns = numpy.divmod(level, width)
        children = (permutation[:, rows] * width + orientation[:, columns]).ravel()
        distances[children[distances[children] == 0]] = marker
        # Re-scanning for the marker deduplicates at C speed. numpy.unique would
        # sort millions of entries per level and costs an order of magnitude more.
        level = numpy.flatnonzero(distances == marker)

    return bytearray(distances.tobytes())


def _expand_slice(
    level: Sequence[int], distances: bytearray, marker: int, tables
) -> list[int]:
    """Expand part of one frontier level. Safe to run concurrently.

    Two workers can reach the same state at once and both claim it. That costs a
    duplicate in the next frontier -- the state gets expanded twice, finding
    nothing new the second time -- but never a wrong distance, because both
    write the same marker. Guarding it would cost more than the duplicates do.
    """
    width = coords.N_ORIENTATIONS
    found: list[int] = []
    append = found.append
    for node in level:
        quotient, remainder = divmod(node, width)
        for _, permutation, orientation in tables:
            child = permutation[quotient] + orientation[remainder]
            if not distances[child]:
                distances[child] = marker
                append(child)
    return found


def _build_threaded(progress: Progress | None, workers: int) -> bytearray:
    """The same breadth-first search, one thread per slice of each level.

    Whether this helps depends entirely on the interpreter. Under the standard
    GIL build it cannot: the inner loop is pure bytecode, so the threads take
    turns. On a free-threaded build (``python3.14t``) they genuinely run at once.
    Either way the answer is identical -- only the clock differs.
    """
    tables = coords.wide_tables()
    distances = bytearray(coords.N_STATES)
    distances[coords.SOLVED_INDEX] = 1
    level: list[int] = [coords.SOLVED_INDEX]
    depth = 0

    with ThreadPoolExecutor(max_workers=workers, thread_name_prefix="pyrubik-bfs") as pool:
        while level:
            if progress is not None:
                progress(depth, len(level))
            depth += 1
            marker = depth + 1
            if len(level) < workers * 64:  # too small to be worth splitting
                level = _expand_slice(level, distances, marker, tables)
                continue
            size = -(-len(level) // workers)  # ceiling division
            slices = [level[start:start + size] for start in range(0, len(level), size)]
            level = [
                child
                for part in pool.map(_expand_slice, slices,
                                     repeat(distances), repeat(marker), repeat(tables))
                for child in part
            ]

    return distances


@dataclass(frozen=True, slots=True)
class DistanceTable:
    """Distance-to-solved for all 3,674,160 states, stored as ``distance + 1``.

    Zero would mean "not yet reached", which is what the build uses it for.
    """

    distances: bytearray
    #: Which implementation produced this table, for reporting.
    backend: str = "python"

    @classmethod
    def build(cls, progress: Progress | None = None, pure: bool = False) -> Self:
        """Search the whole state space. Uses numpy when available.

        ``pure`` forces the Python implementation; both produce identical tables.
        """
        if pure or numpy is None:
            return cls(_build_python(progress), "python")
        return cls(_build_numpy(progress), "numpy")

    @classmethod
    def build_parallel(cls, workers: int | None = None, progress: Progress | None = None) -> Self:
        """Build with threads. See :func:`_build_threaded` for what that is worth."""
        workers = workers or (os.process_cpu_count() or 1)
        return cls(_build_threaded(progress, workers), f"threads({workers})")

    def __len__(self) -> int:
        return len(self.distances)

    def distance(self, index: int) -> int:
        """Quarter turns from the encoded state ``index`` to solved."""
        return self.distances[index] - 1

    def histogram(self) -> list[int]:
        """States per distance, index 0 being the solved state."""
        return [self.distances.count(d + 1) for d in range(max(self.distances))]

    def solve(self, state: State) -> Moves:
        """Walk downhill: one lookup per move, per step."""
        distances = self.distances
        width = coords.N_ORIENTATIONS
        tables = coords.wide_tables()

        index = coords.encode(state)
        remaining = distances[index] - 1
        path: list[str] = []
        while remaining:
            quotient, remainder = divmod(index, width)
            for move, permutation, orientation in tables:
                child = permutation[quotient] + orientation[remainder]
                if distances[child] == remaining:  # == (remaining - 1) + 1
                    path.append(move)
                    index = child
                    remaining -= 1
                    break
            else:  # pragma: no cover - impossible for a correctly built table
                raise UnsolvableError("distance table is inconsistent")
        return "".join(path)


class BackgroundTable:
    """Builds a :class:`DistanceTable` on a worker thread.

    Lets an interactive session start instantly and answer with search until the
    table lands, at which point solves become lookups. The build releases the
    GIL only rarely, so this trades a little throughput for a usable prompt --
    the right trade when a human is waiting at one.
    """

    __slots__ = ("_executor", "_future")

    def __init__(self, pure: bool = False) -> None:
        self._executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="pyrubik-table")
        self._future: Future[DistanceTable] = self._executor.submit(
            DistanceTable.build, None, pure
        )
        self._executor.shutdown(wait=False)

    @property
    def ready(self) -> DistanceTable | None:
        """The table if the build has finished, else ``None``. Never blocks."""
        return self._future.result() if self._future.done() else None

    def wait(self, timeout: float | None = None) -> DistanceTable:
        """Block until the table exists."""
        return self._future.result(timeout)
