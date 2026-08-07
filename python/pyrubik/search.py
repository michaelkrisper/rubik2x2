"""Bidirectional breadth-first search: the solver that needs no setup.

Two frontiers grow a level at a time -- one from the scramble, one from the
goal -- and the search stops where they touch. God's number for a pocket cube is
14 quarter turns and the level sizes peak at 1,350,852 states at depth 11, so a
one-sided search would have to walk most of the space. Meeting in the middle
needs depth 7 from each side, where only 33,058 states live.

Expanding by whole levels is what makes the result optimal. When the frontiers
meet, every shorter pairing has already been tried, so the first meeting point
is a shortest solution -- no heuristic and no tie-breaking required.
"""

from .coords import N_ORIENTATIONS, SOLVED_INDEX, encode, wide_tables
from .cube import Moves, State, UnsolvableError, invert

#: God's number for the pocket cube in the quarter-turn metric.
MAX_DEPTH = 14

type _Frontier = list[int]
type _Seen = dict[int, Moves]


def solve(state: State, max_depth: int = MAX_DEPTH) -> Moves:
    """Return a shortest move sequence solving ``state``; empty if already solved.

    Raises :class:`~pyrubik.cube.UnsolvableError` when no sequence of turns reaches
    the state, or when none exists within ``max_depth``.
    """
    start = encode(state)
    if start == SOLVED_INDEX:
        return ""

    tables = wide_tables()
    width = N_ORIENTATIONS

    forward: _Seen = {start: ""}
    backward: _Seen = {SOLVED_INDEX: ""}
    forward_level: _Frontier = [start]
    backward_level: _Frontier = [SOLVED_INDEX]

    for _ in range(max_depth):
        # Always grow the cheaper side, which keeps both halves shallow.
        from_goal = len(backward_level) < len(forward_level)
        level, seen, other = (
            (backward_level, backward, forward) if from_goal
            else (forward_level, forward, backward)
        )

        following: _Frontier = []
        append = following.append
        meeting: tuple[Moves, Moves] | None = None

        for node in level:
            path = seen[node]
            quotient, remainder = divmod(node, width)
            for move, permutation, orientation in tables:
                child = permutation[quotient] + orientation[remainder]
                if child in seen:
                    continue
                extended = path + move
                if child in other:
                    meeting = (extended, other[child])
                    break
                seen[child] = extended
                append(child)
            if meeting is not None:
                break

        if meeting is not None:
            from_start, to_goal = meeting[::-1] if from_goal else meeting
            return from_start + invert(to_goal)

        if from_goal:
            backward_level = following
        else:
            forward_level = following
        if not forward_level or not backward_level:
            break

    raise UnsolvableError(f"no solution within {max_depth} moves")
