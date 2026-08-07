/** The renderer: an unfolded net, with or without colour. */

import assert from "node:assert/strict";
import { describe, it } from "node:test";

import { SOLVED, applyMoves } from "../src/cube.js";
import { PALETTE, render } from "../src/display.js";

describe("render", () => {
  it("draws the solved cube as a labelled net", () => {
    const lines = render(SOLVED, "none").split("\n");
    assert.equal(lines.length, 11); // UP, blank, the band, blank, DOWN
    assert.match(lines[0], /UP/);
    assert.match(lines.join("\n"), /LEFT {2,}FRONT {2,}RIGHT {2,}BACK/);
    assert.match(lines.at(-1), /^ +W {3}W *$/); // the bottom row of DOWN
  });

  it("draws all 24 stickers, four of each colour", () => {
    const stickers = render(SOLVED, "none").match(/(?<= )[RWBYPG](?= )/g);
    assert.equal(stickers.length, 24);
    for (const letter of "RWBYPG") {
      assert.equal(stickers.filter((sticker) => sticker === letter).length, 4);
    }
  });

  it("moves stickers when the cube is turned", () => {
    assert.notEqual(render(SOLVED, "none"), render(applyMoves(SOLVED, "l"), "none"));
  });

  it("emits truecolor escapes and doubles the cell height", () => {
    const plain = render(SOLVED, "none").split("\n").length;
    const colored = render(SOLVED, "truecolor");
    assert.match(colored, /\x1b\[48;2;255;213;0m/); // yellow UP
    assert.equal(colored.split("\n").length, plain + 6);
  });

  it("falls back to the 256-colour cube", () => {
    const colored = render(SOLVED, "256");
    assert.match(colored, /\x1b\[48;5;\d+m/);
    assert.doesNotMatch(colored, /48;2;/);
  });

  it("knows a colour for every sticker letter", () => {
    assert.deepEqual(Object.keys(PALETTE).sort(), [..."rwbypg"].sort());
  });
});
