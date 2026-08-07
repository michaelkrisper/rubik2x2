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

import { N_ORIENTATIONS, N_PERMUTATIONS, SOLVED_INDEX, encode, moveTables } from "./coords.js";
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
  // The state travels as its (permutation rank, orientation rank) pair rather
  // than one packed index, so the descent never divides: a child is two plain
  // table reads. The six move tables are hoisted into locals -- an indexed
  // `tables[position][rank]` load in the innermost loop is what V8 cannot keep
  // monomorphic -- and the loop over moves is unrolled against them. Children
  // are bounds-checked *before* recursing; at the fringe, where nearly every
  // child fails its bound, that trades six calls for six comparisons.
  const tables = moveTables();
  const p0 = tables[0].permutation, o0 = tables[0].orientation;
  const p1 = tables[1].permutation, o1 = tables[1].orientation;
  const p2 = tables[2].permutation, o2 = tables[2].orientation;
  const p3 = tables[3].permutation, o3 = tables[3].orientation;
  const p4 = tables[4].permutation, o4 = tables[4].orientation;
  const p5 = tables[5].permutation, o5 = tables[5].orientation;
  const path = new Int32Array(maxDepth + 1);
  let found = 0;

  /**
   * Expand the children of a node the caller has already bounds-checked.
   * `limit1` is the depth limit plus 1, matching the `distance + 1` stored in
   * the databases. `last` skips the inverse of the move that produced the node:
   * SOLVE_MOVES pairs each turn with its inverse at adjacent indices, so move
   * `m` undoes move `m ^ 1`.
   * @param {number} q permutation rank
   * @param {number} r orientation rank
   * @param {number} depth moves already made
   * @param {number} limit1 budget for `depth + bound`, plus 1
   * @param {number} last index of the move that produced the node
   */
  const descend = (q, r, depth, limit1, last) => {
    const depth1 = depth + 1;
    let cq, cr, pb, ob;
    if (last !== 1) {
      cq = p0[q];
      cr = o0[r];
      if (cq === 0 && cr === 0) return (path[depth] = 0), (found = depth1), true;
      pb = permutationDb[cq];
      ob = orientationDb[cr];
      if ((pb > ob ? pb : ob) + depth1 <= limit1) {
        path[depth] = 0;
        if (descend(cq, cr, depth1, limit1, 0)) return true;
      }
    }
    if (last !== 0) {
      cq = p1[q];
      cr = o1[r];
      if (cq === 0 && cr === 0) return (path[depth] = 1), (found = depth1), true;
      pb = permutationDb[cq];
      ob = orientationDb[cr];
      if ((pb > ob ? pb : ob) + depth1 <= limit1) {
        path[depth] = 1;
        if (descend(cq, cr, depth1, limit1, 1)) return true;
      }
    }
    if (last !== 3) {
      cq = p2[q];
      cr = o2[r];
      if (cq === 0 && cr === 0) return (path[depth] = 2), (found = depth1), true;
      pb = permutationDb[cq];
      ob = orientationDb[cr];
      if ((pb > ob ? pb : ob) + depth1 <= limit1) {
        path[depth] = 2;
        if (descend(cq, cr, depth1, limit1, 2)) return true;
      }
    }
    if (last !== 2) {
      cq = p3[q];
      cr = o3[r];
      if (cq === 0 && cr === 0) return (path[depth] = 3), (found = depth1), true;
      pb = permutationDb[cq];
      ob = orientationDb[cr];
      if ((pb > ob ? pb : ob) + depth1 <= limit1) {
        path[depth] = 3;
        if (descend(cq, cr, depth1, limit1, 3)) return true;
      }
    }
    if (last !== 5) {
      cq = p4[q];
      cr = o4[r];
      if (cq === 0 && cr === 0) return (path[depth] = 4), (found = depth1), true;
      pb = permutationDb[cq];
      ob = orientationDb[cr];
      if ((pb > ob ? pb : ob) + depth1 <= limit1) {
        path[depth] = 4;
        if (descend(cq, cr, depth1, limit1, 4)) return true;
      }
    }
    if (last !== 4) {
      cq = p5[q];
      cr = o5[r];
      if (cq === 0 && cr === 0) return (path[depth] = 5), (found = depth1), true;
      pb = permutationDb[cq];
      ob = orientationDb[cr];
      if ((pb > ob ? pb : ob) + depth1 <= limit1) {
        path[depth] = 5;
        if (descend(cq, cr, depth1, limit1, 5)) return true;
      }
    }
    return false;
  };

  const rootQ = Math.trunc(index / N_ORIENTATIONS);
  const rootR = index % N_ORIENTATIONS;
  for (let limit = databases.bound(index); limit <= maxDepth; limit++) {
    if (descend(rootQ, rootR, 0, limit + 1, -1)) {
      let solution = "";
      for (let step = 0; step < found; step++) solution += tables[path[step]].move;
      return solution;
    }
  }

  throw new UnsolvableError(`no solution within ${maxDepth} moves`);
}
