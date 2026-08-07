/**
 * The three algorithms, held to the same standard: every solution must actually
 * solve its cube, and must be as short as the distance table says is possible.
 */

import assert from "node:assert/strict";
import { before, describe, it } from "node:test";

import { encode } from "../src/coords.js";
import {
  SOLVED,
  UnsolvableError,
  applyMoves,
  isSolved,
  orientations,
  parse,
  randomSource,
  scramble,
  toString,
} from "../src/cube.js";
import { PatternDatabases, solve as idaSolve } from "../src/ida.js";
import { solve as bfsSolve } from "../src/search.js";
import { ALGORITHMS, makeSolver } from "../src/solvers.js";
import { DistanceTable } from "../src/table.js";

/** An antipodal position: 14 quarter turns from solved. */
const HARDEST = "wppryygrygrwgprbbbwpbwyg";

const random = randomSource(4242);
const sample = Array.from({ length: 25 }, () => scramble(SOLVED, 1 + Math.floor(random() * 14), random));

/** @type {DistanceTable} */
let table;
/** @type {Record<string, (state: Uint8Array) => string>} */
let solvers;

before(() => {
  table = DistanceTable.build();
  solvers = Object.fromEntries(ALGORITHMS.map((name) => [name, makeSolver(name, table)]));
});

describe("every algorithm", () => {
  it("returns nothing for a solved cube", () => {
    for (const name of ALGORITHMS) assert.equal(solvers[name](SOLVED), "");
  });

  it("returns a solution that actually solves the cube", () => {
    for (const state of sample) {
      for (const name of ALGORITHMS) {
        const solution = solvers[name](state);
        assert.ok(
          isSolved(applyMoves(state, solution)),
          `${name} returned ${solution} for ${toString(state)}, which does not solve it`,
        );
      }
    }
  });

  it("returns solutions of equal -- and optimal -- length", () => {
    for (const state of sample) {
      const optimal = table.distance(encode(state));
      const lengths = ALGORITHMS.map((name) => solvers[name](state).length);
      assert.deepEqual(lengths, ALGORITHMS.map(() => optimal), `disagreement on ${toString(state)}`);
    }
  });

  it("needs exactly 14 moves for the antipodal position", () => {
    const hardest = parse(HARDEST);
    for (const name of ALGORITHMS) {
      const solution = solvers[name](hardest);
      assert.equal(solution.length, 14, `${name} returned ${solution.length} moves`);
      assert.ok(isSolved(applyMoves(hardest, solution)));
    }
  });

  it("solves a cube handed over in any of the 24 orientations", () => {
    for (const text of orientations(parse(HARDEST))) {
      const state = parse(text);
      for (const name of ALGORITHMS) {
        const solution = solvers[name](state);
        assert.equal(solution.length, 14);
        assert.ok(isSolved(applyMoves(state, solution)), `${name} failed on ${text}`);
      }
    }
  });

  it("uses only the six solve moves", () => {
    for (const state of sample) {
      for (const name of ALGORITHMS) {
        assert.match(solvers[name](state), /^[lLtTfF]*$/);
      }
    }
  });

  it("rejects a state no turns can reach", () => {
    const broken = Uint8Array.from(SOLVED);
    broken[0] = SOLVED[4];
    for (const name of ALGORITHMS) assert.throws(() => solvers[name](broken), UnsolvableError);
  });
});

describe("bidirectional search", () => {
  it("gives up below its depth limit rather than returning a long solution", () => {
    assert.throws(() => bfsSolve(parse(HARDEST), 6), /no solution within 6 moves/);
  });

  it("finds one-move solutions directly", () => {
    for (const move of "lLtTfF") {
      const solution = bfsSolve(applyMoves(SOLVED, move));
      assert.equal(solution.length, 1);
      assert.ok(isSolved(applyMoves(applyMoves(SOLVED, move), solution)));
    }
  });
});

describe("pattern databases", () => {
  it("are admissible: never more than the true distance", () => {
    const databases = PatternDatabases.build();
    for (const state of sample) {
      const index = encode(state);
      assert.ok(
        databases.bound(index) <= table.distance(index),
        `bound overshoots for ${toString(state)}`,
      );
    }
  });

  it("reach only distance 8, which is why IDA* struggles here", () => {
    assert.deepEqual(PatternDatabases.build().reach, [8, 8]);
  });

  it("are cached", () => {
    assert.equal(PatternDatabases.build(), PatternDatabases.build());
  });

  it("give up below the depth limit", () => {
    assert.throws(() => idaSolve(parse(HARDEST), 10), /no solution within 10 moves/);
  });
});

describe("makeSolver", () => {
  it("refuses the table algorithm without a table", () => {
    assert.throws(() => makeSolver("table"), TypeError);
  });

  it("refuses an unknown algorithm", () => {
    assert.throws(() => makeSolver("psychic", table), TypeError);
  });
});
