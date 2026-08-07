/**
 * Public surface of the library, layered from the readable model outwards:
 * `cube` (24 stickers) -> `coords` (dense index) -> the three solvers, with
 * `display` off to the side because drawing is nobody else's business.
 *
 * @module rubik2x2
 */

export * as bench from "./bench.js";
export * as coords from "./coords.js";
export * as cube from "./cube.js";
export * as display from "./display.js";
export * as ida from "./ida.js";
export * as search from "./search.js";
export { ALGORITHMS, DESCRIPTIONS, makeSolver } from "./solvers.js";
export { DistanceTable } from "./table.js";
