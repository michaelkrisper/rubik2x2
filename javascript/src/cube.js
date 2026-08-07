/**
 * The 2x2x2 pocket cube, modelled as 24 stickers.
 *
 * A state is a `Uint8Array` of 24 sticker colour ids, one per facelet, ordered
 * side by side: UP, FRONT, DOWN, BACK, LEFT, RIGHT. Within a side the four
 * stickers are row-major:
 *
 * ```
 * -------
 * |0  1 |
 * |2  3 |
 * -------
 * ```
 *
 * so the flat index is `side * 4 + row * 2 + column`.
 *
 * Every move is a permutation applied as `next[i] = state[PERM[i]]`. The tables
 * are checked rather than trusted: test/cube.test.js pins the algebra they must
 * obey (order four, inverses cancel, opposite faces commute), and building the
 * distance table reproduces OEIS A079762 exactly, which no wrong table could do.
 *
 * This module is the readable model and nothing else -- no search, no display.
 * `coords.js` re-encodes states for the solvers, `display.js` draws them.
 *
 * @module cube
 */

/**
 * A cube state: 24 sticker colour ids, values 0..5.
 * @typedef {Uint8Array} State
 */

/**
 * A move sequence, one character per quarter turn, e.g. `"lFtL"`.
 * @typedef {string} Moves
 */

/** A sticker arrangement no sequence of turns can reach from a solved cube. */
export class UnsolvableError extends Error {
  /** @param {string} message */
  constructor(message) {
    super(message);
    this.name = "UnsolvableError";
  }
}

export const UP = 0;
export const FRONT = 1;
export const DOWN = 2;
export const BACK = 3;
export const LEFT = 4;
export const RIGHT = 5;

/** Colour letter per colour id: red, white, blue, yellow, purple, green. */
export const COLOR_CHARS = "rwbypg";

const [R, W, B, Y, P, G] = [0, 1, 2, 3, 4, 5];

/** @type {State} Solved cube in the standard orientation: yellow up, red front. */
export const SOLVED = Uint8Array.from([Y, R, W, P, B, G].flatMap((color) => [color, color, color, color]));

/**
 * Move notation. Lowercase and uppercase are inverse quarter turns of one face:
 * `l`/`L` left column down/up, `r`/`R` right column down/up, `t`/`T` top layer
 * left/right, `b`/`B` bottom layer left/right, `f`/`F` front face cw/ccw,
 * `a`/`A` back face cw/ccw.
 *
 * @type {Readonly<Record<string, Uint8Array>>}
 */
export const MOVES = Object.freeze(
  Object.fromEntries(
    Object.entries({
      l: [12, 1, 14, 3, 0, 5, 2, 7, 4, 9, 6, 11, 8, 13, 10, 15, 18, 16, 19, 17, 20, 21, 22, 23],
      L: [4, 1, 6, 3, 8, 5, 10, 7, 12, 9, 14, 11, 0, 13, 2, 15, 17, 19, 16, 18, 20, 21, 22, 23],
      r: [0, 13, 2, 15, 4, 1, 6, 3, 8, 5, 10, 7, 12, 9, 14, 11, 16, 17, 18, 19, 21, 23, 20, 22],
      R: [0, 5, 2, 7, 4, 9, 6, 11, 8, 13, 10, 15, 12, 1, 14, 3, 16, 17, 18, 19, 22, 20, 23, 21],
      t: [2, 0, 3, 1, 20, 21, 6, 7, 8, 9, 10, 11, 12, 13, 17, 16, 4, 5, 18, 19, 15, 14, 22, 23],
      T: [1, 3, 0, 2, 16, 17, 6, 7, 8, 9, 10, 11, 12, 13, 21, 20, 15, 14, 18, 19, 4, 5, 22, 23],
      b: [0, 1, 2, 3, 4, 5, 22, 23, 9, 11, 8, 10, 19, 18, 14, 15, 16, 17, 6, 7, 20, 21, 13, 12],
      B: [0, 1, 2, 3, 4, 5, 18, 19, 10, 8, 11, 9, 23, 22, 14, 15, 16, 17, 13, 12, 20, 21, 6, 7],
      f: [0, 1, 19, 17, 6, 4, 7, 5, 22, 20, 10, 11, 12, 13, 14, 15, 16, 8, 18, 9, 2, 21, 3, 23],
      F: [0, 1, 20, 22, 5, 7, 4, 6, 17, 19, 10, 11, 12, 13, 14, 15, 16, 3, 18, 2, 9, 21, 8, 23],
      a: [18, 16, 2, 3, 4, 5, 6, 7, 8, 9, 23, 21, 13, 15, 12, 14, 10, 17, 11, 19, 20, 0, 22, 1],
      A: [21, 23, 2, 3, 4, 5, 6, 7, 8, 9, 16, 18, 14, 12, 15, 13, 1, 17, 0, 19, 20, 11, 22, 10],
    }).map(([move, permutation]) => [move, Uint8Array.from(permutation)]),
  ),
);

/**
 * The quarter-turn generators the solvers use. A fourth face would be redundant:
 * turning a face equals turning the opposite face plus a whole-cube rotation.
 * Inverses sit at adjacent indices, which `ida.js` uses to prune, so keep them
 * paired.
 */
export const SOLVE_MOVES = "lLtTfF";

/**
 * Facelets of the corner {@link SOLVE_MOVES} leaves in place (down-back-right).
 * Holding one corner still is what collapses the 24-fold rotation symmetry.
 */
export const FIXED_CORNER = Object.freeze([11, 13, 23]);

/** Whole-cube rotations, used to enumerate the 24 ways of holding one state. */
const ROTATION_GENERATORS = [
  [12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 18, 16, 19, 17, 21, 23, 20, 22],
  [2, 0, 3, 1, 20, 21, 22, 23, 9, 11, 8, 10, 19, 18, 17, 16, 4, 5, 6, 7, 15, 14, 13, 12],
  [18, 16, 19, 17, 6, 4, 7, 5, 22, 20, 23, 21, 13, 15, 12, 14, 10, 8, 11, 9, 2, 0, 3, 1],
];

function rotationGroup() {
  const identity = Array.from({ length: 24 }, (_, index) => index);
  const seen = new Map([[identity.join(","), identity]]);
  const frontier = [identity];
  while (frontier.length > 0) {
    const current = frontier.pop();
    for (const generator of ROTATION_GENERATORS) {
      const composed = generator.map((index) => current[index]);
      const key = composed.join(",");
      if (!seen.has(key)) {
        seen.set(key, composed);
        frontier.push(composed);
      }
    }
  }
  return [...seen.values()].map((rotation) => Uint8Array.from(rotation));
}

/** The 24 ways of holding a cube. Checked in test/cube.test.js. */
export const ROTATIONS = Object.freeze(rotationGroup());

/**
 * Turn one face.
 * @param {State} state
 * @param {string} move one of the twelve keys of {@link MOVES}
 * @returns {State}
 */
export function applyMove(state, move) {
  const permutation = MOVES[move];
  if (permutation === undefined) throw new Error(`unknown move: ${JSON.stringify(move)}`);
  const next = new Uint8Array(24);
  for (let i = 0; i < 24; i++) next[i] = state[permutation[i]];
  return next;
}

/**
 * Turn faces in sequence.
 * @param {State} state
 * @param {Moves} moves
 * @returns {State}
 */
export function applyMoves(state, moves) {
  let current = state;
  for (const move of moves) current = applyMove(current, move);
  return current;
}

/**
 * Undo a sequence: reverse the order, flip the case of every turn.
 * @param {Moves} moves
 * @returns {Moves}
 */
export function invert(moves) {
  let inverted = "";
  for (let i = moves.length - 1; i >= 0; i--) {
    const move = moves[i];
    inverted += move === move.toLowerCase() ? move.toUpperCase() : move.toLowerCase();
  }
  return inverted;
}

/**
 * All 24 whole-cube rotations of a state, as state strings.
 * @param {State} state
 * @returns {Set<string>}
 */
export function orientations(state) {
  const result = new Set();
  for (const rotation of ROTATIONS) {
    const rotated = new Uint8Array(24);
    for (let i = 0; i < 24; i++) rotated[i] = state[rotation[i]];
    result.add(toString(rotated));
  }
  return result;
}

/**
 * True when every face shows a single colour.
 * @param {State} state
 */
export function isSolved(state) {
  for (let side = 0; side < 24; side += 4) {
    const color = state[side];
    if (state[side + 1] !== color || state[side + 2] !== color || state[side + 3] !== color) {
      return false;
    }
  }
  return true;
}

/**
 * Read 24 colour letters in side order UP FRONT DOWN BACK LEFT RIGHT.
 * @param {string} text
 * @returns {State}
 * @throws {TypeError} on anything that is not 24 letters of `rwbypg`, four of each
 */
export function parse(text) {
  const letters = String(text).trim().toLowerCase();
  if (letters.length !== 24) {
    throw new TypeError(`expected 24 colour letters, got ${letters.length}`);
  }
  const state = new Uint8Array(24);
  const counts = new Uint8Array(6);
  for (let i = 0; i < 24; i++) {
    const color = COLOR_CHARS.indexOf(letters[i]);
    if (color < 0) {
      throw new TypeError(`invalid colour letter ${JSON.stringify(letters[i])} (allowed: ${COLOR_CHARS})`);
    }
    state[i] = color;
    counts[color] += 1;
  }
  for (let color = 0; color < 6; color++) {
    if (counts[color] !== 4) {
      throw new TypeError(`colour '${COLOR_CHARS[color]}' appears ${counts[color]} times, expected 4`);
    }
  }
  return state;
}

/**
 * The 24 colour letters of a state. Inverse of {@link parse}.
 * @param {State} state
 * @returns {string}
 */
export function toString(state) {
  let text = "";
  for (const color of state) text += COLOR_CHARS[color];
  return text;
}

/**
 * A seedable pseudo-random generator, so `scramble --seed` is reproducible.
 * mulberry32: 32 bits of state, good enough to pick moves with.
 * @param {number} [seed]
 * @returns {() => number} a function yielding floats in [0, 1)
 */
export function randomSource(seed) {
  if (seed === undefined) return Math.random;
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let t = Math.imul(state ^ (state >>> 15), 1 | state);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/**
 * Apply `count` random moves, each reaching a state not seen before.
 * @param {State} state
 * @param {number} count
 * @param {() => number} [random]
 * @returns {State}
 */
export function scramble(state, count, random = Math.random) {
  const names = Object.keys(MOVES);
  const seen = new Set([toString(state)]);
  let current = state;
  let remaining = count;
  while (remaining > 0) {
    const candidate = applyMove(current, names[Math.floor(random() * names.length)]);
    const key = toString(candidate);
    if (!seen.has(key)) {
      seen.add(key);
      current = candidate;
      remaining -= 1;
    }
  }
  return current;
}
