/**
 * Bidirectional breadth-first search: the solver that needs no setup.
 *
 * Two frontiers grow a level at a time -- one from the scramble, one from the
 * goal -- and the search stops where they touch. God's number for a pocket cube
 * is 14 quarter turns and the level sizes peak at 1,350,852 states at depth 11,
 * so a one-sided search would have to walk most of the space. Meeting in the
 * middle needs depth 7 from each side, where only 33,058 states live.
 *
 * Expanding by whole levels is what makes the result optimal: when the frontiers
 * meet, every shorter pairing has already been tried, so the first meeting point
 * is a shortest solution -- no heuristic and no tie-breaking required.
 *
 * Paths are not stored as strings. Each visited state records
 * `parent * 6 + moveIndex`, one number, and the move sequence is reconstructed
 * by walking back to the root; a search that touches a hundred thousand states
 * would otherwise spend most of its time concatenating strings.
 *
 * @module search
 */

import { N_ORIENTATIONS, SOLVED_INDEX, encode, wideTables } from "./coords.js";
import { UnsolvableError, invert } from "./cube.js";

/** God's number for the pocket cube in the quarter-turn metric. */
export const MAX_DEPTH = 14;

const ROOT = -1;

/**
 * Walk a `parent * 6 + moveIndex` chain back to its root and read it forwards.
 * @param {Map<number, number>} seen
 * @param {number} node
 * @param {import("./coords.js").MoveTable[]} tables
 * @returns {string}
 */
function pathTo(seen, node, tables) {
  const moves = [];
  for (let step = seen.get(node); step !== ROOT; step = seen.get(node)) {
    moves.push(tables[step % 6].move);
    node = (step - (step % 6)) / 6;
  }
  return moves.reverse().join("");
}

/**
 * Return a shortest move sequence solving `state`; empty if it is already solved.
 * @param {Uint8Array} state
 * @param {number} [maxDepth]
 * @returns {string}
 * @throws {UnsolvableError} when no sequence of turns within `maxDepth` reaches it
 */
export function solve(state, maxDepth = MAX_DEPTH) {
  const start = encode(state);
  if (start === SOLVED_INDEX) return "";

  const tables = wideTables();
  const width = N_ORIENTATIONS;

  /** @type {Map<number, number>} */
  const forward = new Map([[start, ROOT]]);
  /** @type {Map<number, number>} */
  const backward = new Map([[SOLVED_INDEX, ROOT]]);
  let forwardLevel = [start];
  let backwardLevel = [SOLVED_INDEX];

  for (let depth = 0; depth < maxDepth; depth++) {
    // Always grow the cheaper side, which keeps both halves shallow.
    const fromGoal = backwardLevel.length < forwardLevel.length;
    const level = fromGoal ? backwardLevel : forwardLevel;
    const seen = fromGoal ? backward : forward;
    const other = fromGoal ? forward : backward;

    const following = [];
    for (const node of level) {
      const quotient = (node - (node % width)) / width;
      const remainder = node % width;
      for (let position = 0; position < 6; position++) {
        const { permutation, orientation } = tables[position];
        const child = permutation[quotient] + orientation[remainder];
        if (seen.has(child)) continue;
        if (other.has(child)) {
          seen.set(child, node * 6 + position);
          const here = pathTo(seen, child, tables);
          const there = pathTo(other, child, tables);
          return fromGoal ? there + invert(here) : here + invert(there);
        }
        seen.set(child, node * 6 + position);
        following.push(child);
      }
    }

    if (fromGoal) backwardLevel = following;
    else forwardLevel = following;
    if (forwardLevel.length === 0 || backwardLevel.length === 0) break;
  }

  throw new UnsolvableError(`no solution within ${maxDepth} moves`);
}
