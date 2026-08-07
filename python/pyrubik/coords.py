"""Cubie coordinates: the compact state encoding every solver runs on.

:mod:`pyrubik.cube` models a state as 24 stickers, which is what a human reads
but not what a search wants -- every move there allocates a fresh 24-byte
object. A pocket cube with one corner held still is described by far less:
which of the 7 free corner *pieces* sits in each of the 7 free *slots*
(7! = 5040 arrangements) and how each is twisted (3^7, minus the constraint
that the twists sum to a multiple of 3, so 3^6 = 729). That is
5040 * 729 = 3,674,160 states -- exactly what an exhaustive search over
stickers finds.

The payoff is that a move acts on the two halves independently::

    new_permutation[slot] = old_permutation[source[slot]]
    new_orientation[slot] = old_orientation[source[slot]] + twist[slot]  (mod 3)

Neither half depends on the other, so a move is two lookups in tables of 5040
and 729 entries, and a whole state is a single int below 3,674,160. That makes
moves about twice as fast as rebuilding a sticker array; more importantly, a
dense bounded index is what makes :mod:`pyrubik.table` possible at all, and
what hands :mod:`pyrubik.ida` its pattern databases for free.

Everything here is verified against the sticker model in tests/test_coords.py.
"""

from functools import cache
from itertools import permutations
from operator import itemgetter

from .cube import FIXED_CORNER, SOLVE_MOVES, SOLVED, UnsolvableError, apply_move, orientations

#: Each corner slot as its three facelets, listed in a consistent rotational
#: sense so that "twist" means the same thing at every slot. The order is
#: (x-face, y-face, z-face) where sign(x)*sign(y)*sign(z) == +1 and
#: (x-face, z-face, y-face) otherwise, which makes all eight triples agree in
#: handedness. Slot 7 is the corner the solve moves never touch.
SLOTS = (
    (16, 0, 14),   # left-up-back
    (21, 15, 1),   # right-up-back
    (17, 4, 2),    # left-up-front
    (20, 3, 5),    # right-up-front
    (19, 8, 6),    # left-down-front
    (22, 7, 9),    # right-down-front
    (18, 12, 10),  # left-down-back
    (23, 11, 13),  # right-down-back -- fixed
)

#: Number of slots that actually move.
FREE = 7

N_ORIENTATIONS = 3 ** (FREE - 1)
N_PERMUTATIONS = 5040
N_STATES = N_PERMUTATIONS * N_ORIENTATIONS

#: A solved cube is the identity permutation with no twists, which ranks as 0.
SOLVED_INDEX = 0

_PERMUTATIONS = list(permutations(range(FREE)))
_PERMUTATION_RANK = {value: rank for rank, value in enumerate(_PERMUTATIONS)}


def pieces_of(goal: bytes) -> tuple[tuple[int, ...], ...]:
    """The canonical color triple of each corner piece, read off a solved cube."""
    return tuple(tuple(goal[facelet] for facelet in slot) for slot in SLOTS)


PIECES = pieces_of(SOLVED)

#: The fixed corner's three colors identify which of the 24 rotations of the
#: solved cube a scramble is aiming at, so a cube held any which way still
#: encodes correctly. Built once here rather than re-derived on every solve.
_GOALS = {
    tuple(goal[facelet] for facelet in FIXED_CORNER): (goal, pieces_of(goal))
    for goal in orientations(SOLVED)
}


def _goal_entry(state: bytes):
    corner = tuple(state[facelet] for facelet in FIXED_CORNER)
    try:
        return _GOALS[corner]
    except KeyError:
        raise UnsolvableError(f"corner colors {corner} do not occur on a solved cube") from None


def goal_for(state: bytes) -> bytes:
    """The solved cube whose fixed corner matches ``state``'s fixed corner."""
    return _goal_entry(state)[0]


def to_cubies(state: bytes, pieces=PIECES) -> tuple[list[int], list[int]]:
    """Split a sticker state into (piece per slot, twist per slot).

    Rejects arrangements no sequence of turns can produce. Two of them are not
    obvious from the stickers alone: the same piece appearing in two slots, and
    a set of twists that does not sum to a multiple of three. The second matters
    because :func:`_rank_orientation` stores only six twists and derives the
    seventh -- without the check, a single twisted corner would quietly encode
    as some *other*, legal cube, and the solver would return moves that solve
    that one instead.
    """
    permutation, orientation = [], []
    for slot in SLOTS[:FREE]:
        colors = tuple(state[facelet] for facelet in slot)
        for piece, canonical in enumerate(pieces):
            for twist in range(3):
                if colors == canonical[twist:] + canonical[:twist]:
                    permutation.append(piece)
                    orientation.append(twist)
                    break
            else:
                continue
            break
        else:
            raise UnsolvableError(f"facelets {slot} do not hold a corner of this cube")

    if len(set(permutation)) != FREE:
        raise UnsolvableError("a corner piece appears in more than one slot")
    if sum(orientation) % 3:
        raise UnsolvableError("corner twists do not sum to a multiple of three")
    return permutation, orientation


def from_cubies(permutation, orientation, pieces=PIECES) -> bytes:
    """Rebuild a sticker state. Inverse of :func:`to_cubies`."""
    state = bytearray(24)
    for slot, (piece, twist) in enumerate(zip(permutation, orientation, strict=True)):
        colors = pieces[piece]
        for facelet, color in zip(SLOTS[slot], colors[twist:] + colors[:twist], strict=True):
            state[facelet] = color
    for facelet, color in zip(SLOTS[FREE], pieces[FREE], strict=True):
        state[facelet] = color
    return bytes(state)


def _rank_orientation(orientation) -> int:
    value = 0
    for twist in reversed(orientation[: FREE - 1]):
        value = value * 3 + twist
    return value


def _unrank_orientation(value: int) -> list[int]:
    orientation = []
    for _ in range(FREE - 1):
        orientation.append(value % 3)
        value //= 3
    orientation.append(-sum(orientation) % 3)  # twists sum to 0 mod 3
    return orientation


def encode(state: bytes, pieces: tuple | None = None) -> int:
    """Sticker state -> dense index below :data:`N_STATES`.

    Without ``pieces`` the cube's own goal orientation is looked up, so a cube
    held any of the 24 ways still encodes -- each aiming at its own goal, which
    is always index 0. The *index* is not rotation-invariant: rotating conjugates
    which piece counts as which. The distance is, which is what matters.
    """
    if pieces is None:
        pieces = _goal_entry(state)[1]
    permutation, orientation = to_cubies(state, pieces)
    return _PERMUTATION_RANK[tuple(permutation)] * N_ORIENTATIONS + _rank_orientation(orientation)


def decode(index: int, pieces=PIECES) -> bytes:
    """Dense index -> sticker state. Inverse of :func:`encode`."""
    permutation, orientation = divmod(index, N_ORIENTATIONS)
    return from_cubies(_PERMUTATIONS[permutation], _unrank_orientation(orientation), pieces)


@cache
def move_tables() -> tuple[tuple[str, tuple[int, ...], tuple[int, ...]], ...]:
    """One (move, permutation table, orientation table) triple per solve move.

    Built on first use -- roughly 35k entries, a few tens of milliseconds -- so
    importing the package stays cheap. The order follows
    :data:`~pyrubik.cube.SOLVE_MOVES`, which pairs every turn with its inverse
    at adjacent indices; :mod:`pyrubik.ida` relies on that to prune.
    """
    tables = []
    identity = from_cubies(list(range(FREE)), [0] * FREE)
    for move in SOLVE_MOVES:
        # On the identity state the permutation *is* the slot-to-slot mapping
        # and the orientation *is* the twist each slot picks up.
        source, twist = to_cubies(apply_move(identity, move))

        # itemgetter rearranges a whole permutation in one C-level call, which
        # matters because this runs 5040 times per move on every cold start.
        rearrange = itemgetter(*source)
        rank_of = _PERMUTATION_RANK.__getitem__
        permutation_table = tuple(map(rank_of, map(rearrange, _PERMUTATIONS)))

        orientation_table = tuple(
            _rank_orientation([(old[source[slot]] + twist[slot]) % 3 for slot in range(FREE)])
            for old in map(_unrank_orientation, range(N_ORIENTATIONS))
        )
        tables.append((move, permutation_table, orientation_table))
    return tuple(tables)


@cache
def wide_tables() -> tuple[tuple[str, tuple[int, ...], tuple[int, ...]], ...]:
    """Move tables with the permutation side pre-multiplied by the row width.

    A search then computes a child as ``permutation[q] + orientation[r]`` -- one
    add rather than a multiply and an add, which is worth having in a loop that
    runs tens of millions of times.
    """
    return tuple(
        (move, tuple(value * N_ORIENTATIONS for value in permutation), orientation)
        for move, permutation, orientation in move_tables()
    )


@cache
def _by_name() -> dict[str, tuple[tuple[int, ...], tuple[int, ...]]]:
    return {move: (permutation, orientation) for move, permutation, orientation in move_tables()}


def apply(index: int, move: str) -> int:
    """Apply one solve move to an encoded state."""
    permutation_table, orientation_table = _by_name()[move]
    quotient, remainder = divmod(index, N_ORIENTATIONS)
    return permutation_table[quotient] * N_ORIENTATIONS + orientation_table[remainder]
