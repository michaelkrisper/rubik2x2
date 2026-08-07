//! Bidirectional breadth-first search: the solver that needs no setup.
//!
//! Two frontiers grow a level at a time — one from the scramble, one from the
//! goal — and the search stops where they touch. God's number for a pocket cube
//! is 14 quarter turns and the level sizes peak at 1,350,852 states at depth 11,
//! so a one-sided search would have to walk most of the space. Meeting in the
//! middle needs depth 7 from each side, where only 33,058 states live.
//!
//! Expanding by whole levels is what makes the result optimal. When the
//! frontiers meet, every shorter pairing has already been tried, so the first
//! meeting point is a shortest solution — no heuristic, no tie-breaking.

use crate::coords::{self, SOLVED_INDEX, Tables};
use crate::cube::{State, invert};
use crate::error::{Error, Result};
use crate::hash::IntMap;

/// A path of at most ten moves, packed three bits at a time.
///
/// Each side of the search reaches depth seven at worst, and there are up to
/// 45,000 nodes per side, so keeping paths as `String`s would mean tens of
/// thousands of little allocations. A `u32` needs none.
#[derive(Clone, Copy, Default, PartialEq, Eq, Debug)]
struct Path {
    code: u32,
    len: u8,
}

impl Path {
    fn push(self, turn: usize) -> Self {
        Self { code: self.code | (turn as u32) << (3 * self.len), len: self.len + 1 }
    }

    fn turns(self) -> impl Iterator<Item = usize> {
        (0..self.len).map(move |i| ((self.code >> (3 * i)) & 0b111) as usize)
    }

    fn to_moves(self) -> String {
        coords::moves_to_string(self.turns())
    }
}

type Seen = IntMap<u32, Path>;

/// Return a shortest move sequence solving `state`; empty if already solved.
pub fn solve(state: &State) -> Result<String> {
    solve_within(state, crate::MAX_DEPTH)
}

/// [`solve`], with a cap on how deep the search may go.
pub fn solve_within(state: &State, max_depth: u8) -> Result<String> {
    let start = coords::encode(state)?;
    if start == SOLVED_INDEX {
        return Ok(String::new());
    }

    let tables = coords::tables();
    let mut forward = Seen::from_iter([(start, Path::default())]);
    let mut backward = Seen::from_iter([(SOLVED_INDEX, Path::default())]);
    let mut forward_level = vec![start];
    let mut backward_level = vec![SOLVED_INDEX];

    for _ in 0..max_depth {
        // Always grow the cheaper side, which keeps both halves shallow.
        let from_goal = backward_level.len() < forward_level.len();
        let mut following = Vec::new();
        let meeting = if from_goal {
            expand(&backward_level, &mut backward, &forward, tables, &mut following)
        } else {
            expand(&forward_level, &mut forward, &backward, tables, &mut following)
        };

        if let Some((extended, met)) = meeting {
            let (from_start, to_goal) = if from_goal { (met, extended) } else { (extended, met) };
            return Ok(from_start.to_moves() + &invert(&to_goal.to_moves()));
        }

        if from_goal {
            backward_level = following;
        } else {
            forward_level = following;
        }
        if forward_level.is_empty() || backward_level.is_empty() {
            break;
        }
    }

    Err(Error::NoSolution(max_depth))
}

/// One level of one side. Returns the two half-paths as soon as a child lands
/// in the other side's map.
fn expand(
    level: &[u32],
    seen: &mut Seen,
    other: &Seen,
    tables: &Tables,
    following: &mut Vec<u32>,
) -> Option<(Path, Path)> {
    for &node in level {
        let path = seen[&node];
        for turn in 0..tables.moves.len() {
            let child = tables.apply(node, turn);
            if seen.contains_key(&child) {
                continue;
            }
            let extended = path.push(turn);
            if let Some(&met) = other.get(&child) {
                return Some((extended, met));
            }
            seen.insert(child, extended);
            following.push(child);
        }
    }
    None
}
