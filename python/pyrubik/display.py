"""Drawing a cube for a terminal.

Deliberately separate from :mod:`pyrubik.cube`: the model is 24 bytes and knows
nothing about how anyone looks at it.

The picture is the standard unfolded net -- UP above FRONT, the four side faces
in a band, DOWN below -- so it matches the order a state string is written in.
BACK is drawn as seen from behind, which is what makes the band read as one
continuous strip around the cube.

Color is used when the terminal really supports it and letters otherwise, per
``NO_COLOR``/``FORCE_COLOR`` and whether output is a terminal at all. Colored
facelets are two rows tall so they come out roughly square, since terminal cells
are about twice as tall as they are wide; the letter fallback uses one row,
where squareness would only cost readability.
"""

import os
import sys
from enum import Enum
from typing import Self

from .cube import BACK, COLOR_CHARS, DOWN, FRONT, LEFT, RIGHT, UP, State

#: Face-color RGB, close to a real cube. This puzzle uses purple where a
#: standard cube has orange.
PALETTE: dict[str, tuple[int, int, int]] = {
    "r": (196, 30, 58),
    "w": (245, 245, 245),
    "b": (0, 81, 186),
    "y": (255, 213, 0),
    "p": (137, 66, 175),
    "g": (0, 158, 96),
}

#: The side faces, left to right, as (label, side index).
_BAND: tuple[tuple[str, int], ...] = (
    ("LEFT", LEFT), ("FRONT", FRONT), ("RIGHT", RIGHT), ("BACK", BACK),
)

_CELL_WIDTH = 4
_FACE_WIDTH = _CELL_WIDTH * 2
_GAP = "  "

_RESET = "\x1b[0m"
_DIM = "\x1b[2m"


class Color(Enum):
    """How much color the output may use."""

    NONE = "none"
    ANSI256 = "256"
    TRUE = "truecolor"

    @classmethod
    def best(cls) -> Self:
        """The richest mode this terminal understands, ignoring where output goes."""
        if os.environ.get("TERM") == "dumb":
            return cls.NONE
        if os.environ.get("COLORTERM", "").lower() in ("truecolor", "24bit"):
            return cls.TRUE
        return cls.ANSI256

    @classmethod
    def detect(cls, stream=None) -> Self:
        """What to actually use, honouring ``NO_COLOR``, ``FORCE_COLOR`` and piping."""
        if os.environ.get("NO_COLOR"):
            return cls.NONE
        stream = stream if stream is not None else sys.stdout
        piped = not hasattr(stream, "isatty") or not stream.isatty()
        if piped and not os.environ.get("FORCE_COLOR"):
            return cls.NONE
        return cls.best()

    @property
    def cell_height(self) -> int:
        """Rows per facelet: two for color blocks, one for letters."""
        return 1 if self is Color.NONE else 2


def _ansi256(red: int, green: int, blue: int) -> int:
    """Nearest index in the xterm 6x6x6 color cube."""
    return 16 + 36 * (red * 5 // 255) + 6 * (green * 5 // 255) + (blue * 5 // 255)


def _paint(char: str, color: Color) -> str:
    """One facelet row, always :data:`_CELL_WIDTH` columns wide."""
    if color is Color.NONE:
        return char.upper().center(_CELL_WIDTH)
    red, green, blue = PALETTE[char]
    background = (
        f"\x1b[48;2;{red};{green};{blue}m" if color is Color.TRUE
        else f"\x1b[48;5;{_ansi256(red, green, blue)}m"
    )
    return f"{background}{' ' * _CELL_WIDTH}{_RESET}"


def _face(state: State, side: int, color: Color) -> list[str]:
    """One face as text rows: two facelet rows, each ``cell_height`` tall."""
    base = side * 4
    order = ((3, 2), (1, 0)) if side == BACK else ((0, 1), (2, 3))
    rows = []
    for pair in order:
        painted = "".join(_paint(COLOR_CHARS[state[base + index]], color) for index in pair)
        rows.extend([painted] * color.cell_height)
    return rows


def _label(text: str, color: Color) -> str:
    centered = text.center(_FACE_WIDTH)
    return f"{_DIM}{centered}{_RESET}" if color is not Color.NONE else centered


def render(state: State, color: Color | bool | None = None) -> str:
    """Draw the cube as an unfolded net.

    ``color`` takes a :class:`Color`, or ``True``/``False`` to force the best
    available mode or plain letters. ``None`` detects from the terminal.
    """
    match color:
        case None:
            color = Color.detect()
        case True:
            color = Color.best()
        case False:
            color = Color.NONE

    # UP and DOWN sit over FRONT, the second face in the band.
    indent = " " * (_FACE_WIDTH + len(_GAP))
    band = [_face(state, side, color) for _, side in _BAND]

    lines = [indent + _label("UP", color)]
    lines += [indent + row for row in _face(state, UP, color)]
    lines.append("")
    lines.append(_GAP.join(_label(name, color) for name, _ in _BAND))
    lines += [_GAP.join(face[row] for face in band) for row in range(len(band[0]))]
    lines.append("")
    lines.append(indent + _label("DOWN", color))
    lines += [indent + row for row in _face(state, DOWN, color)]
    return "\n".join(lines)
