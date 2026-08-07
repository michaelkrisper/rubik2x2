/**
 * God's algorithm, and the check that validates everything underneath it.
 *
 * The distance histogram has to equal OEIS A079762. No wrong move table, slot
 * list or ranking function can reproduce those fifteen numbers, so this single
 * assertion covers the whole stack from the sticker permutations up.
 */

import assert from "node:assert/strict";
import { after, before, describe, it } from "node:test";

import { N_STATES, encode } from "../src/coords.js";
import { SOLVED, applyMoves, isSolved, parse, randomSource, scramble } from "../src/cube.js";
import { DistanceTable, buildDistances } from "../src/table.js";

/** OEIS A079762: pocket cube positions at each quarter-turn distance. */
const A079762 = [
  1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508, 930588, 1350852, 782536, 90280, 276,
];

const HARDEST = "wppryygrygrwgprbbbwpbwyg";

describe("distance table", () => {
  /** @type {DistanceTable} */
  let table;
  before(() => {
    table = DistanceTable.build();
  });

  it("reproduces OEIS A079762", () => {
    assert.deepEqual(table.histogram(), A079762);
  });

  it("covers every state exactly once, out to distance 14", () => {
    assert.equal(
      A079762.reduce((sum, count) => sum + count, 0),
      N_STATES,
    );
    assert.equal(table.histogram().length - 1, 14);
    assert.ok(table.distances.every((value) => value > 0), "some state was never reached");
  });

  it("reports the solved state at distance 0 and the antipode at 14", () => {
    assert.equal(table.distance(encode(SOLVED)), 0);
    assert.equal(table.distance(encode(parse(HARDEST))), 14);
  });

  it("reports progress once per level", () => {
    const levels = [];
    buildDistances((depth, count) => levels.push([depth, count]));
    assert.deepEqual(
      levels.map(([, count]) => count),
      A079762,
    );
    assert.deepEqual(
      levels.map(([depth]) => depth),
      A079762.map((_, depth) => depth),
    );
  });

  it("solves by walking downhill", () => {
    const random = randomSource(11);
    for (let trial = 0; trial < 50; trial++) {
      const state = scramble(SOLVED, 1 + Math.floor(random() * 14), random);
      const solution = table.solve(state);
      assert.ok(isSolved(applyMoves(state, solution)), `${solution} does not solve the cube`);
      assert.equal(solution.length, table.distance(encode(state)));
    }
  });

  it("returns nothing for a solved cube", () => {
    assert.equal(table.solve(SOLVED), "");
  });
});

describe("parallel build", () => {
  /** @type {DistanceTable} */
  let parallel;
  before(async () => {
    parallel = await DistanceTable.buildParallel({ workers: 2 });
  });
  after(() => {
    parallel = undefined;
  });

  it("produces a table identical to the serial one, byte for byte", () => {
    const serial = buildDistances();
    assert.equal(parallel.distances.length, serial.length);
    assert.ok(
      Buffer.compare(Buffer.from(parallel.distances), Buffer.from(serial)) === 0,
      "the parallel build disagrees with the serial one -- a lost compare-and-exchange?",
    );
  });

  it("reproduces OEIS A079762", () => {
    assert.deepEqual(parallel.histogram(), A079762);
  });

  it("says how it was built", () => {
    assert.equal(parallel.backend, "parallel/2");
  });
});
