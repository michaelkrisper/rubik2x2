/**
 * One worker of the parallel distance-table build (see `table.js`).
 *
 * The main thread owns the level structure and hands each worker a contiguous
 * slice `[start, end)` of the current frontier. The worker expands it, claims
 * unvisited children in the shared `distances` array with an atomic
 * compare-and-exchange, buffers what it won, and appends that buffer to the
 * shared queue in one reserved block before replying.
 *
 * @module table-worker
 */

import { parentPort, workerData } from "node:worker_threads";

const { distances: distancesBuffer, queue: queueBuffer, control: controlBuffer } = workerData;

const distances = new Uint8Array(distancesBuffer);
const queue = new Int32Array(queueBuffer);
const control = new Int32Array(controlBuffer);

const [p0, p1, p2, p3, p4, p5] = workerData.permutation.map((buffer) => new Int32Array(buffer));
const [o0, o1, o2, o3, o4, o5] = workerData.orientation.map((buffer) => new Int32Array(buffer));

const WIDTH = o0.length; // 729 orientations per permutation

/** Found-this-level scratch space; doubles rather than reserving a shared block per state. */
let found = new Int32Array(1 << 16);

/** @param {number} needed */
function reserve(needed) {
  if (needed <= found.length) return;
  let size = found.length;
  while (size < needed) size *= 2;
  const grown = new Int32Array(size);
  grown.set(found);
  found = grown;
}

parentPort.on("message", ({ start, end, marker }) => {
  let count = 0;
  for (let cursor = start; cursor < end; cursor++) {
    const node = queue[cursor];
    const q = (node / WIDTH) | 0;
    const r = node - q * WIDTH;
    reserve(count + 6);
    // The plain read filters out the ~85% of edges leading somewhere already
    // visited; only a candidate that still looks unvisited costs an atomic.
    let c = p0[q] + o0[r];
    if (distances[c] === 0 && Atomics.compareExchange(distances, c, 0, marker) === 0) found[count++] = c;
    c = p1[q] + o1[r];
    if (distances[c] === 0 && Atomics.compareExchange(distances, c, 0, marker) === 0) found[count++] = c;
    c = p2[q] + o2[r];
    if (distances[c] === 0 && Atomics.compareExchange(distances, c, 0, marker) === 0) found[count++] = c;
    c = p3[q] + o3[r];
    if (distances[c] === 0 && Atomics.compareExchange(distances, c, 0, marker) === 0) found[count++] = c;
    c = p4[q] + o4[r];
    if (distances[c] === 0 && Atomics.compareExchange(distances, c, 0, marker) === 0) found[count++] = c;
    c = p5[q] + o5[r];
    if (distances[c] === 0 && Atomics.compareExchange(distances, c, 0, marker) === 0) found[count++] = c;
  }

  if (count > 0) {
    const base = Atomics.add(control, 0, count);
    queue.set(found.subarray(0, count), base);
  }
  parentPort.postMessage(count);
});
