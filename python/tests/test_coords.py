"""The cubie encoding must agree with the sticker model it replaces."""

import random

import pytest

from pyrubik import coords, cube
from pyrubik.cube import SOLVE_MOVES, SOLVED, UnsolvableError, apply_move


def in_frame(rng: random.Random, count: int) -> bytes:
    """Scramble with solve moves only, so the fixed corner stays home."""
    state = SOLVED
    for _ in range(count):
        state = apply_move(state, rng.choice(SOLVE_MOVES))
    return state


def test_state_space_size():
    # 7! arrangements of the free corners x 3^6 twist combinations.
    assert coords.N_STATES == 3_674_160
    assert coords.N_PERMUTATIONS * coords.N_ORIENTATIONS == coords.N_STATES


def test_solved_cube_is_index_zero():
    assert coords.encode(SOLVED) == coords.SOLVED_INDEX == 0


def test_slots_cover_every_facelet_exactly_once():
    assert sorted(f for slot in coords.SLOTS for f in slot) == list(range(24))


def test_the_fixed_slot_is_the_fixed_corner():
    assert set(coords.SLOTS[coords.FREE]) == set(cube.FIXED_CORNER)


@pytest.mark.parametrize("seed", range(20))
def test_encode_decode_roundtrip(seed):
    rng = random.Random(seed)
    state = in_frame(rng, rng.randrange(15))
    assert coords.decode(coords.encode(state)) == state


@pytest.mark.parametrize("move", list(SOLVE_MOVES))
def test_moves_agree_with_the_sticker_model(move):
    rng = random.Random(ord(move))
    for _ in range(50):
        state = in_frame(rng, rng.randrange(15))
        assert coords.encode(apply_move(state, move)) == coords.apply(coords.encode(state), move)


@pytest.mark.parametrize("seed", range(10))
def test_every_orientation_encodes_to_a_valid_index(seed):
    """Holding the same scramble differently still encodes, via its own goal.

    The index itself is *not* rotation-invariant: rotating relabels which piece
    counts as which, so the permutation is conjugated. What is invariant is the
    distance, which tests/test_search.py checks.
    """
    state = in_frame(random.Random(seed), 9)
    indices = {coords.encode(rotated) for rotated in cube.orientations(state)}
    assert len(indices) >= 1
    assert all(0 <= index < coords.N_STATES for index in indices)


def test_goal_matches_the_input_orientation():
    scrambled = cube.scramble(SOLVED, 9, random.Random(3))
    goal = coords.goal_for(scrambled)
    assert cube.is_solved(goal)
    assert all(goal[i] == scrambled[i] for i in cube.FIXED_CORNER)


def test_indices_stay_in_range():
    rng = random.Random(0)
    for _ in range(200):
        assert 0 <= coords.encode(in_frame(rng, rng.randrange(15))) < coords.N_STATES


def test_move_tables_are_permutations_of_their_coordinate():
    for _, permutation, orientation in coords.move_tables():
        assert sorted(permutation) == list(range(coords.N_PERMUTATIONS))
        assert sorted(orientation) == list(range(coords.N_ORIENTATIONS))


def test_wide_tables_are_the_move_tables_scaled():
    for (move, permutation, orientation), (wide_move, wide, wide_orientation) in zip(
        coords.move_tables(), coords.wide_tables(), strict=True
    ):
        assert move == wide_move
        assert orientation == wide_orientation
        assert wide == tuple(value * coords.N_ORIENTATIONS for value in permutation)


def test_impossible_corner_is_rejected():
    broken = bytearray(SOLVED)
    broken[cube.FIXED_CORNER[0]] = broken[cube.FIXED_CORNER[1]]
    with pytest.raises(UnsolvableError, match="do not occur"):
        coords.encode(bytes(broken))


def test_impossible_piece_is_rejected():
    # Swapping two stickers inside one corner makes a cubie that does not exist.
    broken = bytearray(SOLVED)
    broken[0], broken[16] = broken[16], broken[0]
    with pytest.raises(UnsolvableError, match="do not hold a corner"):
        coords.encode(bytes(broken))
