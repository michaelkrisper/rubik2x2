"""The 2x2x2 pocket cube, modelled as 24 stickers.

A state is an immutable ``bytes`` of 24 sticker color ids, one per facelet,
ordered side by side: UP, FRONT, DOWN, BACK, LEFT, RIGHT. Within a side the four
stickers are row-major::

    -------
    |0  1 |
    |2  3 |
    -------

so the flat index is ``side * 4 + row * 2 + column``.

Every move is a permutation table applied as ``new[i] = old[PERM[i]]``. The
tables are checked rather than trusted: tests/test_cube.py pins the algebra they
must obey (each move has order four, inverses cancel, opposite faces commute),
and building the distance table reproduces OEIS A079762 exactly, which no wrong
table could do.

This module is the readable model and nothing else -- no search, no display.
:mod:`pyrubik.coords` re-encodes states for the solvers; :mod:`pyrubik.render`
draws them.
"""

import random
from collections.abc import Callable
from operator import itemgetter

#: A cube state: 24 sticker color ids.
type State = bytes
#: A sequence of moves, one character each, e.g. ``"lFtL"``.
type Moves = str
#: A permutation of the 24 facelets.
type Permutation = tuple[int, ...]

UP, FRONT, DOWN, BACK, LEFT, RIGHT = range(6)


class UnsolvableError(ValueError):
    """A sticker arrangement no sequence of turns can reach from a solved cube."""


#: Color letter per color id: red, white, blue, yellow, purple, green.
COLOR_CHARS = "rwbypg"
R, W, B, Y, P, G = range(6)

#: Solved cube in the standard orientation: yellow up, red front.
SOLVED: State = bytes(color for color in (Y, R, W, P, B, G) for _ in range(4))

#: Move notation. Lowercase and uppercase are inverse quarter turns of one face:
#:   l/L  left column down/up        r/R  right column down/up
#:   t/T  top layer left/right       b/B  bottom layer left/right
#:   f/F  front face cw/ccw          a/A  back face cw/ccw
MOVES: dict[str, Permutation] = {
    "l": (12, 1, 14, 3, 0, 5, 2, 7, 4, 9, 6, 11, 8, 13, 10, 15, 18, 16, 19, 17, 20, 21, 22, 23),
    "L": (4, 1, 6, 3, 8, 5, 10, 7, 12, 9, 14, 11, 0, 13, 2, 15, 17, 19, 16, 18, 20, 21, 22, 23),
    "r": (0, 13, 2, 15, 4, 1, 6, 3, 8, 5, 10, 7, 12, 9, 14, 11, 16, 17, 18, 19, 21, 23, 20, 22),
    "R": (0, 5, 2, 7, 4, 9, 6, 11, 8, 13, 10, 15, 12, 1, 14, 3, 16, 17, 18, 19, 22, 20, 23, 21),
    "t": (2, 0, 3, 1, 20, 21, 6, 7, 8, 9, 10, 11, 12, 13, 17, 16, 4, 5, 18, 19, 15, 14, 22, 23),
    "T": (1, 3, 0, 2, 16, 17, 6, 7, 8, 9, 10, 11, 12, 13, 21, 20, 15, 14, 18, 19, 4, 5, 22, 23),
    "b": (0, 1, 2, 3, 4, 5, 22, 23, 9, 11, 8, 10, 19, 18, 14, 15, 16, 17, 6, 7, 20, 21, 13, 12),
    "B": (0, 1, 2, 3, 4, 5, 18, 19, 10, 8, 11, 9, 23, 22, 14, 15, 16, 17, 13, 12, 20, 21, 6, 7),
    "f": (0, 1, 19, 17, 6, 4, 7, 5, 22, 20, 10, 11, 12, 13, 14, 15, 16, 8, 18, 9, 2, 21, 3, 23),
    "F": (0, 1, 20, 22, 5, 7, 4, 6, 17, 19, 10, 11, 12, 13, 14, 15, 16, 3, 18, 2, 9, 21, 8, 23),
    "a": (18, 16, 2, 3, 4, 5, 6, 7, 8, 9, 23, 21, 13, 15, 12, 14, 10, 17, 11, 19, 20, 0, 22, 1),
    "A": (21, 23, 2, 3, 4, 5, 6, 7, 8, 9, 16, 18, 14, 12, 15, 13, 1, 17, 0, 19, 20, 11, 22, 10),
}

#: The quarter-turn generators the solvers use. A fourth face would be
#: redundant: turning a face equals turning the opposite face plus a whole-cube
#: rotation. Inverses sit at adjacent indices, which :mod:`pyrubik.ida` uses to
#: prune, so keep them paired.
SOLVE_MOVES = "lLtTfF"

#: Facelets of the corner :data:`SOLVE_MOVES` leave in place (down-back-right).
#: Holding one corner still is what collapses the 24-fold rotation symmetry.
FIXED_CORNER = (11, 13, 23)

#: Whole-cube rotations, used to enumerate the 24 ways of holding one state.
_ROTATION_GENERATORS = (
    (12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 18, 16, 19, 17, 21, 23, 20, 22),
    (2, 0, 3, 1, 20, 21, 22, 23, 9, 11, 8, 10, 19, 18, 17, 16, 4, 5, 6, 7, 15, 14, 13, 12),
    (18, 16, 19, 17, 6, 4, 7, 5, 22, 20, 23, 21, 13, 15, 12, 14, 10, 8, 11, 9, 2, 0, 3, 1),
)


def _rotation_group() -> frozenset[Permutation]:
    identity = tuple(range(24))
    seen = {identity}
    frontier = [identity]
    while frontier:
        current = frontier.pop()
        for generator in _ROTATION_GENERATORS:
            composed = tuple(current[index] for index in generator)
            if composed not in seen:
                seen.add(composed)
                frontier.append(composed)
    return frozenset(seen)


#: The 24 ways of holding a cube. Checked in tests/test_cube.py.
ROTATIONS = _rotation_group()

#: Pre-bound per-move rearrangers. ``itemgetter`` rearranges all 24 stickers in
#: one C-level call, roughly threefold faster than a generator expression.
_PICKERS: dict[str, Callable[[State], tuple[int, ...]]] = {
    move: itemgetter(*permutation) for move, permutation in MOVES.items()
}


def apply_move(state: State, move: str) -> State:
    """Turn one face."""
    return bytes(_PICKERS[move](state))


def apply_moves(state: State, moves: Moves) -> State:
    """Turn faces in sequence."""
    for move in moves:
        state = apply_move(state, move)
    return state


def invert(moves: Moves) -> Moves:
    """Undo a sequence: reverse the order, flip every turn."""
    return moves[::-1].swapcase()


def orientations(state: State) -> frozenset[State]:
    """All 24 whole-cube rotations of ``state``."""
    return frozenset(bytes(state[index] for index in rotation) for rotation in ROTATIONS)


def is_solved(state: State) -> bool:
    """True when every face shows a single color."""
    return all(
        state[side] == state[side + 1] == state[side + 2] == state[side + 3]
        for side in range(0, 24, 4)
    )


def parse(text: str) -> State:
    """Read 24 color letters in side order UP FRONT DOWN BACK LEFT RIGHT."""
    text = text.strip().lower()
    if len(text) != 24:
        raise ValueError(f"expected 24 color letters, got {len(text)}")
    if unknown := set(text) - set(COLOR_CHARS):
        raise ValueError(f"invalid color letters: {''.join(sorted(unknown))!r} "
                         f"(allowed: {COLOR_CHARS})")
    state = bytes(COLOR_CHARS.index(char) for char in text)
    for color, char in enumerate(COLOR_CHARS):
        if (count := state.count(color)) != 4:
            raise ValueError(f"color {char!r} appears {count} times, expected 4")
    return state


def to_string(state: State) -> str:
    """The 24 color letters of a state. Inverse of :func:`parse`."""
    return "".join(COLOR_CHARS[color] for color in state)


def scramble(state: State, count: int, rng: random.Random | None = None) -> State:
    """Apply ``count`` random moves, each reaching a state not seen before."""
    rng = rng or random.Random()
    seen = {state}
    moves = list(MOVES)
    while count > 0:
        candidate = apply_move(state, rng.choice(moves))
        if candidate not in seen:
            seen.add(candidate)
            state = candidate
            count -= 1
    return state
