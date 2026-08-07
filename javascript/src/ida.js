/**
 * Korf's IDA* with pattern databases -- the textbook optimal cube solver.
 *
 * Iterative-deepening A* runs depth-first passes with a rising limit on `g + h`,
 * where `h` is a lower bound on the moves still needed. Korf's contribution was
 * to get `h` from *pattern databases*: exact distances for a relaxed version of
 * the puzzle, precomputed once.
 *
 * Here the relaxations fall out of the encoding for free. A move acts on the
 * permutation coordinate and the orientation coordinate independently, so each
 * coordinate on its own is a genuine quotient of the state graph -- solving the
 * cube necessarily solves both projections. Their exact distances (5040 and 729
 * entries, computed in milliseconds) are therefore admissible, and the larger of
 * the two is the better bound.
 *
 * This is here for comparison, not because it wins. On a 2x2x2 it loses to plain
 * bidirectional search, and measurably so: both databases top out at distance 8
 * while real positions reach 14, leaving the bound too loose to prune well.
 * Pattern databases pay off when a puzzle has more pieces than you can afford to
 * track and you keep only some of them; a pocket cube is nothing *but* the
 * pieces the databases already cover.
 *
 * @module ida
 */

import {
  N_ORIENTATIONS,
  N_PERMUTATIONS,
  SOLVED_INDEX,
  encode,
  moveTables,
  wideTables,
} from "./coords.js";
import { UnsolvableError } from "./cube.js";
import { MAX_DEPTH } from "./search.js";

/**
 * Exact distance-to-solved within one coordinate, stored as `distance + 1` so
 * that 0 can mean "not yet reached".
 * @param {Int32Array[]} tables
 * @param {number} size
 * @returns {Uint8Array}
 */
function project(tables, size) {
  const distances = new Uint8Array(size);
  distances[0] = 1;
  let level = [0];
  for (let depth = 1; level.length > 0; depth++) {
    const following = [];
    for (const node of level) {
      for (const table of tables) {
        const child = table[node];
        if (distances[child] === 0) {
          distances[child] = depth + 1;
          following.push(child);
        }
      }
    }
    level = following;
  }
  return distances;
}

/** Exact distances for the two coordinate projections of the cube. */
export class PatternDatabases {
  /** @type {PatternDatabases | null} */
  static #cached = null;

  /**
   * @param {Uint8Array} permutation distance + 1 per permutation rank
   * @param {Uint8Array} orientation distance + 1 per orientation rank
   */
  constructor(permutation, orientation) {
    this.permutation = permutation;
    this.orientation = orientation;
    Object.freeze(this);
  }

  /** Both databases; cached, since they are the same on every call. */
  static build() {
    const tables = moveTables();
    PatternDatabases.#cached ??= new PatternDatabases(
      project(
        tables.map(({ permutation }) => permutation),
        N_PERMUTATIONS,
      ),
      project(
        tables.map(({ orientation }) => orientation),
        N_ORIENTATIONS,
      ),
    );
    return PatternDatabases.#cached;
  }

  /**
   * Admissible lower bound on the moves left from an encoded state.
   * @param {number} index
   * @returns {number}
   */
  bound(index) {
    const quotient = Math.trunc(index / N_ORIENTATIONS);
    return Math.max(this.permutation[quotient], this.orientation[index % N_ORIENTATIONS]) - 1;
  }

  /** The largest bound each database can ever return. @returns {[number, number]} */
  get reach() {
    return [Math.max(...this.permutation) - 1, Math.max(...this.orientation) - 1];
  }
}

/**
 * Shortest solution by iterative-deepening A*. Optimal, but see the module docs.
 * @param {Uint8Array} state
 * @param {number} [maxDepth]
 * @returns {string}
 */
export function solve(state, maxDepth = MAX_DEPTH) {
  const index = encode(state);
  if (index === SOLVED_INDEX) return "";

  const databases = PatternDatabases.build();
  const permutationDb = databases.permutation;
  const orientationDb = databases.orientation;
  const width = N_ORIENTATIONS;
  const tables = wideTables();
  const permutations = tables.map((table) => table.permutation);
  const orientations = tables.map((table) => table.orientation);
  /** @type {number[]} */
  const path = [];

  /**
   * @param {number} node
   * @param {number} depth moves already made
   * @param {number} limit budget for `depth + bound`
   * @param {number} last index of the move that produced `node`
   */
  const descend = (node, depth, limit, last) => {
    const quotient = (node - (node % width)) / width;
    const remainder = node % width;
    const bound = Math.max(permutationDb[quotient], orientationDb[remainder]) - 1;
    if (depth + bound > limit) return false;
    if (node === SOLVED_INDEX) return true;
    for (let position = 0; position < 6; position++) {
      // SOLVE_MOVES pairs each turn with its inverse at adjacent indices, so
      // `position ^ 1` is the move that would undo the previous one.
      if ((position ^ 1) === last) continue;
      path.push(position);
      if (descend(permutations[position][quotient] + orientations[position][remainder], depth + 1, limit, position)) {
        return true;
      }
      path.pop();
    }
    return false;
  };

  for (let limit = databases.bound(index); limit <= maxDepth; limit++) {
    path.length = 0;
    if (descend(index, 0, limit, -1)) return path.map((position) => tables[position].move).join("");
  }

  throw new UnsolvableError(`no solution within ${maxDepth} moves`);
}
