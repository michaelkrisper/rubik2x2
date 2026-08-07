/** The encoding: round trips, orientation independence, and the move tables. */

import assert from "node:assert/strict";
import { describe, it } from "node:test";

import {
  FREE,
  N_ORIENTATIONS,
  N_PERMUTATIONS,
  N_STATES,
  SLOTS,
  SOLVED_INDEX,
  apply,
  decode,
  encode,
  goalFor,
  moveTables,
  permutations,
  rankPermutation,
  toCubies,
  wideTables,
} from "../src/coords.js";
import {
  SOLVE_MOVES,
  SOLVED,
  UnsolvableError,
  applyMove,
  orientations,
  parse,
  randomSource,
  scramble,
  toString,
} from "../src/cube.js";

const random = randomSource(20260805);

/** Scrambles reached with all twelve moves, so they arrive in every orientation. */
const sample = Array.from({ length: 60 }, () => scramble(SOLVED, 1 + Math.floor(random() * 14), random));

/** Scrambles reached with the six solve moves only, so the fixed corner stays put. */
const upright = Array.from({ length: 20 }, () => {
  let state = SOLVED;
  for (let count = 0; count < 14; count++) {
    state = applyMove(state, SOLVE_MOVES[Math.floor(random() * SOLVE_MOVES.length)]);
  }
  return state;
});

describe("ranking", () => {
  it("counts the space correctly", () => {
    assert.equal(N_PERMUTATIONS, 5040);
    assert.equal(N_ORIENTATIONS, 729);
    assert.equal(N_STATES, 3674160);
  });

  it("generates permutations in Lehmer order", () => {
    const all = [...permutations(FREE)];
    assert.equal(all.length, N_PERMUTATIONS);
    assert.equal(new Set(all.map((p) => p.join(""))).size, N_PERMUTATIONS);
    all.forEach((p, index) => assert.equal(rankPermutation(p), index));
  });

  it("ranks the solved cube as zero", () => {
    assert.equal(encode(SOLVED), SOLVED_INDEX);
  });
});

describe("encode", () => {
  it("round-trips through decode", () => {
    // Cubes turned only by the solve moves keep their fixed corner where the
    // canonical solved cube has it, so they decode back sticker for sticker.
    for (const state of upright) {
      assert.equal(toString(decode(encode(state))), toString(state));
    }
  });

  it("round-trips a cube held any which way, against its own goal", () => {
    for (const state of sample) {
      const goal = goalFor(state);
      assert.equal(toString(decode(encode(state, goal), goal.pieces)), toString(state));
    }
  });

  it("round-trips from an index", () => {
    for (const index of [0, 1, 728, 729, 12345, N_STATES - 1]) {
      assert.equal(encode(decode(index)), index);
    }
  });

  it("encodes a solved cube as 0 however it is held", () => {
    const indices = new Set([...orientations(SOLVED)].map((text) => encode(parse(text))));
    assert.deepEqual([...indices], [SOLVED_INDEX]);
  });

  it("accepts every one of the 24 ways of holding a scramble", () => {
    // The goal orientation is chosen from the fixed corner, so a cube handed
    // over any which way still encodes -- to an index at the same distance,
    // though not necessarily the same one. (test/solvers.test.js checks that.)
    for (const state of sample.slice(0, 10)) {
      for (const text of orientations(state)) {
        const index = encode(parse(text));
        assert.ok(index >= 0 && index < N_STATES);
      }
    }
  });

  it("gives distinct indices to states that are not rotations of each other", () => {
    // `l` and `R` differ only by a whole-cube rotation, so the encoding -- which
    // deliberately quotients those out -- must map them to the same index.
    assert.equal(encode(applyMove(SOLVED, "l")), encode(applyMove(SOLVED, "R")));
    assert.notEqual(encode(applyMove(SOLVED, "l")), encode(applyMove(SOLVED, "L")));
  });

  it("splits a state into pieces and twists", () => {
    const { permutation, orientation } = toCubies(SOLVED);
    assert.deepEqual([...permutation], [0, 1, 2, 3, 4, 5, 6]);
    assert.deepEqual([...orientation], [0, 0, 0, 0, 0, 0, 0]);
  });

  it("rejects a slot whose colours are no corner of this cube", () => {
    const broken = Uint8Array.from(SOLVED);
    const [, up] = SLOTS[0]; // left-up-back, showing left, up and back colours
    broken[up] = SOLVED[4]; // ... now claiming to show the front colour too
    assert.throws(() => encode(broken), UnsolvableError);
  });

  it("rejects a fixed corner no solved cube has", () => {
    const broken = Uint8Array.from(SOLVED);
    broken[11] = broken[23]; // two faces of one corner sharing a colour
    assert.throws(() => encode(broken), UnsolvableError);
  });
});

describe("move tables", () => {
  const tables = moveTables();

  it("has one per solve move, in order", () => {
    assert.deepEqual(
      tables.map(({ move }) => move),
      [...SOLVE_MOVES],
    );
    for (const { permutation, orientation } of tables) {
      assert.equal(permutation.length, N_PERMUTATIONS);
      assert.equal(orientation.length, N_ORIENTATIONS);
    }
  });

  it("agrees with the sticker model", () => {
    for (const state of sample) {
      for (const move of SOLVE_MOVES) {
        assert.equal(
          apply(encode(state), move),
          encode(applyMove(state, move)),
          `table for ${move} disagrees with the sticker model`,
        );
      }
    }
  });

  it("pairs inverses at adjacent indices", () => {
    for (const state of sample.slice(0, 12)) {
      const index = encode(state);
      for (let position = 0; position < 6; position += 2) {
        const forth = SOLVE_MOVES[position];
        const back = SOLVE_MOVES[position + 1];
        assert.equal(apply(apply(index, forth), back), index);
        assert.equal(apply(apply(index, back), forth), index);
      }
    }
  });

  it("wide tables are the move tables times the row width", () => {
    const wide = wideTables();
    tables.forEach(({ permutation }, position) => {
      assert.equal(wide[position].permutation[17], permutation[17] * N_ORIENTATIONS);
      assert.equal(
        wide[position].permutation[500] + wide[position].orientation[300],
        permutation[500] * N_ORIENTATIONS + tables[position].orientation[300],
      );
    });
  });

  it("caches: repeated calls return the same objects", () => {
    assert.equal(moveTables(), tables);
    assert.equal(wideTables(), wideTables());
  });
});
