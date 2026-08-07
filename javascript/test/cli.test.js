/** The command line: exit codes, stdout discipline, and the JSON benchmark. */

import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { describe, it } from "node:test";

import { applyMoves, isSolved, parse } from "../src/cube.js";

const CLI = new URL("../src/cli.js", import.meta.url).pathname;
const SOLVED_TEXT = "yyyyrrrrwwwwppppbbbbgggg";
const HARDEST = "wppryygrygrwgprbbbwpbwyg";

/** @returns {{stdout: string, stderr: string, status: number}} */
function cli(...args) {
  const { stdout, stderr, status } = spawnSync(process.execPath, [CLI, ...args], {
    encoding: "utf8",
  });
  return { stdout, stderr, status };
}

describe("solve", () => {
  for (const algorithm of ["bfs", "ida", "table"]) {
    it(`prints nothing but the moves (${algorithm})`, () => {
      const { stdout, status } = cli("solve", HARDEST, "--algorithm", algorithm);
      assert.equal(status, 0);
      assert.match(stdout, /^[lLtTfF]{14}\n$/);
      assert.ok(isSolved(applyMoves(parse(HARDEST), stdout.trim())));
    });
  }

  it("reads a state from stdin", () => {
    const stdout = execFileSync(process.execPath, [CLI, "solve", "-"], {
      encoding: "utf8",
      input: `${HARDEST}\n`,
    });
    assert.equal(stdout.trim().length, 14);
  });

  it("says nothing on stdout for an already solved cube", () => {
    const { stdout, stderr, status } = cli("solve", SOLVED_TEXT);
    assert.equal(status, 0);
    assert.equal(stdout, "");
    assert.match(stderr, /Already solved/);
  });

  it("rejects a malformed state with exit code 2", () => {
    const { status, stderr } = cli("solve", "nonsense");
    assert.equal(status, 2);
    assert.match(stderr, /Invalid input/);
  });

  it("rejects an unknown algorithm", () => {
    assert.equal(cli("solve", HARDEST, "--algorithm", "psychic").status, 2);
  });
});

describe("scramble", () => {
  it("prints a legal state string", () => {
    const { stdout, status } = cli("scramble");
    assert.equal(status, 0);
    assert.doesNotThrow(() => parse(stdout));
  });

  it("is reproducible with a seed and varies without one", () => {
    assert.equal(cli("scramble", "--seed", "5").stdout, cli("scramble", "--seed", "5").stdout);
    assert.notEqual(cli("scramble", "--seed", "5").stdout, cli("scramble", "--seed", "6").stdout);
  });

  it("honours -n", () => {
    const { stdout } = cli("scramble", "-n", "1", "--seed", "3");
    const state = parse(stdout);
    assert.ok(!isSolved(state));
  });

  it("rejects a non-numeric count", () => {
    assert.equal(cli("scramble", "-n", "many").status, 2);
  });
});

describe("table", () => {
  it("prints the OEIS A079762 histogram", () => {
    const { stdout, status } = cli("table");
    assert.equal(status, 0);
    assert.match(
      stdout,
      /^1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508, 930588, 1350852, 782536, 90280, 276$/m,
    );
    assert.match(stdout, /Total 3,674,160, maximum distance 14/);
  });
});

describe("bench", () => {
  it("emits exactly one JSON object with all four algorithms", () => {
    const { stdout, status } = cli(
      "bench",
      "--json",
      "-n",
      "3",
      "--min-ms",
      "1",
      "--workers",
      "2",
    );
    assert.equal(status, 0);
    const report = JSON.parse(stdout);
    assert.equal(report.language, "JavaScript");
    assert.equal(typeof report.version, "string");
    assert.equal(typeof report.build, "string");
    assert.deepEqual(
      report.algorithms.map(({ name }) => name),
      ["bfs", "ida", "table", "table-par"],
    );
    for (const entry of report.algorithms) {
      for (const field of ["setup_ms", "avg_ms", "worst_ms", "peak_mb"]) {
        assert.equal(typeof entry[field], "number", `${entry.name}.${field}`);
      }
      assert.equal(entry.optimal, true, `${entry.name} was not optimal`);
    }
    assert.equal(report.algorithms[0].setup_ms, 0, "bfs needs no setup");
    assert.equal(report.algorithms.at(-1).workers, 2);
  });
});

describe("usage", () => {
  it("prints help on request", () => {
    const { stdout, status } = cli("--help");
    assert.equal(status, 0);
    assert.match(stdout, /Usage:/);
  });

  it("exits 2 with usage when given nothing", () => {
    assert.equal(cli().status, 2);
  });

  it("exits 2 on an unknown command", () => {
    assert.equal(cli("levitate").status, 2);
  });
});
