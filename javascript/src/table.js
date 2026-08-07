/**
 * God's algorithm: the exact distance of every reachable state.
 *
 * The pocket cube has 3,674,160 positions -- few enough to hold every one's
 * exact distance to solved in a flat byte array of 3.7 MB. Then there is nothing
 * left to search, only a table to read:
 *
 * > at a state *d* moves from solved, exactly one neighbour is *d-1* away;
 * > step to it, repeat.
 *
 * A solve costs *d* lookups: constant time in the difficulty of the scramble,
 * and optimal by construction. This is the classic result for the 2x2x2 -- Jerry
 * Bryan computed the same distribution in 1993, and it is recorded as OEIS
 * A079762, which {@link DistanceTable#histogram} reproduces exactly.
 *
 * The table is built in memory and never cached to disk, so it always matches
 * the code that produced it. Building costs one exhaustive breadth-first search
 * over 22 million edges, which is the whole trade: instant solves, paid for up
 * front. {@link DistanceTable.buildParallel} splits that build across worker
 * threads; see the README for what it actually bought.
 *
 * @module table
 */

import { availableParallelism } from "node:os";
import { fileURLToPath } from "node:url";
import { Worker } from "node:worker_threads";

import { N_ORIENTATIONS, N_STATES, SOLVED_INDEX, encode, wideTables } from "./coords.js";
import { UnsolvableError } from "./cube.js";

/**
 * Called once per breadth-first level with the depth and how many states it holds.
 * @callback Progress
 * @param {number} depth
 * @param {number} count
 */

const WORKER_URL = new URL("./table-worker.js", import.meta.url);

/**
 * Breadth-first over the whole space, single-threaded. Returns `distance + 1`
 * per state, so 0 can mean "not yet reached" during the build.
 *
 * The queue is one `Int32Array` holding all 3,674,160 states in visit order --
 * every state is enqueued exactly once, so the whole search fits in a single
 * 14.7 MB allocation with the level boundaries as indices into it, and no
 * per-level array ever has to grow. The loop is unrolled over the six moves and
 * the permutation tables come pre-multiplied by the row width, because this runs
 * 22 million times and at that point an index computation is the cost.
 *
 * @param {Progress} [onLevel]
 * @param {Uint8Array} [distances] optional destination, e.g. a shared buffer
 * @returns {Uint8Array}
 */
export function buildDistances(onLevel, distances = new Uint8Array(N_STATES)) {
  const width = N_ORIENTATIONS;
  const [t0, t1, t2, t3, t4, t5] = wideTables();
  const [p0, p1, p2, p3, p4, p5] = [t0, t1, t2, t3, t4, t5].map((table) => table.permutation);
  const [o0, o1, o2, o3, o4, o5] = [t0, t1, t2, t3, t4, t5].map((table) => table.orientation);

  const queue = new Int32Array(N_STATES);
  distances[SOLVED_INDEX] = 1;
  queue[0] = SOLVED_INDEX;

  let head = 0;
  let tail = 1;
  for (let depth = 0; head < tail; depth++) {
    const levelEnd = tail;
    onLevel?.(depth, levelEnd - head);
    const marker = depth + 2; // children sit at distance depth + 1
    while (head < levelEnd) {
      const node = queue[head++];
      const q = (node / width) | 0;
      const r = node - q * width;
      let c = p0[q] + o0[r];
      if (distances[c] === 0) (distances[c] = marker), (queue[tail++] = c);
      c = p1[q] + o1[r];
      if (distances[c] === 0) (distances[c] = marker), (queue[tail++] = c);
      c = p2[q] + o2[r];
      if (distances[c] === 0) (distances[c] = marker), (queue[tail++] = c);
      c = p3[q] + o3[r];
      if (distances[c] === 0) (distances[c] = marker), (queue[tail++] = c);
      c = p4[q] + o4[r];
      if (distances[c] === 0) (distances[c] = marker), (queue[tail++] = c);
      c = p5[q] + o5[r];
      if (distances[c] === 0) (distances[c] = marker), (queue[tail++] = c);
    }
  }
  return distances;
}

/**
 * The same search, level-synchronously split across worker threads.
 *
 * Within a level the frontier is independent work, so each worker takes a
 * contiguous slice of it. The one hazard is the shared visited array: two
 * workers can reach the same state in the same level. A worker therefore claims
 * a state with `Atomics.compareExchange(distances, child, 0, marker)` and only
 * enqueues it when the exchange reports it won -- the plain read that precedes
 * the exchange filters out the ~85% of edges that lead to already-visited states
 * without touching an atomic at all.
 *
 * Newly found states go into a per-worker buffer and are copied into the shared
 * queue at the level boundary, after one `Atomics.add` reserves a contiguous
 * block. One atomic per worker per level rather than one per state.
 *
 * @param {{workers?: number, onLevel?: Progress}} [options]
 * @returns {Promise<{distances: Uint8Array, workers: number}>}
 */
export async function buildDistancesParallel({ workers, onLevel } = {}) {
  const count = Math.max(1, workers ?? availableParallelism());

  const distances = new Uint8Array(new SharedArrayBuffer(N_STATES));
  const queue = new Int32Array(new SharedArrayBuffer(N_STATES * 4));
  const control = new Int32Array(new SharedArrayBuffer(4)); // control[0] is the queue tail

  const tables = wideTables();
  const workerData = {
    distances: distances.buffer,
    queue: queue.buffer,
    control: control.buffer,
    permutation: tables.map((table) => table.permutation.buffer),
    orientation: tables.map((table) => table.orientation.buffer),
  };

  const pool = Array.from({ length: count }, () => new Worker(fileURLToPath(WORKER_URL), { workerData }));
  /** @type {Map<Worker, (value: unknown) => void>} */
  const pending = new Map();
  const ready = pool.map(
    (worker) =>
      new Promise((resolve, reject) => {
        worker.once("error", reject);
        worker.on("message", () => pending.get(worker)?.());
        worker.once("online", resolve);
      }),
  );

  try {
    await Promise.all(ready);

    distances[SOLVED_INDEX] = 1;
    queue[0] = SOLVED_INDEX;
    Atomics.store(control, 0, 1);

    let head = 0;
    let levelEnd = 1;
    for (let depth = 0; head < levelEnd; depth++) {
      onLevel?.(depth, levelEnd - head);
      const size = levelEnd - head;
      const share = Math.ceil(size / count);
      await Promise.all(
        pool.map((worker, id) => {
          const start = Math.min(head + id * share, levelEnd);
          const end = Math.min(start + share, levelEnd);
          return new Promise((resolve) => {
            pending.set(worker, resolve);
            worker.postMessage({ start, end, marker: depth + 2 });
          });
        }),
      );
      head = levelEnd;
      levelEnd = Atomics.load(control, 0);
    }
  } finally {
    await Promise.all(pool.map((worker) => worker.terminate()));
  }

  return { distances, workers: count };
}

/**
 * Distance-to-solved for all 3,674,160 states, stored as `distance + 1`.
 * Zero would mean "not yet reached", which is what the build uses it for.
 */
export class DistanceTable {
  /**
   * @param {Uint8Array} distances
   * @param {string} [backend] which build produced it, for reporting
   */
  constructor(distances, backend = "serial") {
    this.distances = distances;
    this.backend = backend;
    Object.freeze(this);
  }

  /**
   * Search the whole state space on this thread.
   * @param {Progress} [onLevel]
   * @returns {DistanceTable}
   */
  static build(onLevel) {
    return new DistanceTable(buildDistances(onLevel), "serial");
  }

  /**
   * Search the whole state space on `workers` threads.
   * @param {{workers?: number, onLevel?: Progress}} [options]
   * @returns {Promise<DistanceTable>}
   */
  static async buildParallel(options) {
    const { distances, workers } = await buildDistancesParallel(options);
    return new DistanceTable(distances, `parallel/${workers}`);
  }

  /** Bytes of memory the table occupies. */
  get byteLength() {
    return this.distances.length;
  }

  /**
   * Quarter turns from the encoded state `index` to solved.
   * @param {number} index
   * @returns {number}
   */
  distance(index) {
    return this.distances[index] - 1;
  }

  /**
   * States per distance, index 0 being the solved state.
   * @returns {number[]}
   */
  histogram() {
    const counts = new Uint32Array(256);
    const { distances } = this;
    for (let index = 0; index < distances.length; index++) counts[distances[index]]++;
    let last = counts.length - 1;
    while (last > 0 && counts[last] === 0) last--;
    return [...counts.subarray(1, last + 1)];
  }

  /**
   * Walk downhill: one table lookup per neighbour, `d` steps in total.
   * @param {Uint8Array} state
   * @returns {string}
   */
  solve(state) {
    const { distances } = this;
    const width = N_ORIENTATIONS;
    const tables = wideTables();

    let index = encode(state);
    let remaining = distances[index] - 1;
    let path = "";
    while (remaining > 0) {
      const quotient = (index / width) | 0;
      const remainder = index - quotient * width;
      let stepped = false;
      for (const { move, permutation, orientation } of tables) {
        const child = permutation[quotient] + orientation[remainder];
        if (distances[child] === remaining) {
          // === (remaining - 1) + 1: one move closer.
          path += move;
          index = child;
          remaining -= 1;
          stepped = true;
          break;
        }
      }
      if (!stepped) throw new UnsolvableError("distance table is inconsistent");
    }
    return path;
  }
}
