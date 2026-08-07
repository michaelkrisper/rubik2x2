"""IDA* with pattern databases: kept as a reference point, not as the fast path."""

import random

import pytest

from pyrubik import coords, cube, ida, search
from pyrubik.cube import SOLVED


def test_pattern_databases_cover_their_coordinate():
    databases = ida.PatternDatabases.build()
    assert len(databases.permutation) == coords.N_PERMUTATIONS
    assert len(databases.orientation) == coords.N_ORIENTATIONS
    assert 0 not in databases.permutation  # every projected state is reachable
    assert 0 not in databases.orientation


def test_databases_are_built_once():
    assert ida.PatternDatabases.build() is ida.PatternDatabases.build()


def test_the_bound_is_zero_exactly_when_solved():
    databases = ida.PatternDatabases.build()
    assert databases.bound(coords.SOLVED_INDEX) == 0
    scrambled = coords.encode(cube.scramble(SOLVED, 6, random.Random(2)))
    assert databases.bound(scrambled) > 0


def test_databases_reach_less_far_than_the_real_distance():
    # This is why IDA* loses here: the bound tops out well below 14.
    assert max(ida.PatternDatabases.build().reach) < 14


def test_solved_cube_needs_no_moves():
    assert ida.solve(SOLVED) == ""


@pytest.mark.parametrize("seed", range(8))
def test_solutions_are_optimal(seed):
    state = cube.scramble(SOLVED, 10, random.Random(seed))
    solution = ida.solve(state)
    assert cube.is_solved(cube.apply_moves(state, solution))
    assert len(solution) == len(search.solve(state))


@pytest.mark.slow
def test_bound_never_exceeds_the_true_distance(table):
    """Admissibility: a heuristic that overestimates would break optimality."""
    databases = ida.PatternDatabases.build()
    rng = random.Random(0)
    for _ in range(300):
        index = coords.encode(cube.scramble(SOLVED, rng.randrange(15), rng))
        assert databases.bound(index) <= table.distance(index)
