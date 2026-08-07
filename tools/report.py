#!/usr/bin/env python3
"""Aggregate per-language benchmark JSON into the comparison tables.

Each implementation emits one JSON object (see SPEC.md section 6). Collect them
in bench/results/<language>.json and run::

    python3 tools/report.py                 # print markdown
    python3 tools/report.py --update-readme # rewrite the section in README.md

Only the standard library, so it runs anywhere without setup.
"""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "bench" / "results"
README = ROOT / "README.md"

BEGIN = "<!-- BENCHMARK:BEGIN -->"
END = "<!-- BENCHMARK:END -->"

ALGORITHMS = ("bfs", "ida", "table", "table-par")


def load() -> list[dict]:
    """Every result file, fastest table build first."""
    reports = [json.loads(path.read_text()) for path in sorted(RESULTS.glob("*.json"))]
    if not reports:
        sys.exit(f"no result files in {RESULTS}")
    return sorted(reports, key=lambda report: _entry(report, "table")["setup_ms"])


def _entry(report: dict, name: str) -> dict:
    for algorithm in report["algorithms"]:
        if algorithm["name"] == name:
            return algorithm
    return {}


def _number(value: float) -> str:
    """Enough digits to be honest, few enough to read.

    Sub-millisecond table lookups and second-long builds share these columns, so
    the precision has to follow the magnitude rather than being fixed.
    """
    if value == 0:
        return "0"
    if value >= 100:
        return f"{value:,.0f}"
    if value >= 1:
        return f"{value:.1f}"
    if value >= 0.01:
        return f"{value:.3f}".rstrip("0")
    return f"{value:.2g}"


def table_for(reports: list[dict], field: str, unit: str) -> str:
    """One markdown table: languages down the side, algorithms across."""
    lines = [
        f"| Language | {' | '.join(ALGORITHMS)} |",
        "|---" * (len(ALGORITHMS) + 1) + "|",
    ]
    for report in reports:
        cells = []
        for name in ALGORITHMS:
            entry = _entry(report, name)
            cells.append(_number(entry[field]) if field in entry else "—")
        lines.append(f"| {report['language']} | {' | '.join(cells)} |")
    return f"**{unit}**\n\n" + "\n".join(lines)


def break_even(report: dict) -> str:
    """Solves after which building the table beats searching."""
    bfs, table = _entry(report, "bfs"), _entry(report, "table")
    gain = bfs["avg_ms"] - table["avg_ms"]
    if gain <= 0:
        return "—"
    return f"{int((table['setup_ms'] - bfs['setup_ms']) / gain) + 1:,}"


def speedup(report: dict) -> str:
    """How much threading the table build actually bought."""
    serial, parallel = _entry(report, "table"), _entry(report, "table-par")
    if not parallel or not parallel.get("setup_ms"):
        return "—"
    factor = serial["setup_ms"] / parallel["setup_ms"]
    workers = parallel.get("workers")
    return f"{factor:.2f}×" + (f" ({workers} threads)" if workers else "")


def _bytes(count: int | None) -> str:
    if not count:
        return "—"
    if count >= 1 << 20:
        return f"{count / (1 << 20):.1f} MB"
    return f"{count / 1024:.0f} KB"


def _seconds(milliseconds: float | None) -> str:
    return "—" if milliseconds is None else f"{milliseconds / 1000:.1f} s"


def summary(reports: list[dict]) -> str:
    """The headline: how fast it runs against what it cost to get there."""
    lines = [
        "| Language | Version | Table build | Per solve | Break-even | Threaded build |",
        "|---|---|---|---|---|---|",
    ]
    for report in reports:
        table = _entry(report, "table")
        lines.append(
            f"| {report['language']} | {report['version']} "
            f"| {_number(table['setup_ms'])} ms | {_number(table['avg_ms'])} ms "
            f"| {break_even(report)} solves | {speedup(report)} |"
        )
    return "\n".join(lines)


def cost(reports: list[dict]) -> str:
    """What each implementation costs to build and to maintain."""
    lines = [
        "| Language | Build from clean | Artifact | Source files | Source lines | Test lines |",
        "|---|---|---|---|---|---|",
    ]
    for report in reports:
        lines.append(
            f"| {report['language']} | {_seconds(report.get('build_ms'))} "
            f"| {_bytes(report.get('artifact_bytes'))} "
            f"| {report.get('source_files', '—')} "
            f"| {report.get('source_lines', '—'):,} "
            f"| {report.get('test_lines', '—'):,} |"
        )
    return "\n".join(lines)


def startup(reports: list[dict]) -> str:
    """What it costs merely to start the program, before any solving."""
    lines = [
        "| Language | Startup | Resident at startup | vs. one table lookup |",
        "|---|---|---|---|",
    ]
    for report in reports:
        milliseconds = report.get("startup_ms")
        resident = report.get("startup_rss_kb")
        lookup = _entry(report, "table").get("avg_ms")
        ratio = (
            f"{milliseconds / lookup:,.0f}×"
            if milliseconds and lookup else "—"
        )
        lines.append(
            f"| {report['language']} "
            f"| {_number(milliseconds) + ' ms' if milliseconds else '—'} "
            f"| {_bytes(resident * 1024) if resident else '—'} "
            f"| {ratio} |"
        )
    return "\n".join(lines)


def render(reports: list[dict]) -> str:
    parts = [
        summary(reports),
        "",
        "### What it cost to get there",
        "",
        cost(reports),
        "",
        "### What it costs just to start",
        "",
        startup(reports),
        "",
        "### Every measurement",
        "",
        table_for(reports, "setup_ms", "Setup — one-time precomputation, milliseconds"),
        "",
        table_for(reports, "avg_ms", "Average per solve, milliseconds (200 scrambles)"),
        "",
        table_for(reports, "worst_ms", "Worst case — the antipodal position, milliseconds"),
        "",
        table_for(reports, "peak_mb", "Peak memory while solving, MB"),
        "",
        "Builds: " + "; ".join(f"{r['language']} {r.get('build', '?')}" for r in reports),
    ]
    return "\n".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update-readme", action="store_true")
    args = parser.parse_args()

    reports = load()
    for report in reports:  # every implementation must agree on optimality
        for algorithm in report["algorithms"]:
            if not algorithm.get("optimal", False):
                sys.exit(f"{report['language']} {algorithm['name']} is not optimal")

    body = render(reports)
    if not args.update_readme:
        print(body)
        return 0

    text = README.read_text()
    if BEGIN not in text or END not in text:
        sys.exit(f"{README} has no {BEGIN} / {END} markers")
    head, _, rest = text.partition(BEGIN)
    _, _, tail = rest.partition(END)
    README.write_text(f"{head}{BEGIN}\n\n{body}\n\n{END}{tail}")
    print(f"updated {README}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
