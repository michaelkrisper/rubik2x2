/**
 * Drawing a cube for a terminal.
 *
 * Deliberately separate from `cube.js`: the model is 24 bytes and knows nothing
 * about how anyone looks at it.
 *
 * The picture is the standard unfolded net -- UP above FRONT, the four side
 * faces in a band, DOWN below -- so it matches the order a state string is
 * written in. BACK is drawn as seen from behind, which is what makes the band
 * read as one continuous strip around the cube.
 *
 * Colour is used when the terminal really supports it and letters otherwise, per
 * `NO_COLOR`/`FORCE_COLOR` and whether output is a terminal at all. Coloured
 * facelets are two rows tall so they come out roughly square, since terminal
 * cells are about twice as tall as they are wide; the letter fallback uses one
 * row, where squareness would only cost readability.
 *
 * @module display
 */

import { BACK, COLOR_CHARS, DOWN, FRONT, LEFT, RIGHT, UP } from "./cube.js";

/**
 * Face-colour RGB, close to a real cube. This puzzle uses purple where a
 * standard cube has orange.
 * @type {Readonly<Record<string, [number, number, number]>>}
 */
export const PALETTE = Object.freeze({
  r: [196, 30, 58],
  w: [245, 245, 245],
  b: [0, 81, 186],
  y: [255, 213, 0],
  p: [137, 66, 175],
  g: [0, 158, 96],
});

/** How much colour the output may use. @typedef {"none" | "256" | "truecolor"} ColorMode */

const BAND = /** @type {const} */ ([
  ["LEFT", LEFT],
  ["FRONT", FRONT],
  ["RIGHT", RIGHT],
  ["BACK", BACK],
]);

const CELL_WIDTH = 4;
const FACE_WIDTH = CELL_WIDTH * 2;
const GAP = "  ";
const RESET = "\x1b[0m";
const DIM = "\x1b[2m";

/**
 * The richest mode this terminal understands, ignoring where output goes.
 * @returns {ColorMode}
 */
export function bestColor() {
  if (process.env.TERM === "dumb") return "none";
  return ["truecolor", "24bit"].includes((process.env.COLORTERM ?? "").toLowerCase())
    ? "truecolor"
    : "256";
}

/**
 * What to actually use, honouring `NO_COLOR`, `FORCE_COLOR` and piping.
 * @param {{isTTY?: boolean}} [stream]
 * @returns {ColorMode}
 */
export function detectColor(stream = process.stdout) {
  if (process.env.NO_COLOR) return "none";
  if (!stream?.isTTY && !process.env.FORCE_COLOR) return "none";
  return bestColor();
}

/** Rows per facelet: two for colour blocks, one for letters. */
const cellHeight = (color) => (color === "none" ? 1 : 2);

/** Nearest index in the xterm 6x6x6 colour cube. */
const ansi256 = (red, green, blue) =>
  16 + 36 * ((red * 5 / 255) | 0) + 6 * ((green * 5 / 255) | 0) + ((blue * 5 / 255) | 0);

const center = (text, width) => {
  const left = Math.max(0, (width - text.length) >> 1);
  return " ".repeat(left) + text + " ".repeat(Math.max(0, width - text.length - left));
};

/** One facelet row, always CELL_WIDTH columns wide. */
function paint(char, color) {
  if (color === "none") return center(char.toUpperCase(), CELL_WIDTH);
  const [red, green, blue] = PALETTE[char];
  const background =
    color === "truecolor" ? `\x1b[48;2;${red};${green};${blue}m` : `\x1b[48;5;${ansi256(red, green, blue)}m`;
  return `${background}${" ".repeat(CELL_WIDTH)}${RESET}`;
}

/** One face as text rows: two facelet rows, each `cellHeight` tall. */
function face(state, side, color) {
  const base = side * 4;
  const order = side === BACK ? [[3, 2], [1, 0]] : [[0, 1], [2, 3]];
  return order.flatMap((pair) => {
    const painted = pair.map((index) => paint(COLOR_CHARS[state[base + index]], color)).join("");
    return Array.from({ length: cellHeight(color) }, () => painted);
  });
}

const label = (text, color) =>
  color === "none" ? center(text, FACE_WIDTH) : `${DIM}${center(text, FACE_WIDTH)}${RESET}`;

/**
 * Draw the cube as an unfolded net.
 * @param {Uint8Array} state
 * @param {ColorMode} [color] defaults to what the terminal supports
 * @returns {string}
 */
export function render(state, color = detectColor()) {
  // UP and DOWN sit over FRONT, the second face in the band.
  const indent = " ".repeat(FACE_WIDTH + GAP.length);
  const band = BAND.map(([, side]) => face(state, side, color));

  const lines = [
    indent + label("UP", color),
    ...face(state, UP, color).map((row) => indent + row),
    "",
    BAND.map(([name]) => label(name, color)).join(GAP),
    ...band[0].map((_, row) => band.map((side) => side[row]).join(GAP)),
    "",
    indent + label("DOWN", color),
    ...face(state, DOWN, color).map((row) => indent + row),
  ];
  return lines.join("\n");
}
