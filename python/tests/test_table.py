"""God's algorithm: the exhaustive distance table.

Building takes a second or two, so these are marked ``slow`` and deselected by
default. Run them with ``pytest -m slow``.
"""

import random

import pytest

from pyrubik import coords, cube, search
from pyrubik.cube import SOLVED
from pyrubik.table import BackgroundTable, DistanceTable, numpy

pytestmark = pytest.mark.slow

#: OEIS A079762 -- positions at exactly n quarter turns from solved. Reproducing
#: this checks the move tables, the encoding and the search all at once.
A079762 = [1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508,
           930588, 1350852, 782536, 90280, 276]


def test_distance_distribution_matches_oeis(table):
    assert table.histogram() == A079762


def test_every_state_is_reachable(table):
    assert len(table) == coords.N_STATES
    assert 0 not in table.distances  # 0 would mean "never reached"


def test_gods_number_is_fourteen(table):
    assert max(table.distances) - 1 == 14


def test_solved_state_has_distance_zero(table):
    assert table.distance(coords.SOLVED_INDEX) == 0


@pytest.mark.skipif(numpy is None, reason="numpy not installed")
def test_both_backends_agree(table):
    assert DistanceTable.build(pure=True).distances == table.distances


def test_the_threaded_build_agrees(table):
    """Workers racing for the same state is benign -- they write the same marker."""
    assert DistanceTable.build_parallel(workers=4).distances == table.distances


@pytest.mark.parametrize("seed", range(25))
def test_table_solutions_are_optimal(table, seed):
    state = cube.scramble(SOLVED, 14, random.Random(seed))
    solution = table.solve(state)
    assert cube.is_solved(cube.apply_moves(state, solution))
    assert len(solution) == len(search.solve(state))


def test_solves_an_antipodal_position(table):
    assert len(table.solve(cube.parse("wppryygrygrwgprbbbwpbwyg"))) == 14


def test_already_solved_needs_no_moves(table):
    assert all(table.solve(state) == "" for state in cube.orientations(SOLVED))


def test_distance_agrees_with_solution_length(table):
    rng = random.Random(5)
    for _ in range(20):
        state = cube.scramble(SOLVED, rng.randrange(15), rng)
        assert table.distance(coords.encode(state)) == len(table.solve(state))


def test_background_table_produces_a_usable_table():
    built = BackgroundTable().wait(timeout=300)
    state = cube.scramble(SOLVED, 11, random.Random(4))
    assert cube.is_solved(cube.apply_moves(state, built.solve(state)))


def test_background_table_does_not_block_before_it_is_done():
    background = BackgroundTable()
    assert background.ready is None or isinstance(background.ready, DistanceTable)
    assert background.wait(timeout=300) is not None
    assert background.ready is not None
