/**
 * The three algorithms behind one interface, so callers can compare them.
 *
 * Each is a plain function from a cube state to a move sequence. All three
 * return *shortest* solutions -- they differ only in what they spend to get
 * there:
 *
 * | name    | setup                | per solve        |
 * |---------|----------------------|------------------|
 * | `bfs`   | none                 | moderate         |
 * | `ida`   | cheap (two 8 KB dbs) | slowest          |
 * | `table` | one 3.7 MB search    | effectively free |
 *
 * @module solvers
 */

import * as ida from "./ida.js";
import * as search from "./search.js";
import { DistanceTable } from "./table.js";

/** @typedef {"bfs" | "ida" | "table"} Algorithm */

/** @type {readonly Algorithm[]} */
export const ALGORITHMS = Object.freeze(["bfs", "ida", "table"]);

/** @type {Readonly<Record<Algorithm, string>>} */
export const DESCRIPTIONS = Object.freeze({
  bfs: "bidirectional breadth-first search, no precomputation",
  ida: "iterative-deepening A* with pattern databases",
  table: "lookup in the full distance table",
});

/**
 * A solver: a cube state in, a shortest move sequence out.
 * @callback Solver
 * @param {Uint8Array} state
 * @returns {string}
 */

/**
 * Build a solver.
 * @param {Algorithm} algorithm
 * @param {DistanceTable} [table] required for `"table"`
 * @returns {Solver}
 */
export function makeSolver(algorithm, table) {
  switch (algorithm) {
    case "bfs":
      return (state) => search.solve(state);
    case "ida":
      return (state) => ida.solve(state);
    case "table":
      if (!(table instanceof DistanceTable)) {
        throw new TypeError("the table algorithm needs a DistanceTable");
      }
      return (state) => table.solve(state);
    default:
      throw new TypeError(`unknown algorithm: ${JSON.stringify(algorithm)}`);
  }
}
