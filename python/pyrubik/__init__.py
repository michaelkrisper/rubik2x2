"""An optimal solver for the 2x2x2 Rubik's Cube (Pocket Cube, Mini Cube).

    >>> import pyrubik
    >>> state = pyrubik.parse("rgrggpyybbppwwbrwwgpyyrb")
    >>> solution = pyrubik.solve(state)
    >>> pyrubik.is_solved(pyrubik.apply_moves(state, solution))
    True

:func:`solve` is bidirectional search: no setup, a few milliseconds, and always
a shortest solution. To solve many cubes, build the distance table once and let
it answer instead::

    table = DistanceTable.build()
    table.solve(state)

See :mod:`pyrubik.solvers` for all three algorithms and ``pyrubik bench`` for
how they compare.
"""

from .cube import (
    FIXED_CORNER,
    MOVES,
    SOLVE_MOVES,
    SOLVED,
    Moves,
    State,
    UnsolvableError,
    apply_move,
    apply_moves,
    invert,
    is_solved,
    orientations,
    parse,
    scramble,
    to_string,
)
from .display import Color, render
from .search import MAX_DEPTH, solve
from .solvers import Algorithm, Solver, make
from .table import BackgroundTable, DistanceTable

__version__ = "2.0.0"

__all__ = [
    "FIXED_CORNER",
    "MAX_DEPTH",
    "MOVES",
    "SOLVED",
    "SOLVE_MOVES",
    "Algorithm",
    "BackgroundTable",
    "Color",
    "DistanceTable",
    "Moves",
    "Solver",
    "State",
    "UnsolvableError",
    "apply_move",
    "apply_moves",
    "invert",
    "is_solved",
    "make",
    "orientations",
    "parse",
    "render",
    "scramble",
    "solve",
    "to_string",
]
