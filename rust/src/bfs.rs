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

use std::cell::RefCell;

use crate::coords::{self, N_STATES, SOLVED_INDEX};
use crate::cube::{State, invert};
use crate::error::{Error, Result};

/// How a visited state is recorded, packed into one `u32`:
///
/// | bits    | meaning                                              |
/// |---------|------------------------------------------------------|
/// | `0..3`  | the move that arrived here (`ROOT_MOVE` for a root)   |
/// | `3..25` | the state it arrived from (3,674,160 < 2²²)          |
/// | `25`    | which frontier claimed it                            |
/// | `26..`  | generation stamp                                     |
///
/// A cube state is already a dense index below [`N_STATES`], so the visited set
/// is a flat array subscripted by the state itself rather than a hash map: no
/// hashing, no probing, and one array write per edge instead of a lookup, a
/// second lookup and an insert.
const MOVE_BITS: u32 = 3;
const MOVE_MASK: u32 = (1 << MOVE_BITS) - 1;
const ROOT_MOVE: u32 = MOVE_MASK; // no real move index reaches 7
const PARENT_POS: u32 = MOVE_BITS;
const PARENT_BITS: u32 = 22;
const PARENT_MASK: u32 = (1 << PARENT_BITS) - 1;
const SIDE_POS: u32 = PARENT_POS + PARENT_BITS;
const SIDE_BIT: u32 = 1 << SIDE_POS;
const GEN_POS: u32 = SIDE_POS + 1;
const GEN_MASK: u32 = !(SIDE_BIT | (PARENT_MASK << PARENT_POS) | MOVE_MASK);
const MAX_GEN: u32 = (u32::MAX >> GEN_POS) - 1;

/// The working set of one solve, kept alive between solves.
///
/// Allocating 14.7 MB per solve would cost far more than the search; instead a
/// generation stamp ages the marks, so a solve bumps a counter and only a wrap
/// every 63 solves pays for a real clear. Thread-local rather than global, so
/// concurrent callers get one each without a lock.
struct Scratch {
    marks: Vec<u32>,
    generation: u32,
    buffers: [Vec<u32>; 3],
}

impl Scratch {
    fn new() -> Self {
        Self { marks: vec![0; N_STATES as usize], generation: 0, buffers: Default::default() }
    }

    /// Begin a solve; returns the tag this solve's marks carry.
    fn stamp(&mut self) -> u32 {
        if self.generation >= MAX_GEN {
            self.marks.fill(0);
            self.generation = 0;
        }
        self.generation += 1;
        self.generation << GEN_POS
    }
}

thread_local! {
    static SCRATCH: RefCell<Scratch> = RefCell::new(Scratch::new());
}

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

    SCRATCH.with(|scratch| {
        let mut scratch = scratch.borrow_mut();
        let generation = scratch.stamp();
        let Scratch { marks, buffers, .. } = &mut *scratch;
        let tables = coords::tables();

        marks[start as usize] = generation | ROOT_MOVE;
        marks[SOLVED_INDEX as usize] = generation | SIDE_BIT | ROOT_MOVE;

        let [forward, backward, following] = buffers;
        forward.clear();
        forward.push(start);
        backward.clear();
        backward.push(SOLVED_INDEX);

        for _ in 0..max_depth {
            // Always grow the cheaper side, which keeps both halves shallow.
            let from_goal = backward.len() < forward.len();
            let (level, side) = if from_goal { (&*backward, SIDE_BIT) } else { (&*forward, 0) };

            following.clear();
            let mut meeting = None;

            'level: for &node in level.iter() {
                for turn in 0..tables.moves.len() {
                    let child = tables.apply(node, turn);
                    let mark = marks[child as usize];
                    if mark & GEN_MASK == generation {
                        if mark & SIDE_BIT == side {
                            continue; // this side has already been here
                        }
                        meeting = Some((child, node, turn));
                        break 'level;
                    }
                    marks[child as usize] = generation | side | node << PARENT_POS | turn as u32;
                    following.push(child);
                }
            }

            if let Some((met, from, turn)) = meeting {
                let mut reached = trail(marks, from);
                reached.push(turn);
                let reached = coords::moves_to_string(reached);
                let opposite = coords::moves_to_string(trail(marks, met));
                return Ok(if from_goal {
                    opposite + &invert(&reached)
                } else {
                    reached + &invert(&opposite)
                });
            }

            if from_goal {
                std::mem::swap(backward, following);
            } else {
                std::mem::swap(forward, following);
            }
            if forward.is_empty() || backward.is_empty() {
                break;
            }
        }

        Err(Error::NoSolution(max_depth))
    })
}

/// Walk the marks back to this frontier's root, yielding the moves in forward
/// order: the sequence that turns that root into `node`.
fn trail(marks: &[u32], mut node: u32) -> Vec<usize> {
    let mut turns = Vec::new();
    loop {
        let mark = marks[node as usize];
        let turn = mark & MOVE_MASK;
        if turn == ROOT_MOVE {
            break;
        }
        turns.push(turn as usize);
        node = (mark >> PARENT_POS) & PARENT_MASK;
    }
    turns.reverse();
    turns
}
