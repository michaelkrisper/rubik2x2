"""Korf's IDA* with pattern databases -- the textbook optimal cube solver.

Iterative-deepening A* runs depth-first passes with a rising limit on ``g + h``,
where ``h`` is a lower bound on the moves still needed. Korf's contribution was
to get ``h`` from *pattern databases*: exact distances for a relaxed version of
the puzzle, precomputed once.

Here the relaxations fall out of the encoding for free. A move acts on the
permutation coordinate and the orientation coordinate independently
(:mod:`pyrubik.coords`), so each coordinate on its own is a genuine quotient of
the state graph -- solving the cube necessarily solves both projections. Their
exact distances (5040 and 729 entries, computed in milliseconds) are therefore
admissible, and the larger of the two is the better bound.

This is here for comparison, not because it wins. On a 2x2x2 it loses to plain
bidirectional search, and measurably so: both databases top out at distance 8
while real positions reach 14, leaving the bound too loose to prune well. The
reason is structural. Pattern databases pay off when a puzzle has more pieces
than you can afford to track and you keep only some of them; a pocket cube is
nothing *but* the pieces the databases already cover. Run ``pyrubik bench`` for
the numbers.
"""

from dataclasses import dataclass
from functools import cache
from typing import Self

from . import coords
from .cube import Moves, State, UnsolvableError
from .search import MAX_DEPTH


def _project(tables: list[tuple[int, ...]], size: int) -> bytearray:
    """Exact distance-to-solved within one coordinate. Returns distance + 1."""
    distances = bytearray(size)
    distances[0] = 1
    level = [0]
    depth = 0
    while level:
        depth += 1
        following = []
        for node in level:
            for table in tables:
                if not distances[child := table[node]]:
                    distances[child] = depth + 1
                    following.append(child)
        level = following
    return distances


@dataclass(frozen=True, slots=True)
class PatternDatabases:
    """Exact distances for the two coordinate projections of the cube."""

    permutation: bytearray
    orientation: bytearray

    @classmethod
    @cache
    def build(cls) -> Self:
        """Both databases; cached, since they are the same on every call."""
        tables = coords.move_tables()
        return cls(
            _project([table for _, table, _ in tables], coords.N_PERMUTATIONS),
            _project([table for _, _, table in tables], coords.N_ORIENTATIONS),
        )

    def bound(self, index: int) -> int:
        """Admissible lower bound on the moves left from an encoded state."""
        quotient, remainder = divmod(index, coords.N_ORIENTATIONS)
        return max(self.permutation[quotient], self.orientation[remainder]) - 1

    @property
    def reach(self) -> tuple[int, int]:
        """The largest bound each database can ever return."""
        return max(self.permutation) - 1, max(self.orientation) - 1


def solve(state: State, max_depth: int = MAX_DEPTH) -> Moves:
    """Shortest solution by iterative-deepening A*. Optimal, but see the module docs."""
    index = coords.encode(state)
    if index == coords.SOLVED_INDEX:
        return ""

    databases = PatternDatabases.build()
    permutation_db, orientation_db = databases.permutation, databases.orientation
    width = coords.N_ORIENTATIONS
    tables = coords.wide_tables()

    def descend(index: int, depth: int, limit: int, path: list[str], last: int) -> bool:
        quotient, remainder = divmod(index, width)
        bound = max(permutation_db[quotient], orientation_db[remainder]) - 1
        if depth + bound > limit:
            return False
        if index == coords.SOLVED_INDEX:
            return True
        for position, (move, permutation, orientation) in enumerate(tables):
            # SOLVE_MOVES pairs each turn with its inverse at adjacent indices,
            # so `position ^ 1` is the move that would undo the previous one.
            if position ^ 1 == last:
                continue
            path.append(move)
            child = permutation[quotient] + orientation[remainder]
            if descend(child, depth + 1, limit, path, position):
                return True
            path.pop()
        return False

    for limit in range(databases.bound(index), max_depth + 1):
        path: list[str] = []
        if descend(index, 0, limit, path, -1):
            return "".join(path)

    raise UnsolvableError(f"no solution within {max_depth} moves")
