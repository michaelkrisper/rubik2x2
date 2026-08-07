//! Optimal solvers for the 2x2x2 Rubik's cube (pocket cube, mini cube).
//!
//! Every solver here returns a *shortest* move sequence, never merely a working
//! one. Three of them are implemented, and they differ only in where they spend
//! their time:
//!
//! - [`bfs`] — bidirectional breadth-first search. No precomputation at all.
//! - [`ida`] — iterative-deepening A* over two pattern databases.
//! - [`table`] — the exact distance of all 3,674,160 states, then pure lookup.
//!
//! # Layering
//!
//! [`cube`] is the readable model: 24 sticker colours and a permutation table
//! per move. It is what a human reads and what the tests check the rest
//! against, and it knows nothing about searching.
//!
//! [`coords`] is the compact model the solvers actually run on. Holding the
//! down-back-right corner still removes the 24-fold whole-cube-rotation
//! symmetry, which leaves 7 free corner pieces in 7 free slots (7! = 5040
//! arrangements) each twisted one of three ways (3^6 = 729, since the twists
//! must sum to a multiple of three). 5040 * 729 = 3,674,160 states, and a state
//! is a single `u32` below that. Crucially a move acts on the two coordinates
//! *independently*, so applying one is two array lookups — and each coordinate
//! alone is a legitimate relaxation of the puzzle, which is where [`ida`] gets
//! its heuristics for free.
//!
//! [`display`] draws a cube. Neither of the two models knows about it.

pub mod bench;
pub mod bfs;
pub mod coords;
pub mod cube;
pub mod display;
pub mod error;
pub mod hash;
pub mod ida;
pub mod memory;
pub mod rng;
pub mod table;

pub use cube::State;
pub use error::{Error, Result};

/// God's number for the pocket cube in the quarter-turn metric.
pub const MAX_DEPTH: u8 = 14;

/// An antipodal position: 14 quarter turns from solved, the hardest there is.
pub const HARDEST: &str = "wppryygrygrwgprbbbwpbwyg";
