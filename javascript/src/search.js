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
 * The visited set is not a `Map`. A cube state is already a dense integer below
 * `N_STATES`, so a `Uint16Array` subscripted by the state itself answers "seen,
 * by which side, how far in" in one load and records a child in one store -- no
 * hashing, no boxing, and nothing for the garbage collector to walk. Each mark
 * packs:
 *
 * | bits    | meaning                                     |
 * |---------|---------------------------------------------|
 * | `0..3`  | its distance from this frontier's root      |
 * | `4`     | which frontier claimed it                   |
 * | `5..15` | generation stamp                            |
 *
 * The array is 7.3 MB and lives at module scope, allocated once: clearing it
 * per solve would cost more than the search. The generation stamp is what makes
 * that reuse safe -- a solve bumps a counter and every mark left by an earlier
 * solve reads as unvisited, so a real clear happens once in 2,046 solves and
 * ages out old marks instead of paying a fill on every one. Sixteen-bit marks
 * halve the array against a 32-bit layout, and that cache footprint is the win:
 * every edge of the search loads one random word of it.
 *
 * Storing a *distance* rather than a parent state is what lets a mark fit in 16
 * bits at all: a parent needs 22. Distances are enough to rebuild the path,
 * because breadth-first marks are exact -- from a node at distance `d`, a
 * neighbour at `d - 1` under the same stamp and side bit is a parent, and there
 * are only six neighbours to try. That costs a few dozen lookups once per solve,
 * against a store saved on every one of some 150,000 edges.
 *
 * @module search
 */

import { N_ORIENTATIONS, N_STATES, SOLVED_INDEX, encode, wideTables } from "./coords.js";
import { UnsolvableError, invert } from "./cube.js";

/** God's number for the pocket cube in the quarter-turn metric. */
export const MAX_DEPTH = 14;

const DEPTH_BITS = 4; // depths never exceed 8, and MAX_DEPTH fits regardless
const DEPTH_MASK = (1 << DEPTH_BITS) - 1;
const SIDE_BIT = 1 << DEPTH_BITS;
const GEN_POS = DEPTH_BITS + 1;
const GEN_MASK = 0xffff & ~(SIDE_BIT | DEPTH_MASK);
const MAX_GEN = (0xffff >> GEN_POS) - 1;

/** @type {Uint16Array | null} */
let marks = null;
let generation = 0;

/**
 * The two frontiers and the level being built, rotated between rounds. Kept at
 * module scope with the marks, so a solve allocates nothing after the first.
 * @type {Uint32Array[]}
 */
let buffers = [new Uint32Array(4096), new Uint32Array(4096), new Uint32Array(4096)];

/** Bytes the search keeps alive between solves. */
export function retainedBytes() {
  const levels = buffers.reduce((total, buffer) => total + buffer.byteLength, 0);
  return (marks === null ? 0 : marks.byteLength) + levels;
}

/**
 * Begin a solve; yields the stamp its marks carry.
 * @returns {number}
 */
function stamp() {
  marks ??= new Uint16Array(N_STATES);
  if (generation >= MAX_GEN) {
    marks.fill(0);
    generation = 0;
  }
  generation += 1;
  return generation << GEN_POS;
}

/**
 * A level buffer with room for at least `wanted` entries. The contents are not
 * preserved -- a level is always filled from the start. Growth happens a handful
 * of times in a process and then never again.
 * @param {Uint32Array} buffer
 * @param {number} wanted
 * @returns {Uint32Array}
 */
function reserve(buffer, wanted) {
  if (wanted <= buffer.length) return buffer;
  let size = buffer.length;
  while (size < wanted) size *= 2;
  return new Uint32Array(size);
}

/**
 * The moves from `node`'s frontier root down to `node`, in root-first order.
 *
 * `tag` is the generation and side bit that frontier's marks carry. The move
 * reaching `node` from its parent is the inverse of the one that walks back to
 * it, and `SOLVE_MOVES` pairs every turn with its inverse at adjacent indices,
 * so `m` and `m ^ 1` undo each other.
 *
 * @param {number} node
 * @param {number} tag
 * @param {import("./coords.js").MoveTable[]} tables
 * @returns {string}
 */
function trail(node, tag, tables) {
  const seen = /** @type {Uint16Array} */ (marks);
  const moves = [];
  for (let depth = seen[node] & DEPTH_MASK; depth > 0; depth--) {
    const parent = tag | (depth - 1);
    const quotient = (node - (node % N_ORIENTATIONS)) / N_ORIENTATIONS;
    const remainder = node % N_ORIENTATIONS;
    for (let position = 0; position < 6; position++) {
      const table = tables[position ^ 1];
      const back = table.permutation[quotient] + table.orientation[remainder];
      if (seen[back] === parent) {
        moves.push(tables[position].move);
        node = back;
        break;
      }
    }
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
  const stampedAt = stamp();
  const seen = /** @type {Uint16Array} */ (marks);

  // The six move tables are hoisted into locals and the loop over them unrolled:
  // an indexed `tables[position].permutation` load in the innermost loop is what
  // V8 cannot keep monomorphic, and this loop runs for every edge of the search.
  const p0 = tables[0].permutation, o0 = tables[0].orientation;
  const p1 = tables[1].permutation, o1 = tables[1].orientation;
  const p2 = tables[2].permutation, o2 = tables[2].orientation;
  const p3 = tables[3].permutation, o3 = tables[3].orientation;
  const p4 = tables[4].permutation, o4 = tables[4].orientation;
  const p5 = tables[5].permutation, o5 = tables[5].orientation;

  seen[start] = stampedAt;
  seen[SOLVED_INDEX] = stampedAt | SIDE_BIT;

  let [forward, backward, following] = buffers;
  forward[0] = start;
  backward[0] = SOLVED_INDEX;
  let forwardCount = 1;
  let backwardCount = 1;
  // Each round grows only one side, so each carries its own level depth -- which
  // is exactly what its marks record.
  let forwardDepth = 0;
  let backwardDepth = 0;

  let solution = null;
  for (let round = 0; round < maxDepth; round++) {
    // Always grow the cheaper side, which keeps both halves shallow.
    const fromGoal = backwardCount < forwardCount;
    const level = fromGoal ? backward : forward;
    const levelCount = fromGoal ? backwardCount : forwardCount;
    const side = fromGoal ? SIDE_BIT : 0;
    // Every state reached this round carries the same mark, so the store in the
    // innermost loop is a constant.
    const reached = stampedAt | side | ((fromGoal ? backwardDepth : forwardDepth) + 1);

    let count = 0;
    let meeting = -1;
    let from = 0;
    let by = 0;

    // Six children per node at most, reserved once so the inner loop can store
    // without checking.
    following = reserve(following, levelCount * 6);

    for (let i = 0; i < levelCount && meeting < 0; i++) {
      const node = level[i];
      const remainder = node % width;
      const quotient = (node - remainder) / width;
      let child = 0;
      let mark = 0;

      child = p0[quotient] + o0[remainder];
      mark = seen[child];
      if ((mark & GEN_MASK) !== stampedAt) {
        seen[child] = reached;
        following[count++] = child;
      } else if ((mark & SIDE_BIT) !== side) {
        meeting = child; from = node; by = 0;
      }
      if (meeting < 0) {
        child = p1[quotient] + o1[remainder];
        mark = seen[child];
        if ((mark & GEN_MASK) !== stampedAt) {
          seen[child] = reached;
          following[count++] = child;
        } else if ((mark & SIDE_BIT) !== side) {
          meeting = child; from = node; by = 1;
        }
      }
      if (meeting < 0) {
        child = p2[quotient] + o2[remainder];
        mark = seen[child];
        if ((mark & GEN_MASK) !== stampedAt) {
          seen[child] = reached;
          following[count++] = child;
        } else if ((mark & SIDE_BIT) !== side) {
          meeting = child; from = node; by = 2;
        }
      }
      if (meeting < 0) {
        child = p3[quotient] + o3[remainder];
        mark = seen[child];
        if ((mark & GEN_MASK) !== stampedAt) {
          seen[child] = reached;
          following[count++] = child;
        } else if ((mark & SIDE_BIT) !== side) {
          meeting = child; from = node; by = 3;
        }
      }
      if (meeting < 0) {
        child = p4[quotient] + o4[remainder];
        mark = seen[child];
        if ((mark & GEN_MASK) !== stampedAt) {
          seen[child] = reached;
          following[count++] = child;
        } else if ((mark & SIDE_BIT) !== side) {
          meeting = child; from = node; by = 4;
        }
      }
      if (meeting < 0) {
        child = p5[quotient] + o5[remainder];
        mark = seen[child];
        if ((mark & GEN_MASK) !== stampedAt) {
          seen[child] = reached;
          following[count++] = child;
        } else if ((mark & SIDE_BIT) !== side) {
          meeting = child; from = node; by = 5;
        }
      }
    }

    if (meeting >= 0) {
      // One trail runs from each root to the meeting point; the move that joined
      // them belongs to whichever side was expanding.
      const here = trail(from, stampedAt | side, tables) + tables[by].move;
      const there = trail(meeting, stampedAt | (side ^ SIDE_BIT), tables);
      solution = fromGoal ? there + invert(here) : here + invert(there);
      break;
    }

    const spent = level;
    if (fromGoal) {
      backward = following;
      backwardCount = count;
      backwardDepth += 1;
    } else {
      forward = following;
      forwardCount = count;
      forwardDepth += 1;
    }
    following = spent;
    if (forwardCount === 0 || backwardCount === 0) break;
  }

  // The buffers rotate by reference, and `reserve` may have replaced one, so put
  // whatever the search ended up holding back for the next solve.
  buffers = [forward, backward, following];

  if (solution === null) throw new UnsolvableError(`no solution within ${maxDepth} moves`);
  return solution;
}
