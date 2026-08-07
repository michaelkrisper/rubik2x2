"""Rendering: the picture must stay aligned in every color mode."""

import io
import random
import re

import pytest

from pyrubik import cube
from pyrubik.cube import COLOR_CHARS, SOLVED
from pyrubik.display import PALETTE, Color, render

ANSI = re.compile(r"\x1b\[[0-9;]*m")


def widths(text: str) -> set[int]:
    """Printable width of each non-empty line."""
    return {len(ANSI.sub("", line)) for line in text.splitlines() if line.strip()}


@pytest.mark.parametrize("mode", list(Color))
def test_every_line_of_a_face_block_has_a_consistent_width(mode):
    drawing = render(cube.scramble(SOLVED, 7, random.Random(1)), mode)
    # Two widths only: the narrow UP/DOWN column and the wide side band.
    assert len(widths(drawing)) == 2


@pytest.mark.parametrize("mode", list(Color))
def test_all_24_facelets_are_drawn(mode):
    drawing = render(SOLVED, mode)
    if mode is Color.NONE:
        # 24 facelets, plus letters in the UP/DOWN/LEFT/FRONT/RIGHT/BACK labels.
        letters = [char for char in drawing if char in "RWBYPG"]
        assert len(letters) >= 24
    else:
        assert len(ANSI.findall(drawing)) >= 24


def test_plain_mode_has_no_escape_sequences():
    assert "\x1b[" not in render(SOLVED, Color.NONE)


def test_color_modes_emit_escape_sequences():
    assert "\x1b[48;2;" in render(SOLVED, Color.TRUE)
    assert "\x1b[48;5;" in render(SOLVED, Color.ANSI256)


def test_booleans_select_plain_or_best():
    assert "\x1b[" not in render(SOLVED, False)
    assert "\x1b[" in render(SOLVED, True)


def test_palette_covers_every_color():
    assert set(PALETTE) == set(COLOR_CHARS)


def test_back_face_is_mirrored():
    """The band reads as one strip around the cube, so BACK is seen from behind."""
    state = bytearray(SOLVED)
    state[cube.BACK * 4 : cube.BACK * 4 + 4] = bytes((0, 1, 2, 3))  # r w b y
    lines = ANSI.sub("", render(bytes(state), Color.NONE)).splitlines()

    label = next(index for index, line in enumerate(lines) if "LEFT" in line)
    back = slice(-8, None)  # BACK is the last face in the band
    assert lines[label + 1][back].split() == ["Y", "B"]  # facelets 3, 2
    assert lines[label + 2][back].split() == ["W", "R"]  # facelets 1, 0


def test_detect_says_none_when_piped():
    assert Color.detect(io.StringIO()) is Color.NONE


def test_detect_honours_no_color(monkeypatch):
    monkeypatch.setenv("NO_COLOR", "1")
    monkeypatch.setenv("FORCE_COLOR", "1")
    assert Color.detect(io.StringIO()) is Color.NONE


def test_force_color_overrides_piping(monkeypatch):
    monkeypatch.delenv("NO_COLOR", raising=False)
    monkeypatch.setenv("FORCE_COLOR", "1")
    assert Color.detect(io.StringIO()) is not Color.NONE


def test_truecolor_is_used_when_advertised(monkeypatch):
    monkeypatch.delenv("NO_COLOR", raising=False)
    monkeypatch.setenv("COLORTERM", "truecolor")
    monkeypatch.delenv("TERM", raising=False)
    assert Color.best() is Color.TRUE


def test_dumb_terminals_get_no_color(monkeypatch):
    monkeypatch.setenv("TERM", "dumb")
    assert Color.best() is Color.NONE


def test_cell_height_differs_by_mode():
    assert Color.NONE.cell_height == 1
    assert Color.TRUE.cell_height == 2
