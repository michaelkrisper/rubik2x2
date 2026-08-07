"""The sticker model: moves, validity, and the algebra they have to obey."""

import random

import pytest

from pyrubik import cube
from pyrubik.cube import MOVES, SOLVED


def test_solved_cube_is_solved():
    assert cube.is_solved(SOLVED)
    assert cube.to_string(SOLVED) == "yyyy" "rrrr" "wwww" "pppp" "bbbb" "gggg"


@pytest.mark.parametrize("move", sorted(MOVES))
def test_move_is_a_permutation(move):
    assert sorted(MOVES[move]) == list(range(24))


@pytest.mark.parametrize("move", sorted(MOVES))
def test_move_and_its_inverse_cancel(move):
    assert cube.apply_moves(SOLVED, move + move.swapcase()) == SOLVED


@pytest.mark.parametrize("move", sorted(MOVES))
def test_move_has_order_four(move):
    assert cube.apply_moves(SOLVED, move * 4) == SOLVED
    assert cube.apply_moves(SOLVED, move * 2) != SOLVED


@pytest.mark.parametrize("move", sorted(MOVES))
def test_three_turns_equal_the_inverse(move):
    assert cube.apply_moves(SOLVED, move * 3) == cube.apply_move(SOLVED, move.swapcase())


@pytest.mark.parametrize("move", sorted(MOVES))
def test_a_single_move_leaves_the_cube_unsolved(move):
    assert not cube.is_solved(cube.apply_move(SOLVED, move))


def test_opposite_faces_commute():
    # Turning both layers of an axis is a whole-cube rotation, so the order of
    # two opposite-face turns cannot matter.
    for first, second in (("l", "r"), ("t", "b"), ("f", "a")):
        assert cube.apply_moves(SOLVED, first + second) == cube.apply_moves(SOLVED, second + first)


def test_whole_cube_rotation_keeps_the_cube_solved():
    for first, second in (("l", "r"), ("t", "b"), ("f", "a")):
        assert cube.is_solved(cube.apply_moves(SOLVED, first + second))


def test_solve_moves_pair_each_turn_with_its_inverse():
    # pyrubik.ida prunes with `position ^ 1`, which needs this ordering.
    moves = cube.SOLVE_MOVES
    for position in range(0, len(moves), 2):
        assert moves[position].swapcase() == moves[position + 1]


def test_solve_moves_fix_exactly_the_fixed_corner():
    fixed = {i for i in range(24) if all(MOVES[m][i] == i for m in cube.SOLVE_MOVES)}
    assert fixed == set(cube.FIXED_CORNER)


def test_invert_undoes_a_sequence():
    path = "lTfFtL"
    scrambled = cube.apply_moves(SOLVED, path)
    assert cube.apply_moves(scrambled, cube.invert(path)) == SOLVED


def test_there_are_exactly_24_orientations():
    assert len(cube.ROTATIONS) == 24
    assert len(cube.orientations(SOLVED)) == 24
    assert len(cube.orientations(cube.scramble(SOLVED, 6, random.Random(1)))) == 24


def test_orientations_of_solved_are_all_solved():
    assert all(cube.is_solved(state) for state in cube.orientations(SOLVED))


def test_roundtrip_parse_and_to_string():
    scrambled = cube.scramble(SOLVED, 10, random.Random(7))
    assert cube.parse(cube.to_string(scrambled)) == scrambled


@pytest.mark.parametrize(
    "text, message",
    [
        ("yyyy", "24 color letters"),
        ("x" * 24, "invalid color letters"),
        ("y" * 24, "appears 0 times"),
    ],
)
def test_parse_rejects_bad_input(text, message):
    with pytest.raises(ValueError, match=message):
        cube.parse(text)


def test_scramble_is_reproducible_and_changes_the_cube():
    first = cube.scramble(SOLVED, 8, random.Random(42))
    assert first == cube.scramble(SOLVED, 8, random.Random(42))
    assert first != SOLVED
