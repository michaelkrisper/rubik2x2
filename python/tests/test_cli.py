"""The command line surface."""

import random

import pytest

from pyrubik import cube
from pyrubik.cli import main


@pytest.fixture
def run(capsys, monkeypatch):
    monkeypatch.setenv("NO_COLOR", "1")

    def invoke(*argv):
        code = main(list(argv))
        captured = capsys.readouterr()
        return code, captured.out, captured.err

    return invoke


@pytest.fixture
def answers(monkeypatch):
    def feed(*lines):
        replies = iter(lines)
        monkeypatch.setattr("builtins.input", lambda _="": next(replies))

    return feed


def test_solve_prints_only_the_solution(run):
    scrambled = cube.to_string(cube.scramble(cube.SOLVED, 6, random.Random(5)))
    code, out, _ = run("solve", scrambled)
    assert code == 0
    assert cube.is_solved(cube.apply_moves(cube.parse(scrambled), out.strip()))


@pytest.mark.parametrize("algorithm", ["bfs", "ida"])
def test_solve_honours_the_algorithm_flag(run, algorithm):
    scrambled = cube.to_string(cube.scramble(cube.SOLVED, 6, random.Random(11)))
    code, out, _ = run("solve", scrambled, "--algorithm", algorithm)
    assert code == 0
    assert cube.is_solved(cube.apply_moves(cube.parse(scrambled), out.strip()))


def test_solve_reports_an_already_solved_cube(run):
    code, _, err = run("solve", cube.to_string(cube.SOLVED))
    assert code == 0
    assert "Already solved" in err


def test_solve_rejects_an_invalid_state(run):
    code, _, err = run("solve", "nonsense")
    assert code == 2
    assert "Invalid cube" in err


def test_solve_reports_an_unreachable_cube(run):
    broken = bytearray(cube.SOLVED)
    broken[0], broken[16] = broken[16], broken[0]
    code, _, err = run("solve", cube.to_string(bytes(broken)))
    assert code == 1
    assert "No solution" in err


def test_scramble_is_reproducible(run):
    _, first, _ = run("scramble", "--seed", "1")
    _, second, _ = run("scramble", "--seed", "1")
    assert first == second
    assert cube.parse(first) != cube.SOLVED


def test_scramble_output_is_solvable(run):
    _, scrambled, _ = run("scramble", "-n", "11", "--seed", "9")
    code, solution, _ = run("solve", scrambled.strip())
    assert code == 0
    assert cube.is_solved(cube.apply_moves(cube.parse(scrambled), solution.strip()))


def test_shell_starts_from_a_given_state(run, answers):
    scrambled = cube.to_string(cube.scramble(cube.SOLVED, 4, random.Random(2)))
    answers("state", "exit")
    code, out, _ = run("shell", scrambled)
    assert code == 0
    assert "optimal 2x2x2 solver" in out
    assert scrambled in out


def test_no_arguments_starts_the_shell_solved(run, answers):
    answers("state", "exit")
    code, out, _ = run()
    assert code == 0
    assert cube.to_string(cube.SOLVED) in out


def test_shell_applies_move_chains_and_solves(run, answers):
    answers("lFtL", "state", "solve", "exit")
    code, out, _ = run()
    assert code == 0
    assert cube.to_string(cube.apply_moves(cube.SOLVED, "lFtL")) in out
    solved = [line for line in out.splitlines() if "moves via" in line]
    assert len(solved) == 1
    assert solved[0].split(":")[-1].strip() == "lTfL"


def test_shell_scramble_and_reset(run, answers):
    answers("scramble 5", "reset", "state", "exit")
    code, out, _ = run()
    assert code == 0
    assert cube.to_string(cube.SOLVED) in out


def test_shell_rejects_a_bad_scramble_count(run, answers):
    answers("scramble many", "exit")
    code, _, err = run()
    assert code == 0
    assert "Not a number" in err


def test_shell_rejects_a_bad_init(run, answers):
    answers("init nope", "exit")
    code, _, err = run()
    assert code == 0
    assert "24 color letters" in err


def test_shell_reports_unknown_commands(run, answers):
    answers("frobnicate", "exit")
    code, _, err = run()
    assert code == 0
    assert "Unknown command" in err


def test_shell_help_lists_commands(run, answers):
    answers("help", "exit")
    code, out, _ = run()
    assert code == 0
    assert "scramble" in out and "init" in out
