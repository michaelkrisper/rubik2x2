"""Command line interface: one-shot subcommands and an interactive shell."""

import argparse
import contextlib
import random
import sys
import time
from dataclasses import dataclass, field

from . import bench, coords, cube
from .cube import Moves, State, UnsolvableError
from .display import Color, render
from .search import solve as search_solve
from .solvers import Algorithm, make
from .table import BackgroundTable, DistanceTable

with contextlib.suppress(ImportError):  # line editing and history, where available
    import readline  # noqa: F401

HELP = """\
Commands
  solve             find a shortest solution for the current cube
  print             re-draw the current cube
  state             print the current cube as 24 color letters
  scramble [n]      apply n random moves (default 8)
  reset             restore the solved cube
  init <24 letters> replace the cube with the given state
  help              show this text
  exit              leave the shell

Moves -- lowercase and uppercase are inverse quarter turns, and chain as "lFtL"
  l/L  left column down/up      r/R  right column down/up
  t/T  top layer left/right     b/B  bottom layer left/right
  f/F  front face cw/ccw        a/A  back face cw/ccw

A state string is 24 color letters (rwbypg) in side order
UP FRONT DOWN BACK LEFT RIGHT, each face read row-major.
"""


@dataclass(slots=True)
class Session:
    """One interactive session: a cube, a display mode, and a table being built."""

    state: State
    color: Color
    table: BackgroundTable = field(default_factory=BackgroundTable)

    def show(self) -> None:
        print(render(self.state, self.color))

    def solve(self) -> None:
        """Solve with the table if it has landed, otherwise search."""
        if (table := self.table.ready) is not None:
            solver, how = table.solve, "table"
        else:
            solver, how = search_solve, "search"

        started = time.perf_counter()
        try:
            solution = solver(self.state)
        except UnsolvableError as error:
            print(f"No solution: {error}", file=sys.stderr)
            return
        elapsed = (time.perf_counter() - started) * 1000

        if not solution:
            print("Already solved.")
            return
        print(f"{len(solution)} moves via {how} in {elapsed:.1f} ms:  {solution}")
        print(render(cube.apply_moves(self.state, solution), self.color))


def repl(session: Session) -> int:
    print("pyrubik -- optimal 2x2x2 solver.  Type 'help' for commands.")
    session.show()
    while True:
        try:
            line = input("\n> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return 0

        command, _, argument = line.partition(" ")
        argument = argument.strip()

        match command:
            case "":
                continue
            case "exit" | "quit":
                return 0
            case "help":
                print(HELP)
            case "print":
                session.show()
            case "state":
                print(cube.to_string(session.state))
            case "reset":
                session.state = cube.SOLVED
                session.show()
            case "solve":
                try:
                    session.solve()
                except KeyboardInterrupt:
                    print("\nAborted.")
            case "scramble":
                try:
                    count = int(argument) if argument else 8
                except ValueError:
                    print(f"Not a number: {argument}", file=sys.stderr)
                    continue
                session.state = cube.scramble(session.state, count)
                session.show()
            case "init":
                try:
                    session.state = cube.parse(argument)
                except ValueError as error:
                    print(error, file=sys.stderr)
                    continue
                session.show()
            case _ if line and set(line) <= set(cube.MOVES):
                session.state = cube.apply_moves(session.state, line)
                session.show()
            case _:
                print(f"Unknown command: {line!r}. Type 'help'.", file=sys.stderr)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="pyrubik",
        description="Solve the 2x2x2 Rubik's Cube (Pocket Cube) optimally.",
    )
    colors = parser.add_mutually_exclusive_group()
    colors.add_argument("--color", action="store_true", help="force colored output")
    colors.add_argument("--no-color", action="store_true", help="plain letters only")
    subcommands = parser.add_subparsers(dest="command")

    shell = subcommands.add_parser("shell", help="interactive shell (default)")
    shell.add_argument("state", nargs="?", help="24 color letters; default is solved")

    solver = subcommands.add_parser("solve", help="print a shortest solution and exit")
    solver.add_argument("state", help="24 color letters, or '-' to read from stdin")
    solver.add_argument(
        "--algorithm",
        type=Algorithm,
        choices=list(Algorithm),
        default=Algorithm.BFS,
        help="default bfs needs no setup; table builds the full distance table first",
    )

    scrambler = subcommands.add_parser("scramble", help="print a scrambled state")
    scrambler.add_argument("-n", "--moves", type=int, default=8)
    scrambler.add_argument("--seed", type=int, help="seed for reproducible output")

    builder = subcommands.add_parser("table", help="build the distance table and describe it")
    builder.add_argument("--pure", action="store_true", help="skip the numpy fast path")

    marks = subcommands.add_parser("bench", help="compare the three solvers")
    marks.add_argument("-n", "--samples", type=int, default=200)

    return parser


def _resolve_color(args: argparse.Namespace) -> Color:
    if args.no_color:
        return Color.NONE
    if args.color:
        return Color.best()
    return Color.detect()


def _table_command(pure: bool) -> int:
    print(f"Searching all {coords.N_STATES:,} states...")
    started = time.perf_counter()
    table = DistanceTable.build(
        progress=lambda depth, count: print(f"  depth {depth:2}: {count:>9,} states", flush=True),
        pure=pure,
    )
    elapsed = time.perf_counter() - started
    print(f"\nBuilt in {elapsed:.2f} s via {table.backend}; {len(table) / 1e6:.1f} MB resident.")
    print("This matches OEIS A079762, the published distance distribution.")
    return 0


def _solve_command(state: State, algorithm: Algorithm) -> int:
    table = DistanceTable.build() if algorithm is Algorithm.TABLE else None
    try:
        solution: Moves = make(algorithm, table)(state)
    except UnsolvableError as error:
        print(f"No solution: {error}", file=sys.stderr)
        return 1
    if solution:
        print(solution)
    else:
        print("Already solved.", file=sys.stderr)
    return 0


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(sys.argv[1:] if argv is None else argv)
    color = _resolve_color(args)

    match args.command:
        case "table":
            return _table_command(args.pure)
        case "bench":
            bench.run(args.samples)
            return 0
        case "scramble":
            print(cube.to_string(cube.scramble(cube.SOLVED, args.moves, random.Random(args.seed))))
            return 0

    text = getattr(args, "state", None)
    if text == "-":
        text = sys.stdin.read()
    try:
        state = cube.parse(text) if text else cube.SOLVED
    except ValueError as error:
        print(f"Invalid cube: {error}", file=sys.stderr)
        return 2

    if args.command == "solve":
        return _solve_command(state, args.algorithm)
    return repl(Session(state, color))
