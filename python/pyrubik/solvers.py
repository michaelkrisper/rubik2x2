"""The three algorithms behind one interface, so callers can compare them.

Each is a plain callable from a cube state to a move sequence. All three return
*shortest* solutions -- they differ only in what they spend to get there:

======  ====================================================================
bfs     Bidirectional breadth-first search. No setup, moderate per solve.
ida     Korf's IDA* with pattern databases. Cheap setup, slowest per solve.
table   Full distance table. Expensive setup, then effectively free.
======  ====================================================================
"""

from enum import StrEnum
from typing import Protocol

from . import ida, search
from .cube import Moves, State
from .table import DistanceTable


class Solver(Protocol):
    """Anything that turns a cube state into a shortest move sequence."""

    def __call__(self, state: State) -> Moves: ...


class Algorithm(StrEnum):
    """Selectable solving strategies."""

    BFS = "bfs"
    IDA = "ida"
    TABLE = "table"

    @property
    def description(self) -> str:
        match self:
            case Algorithm.BFS:
                return "bidirectional breadth-first search, no precomputation"
            case Algorithm.IDA:
                return "iterative-deepening A* with pattern databases"
            case Algorithm.TABLE:
                return "lookup in the full distance table"


def make(algorithm: Algorithm, table: DistanceTable | None = None) -> Solver:
    """Build a solver. ``table`` is required for :attr:`Algorithm.TABLE`."""
    match algorithm:
        case Algorithm.BFS:
            return search.solve
        case Algorithm.IDA:
            return ida.solve
        case Algorithm.TABLE:
            if table is None:
                raise ValueError("the table algorithm needs a DistanceTable")
            return table.solve
