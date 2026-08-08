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

/// How a visited state is recorded, packed into one `u16`:
///
/// | bits    | meaning                                    |
/// |---------|--------------------------------------------|
/// | `0..4`  | its distance from this frontier's root     |
/// | `4`     | which frontier claimed it                  |
/// | `5..`   | generation stamp                           |
///
/// A cube state is already a dense index below [`N_STATES`], so the visited set
/// is a flat array subscripted by the state itself rather than a hash map: no
/// hashing, no probing, and one array write per edge instead of a lookup, a
/// second lookup and an insert.
///
/// Recording a distance rather than a parent pointer is what fits the mark in
/// 16 bits: a parent needs 22. Halving the array halves the cache footprint the
/// mark loads fight over, and still leaves eleven bits of stamp, so the wrap
/// that pays for a real clear comes every 2,046 solves instead of every 63.
/// Distances are enough to rebuild the path because breadth-first marks are
/// exact: from a node at distance `d`, exactly the neighbours at `d - 1` on the
/// same side are parents, and there are only six neighbours to try.
const DEPTH_BITS: u16 = 4;
const DEPTH_MASK: u16 = (1 << DEPTH_BITS) - 1;
const SIDE_BIT: u16 = 1 << DEPTH_BITS;
const GEN_POS: u16 = DEPTH_BITS + 1;
const GEN_MASK: u16 = !(SIDE_BIT | DEPTH_MASK);
const MAX_GEN: u16 = (u16::MAX >> GEN_POS) - 1;

/// The working set of one solve, kept alive between solves.
///
/// Allocating 7.3 MB per solve would cost far more than the search; instead a
/// generation stamp ages the marks, so a solve bumps a counter and only a wrap
/// every 2,046 solves pays for a real clear. Thread-local rather than global,
/// so concurrent callers get one each without a lock.
struct Scratch {
    marks: Vec<u16>,
    generation: u16,
    buffers: [Vec<u32>; 3],
}

impl Scratch {
    fn new() -> Self {
        Self { marks: vec![0; N_STATES as usize], generation: 0, buffers: Default::default() }
    }

    /// Begin a solve; returns the tag this solve's marks carry.
    fn stamp(&mut self) -> u16 {
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

/// How many frontier entries ahead to name cache lines. Measured; the win is
/// flat from four to eight and turns back down past that.
const PREFETCH_LOOKAHEAD: usize = 6;

/// Name the mark words the loop will want shortly.
///
/// Profiling puts most of the search in one instruction: the mark load, a random
/// word of a 7.3 MB array, on machines with a few MB of last-level cache. The
/// loop therefore waits on memory rather than on arithmetic, and recomputing an
/// address early lets the misses overlap instead of stalling one at a time.
///
/// A miss to name is only a hint, so a wrong or out-of-range guess would merely
/// waste the hint -- but the bounds are checked anyway, since the cost is
/// nothing against a memory stall.
#[inline]
fn prefetch_marks(marks: &[u16], level: &[u32], position: usize, tables: &coords::Tables) {
    #[cfg(target_arch = "x86_64")]
    if let Some(&ahead) = level.get(position) {
        use std::arch::x86_64::{_MM_HINT_T0, _mm_prefetch};
        for turn in 0..tables.moves.len() {
            let child = tables.apply(ahead, turn) as usize;
            // SAFETY: a prefetch only warms a cache line; `child` is a valid
            // index into `marks`, which `apply` guarantees by construction.
            unsafe { _mm_prefetch(marks.as_ptr().add(child).cast(), _MM_HINT_T0) };
        }
    }
    #[cfg(not(target_arch = "x86_64"))]
    let _ = (marks, level, position, tables);
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

        marks[start as usize] = generation;
        marks[SOLVED_INDEX as usize] = generation | SIDE_BIT;

        let [forward, backward, following] = buffers;
        forward.clear();
        forward.push(start);
        backward.clear();
        backward.push(SOLVED_INDEX);

        // The two sides advance independently — each round grows only one of
        // them — so each carries its own level depth, which the marks record.
        let mut forward_depth: u16 = 0;
        let mut backward_depth: u16 = 0;

        for _ in 0..max_depth {
            // Always grow the cheaper side, which keeps both halves shallow.
            let from_goal = backward.len() < forward.len();
            let (level, depth, side) = if from_goal {
                (&*backward, &mut backward_depth, SIDE_BIT)
            } else {
                (&*forward, &mut forward_depth, 0)
            };
            // Every state reached this round gets the same mark, so the store
            // in the inner loop is a constant.
            let reached = generation | side | (*depth + 1);

            following.clear();
            let mut meeting = None;

            'level: for (position, &node) in level.iter().enumerate() {
                prefetch_marks(marks, level, position + PREFETCH_LOOKAHEAD, tables);
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
                    marks[child as usize] = reached;
                    following.push(child);
                }
            }

            if let Some((met, from, turn)) = meeting {
                let here = generation | side;
                let there = here ^ SIDE_BIT;
                let mut reached = trail(marks, tables, from, here);
                reached.push(turn);
                let reached = coords::moves_to_string(reached);
                let opposite = coords::moves_to_string(trail(marks, tables, met, there));
                return Ok(if from_goal {
                    opposite + &invert(&reached)
                } else {
                    reached + &invert(&opposite)
                });
            }

            *depth += 1;
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
///
/// `tag` is the generation and side bit the frontier's marks carry. A neighbour
/// one step shallower under the same tag is a parent by construction, and the
/// move reaching `node` from it is the inverse of the one that got there —
/// adjacent indices, which is why the solve moves pair each move with its
/// inverse.
fn trail(marks: &[u16], tables: &coords::Tables, mut node: u32, tag: u16) -> Vec<usize> {
    let mut turns = Vec::new();
    for depth in (1..=(marks[node as usize] & DEPTH_MASK)).rev() {
        let parent = tag | (depth - 1);
        for turn in 0..tables.moves.len() {
            let back = tables.apply(node, turn ^ 1);
            if marks[back as usize] == parent {
                turns.push(turn);
                node = back;
                break;
            }
        }
    }
    turns.reverse();
    turns
}
