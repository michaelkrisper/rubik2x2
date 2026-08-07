"""Bidirectional search: correct, and provably shortest."""

import random

import pytest

from pyrubik import coords, cube, search
from pyrubik.cube import SOLVE_MOVES, SOLVED, UnsolvableError


def reference_shortest(state: bytes, limit: int = 6) -> str | None:
    """Deliberately naive one-sided BFS, to check optimality against."""
    frontier = {coords.encode(state): ""}
    seen = set(frontier)
    for _ in range(limit):
        if coords.SOLVED_INDEX in frontier:
            return frontier[coords.SOLVED_INDEX]
        following = {}
        for index, path in frontier.items():
            for move in SOLVE_MOVES:
                child = coords.apply(index, move)
                if child not in seen:
                    seen.add(child)
                    following[child] = path + move
        frontier = following
    return frontier.get(coords.SOLVED_INDEX)


def test_solved_cube_needs_no_moves():
    assert search.solve(SOLVED) == ""


def test_every_rotation_of_solved_needs_no_moves():
    assert all(search.solve(state) == "" for state in cube.orientations(SOLVED))


@pytest.mark.parametrize("seed", range(5))
def test_distance_does_not_depend_on_how_the_cube_is_held(seed):
    """Rotating the whole cube is an isomorphism, so solution length must match."""
    state = cube.scramble(SOLVED, 8, random.Random(seed))
    lengths = set()
    for rotated in cube.orientations(state):
        solution = search.solve(rotated)
        assert cube.is_solved(cube.apply_moves(rotated, solution))
        lengths.add(len(solution))
    assert len(lengths) == 1


@pytest.mark.parametrize("seed", range(5))
def test_solution_length_matches_a_naive_bfs(seed):
    scrambled = cube.scramble(SOLVED, 5, random.Random(100 + seed))
    reference = reference_shortest(scrambled)
    assert reference is not None
    assert len(search.solve(scrambled)) == len(reference)


@pytest.mark.parametrize("depth", range(1, 8))
def test_solution_is_never_longer_than_the_scramble(depth):
    rng = random.Random(depth)
    path = ""
    while len(path) < depth:
        move = rng.choice(SOLVE_MOVES)
        if not path or move != path[-1].swapcase():
            path += move
    scrambled = cube.apply_moves(SOLVED, path)
    solution = search.solve(scrambled)
    assert len(solution) <= len(path)
    assert cube.is_solved(cube.apply_moves(scrambled, solution))


@pytest.mark.parametrize("seed", range(10))
def test_random_scrambles_are_solved(seed):
    scrambled = cube.scramble(SOLVED, 12, random.Random(seed))
    solution = search.solve(scrambled)
    assert set(solution) <= set(SOLVE_MOVES)
    assert cube.is_solved(cube.apply_moves(scrambled, solution))


def test_solves_the_cube_in_a_cube_pattern():
    scrambled = cube.apply_moves(SOLVED, "ttlfTlFlFTLL")
    assert cube.is_solved(cube.apply_moves(scrambled, search.solve(scrambled)))


def test_solves_an_antipodal_position():
    # One of the 276 states at maximum distance; God's number is 14 quarter turns.
    hardest = cube.parse("wppryygrygrwgprbbbwpbwyg")
    solution = search.solve(hardest)
    assert len(solution) == 14
    assert cube.is_solved(cube.apply_moves(hardest, solution))


def test_depth_limit_is_reported():
    scrambled = cube.scramble(SOLVED, 12, random.Random(0))
    with pytest.raises(UnsolvableError, match="within 1 moves"):
        search.solve(scrambled, max_depth=1)
