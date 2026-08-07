/**
 * Cubie coordinates: the compact state encoding every solver runs on.
 *
 * `cube.js` models a state as 24 stickers, which is what a human reads but not
 * what a search wants -- every move there allocates a fresh 24-byte array. A
 * pocket cube with one corner held still is described by far less: which of the
 * 7 free corner *pieces* sits in each of the 7 free *slots* (7! = 5040
 * arrangements) and how each is twisted (3^7, minus the constraint that the
 * twists sum to a multiple of 3, so 3^6 = 729). That is 5040 * 729 = 3,674,160
 * states -- exactly what an exhaustive search over stickers finds.
 *
 * The payoff is that a move acts on the two halves independently:
 *
 * ```
 * next.permutation[slot] = permutation[source[slot]]
 * next.orientation[slot] = orientation[source[slot]] + twist[slot]   (mod 3)
 * ```
 *
 * Neither half depends on the other, so a move is two lookups in tables of 5040
 * and 729 entries and a whole state is a single integer below 3,674,160. More
 * importantly, a dense bounded index is what makes `table.js` possible at all,
 * and what hands `ida.js` its pattern databases for free.
 *
 * @module coords
 */

import {
  FIXED_CORNER,
  SOLVE_MOVES,
  SOLVED,
  UnsolvableError,
  applyMove,
  orientations,
  parse,
} from "./cube.js";

/**
 * Each corner slot as its three facelets, listed in a consistent rotational
 * sense so that "twist" means the same thing at every slot. The order is
 * (x-face, y-face, z-face) where sign(x)*sign(y)*sign(z) === +1 and
 * (x-face, z-face, y-face) otherwise, which makes all eight triples agree in
 * handedness. Slot 7 is the corner the solve moves never touch.
 */
export const SLOTS = Object.freeze([
  [16, 0, 14], // left-up-back
  [21, 15, 1], // right-up-back
  [17, 4, 2], // left-up-front
  [20, 3, 5], // right-up-front
  [19, 8, 6], // left-down-front
  [22, 7, 9], // right-down-front
  [18, 12, 10], // left-down-back
  [23, 11, 13], // right-down-back -- fixed
]);

/** Number of slots that actually move. */
export const FREE = 7;

export const N_ORIENTATIONS = 3 ** (FREE - 1); // 729
export const N_PERMUTATIONS = 5040;
export const N_STATES = N_PERMUTATIONS * N_ORIENTATIONS; // 3_674_160

/** A solved cube is the identity permutation with no twists, which ranks as 0. */
export const SOLVED_INDEX = 0;

const FACTORIAL = [1, 1, 2, 6, 24, 120, 720, 5040];

/**
 * The permutations of `0..n-1` in lexicographic order, which is exactly Lehmer
 * order, so the k-th one yielded has {@link rankPermutation} equal to k.
 * @param {number} n
 * @returns {Generator<Uint8Array>}
 */
export function* permutations(n) {
  const current = Uint8Array.from({ length: n }, (_, index) => index);
  for (;;) {
    yield Uint8Array.from(current);
    let pivot = n - 2;
    while (pivot >= 0 && current[pivot] >= current[pivot + 1]) pivot--;
    if (pivot < 0) return;
    let successor = n - 1;
    while (current[successor] <= current[pivot]) successor--;
    [current[pivot], current[successor]] = [current[successor], current[pivot]];
    for (let left = pivot + 1, right = n - 1; left < right; left++, right--) {
      [current[left], current[right]] = [current[right], current[left]];
    }
  }
}

/**
 * Lehmer code of a permutation of `0..FREE-1`. The identity ranks 0, which is
 * what makes the solved state index 0.
 * @param {ArrayLike<number>} permutation
 * @returns {number}
 */
export function rankPermutation(permutation) {
  let rank = 0;
  for (let i = 0; i < FREE; i++) {
    let smaller = 0;
    for (let j = i + 1; j < FREE; j++) if (permutation[j] < permutation[i]) smaller++;
    rank += smaller * FACTORIAL[FREE - 1 - i];
  }
  return rank;
}

/** All 5040 permutations, indexed by their rank. @type {Uint8Array[]} */
const ALL_PERMUTATIONS = [...permutations(FREE)];

/** All 729 twist vectors, indexed by their orientation rank. @type {Uint8Array[]} */
const ALL_ORIENTATIONS = Array.from({ length: N_ORIENTATIONS }, (_, rank) => {
  const twists = new Uint8Array(FREE);
  let value = rank;
  let sum = 0;
  for (let slot = 0; slot < FREE - 1; slot++) {
    twists[slot] = value % 3;
    sum += twists[slot];
    value = Math.trunc(value / 3);
  }
  twists[FREE - 1] = (3 - (sum % 3)) % 3; // the twists sum to 0 mod 3
  return twists;
});

/**
 * `sum(twist[i] * 3**i)` over the first six slots; slot 6 is implied.
 * @param {ArrayLike<number>} twists
 * @returns {number}
 */
export function rankOrientation(twists) {
  let rank = 0;
  for (let slot = FREE - 2; slot >= 0; slot--) rank = rank * 3 + twists[slot];
  return rank;
}

/** @param {number} rank @returns {Uint8Array} */
export function unrankOrientation(rank) {
  return ALL_ORIENTATIONS[rank];
}

/**
 * The canonical colour triple of each corner piece, read off a solved cube.
 * @param {Uint8Array} goal
 * @returns {number[][]}
 */
export function piecesOf(goal) {
  return SLOTS.map((slot) => slot.map((facelet) => goal[facelet]));
}

/** The pieces of the canonically solved cube (yellow up, red front). */
export const PIECES = piecesOf(SOLVED);

const tripleKey = (a, b, c) => (a * 6 + b) * 6 + c;

/**
 * Map every rotation of every piece's colour triple to `piece * 3 + twist`.
 * All 24 ordered triples are distinct because the slots share a handedness, so
 * reading a slot's three colours identifies both the piece and its twist.
 * @param {number[][]} pieces
 */
function lookupFor(pieces) {
  const lookup = new Map();
  pieces.forEach((colors, piece) => {
    for (let twist = 0; twist < 3; twist++) {
      const [a, b, c] = [colors[twist % 3], colors[(twist + 1) % 3], colors[(twist + 2) % 3]];
      lookup.set(tripleKey(a, b, c), piece * 3 + twist);
    }
  });
  return lookup;
}

/**
 * A goal orientation: a solved cube, its pieces, and the triple lookup they induce.
 * @typedef {{goal: Uint8Array, pieces: number[][], lookup: Map<number, number>}} Goal
 */

/**
 * A cube may be handed over in any of the 24 orientations. The fixed corner's
 * three sticker colours match exactly one rotation of the solved cube, so
 * looking that up once here means every way of holding the same scramble
 * encodes to the same index -- and the goal index is always 0.
 * @type {Map<number, Goal>}
 */
const GOALS = new Map(
  [...orientations(SOLVED)].map((text) => {
    const goal = parse(text);
    const pieces = piecesOf(goal);
    const corner = tripleKey(...FIXED_CORNER.map((facelet) => goal[facelet]));
    return [corner, { goal, pieces, lookup: lookupFor(pieces) }];
  }),
);

/** The goal entry for the canonical solved cube. @type {Goal} */
const CANONICAL_GOAL = GOALS.get(tripleKey(...FIXED_CORNER.map((facelet) => SOLVED[facelet])));

/**
 * The goal orientation a state is aiming at, chosen by its fixed corner.
 * @param {Uint8Array} state
 * @returns {Goal}
 */
export function goalFor(state) {
  const corner = tripleKey(state[11], state[13], state[23]);
  const entry = GOALS.get(corner);
  if (entry === undefined) {
    throw new UnsolvableError(
      `corner colours (${state[11]}, ${state[13]}, ${state[23]}) do not occur on a solved cube`,
    );
  }
  return entry;
}

/**
 * Split a sticker state into (piece per slot, twist per slot).
 * @param {Uint8Array} state
 * @param {Goal} [goal] which solved cube to measure against; defaults to the state's own
 * @returns {{permutation: Uint8Array, orientation: Uint8Array}}
 */
export function toCubies(state, goal = CANONICAL_GOAL) {
  const permutation = new Uint8Array(FREE);
  const orientation = new Uint8Array(FREE);
  for (let slot = 0; slot < FREE; slot++) {
    const [a, b, c] = SLOTS[slot];
    const found = goal.lookup.get(tripleKey(state[a], state[b], state[c]));
    if (found === undefined) {
      throw new UnsolvableError(`facelets ${SLOTS[slot]} do not hold a corner of this cube`);
    }
    permutation[slot] = Math.trunc(found / 3);
    orientation[slot] = found % 3;
  }
  return { permutation, orientation };
}

/**
 * Rebuild a sticker state. Inverse of {@link toCubies}.
 * @param {ArrayLike<number>} permutation
 * @param {ArrayLike<number>} orientation
 * @param {number[][]} [pieces]
 * @returns {Uint8Array}
 */
export function fromCubies(permutation, orientation, pieces = PIECES) {
  const state = new Uint8Array(24);
  for (let slot = 0; slot < FREE; slot++) {
    const colors = pieces[permutation[slot]];
    const twist = orientation[slot];
    SLOTS[slot].forEach((facelet, position) => {
      state[facelet] = colors[(position + twist) % 3];
    });
  }
  SLOTS[FREE].forEach((facelet, position) => {
    state[facelet] = pieces[FREE][position];
  });
  return state;
}

/**
 * Sticker state to dense index below {@link N_STATES}.
 * @param {Uint8Array} state
 * @param {Goal} [goal] defaults to the goal the state's fixed corner selects
 * @returns {number}
 */
export function encode(state, goal = goalFor(state)) {
  const { permutation, orientation } = toCubies(state, goal);
  return rankPermutation(permutation) * N_ORIENTATIONS + rankOrientation(orientation);
}

/**
 * Dense index to sticker state. Inverse of {@link encode}.
 * @param {number} index
 * @param {number[][]} [pieces]
 * @returns {Uint8Array}
 */
export function decode(index, pieces = PIECES) {
  const permutation = Math.trunc(index / N_ORIENTATIONS);
  return fromCubies(ALL_PERMUTATIONS[permutation], ALL_ORIENTATIONS[index % N_ORIENTATIONS], pieces);
}

/**
 * One move's effect on each coordinate.
 * @typedef {{move: string, permutation: Int32Array, orientation: Int32Array}} MoveTable
 */

/** @type {MoveTable[] | null} */
let moveTableCache = null;
/** @type {MoveTable[] | null} */
let wideTableCache = null;

/**
 * One (move, permutation table, orientation table) triple per solve move.
 *
 * Built on first use -- roughly 35k entries -- so importing stays cheap. The
 * order follows `SOLVE_MOVES`, which pairs every turn with its inverse at
 * adjacent indices; `ida.js` relies on that to prune.
 *
 * @returns {MoveTable[]}
 */
export function moveTables() {
  if (moveTableCache !== null) return moveTableCache;

  const identity = fromCubies(
    Uint8Array.from({ length: FREE }, (_, slot) => slot),
    new Uint8Array(FREE),
  );

  moveTableCache = [...SOLVE_MOVES].map((move) => {
    // On the identity state the permutation *is* the slot-to-slot mapping and
    // the orientation *is* the twist each slot picks up.
    const { permutation: source, orientation: twist } = toCubies(applyMove(identity, move));

    const permutation = new Int32Array(N_PERMUTATIONS);
    const rearranged = new Uint8Array(FREE);
    for (let rank = 0; rank < N_PERMUTATIONS; rank++) {
      const old = ALL_PERMUTATIONS[rank];
      for (let slot = 0; slot < FREE; slot++) rearranged[slot] = old[source[slot]];
      permutation[rank] = rankPermutation(rearranged);
    }

    const orientation = new Int32Array(N_ORIENTATIONS);
    const twisted = new Uint8Array(FREE);
    for (let rank = 0; rank < N_ORIENTATIONS; rank++) {
      const old = ALL_ORIENTATIONS[rank];
      for (let slot = 0; slot < FREE; slot++) twisted[slot] = (old[source[slot]] + twist[slot]) % 3;
      orientation[rank] = rankOrientation(twisted);
    }

    return { move, permutation, orientation };
  });
  return moveTableCache;
}

/**
 * Move tables with the permutation side pre-multiplied by the row width, so a
 * search computes a child as `permutation[q] + orientation[r]` -- one add rather
 * than a multiply and an add, in a loop that runs tens of millions of times.
 * @returns {MoveTable[]}
 */
export function wideTables() {
  wideTableCache ??= moveTables().map(({ move, permutation, orientation }) => ({
    move,
    permutation: permutation.map((value) => value * N_ORIENTATIONS),
    orientation,
  }));
  return wideTableCache;
}

/**
 * Apply one solve move to an encoded state.
 * @param {number} index
 * @param {string} move one of {@link SOLVE_MOVES}
 * @returns {number}
 */
export function apply(index, move) {
  const table = moveTables().find((entry) => entry.move === move);
  if (table === undefined) throw new Error(`not a solve move: ${JSON.stringify(move)}`);
  const quotient = Math.trunc(index / N_ORIENTATIONS);
  return table.permutation[quotient] * N_ORIENTATIONS + table.orientation[index % N_ORIENTATIONS];
}
