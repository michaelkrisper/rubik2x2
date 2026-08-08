/**
 * Compare the solvers, setup cost included.
 *
 * Per-solve time alone flatters the lookup table, because it hides the
 * exhaustive search that produced it. What actually decides which algorithm to
 * reach for is total time for the workload you have -- so this reports setup,
 * per-solve, and the break-even point where paying for the table starts to win.
 *
 * The scrambles come from `bench/scrambles.txt`, not from a random generator, so
 * every language implementation measures exactly the same work.
 *
 * @module bench
 */

import { readFileSync } from "node:fs";
import { availableParallelism } from "node:os";
import { setFlagsFromString } from "node:v8";
import { runInNewContext } from "node:vm";

import { N_STATES, encode, wideTables } from "./coords.js";
import { applyMoves, isSolved, parse } from "./cube.js";
import { PatternDatabases } from "./ida.js";
import { retainedBytes } from "./search.js";
import { makeSolver } from "./solvers.js";
import { DistanceTable } from "./table.js";

/** An antipodal position: 14 quarter turns from solved, the hardest there is. */
export const HARDEST = "wppryygrygrwgprbbbwpbwyg";

/** The shared scramble set, two directories up from this module. */
export const DEFAULT_SCRAMBLES = new URL("../../bench/scrambles.txt", import.meta.url);

/**
 * Force a full garbage collection, so a heap reading means something.
 * `--expose-gc` is flipped on for the length of one call rather than demanded
 * on the command line, which would change how the benchmarked code is run.
 * @type {() => void}
 */
const collect = (() => {
  try {
    setFlagsFromString("--expose-gc");
    const gc = runInNewContext("gc");
    setFlagsFromString("--no-expose-gc");
    return gc;
  } catch {
    return () => {};
  }
})();

/** Bytes the process holds in the JS heap and in array buffers. */
const held = () => {
  const { heapUsed, arrayBuffers } = process.memoryUsage();
  return heapUsed + arrayBuffers;
};

/**
 * Mean wall time of one call, in milliseconds. Repeats until the clock has
 * something to say -- a table solve is microseconds, and timing one of those
 * once measures the timer.
 * @param {() => unknown} action
 * @param {{minMs?: number, maxRepeats?: number}} [options]
 * @returns {number}
 */
export function measure(action, { minMs = 300, maxRepeats = 1e9 } = {}) {
  const started = performance.now();
  let repeats = 0;
  let elapsed = 0;
  do {
    action();
    repeats += 1;
    elapsed = performance.now() - started;
  } while (elapsed < minMs && repeats < maxRepeats);
  return elapsed / repeats;
}

/**
 * Peak extra memory a solve holds, best effort: what the algorithm keeps
 * resident (its precomputed tables) plus the heap growth of one hard solve.
 * @param {() => unknown} action
 * @param {number} residentBytes
 * @returns {number} megabytes
 */
function peakMegabytes(action, residentBytes) {
  collect();
  const before = held();
  action();
  const transient = Math.max(0, held() - before);
  return (residentBytes + transient) / 1e6;
}

/**
 * Read scrambles, one state string per line.
 * @param {string | URL} [source]
 * @param {number} [limit]
 * @returns {Uint8Array[]}
 */
export function loadScrambles(source = DEFAULT_SCRAMBLES, limit) {
  const lines = readFileSync(source, "utf8")
    .split("\n")
    .map((line) => line.trim())
    .filter((line) => line.length > 0);
  return (limit === undefined ? lines : lines.slice(0, limit)).map(parse);
}

/**
 * One algorithm's numbers.
 * @typedef {{name: string, setup_ms: number, avg_ms: number, worst_ms: number,
 *            peak_mb: number, optimal: boolean, workers?: number}} Result
 */

const round = (value, digits = 3) => Number(value.toFixed(digits));

/**
 * Run the whole comparison.
 * @param {{scrambles?: string | URL, samples?: number, workers?: number, minMs?: number,
 *          onProgress?: (message: string) => void}} [options]
 * @returns {Promise<{samples: number, report: {language: string, version: string,
 *          build: string, algorithms: Result[]}}>}
 */
export async function run({ scrambles, samples, workers, minMs = 1000, onProgress } = {}) {
  const note = onProgress ?? (() => {});
  const states = loadScrambles(scrambles, samples);
  const hardest = parse(HARDEST);

  // Shared by every algorithm, so it belongs to none of their setup budgets.
  const sharedMs = measure(wideTables, { maxRepeats: 1 });
  note(`shared move tables: ${sharedMs.toFixed(1)} ms`);

  // Each setup is timed on its first call, before any cache is warm.
  const idaSetupMs = measure(() => PatternDatabases.build(), { maxRepeats: 1 });
  const tableSetupMs = measure(() => DistanceTable.build(), { maxRepeats: 1 });
  const table = DistanceTable.build();

  const parallelStarted = performance.now();
  const parallel = await DistanceTable.buildParallel({ workers });
  const parallelSetupMs = performance.now() - parallelStarted;
  const workerCount = Number(parallel.backend.split("/")[1]);

  const databases = PatternDatabases.build();
  const distances = states.map((state) => table.distance(encode(state)));

  /** @type {Result[]} */
  const results = [];
  const plans = [
    // `resident` is read after the algorithm has run, not before: bfs allocates
    // its mark array on the first solve and then keeps it, so asking up front
    // would report nothing.
    { name: "bfs", algorithm: "bfs", setupMs: 0, resident: retainedBytes },
    {
      name: "ida",
      algorithm: "ida",
      setupMs: idaSetupMs,
      resident: () => databases.permutation.length + databases.orientation.length,
    },
    { name: "table", algorithm: "table", setupMs: tableSetupMs, resident: () => table.byteLength },
    {
      name: "table-par",
      algorithm: "table",
      setupMs: parallelSetupMs,
      resident: () => table.byteLength,
      workers: workerCount,
    },
  ];

  for (const plan of plans) {
    note(`timing ${plan.name}...`);
    const solve = makeSolver(plan.algorithm, plan.name === "table-par" ? parallel : table);
    const optimal = states.every((state, index) => {
      const solution = solve(state);
      return solution.length === distances[index] && isSolved(applyMoves(state, solution));
    });

    // Only the build is parallelised, and the two builds produce byte-identical
    // tables (test/table.test.js), so table-par inherits table's solve figures
    // rather than re-measuring the same code and reporting the timing noise.
    const solving =
      plan.name === "table-par"
        ? results.at(-1)
        : {
            avg_ms: round(
              measure(
                () => {
                  for (const state of states) solve(state);
                },
                { minMs },
              ) / states.length,
            ),
            worst_ms: round(measure(() => solve(hardest), { minMs })),
            peak_mb: round(peakMegabytes(() => solve(hardest), plan.resident()), 2),
          };

    results.push({
      name: plan.name,
      setup_ms: round(plan.setupMs, 1),
      avg_ms: solving.avg_ms,
      worst_ms: solving.worst_ms,
      peak_mb: solving.peak_mb,
      optimal,
      ...(plan.workers === undefined ? {} : { workers: plan.workers }),
    });
  }

  return {
    samples: states.length,
    report: {
      language: "JavaScript",
      version: process.versions.node,
      build: `Node.js ${process.version}, V8 ${process.versions.v8}, JIT`,
      algorithms: results,
    },
  };
}

/**
 * The human-readable report: the same numbers plus what they imply.
 * @param {Awaited<ReturnType<typeof run>>["report"]} report
 * @param {number} samples
 * @returns {string}
 */
export function format(report, samples) {
  const lines = [
    `rubik2x2 benchmark -- ${samples} scrambles from bench/scrambles.txt plus one antipodal position`,
    `${N_STATES.toLocaleString("en-US")} reachable states, maximum distance 14 quarter turns`,
    `${report.build}, ${availableParallelism()} logical cores`,
    "",
  ];

  const header =
    "algorithm".padEnd(11) +
    "setup ms".padStart(11) +
    "avg ms".padStart(10) +
    "worst ms".padStart(11) +
    "peak MB".padStart(10) +
    "optimal".padStart(9);
  lines.push(header, "-".repeat(header.length));
  for (const result of report.algorithms) {
    lines.push(
      result.name.padEnd(11) +
        result.setup_ms.toFixed(1).padStart(11) +
        result.avg_ms.toFixed(3).padStart(10) +
        result.worst_ms.toFixed(3).padStart(11) +
        result.peak_mb.toFixed(2).padStart(10) +
        (result.optimal ? "yes" : "NO").padStart(9),
    );
  }

  lines.push("", "total time for a workload (setup + solves), seconds");
  const counts = [1, 10, 100, 1000, 10000];
  lines.push("algorithm".padEnd(11) + counts.map((count) => count.toLocaleString("en-US").padStart(12)).join(""));
  lines.push("-".repeat(11 + 12 * counts.length));
  for (const result of report.algorithms) {
    const row = counts
      .map((count) => ((result.setup_ms + count * result.avg_ms) / 1000).toFixed(2).padStart(12))
      .join("");
    lines.push(result.name.padEnd(11) + row);
  }

  const byName = Object.fromEntries(report.algorithms.map((result) => [result.name, result]));
  const searchers = [byName.bfs, byName.ida];
  const best = searchers.reduce((one, other) => (one.avg_ms <= other.avg_ms ? one : other));
  const gain = best.avg_ms - byName.table.avg_ms;
  const reach = Math.max(...PatternDatabases.build().reach);
  lines.push("", `Fastest without setup: ${best.name}.`);
  if (gain > 0) {
    const crossover = Math.trunc((byName.table.setup_ms - best.setup_ms) / gain) + 1;
    lines.push(
      `The table overtakes it at about ${crossover.toLocaleString("en-US")} solves; below that,`,
      "building it costs more than it saves, above it nothing else comes close.",
    );
  }
  lines.push(
    `IDA*'s pattern databases reach only distance ${reach} of 14 -- too loose to prune`,
    "a puzzle this small, which is why it never dominates the table and only just",
    "holds its own against a plain meet-in-the-middle search.",
  );
  return lines.join("\n");
}
