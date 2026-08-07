//! One error type for the whole crate.
//!
//! The variants are structured rather than stringly typed so that callers (and
//! tests) can match on *why* something failed, and so that the message is built
//! only when someone actually prints it.

use std::fmt;

/// Everything that can go wrong reading, encoding or solving a cube.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    /// A state string that was not 24 characters long.
    Length(usize),
    /// A character outside the colour alphabet `rwbypg`.
    Color(char),
    /// A colour that did not appear exactly four times.
    ColorCount { color: char, count: usize },
    /// A move letter outside `lLrRtTbBfFaA`.
    UnknownMove(char),
    /// The fixed corner shows colours no solved cube has, in any orientation.
    UnknownCorner([u8; 3]),
    /// The three facelets of a slot do not form a corner of this cube.
    NoCornerAt(usize),
    /// One corner piece occupies two slots, so the arrangement is not a
    /// permutation of the pieces and another corner is missing entirely.
    RepeatedPiece(usize),
    /// The corner twists do not sum to a multiple of three — a corner is
    /// twisted in place, which no sequence of turns can produce. Carries the
    /// sum modulo three, so 1 or 2.
    TwistedCorner(u8),
    /// No solution exists within the given depth.
    NoSolution(u8),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Length(got) => write!(f, "expected 24 colour letters, got {got}"),
            Self::Color(char) => {
                write!(f, "invalid colour letter {char:?} (allowed: {})", crate::cube::COLORS)
            }
            Self::ColorCount { color, count } => {
                write!(f, "colour {color:?} appears {count} times, expected 4")
            }
            Self::UnknownMove(char) => write!(f, "unknown move {char:?}"),
            Self::UnknownCorner(colors) => {
                let colors: String = colors.iter().map(|&c| crate::cube::color_char(c)).collect();
                write!(f, "corner colours {colors:?} do not occur on a solved cube")
            }
            Self::NoCornerAt(slot) => {
                write!(
                    f,
                    "facelets {:?} do not hold a corner of this cube",
                    crate::coords::SLOTS[*slot]
                )
            }
            Self::RepeatedPiece(piece) => {
                write!(
                    f,
                    "the corner of facelets {:?} occupies two slots at once, \
                     so this cube cannot exist",
                    crate::coords::SLOTS[*piece]
                )
            }
            Self::TwistedCorner(remainder) => {
                write!(
                    f,
                    "the corner twists sum to {remainder} modulo 3: a corner is twisted \
                     in place, which no sequence of turns can produce"
                )
            }
            Self::NoSolution(depth) => write!(f, "no solution within {depth} moves"),
        }
    }
}

impl std::error::Error for Error {}

/// Shorthand for results carrying this crate's [`Error`].
pub type Result<T> = std::result::Result<T, Error>;
