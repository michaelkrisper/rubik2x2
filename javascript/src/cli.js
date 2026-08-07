#!/usr/bin/env node
/**
 * Command line interface.
 *
 * ```
 * rubik2x2 solve <state> [--algorithm bfs|ida|table]
 * rubik2x2 scramble [-n N] [--seed S]
 * rubik2x2 show <state> [--color|--no-color]
 * rubik2x2 table
 * rubik2x2 bench [--json] [--scrambles FILE] [-n N]
 * ```
 *
 * `solve` prints nothing but the move sequence on stdout, so it pipes.
 *
 * @module cli
 */

import { readFileSync } from "node:fs";
import { parseArgs } from "node:util";

import * as bench from "./bench.js";
import { N_STATES } from "./coords.js";
import {
  COLOR_CHARS,
  SOLVED,
  UnsolvableError,
  parse,
  randomSource,
  scramble,
  toString,
} from "./cube.js";
import { bestColor, render } from "./display.js";
import { ALGORITHMS, DESCRIPTIONS, makeSolver } from "./solvers.js";
import { DistanceTable } from "./table.js";

const USAGE = `rubik2x2 -- optimal 2x2x2 (pocket cube) solver

Usage:
  rubik2x2 solve <state> [--algorithm ${ALGORITHMS.join("|")}]
  rubik2x2 scramble [-n N] [--seed S]
  rubik2x2 show <state> [--color | --no-color]
  rubik2x2 table
  rubik2x2 bench [--json] [--scrambles FILE] [-n N]

Algorithms:
${ALGORITHMS.map((name) => `  ${name.padEnd(6)} ${DESCRIPTIONS[name]}`).join("\n")}

A state is 24 colour letters (${COLOR_CHARS}) in side order UP FRONT DOWN BACK
LEFT RIGHT, each face read row-major; "-" reads it from stdin instead.`;

const OPTIONS = /** @type {const} */ ({
  algorithm: { type: "string", default: "bfs" },
  n: { type: "string", short: "n" },
  seed: { type: "string" },
  json: { type: "boolean", default: false },
  scrambles: { type: "string" },
  workers: { type: "string" },
  "min-ms": { type: "string" }, // shorter timing windows, for smoke tests
  color: { type: "boolean", default: false },
  "no-color": { type: "boolean", default: false },
  help: { type: "boolean", short: "h", default: false },
});

const thousands = (value) => value.toLocaleString("en-US");

/**
 * @param {string | undefined} text a state string, or "-" for stdin
 * @returns {Uint8Array}
 */
function readState(text) {
  if (text === undefined) throw new TypeError("a 24-letter state is required");
  return parse(text === "-" ? readFileSync(0, "utf8") : text);
}

/** @param {string | undefined} text @param {number} fallback */
function readCount(text, fallback) {
  if (text === undefined) return fallback;
  const value = Number(text);
  if (!Number.isInteger(value) || value < 0) throw new TypeError(`not a whole number: ${text}`);
  return value;
}

/**
 * @param {string[]} argv
 * @returns {Promise<number>} process exit code
 */
export async function main(argv = process.argv.slice(2)) {
  let parsed;
  try {
    parsed = parseArgs({ args: argv, options: OPTIONS, allowPositionals: true });
  } catch (error) {
    console.error(`${error.message}\n\n${USAGE}`);
    return 2;
  }
  const { values, positionals } = parsed;
  const [command, argument] = positionals;

  if (values.help || command === undefined) {
    console.log(USAGE);
    return command === undefined && !values.help ? 2 : 0;
  }

  try {
    switch (command) {
      case "solve": {
        const state = readState(argument);
        const algorithm = /** @type {import("./solvers.js").Algorithm} */ (values.algorithm);
        if (!ALGORITHMS.includes(algorithm)) {
          throw new TypeError(`unknown algorithm ${JSON.stringify(algorithm)}, expected one of ${ALGORITHMS.join(", ")}`);
        }
        const table = algorithm === "table" ? DistanceTable.build() : undefined;
        const solution = makeSolver(algorithm, table)(state);
        if (solution) console.log(solution);
        else console.error("Already solved.");
        return 0;
      }

      case "scramble": {
        const seed = values.seed === undefined ? undefined : Number(values.seed);
        const state = scramble(SOLVED, readCount(values.n, 8), randomSource(seed));
        console.log(toString(state));
        return 0;
      }

      case "show": {
        const color = values["no-color"] ? "none" : values.color ? bestColor() : undefined;
        console.log(render(readState(argument), color));
        return 0;
      }

      case "table": {
        console.log(`Searching all ${thousands(N_STATES)} states...`);
        const started = performance.now();
        const table = DistanceTable.build((depth, count) =>
          console.log(`  depth ${String(depth).padStart(2)}: ${thousands(count).padStart(9)} states`),
        );
        const elapsed = (performance.now() - started) / 1000;
        const histogram = table.histogram();
        console.log(`\nBuilt in ${elapsed.toFixed(2)} s; ${(table.byteLength / 1e6).toFixed(1)} MB resident.`);
        console.log("States per distance (OEIS A079762):");
        console.log(histogram.join(", "));
        console.log(`\nTotal ${thousands(histogram.reduce((sum, count) => sum + count, 0))}, maximum distance ${histogram.length - 1}.`);
        return 0;
      }

      case "bench": {
        const { report, samples } = await bench.run({
          scrambles: values.scrambles,
          samples: readCount(values.n, undefined),
          workers: values.workers === undefined ? undefined : Number(values.workers),
          minMs: values["min-ms"] === undefined ? undefined : Number(values["min-ms"]),
          onProgress: values.json ? undefined : (message) => console.error(message),
        });
        console.log(values.json ? JSON.stringify(report) : bench.format(report, samples));
        return 0;
      }

      default:
        console.error(`Unknown command ${JSON.stringify(command)}.\n\n${USAGE}`);
        return 2;
    }
  } catch (error) {
    if (error instanceof UnsolvableError) {
      console.error(`No solution: ${error.message}`);
      return 1;
    }
    if (error instanceof TypeError) {
      console.error(`Invalid input: ${error.message}`);
      return 2;
    }
    throw error;
  }
}

if (import.meta.main) {
  process.exitCode = await main();
}
