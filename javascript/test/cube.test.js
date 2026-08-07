/** The sticker model: the move algebra, parsing, and the fixed corner. */

import assert from "node:assert/strict";
import { describe, it } from "node:test";

import {
  FIXED_CORNER,
  MOVES,
  ROTATIONS,
  SOLVE_MOVES,
  SOLVED,
  applyMove,
  applyMoves,
  invert,
  isSolved,
  orientations,
  parse,
  randomSource,
  scramble,
  toString,
} from "../src/cube.js";

const SOLVED_TEXT = "yyyyrrrrwwwwppppbbbbgggg";

describe("moves", () => {
  it("has twelve, six pairs of inverses", () => {
    assert.equal(Object.keys(MOVES).length, 12);
    for (const move of Object.keys(MOVES)) {
      const opposite = move === move.toLowerCase() ? move.toUpperCase() : move.toLowerCase();
      assert.ok(opposite in MOVES, `${move} has no inverse`);
    }
  });

  it("every move has order four", () => {
    for (const move of Object.keys(MOVES)) {
      const state = applyMoves(SOLVED, move.repeat(4));
      assert.equal(toString(state), SOLVED_TEXT, `${move} does not have order 4`);
      assert.notEqual(toString(applyMoves(SOLVED, move.repeat(2))), SOLVED_TEXT);
    }
  });

  it("a move and its inverse cancel", () => {
    const scrambled = applyMoves(SOLVED, "lFtBra");
    for (const move of Object.keys(MOVES)) {
      const opposite = move === move.toLowerCase() ? move.toUpperCase() : move.toLowerCase();
      assert.equal(toString(applyMoves(scrambled, move + opposite)), toString(scrambled));
      assert.equal(toString(applyMoves(scrambled, opposite + move)), toString(scrambled));
    }
  });

  it("opposite faces commute", () => {
    const scrambled = applyMoves(SOLVED, "ltFaBr");
    for (const [one, other] of [
      ["l", "r"],
      ["t", "b"],
      ["f", "a"],
      ["L", "R"],
      ["l", "R"],
      ["T", "b"],
    ]) {
      assert.equal(
        toString(applyMoves(scrambled, one + other)),
        toString(applyMoves(scrambled, other + one)),
        `${one} and ${other} do not commute`,
      );
    }
  });

  it("adjacent faces do not commute", () => {
    assert.notEqual(toString(applyMoves(SOLVED, "lt")), toString(applyMoves(SOLVED, "tl")));
  });

  it("the solve moves fix exactly facelets 11, 13 and 23", () => {
    const moved = new Set();
    for (const move of SOLVE_MOVES) {
      MOVES[move].forEach((source, target) => {
        if (source !== target) moved.add(target);
      });
    }
    const fixed = [...Array(24).keys()].filter((facelet) => !moved.has(facelet));
    assert.deepEqual(fixed, [...FIXED_CORNER]);
  });

  it("inverts a sequence", () => {
    const moves = "lFtBra";
    assert.equal(invert(moves), "ARbTfL");
    assert.equal(toString(applyMoves(applyMoves(SOLVED, moves), invert(moves))), SOLVED_TEXT);
    assert.equal(invert(invert(moves)), moves);
  });

  it("rejects an unknown move", () => {
    assert.throws(() => applyMove(SOLVED, "x"), /unknown move/);
  });
});

describe("rotations", () => {
  it("form a group of 24", () => {
    assert.equal(ROTATIONS.length, 24);
    assert.equal(orientations(SOLVED).size, 24);
  });

  it("leave every face single-coloured on a solved cube", () => {
    for (const text of orientations(SOLVED)) assert.ok(isSolved(parse(text)));
  });
});

describe("parse", () => {
  it("round-trips", () => {
    assert.equal(toString(parse(SOLVED_TEXT)), SOLVED_TEXT);
    assert.deepEqual([...parse(SOLVED_TEXT)], [...SOLVED]);
  });

  it("is case- and whitespace-insensitive", () => {
    assert.equal(toString(parse(` ${SOLVED_TEXT.toUpperCase()}\n`)), SOLVED_TEXT);
  });

  it("rejects the wrong length", () => {
    assert.throws(() => parse("yyy"), /expected 24 colour letters/);
    assert.throws(() => parse(`${SOLVED_TEXT}y`), /expected 24 colour letters/);
  });

  it("rejects unknown letters", () => {
    assert.throws(() => parse("x".repeat(24)), /invalid colour letter/);
  });

  it("rejects a wrong colour count", () => {
    assert.throws(() => parse(`${SOLVED_TEXT.slice(0, 23)}y`), /appears/);
  });
});

describe("scramble", () => {
  it("is reproducible from a seed", () => {
    const one = toString(scramble(SOLVED, 12, randomSource(7)));
    const other = toString(scramble(SOLVED, 12, randomSource(7)));
    assert.equal(one, other);
    assert.notEqual(one, toString(scramble(SOLVED, 12, randomSource(8))));
  });

  it("produces a legal, unsolved cube", () => {
    const state = scramble(SOLVED, 14, randomSource(1));
    assert.equal(toString(parse(toString(state))), toString(state));
    assert.ok(!isSolved(state));
  });
});
