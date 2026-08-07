//! Korf's IDA* with pattern databases — the textbook optimal cube solver.
//!
//! Iterative-deepening A* runs depth-first passes with a rising limit on
//! `g + h`, where `h` is a lower bound on the moves still needed. Korf's
//! contribution was to get `h` from *pattern databases*: exact distances for a
//! relaxed version of the puzzle, precomputed once.
//!
//! Here the relaxations fall out of the encoding for free. A move acts on the
//! permutation coordinate and the orientation coordinate independently
//! ([`crate::coords`]), so each coordinate on its own is a genuine quotient of
//! the state graph — solving the cube necessarily solves both projections.
//! Their exact distances (5040 and 729 entries, computed in microseconds) are
//! therefore admissible, and the larger of the two is the better bound.
//!
//! This is here for comparison, not because it wins. On a 2x2x2 it loses to
//! plain bidirectional search, and measurably so: both databases top out at
//! distance 8 while real positions reach 14, leaving the bound too loose to
//! prune well. The reason is structural — pattern databases pay off when a
//! puzzle has more pieces than you can afford to track and you keep only some
//! of them, and a pocket cube is nothing *but* the pieces the databases already
//! cover.

use std::sync::LazyLock;

use crate::coords::{self, N_ORIENTATIONS, N_PERMUTATIONS, SOLVED_INDEX, Tables};
use crate::cube::State;
use crate::error::{Error, Result};

/// Exact distances for the two coordinate projections of the cube, stored as
/// `distance + 1` so that 0 can mean "not reached yet" during the build.
pub struct PatternDatabases {
    permutation: Vec<u8>,
    orientation: Vec<u8>,
}

impl PatternDatabases {
    /// Breadth-first over each coordinate on its own. Both fit in 5.8 kB.
    pub fn build() -> Self {
        let tables = coords::tables();
        Self {
            permutation: project(N_PERMUTATIONS, |rank, turn| {
                tables.moves[turn].permutation_rank(rank)
            }),
            orientation: project(N_ORIENTATIONS, |rank, turn| {
                tables.moves[turn].orientation[rank as usize]
            }),
        }
    }

    /// Admissible lower bound on the moves left from an encoded state.
    pub fn bound(&self, index: u32) -> u8 {
        let permutation = self.permutation[(index / N_ORIENTATIONS) as usize];
        let orientation = self.orientation[(index % N_ORIENTATIONS) as usize];
        permutation.max(orientation) - 1
    }

    /// The largest bound each database can ever return.
    pub fn reach(&self) -> (u8, u8) {
        let largest = |database: &[u8]| database.iter().copied().max().unwrap_or(1) - 1;
        (largest(&self.permutation), largest(&self.orientation))
    }

    /// Shortest solution by iterative-deepening A*.
    pub fn solve(&self, state: &State) -> Result<String> {
        self.solve_within(state, crate::MAX_DEPTH)
    }

    /// [`PatternDatabases::solve`], with a cap on how deep to iterate.
    pub fn solve_within(&self, state: &State, max_depth: u8) -> Result<String> {
        let index = coords::encode(state)?;
        let tables = coords::tables();
        let mut path = Vec::with_capacity(max_depth as usize);

        for limit in self.bound(index)..=max_depth {
            if self.descend(tables, index, 0, limit, usize::MAX, &mut path) {
                return Ok(coords::moves_to_string(path));
            }
            path.clear();
        }
        Err(Error::NoSolution(max_depth))
    }

    fn descend(
        &self,
        tables: &Tables,
        index: u32,
        depth: u8,
        limit: u8,
        last: usize,
        path: &mut Vec<usize>,
    ) -> bool {
        if depth + self.bound(index) > limit {
            return false;
        }
        if index == SOLVED_INDEX {
            return true;
        }
        for turn in 0..tables.moves.len() {
            // SOLVE_MOVES pairs each turn with its inverse at adjacent indices,
            // so `turn ^ 1` is the move that would undo the previous one.
            if turn ^ 1 == last {
                continue;
            }
            path.push(turn);
            if self.descend(tables, tables.apply(index, turn), depth + 1, limit, turn, path) {
                return true;
            }
            path.pop();
        }
        false
    }
}

/// Exact distance-to-solved within one coordinate. Returns `distance + 1`.
fn project(size: u32, step: impl Fn(u32, usize) -> u32) -> Vec<u8> {
    let mut distances = vec![0u8; size as usize];
    distances[0] = 1;
    let mut level = vec![0u32];
    let mut depth = 0u8;

    while !level.is_empty() {
        depth += 1;
        let mut following = Vec::new();
        for &node in &level {
            for turn in 0..6 {
                let child = step(node, turn);
                if distances[child as usize] == 0 {
                    distances[child as usize] = depth + 1;
                    following.push(child);
                }
            }
        }
        level = following;
    }
    distances
}

static DATABASES: LazyLock<PatternDatabases> = LazyLock::new(PatternDatabases::build);

/// The pattern databases, built on first use.
pub fn databases() -> &'static PatternDatabases {
    &DATABASES
}

/// Shortest solution by iterative-deepening A*, using the shared databases.
pub fn solve(state: &State) -> Result<String> {
    databases().solve(state)
}
