//! God's algorithm: the exact distance of every reachable state.
//!
//! The pocket cube has 3,674,160 positions — few enough to hold every one's
//! exact distance to solved in a flat byte array of 3.7 MB. Then there is
//! nothing left to search, only a table to read:
//!
//! > at a state *d* moves from solved, exactly one neighbour is *d-1* away;
//! > step to it, repeat.
//!
//! A solve costs *d* lookups: constant time in the difficulty of the scramble,
//! and optimal by construction. This is the classic result for the 2x2x2 —
//! Jerry Bryan computed the same distribution in 1993, and it is recorded as
//! OEIS A079762, which [`DistanceTable::histogram`] reproduces exactly.
//!
//! The table is built in memory and never cached to disk, so it always matches
//! the code that produced it. Building costs one exhaustive breadth-first
//! search over 22 million edges, which is the whole trade: instant solves, paid
//! for up front. Above roughly a few thousand solves it is the cheapest option
//! there is; below that, building it costs more than it saves.

use std::num::NonZero;
use std::sync::atomic::{AtomicU8, Ordering::Relaxed};
use std::thread;

use crate::coords::{self, N_ORIENTATIONS, N_STATES, SOLVED_INDEX};
use crate::cube::State;
use crate::error::{Error, Result};

/// Levels smaller than this are not worth handing to threads: the first six
/// hold 694 states between them, less than the cost of spawning.
const PARALLEL_MIN: usize = 4096;

/// Distance-to-solved for all 3,674,160 states, stored as `distance + 1`.
///
/// Zero would mean "not yet reached", which is what the build uses it for.
#[derive(PartialEq, Eq)]
pub struct DistanceTable {
    distances: Vec<u8>,
}

impl DistanceTable {
    /// Breadth-first over the whole space, on one thread.
    pub fn build() -> Self {
        let (permutation, orientation) = lookups();
        let mut distances = vec![0u8; N_STATES as usize];
        distances[SOLVED_INDEX as usize] = 1;

        let mut level = vec![SOLVED_INDEX];
        let mut depth = 0u8;
        while !level.is_empty() {
            depth += 1;
            let marker = depth + 1;
            let mut following = Vec::with_capacity(level.len() * 3);
            for &node in &level {
                let (row, column) = split(node);
                for turn in 0..6 {
                    let child = (permutation[turn][row] + orientation[turn][column]) as usize;
                    if distances[child] == 0 {
                        distances[child] = marker;
                        following.push(child as u32);
                    }
                }
            }
            level = following;
        }
        Self { distances }
    }

    /// The same search, with each level split across `workers` threads.
    ///
    /// The search is level-synchronous, so within a level the frontier can be
    /// partitioned freely; the only shared mutable state is the visited array.
    /// Two workers can reach the same state at the same moment, and the one
    /// that wins the `compare_exchange` is the one that queues it — so every
    /// state is queued exactly once and the resulting table is byte-for-byte
    /// what [`DistanceTable::build`] produces, whatever order the threads run
    /// in. Only the order of states *within* a level varies, and nothing
    /// depends on that.
    ///
    /// `Relaxed` is enough: a compare-exchange is atomic on its own location
    /// whatever the ordering, so no state can be claimed twice, and joining the
    /// workers at the end of every level is what publishes one level's writes
    /// to the next.
    pub fn build_parallel(workers: usize) -> Self {
        let distances: Vec<AtomicU8> = (0..N_STATES).map(|_| AtomicU8::new(0)).collect();
        distances[SOLVED_INDEX as usize].store(1, Relaxed);
        let shared = &distances;

        let mut level = vec![SOLVED_INDEX];
        let mut depth = 0u8;
        while !level.is_empty() {
            depth += 1;
            let marker = depth + 1;
            level = if workers < 2 || level.len() < PARALLEL_MIN {
                claim(&level, shared, marker)
            } else {
                let chunk = level.len().div_ceil(workers);
                thread::scope(|scope| {
                    let workers: Vec<_> = level
                        .chunks(chunk)
                        .map(|slice| scope.spawn(move || claim(slice, shared, marker)))
                        .collect();
                    workers
                        .into_iter()
                        .flat_map(|worker| worker.join().expect("table worker panicked"))
                        .collect()
                })
            };
        }

        Self { distances: distances.into_iter().map(AtomicU8::into_inner).collect() }
    }

    /// Quarter turns from the encoded state `index` to solved.
    pub fn distance(&self, index: u32) -> u8 {
        self.distances[index as usize] - 1
    }

    /// Resident size in bytes.
    pub fn size_bytes(&self) -> usize {
        self.distances.len()
    }

    /// States per distance, index 0 being the solved state. Must equal
    /// OEIS A079762 — see `tests/table.rs`.
    pub fn histogram(&self) -> Vec<u32> {
        let mut counts = [0u32; 256];
        for &marker in &self.distances {
            counts[marker as usize] += 1;
        }
        let deepest = counts.iter().rposition(|&count| count > 0).unwrap_or(0);
        counts[1..=deepest].to_vec()
    }

    /// Walk downhill: one lookup per candidate move, per step.
    pub fn solve(&self, state: &State) -> Result<String> {
        let tables = coords::tables();
        let mut index = coords::encode(state)?;
        let mut remaining = self.distance(index);
        let mut path = Vec::with_capacity(remaining as usize);

        while remaining > 0 {
            let (row, column) = split(index);
            let step = tables.moves.iter().enumerate().find_map(|(turn, table)| {
                let child = table.permutation[row] + table.orientation[column];
                // `remaining` is the stored marker of a state one move closer.
                (self.distances[child as usize] == remaining).then_some((turn, child))
            });
            let (turn, child) = step.ok_or(Error::NoSolution(crate::MAX_DEPTH))?;
            path.push(turn);
            index = child;
            remaining -= 1;
        }
        Ok(coords::moves_to_string(path))
    }
}

/// Split an index into its permutation row and orientation column.
fn split(index: u32) -> (usize, usize) {
    ((index / N_ORIENTATIONS) as usize, (index % N_ORIENTATIONS) as usize)
}

/// The six move tables as plain slices.
///
/// Hoisting them out of the search costs one line and buys a fifth of the build
/// time: inside the loop they would be reached through a `Box<[u32]>` in a
/// struct, so every one of the 22 million edges pays for the extra indirection.
fn lookups() -> ([&'static [u32]; 6], [&'static [u32]; 6]) {
    let tables = coords::tables();
    (
        std::array::from_fn(|turn| tables.moves[turn].permutation.as_ref()),
        std::array::from_fn(|turn| tables.moves[turn].orientation.as_ref()),
    )
}

/// Expand one slice of a level, claiming unvisited children atomically.
fn claim(level: &[u32], distances: &[AtomicU8], marker: u8) -> Vec<u32> {
    let (permutation, orientation) = lookups();
    let mut following = Vec::with_capacity(level.len() * 3);
    for &node in level {
        let (row, column) = split(node);
        for turn in 0..6 {
            let child = (permutation[turn][row] + orientation[turn][column]) as usize;
            let cell = &distances[child];
            // A relaxed load first: the overwhelming majority of children are
            // already visited, and a plain load is far cheaper than a failed
            // exchange on a contended cache line.
            if cell.load(Relaxed) == 0 && cell.compare_exchange(0, marker, Relaxed, Relaxed).is_ok()
            {
                following.push(child as u32);
            }
        }
    }
    following
}

/// How many threads [`DistanceTable::build_parallel`] should use by default.
pub fn workers() -> usize {
    thread::available_parallelism().map_or(1, NonZero::get)
}
