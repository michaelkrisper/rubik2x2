"""The three algorithms behind one interface."""

import random

import pytest

from pyrubik import cube, solvers
from pyrubik.cube import SOLVED
from pyrubik.solvers import Algorithm


def test_every_algorithm_has_a_description():
    assert all(algorithm.description for algorithm in Algorithm)


def test_every_algorithm_can_be_made():
    from pyrubik.table import DistanceTable

    stub = DistanceTable(bytearray(1))
    assert all(callable(solvers.make(algorithm, stub)) for algorithm in Algorithm)


def test_table_algorithm_requires_a_table():
    with pytest.raises(ValueError, match="needs a DistanceTable"):
        solvers.make(Algorithm.TABLE)


@pytest.mark.parametrize("algorithm", [Algorithm.BFS, Algorithm.IDA])
def test_setup_free_algorithms_solve(algorithm):
    solve = solvers.make(algorithm)
    state = cube.scramble(SOLVED, 8, random.Random(3))
    assert cube.is_solved(cube.apply_moves(state, solve(state)))


@pytest.mark.slow
def test_all_three_agree_on_solution_length(table):
    rng = random.Random(12)
    states = [cube.scramble(SOLVED, 12, rng) for _ in range(10)]
    lengths = {
        algorithm: [len(solvers.make(algorithm, table)(state)) for state in states]
        for algorithm in Algorithm
    }
    assert lengths[Algorithm.BFS] == lengths[Algorithm.IDA] == lengths[Algorithm.TABLE]


def test_algorithm_names_round_trip():
    for algorithm in Algorithm:
        assert Algorithm(str(algorithm)) is algorithm
