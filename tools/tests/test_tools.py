"""Unit tests for the pure helpers in the benchmark tooling."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import measure
import report


def _report(**algorithms: dict) -> dict:
    return {"language": "X", "algorithms": [{"name": n, **v} for n, v in algorithms.items()]}


def test_clang_build_relabels_gcc_strings():
    assert measure._clang_build("g++ -std=c++23 -O3") == "clang++ -std=c++23 -O3"
    assert measure._clang_build("GCC 15.1, -O3") == "clang 15.1, -O3"


def test_clang_build_leaves_clang_alone():
    assert measure._clang_build("clang++ -std=c++23 -O3") == "clang++ -std=c++23 -O3"
    assert measure._clang_build("") == ""


def test_number_precision_follows_magnitude():
    assert report._number(0) == "0"
    assert report._number(1234.4) == "1,234"
    assert report._number(12.34) == "12.3"
    assert report._number(0.068) == "0.068"
    assert report._number(0.0006) == "0.0006"


def test_entry_missing_algorithm_is_empty():
    assert report._entry(_report(bfs={"avg_ms": 1.0}), "table") == {}


def test_break_even():
    r = _report(bfs={"setup_ms": 0, "avg_ms": 0.5}, table={"setup_ms": 100, "avg_ms": 0.1})
    assert report.break_even(r) == "251"
    slower = _report(bfs={"setup_ms": 0, "avg_ms": 0.1}, table={"setup_ms": 100, "avg_ms": 0.5})
    assert report.break_even(slower) == "—"


def test_speedup():
    r = _report(table={"setup_ms": 160}, **{"table-par": {"setup_ms": 100, "workers": 4}})
    assert report.speedup(r) == "1.60× (4 threads)"
    assert report.speedup(_report(table={"setup_ms": 160})) == "—"


def test_bytes_and_seconds():
    assert report._bytes(None) == "—"
    assert report._bytes(48 * 1024) == "48 KB"
    assert report._bytes(3 << 20) == "3.0 MB"
    assert report._seconds(None) == "—"
    assert report._seconds(10_500) == "10.5 s"


def test_table_for_marks_missing_cells():
    r = _report(bfs={"avg_ms": 0.05}, table={"avg_ms": 0.0004})
    lines = report.table_for([r], "avg_ms", "Avg").splitlines()
    assert lines[0] == "**Avg**"
    assert lines[-1] == "| X | 0.05 | — | 0.0004 | — |"
