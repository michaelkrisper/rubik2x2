//! Cubie coordinates: the compact state encoding every solver runs on.
//!
//! [`crate::cube`] models a state as 24 stickers, which is what a human reads
//! but not what a search wants. A pocket cube with one corner held still is
//! described by far less: which of the 7 free corner *pieces* sits in each of
//! the 7 free *slots* (7! = 5040 arrangements) and how each is twisted (3^7,
//! minus the constraint that the twists sum to a multiple of 3, so 3^6 = 729).
//! That is 5040 * 729 = 3,674,160 states — exactly what an exhaustive search
//! over stickers finds.
//!
//! The payoff is that a move acts on the two halves independently:
//!
//! ```text
//! new_permutation[slot] = old_permutation[source[slot]]
//! new_orientation[slot] = old_orientation[source[slot]] + twist[slot]  (mod 3)
//! ```
//!
//! Neither half depends on the other, so a move is two lookups in tables of
//! 5040 and 729 entries and a whole state is a single `u32` below 3,674,160.
//! That dense bounded index is what makes [`crate::table`] possible at all, and
//! what hands [`crate::ida`] its pattern databases for free.
//!
//! Everything here is verified against the sticker model in `tests/coords.rs`.

use std::sync::LazyLock;

use crate::cube::{self, FIXED_CORNER, SOLVE_MOVES, SOLVED, State};
use crate::error::{Error, Result};

/// Each corner slot as its three facelets, listed in a consistent rotational
/// sense so that "twist" means the same thing at every slot: the order is
/// (x-face, y-face, z-face) where `sign(x) * sign(y) * sign(z) == +1` and
/// (x-face, z-face, y-face) otherwise, which makes all eight triples agree in
/// handedness. Slot 7 is the corner the solve moves never touch.
pub const SLOTS: [[usize; 3]; 8] = [
    [16, 0, 14],  // left-up-back
    [21, 15, 1],  // right-up-back
    [17, 4, 2],   // left-up-front
    [20, 3, 5],   // right-up-front
    [19, 8, 6],   // left-down-front
    [22, 7, 9],   // right-down-front
    [18, 12, 10], // left-down-back
    [23, 11, 13], // right-down-back — fixed
];

/// Number of slots that actually move.
pub const FREE: usize = 7;

pub const N_ORIENTATIONS: u32 = 729;
pub const N_PERMUTATIONS: u32 = 5040;
pub const N_STATES: u32 = N_PERMUTATIONS * N_ORIENTATIONS;

/// A solved cube is the identity permutation with no twists, which ranks as 0.
pub const SOLVED_INDEX: u32 = 0;

/// The canonical colour triple of each of the eight corner pieces.
pub type Pieces = [[u8; 3]; 8];

const FACTORIAL: [u32; FREE] = [1, 1, 2, 6, 24, 120, 720];

/// The colour triple each slot shows on a solved cube.
pub fn pieces_of(goal: &State) -> Pieces {
    std::array::from_fn(|slot| std::array::from_fn(|i| goal[SLOTS[slot][i]]))
}

/// A corner's colours rotated left by `twist`, which is what a twist *is*.
fn rotated(colors: &[u8; 3], twist: u8) -> [u8; 3] {
    std::array::from_fn(|i| colors[(i + twist as usize) % 3])
}

/// Split a sticker state into (piece per slot, twist per slot).
///
/// This is where a cube is judged physically possible, because it is the last
/// place that can tell. Ranking is lossy on purpose — only six twists are
/// stored and the seventh is implied — so an impossible cube handed to the
/// ranker does not fail, it *normalises*: it silently becomes some other,
/// legal, cube, and every solver downstream then answers confidently about the
/// wrong puzzle. Two conditions therefore have to be caught here, before the
/// ranking that would hide them:
///
/// - **the permutation must be a bijection.** A piece can sit in one slot only;
///   a repeat means a piece was cloned and another lost.
/// - **the twists must sum to a multiple of three.** Turning faces can never
///   change that sum, so a cube whose corners do not add up cannot have been
///   built by turning any face — the classic "one corner twisted in place" that
///   looks solved and is not solvable.
pub fn to_cubies(state: &State, pieces: &Pieces) -> Result<([u8; FREE], [u8; FREE])> {
    let mut permutation = [0u8; FREE];
    let mut orientation = [0u8; FREE];
    // The fixed corner already occupies its own slot, so its piece starts out
    // claimed: finding it again in a free slot is a repeat like any other.
    let mut claimed = 1u16 << FREE;

    for (slot, facelets) in SLOTS[..FREE].iter().enumerate() {
        let colors: [u8; 3] = std::array::from_fn(|i| state[facelets[i]]);
        let (piece, twist) = pieces
            .iter()
            .enumerate()
            .find_map(|(piece, canonical)| {
                (0..3)
                    .find(|&twist| rotated(canonical, twist) == colors)
                    .map(|twist| (piece, twist))
            })
            .ok_or(Error::NoCornerAt(slot))?;

        if claimed & (1 << piece) != 0 {
            return Err(Error::RepeatedPiece(piece));
        }
        claimed |= 1 << piece;
        permutation[slot] = piece as u8;
        orientation[slot] = twist;
    }

    let sum: u8 = orientation.iter().sum();
    if sum % 3 != 0 {
        return Err(Error::TwistedCorner(sum % 3));
    }
    Ok((permutation, orientation))
}

/// Rebuild a sticker state. Inverse of [`to_cubies`].
pub fn from_cubies(permutation: &[u8; FREE], orientation: &[u8; FREE], pieces: &Pieces) -> State {
    let mut state = [0u8; 24];
    for (slot, facelets) in SLOTS.iter().enumerate() {
        let colors = if slot < FREE {
            rotated(&pieces[permutation[slot] as usize], orientation[slot])
        } else {
            pieces[FREE] // the fixed corner never moves and never twists
        };
        for (&facelet, color) in facelets.iter().zip(colors) {
            state[facelet] = color;
        }
    }
    state
}

/// Lehmer code: the identity ranks 0, which is what makes the solved state
/// index 0 and lets the searches use "reached 0" as their goal test.
fn rank_permutation(permutation: &[u8; FREE]) -> u32 {
    permutation
        .iter()
        .enumerate()
        .map(|(i, &piece)| {
            let smaller = permutation[i + 1..].iter().filter(|&&other| other < piece).count();
            smaller as u32 * FACTORIAL[FREE - 1 - i]
        })
        .sum()
}

fn unrank_permutation(mut rank: u32) -> [u8; FREE] {
    let mut available: [u8; FREE] = std::array::from_fn(|i| i as u8);
    let mut permutation = [0u8; FREE];
    for (i, slot) in permutation.iter_mut().enumerate() {
        let factorial = FACTORIAL[FREE - 1 - i];
        let chosen = (rank / factorial) as usize;
        rank %= factorial;
        *slot = available[chosen];
        available.copy_within(chosen + 1..FREE - i, chosen);
    }
    permutation
}

fn rank_orientation(orientation: &[u8; FREE]) -> u32 {
    orientation[..FREE - 1].iter().rev().fold(0, |value, &twist| value * 3 + twist as u32)
}

fn unrank_orientation(mut value: u32) -> [u8; FREE] {
    let mut orientation = [0u8; FREE];
    for twist in &mut orientation[..FREE - 1] {
        *twist = (value % 3) as u8;
        value /= 3;
    }
    // The twists of a real cube sum to a multiple of three, so the last one is
    // implied — which is why 3^6 and not 3^7 states exist.
    let sum: u8 = orientation.iter().sum();
    orientation[FREE - 1] = (3 - sum % 3) % 3;
    orientation
}

/// Sticker state -> dense index below [`N_STATES`], using an explicit goal.
pub fn encode_with(state: &State, pieces: &Pieces) -> Result<u32> {
    let (permutation, orientation) = to_cubies(state, pieces)?;
    Ok(rank_permutation(&permutation) * N_ORIENTATIONS + rank_orientation(&orientation))
}

/// Sticker state -> dense index below [`N_STATES`].
///
/// The cube's own goal orientation is looked up first, so all 24 ways of
/// holding the same scramble encode to the same index and the goal index is
/// always 0.
pub fn encode(state: &State) -> Result<u32> {
    encode_with(state, tables().pieces_for(state)?)
}

/// Dense index -> sticker state, in the standard orientation. Inverse of
/// [`encode`] up to the whole-cube rotation the index deliberately forgets.
pub fn decode(index: u32) -> State {
    decode_with(index, &SOLVED_PIECES)
}

/// Dense index -> sticker state relative to an explicit goal.
pub fn decode_with(index: u32, pieces: &Pieces) -> State {
    let permutation = unrank_permutation(index / N_ORIENTATIONS);
    let orientation = unrank_orientation(index % N_ORIENTATIONS);
    from_cubies(&permutation, &orientation, pieces)
}

static SOLVED_PIECES: LazyLock<Pieces> = LazyLock::new(|| pieces_of(&SOLVED));

/// One solve move, as the pair of lookup tables it becomes.
/// The letter of each move stays in [`SOLVE_MOVES`], indexed the same way, so
/// that there is one source of truth for the notation.
pub struct MoveTable {
    /// Permutation rank -> permutation rank, **pre-multiplied by
    /// [`N_ORIENTATIONS`]** so that a child is one addition rather than a
    /// multiply and an addition. Worth having in a loop that runs tens of
    /// millions of times.
    pub permutation: Box<[u32]>,
    /// Orientation rank -> orientation rank.
    pub orientation: Box<[u32]>,
}

impl MoveTable {
    /// The plain (not pre-multiplied) permutation image, for the pattern
    /// database that searches the permutation coordinate on its own.
    pub fn permutation_rank(&self, rank: u32) -> u32 {
        self.permutation[rank as usize] / N_ORIENTATIONS
    }
}

/// One solved cube out of the 24 orientations, keyed by its fixed corner.
struct Goal {
    corner: [u8; 3],
    pieces: Pieces,
}

/// Everything derived from the move definitions, built once.
pub struct Tables {
    /// One entry per [`SOLVE_MOVES`], in that order — inverses adjacent.
    pub moves: [MoveTable; 6],
    goals: Vec<Goal>,
}

impl Tables {
    fn build() -> Self {
        let pieces = pieces_of(&SOLVED);
        Self {
            moves: std::array::from_fn(|i| Self::build_move(SOLVE_MOVES[i], &pieces)),
            // A cube may be handed over in any of the 24 orientations. Its
            // fixed corner's colours match exactly one rotation of the solved
            // cube, so that corner names the goal; deriving the piece triples
            // from *it* keeps the goal index 0 whichever way the cube is held.
            goals: cube::orientations(&SOLVED)
                .iter()
                .map(|goal| Goal {
                    corner: std::array::from_fn(|i| goal[FIXED_CORNER[i]]),
                    pieces: pieces_of(goal),
                })
                .collect(),
        }
    }

    fn build_move(name: char, pieces: &Pieces) -> MoveTable {
        let identity = from_cubies(&std::array::from_fn(|i| i as u8), &[0; FREE], pieces);
        let turned = cube::apply_move(&identity, name).expect("solve moves exist");
        // On the identity state the permutation *is* the slot-to-slot mapping
        // and the orientation *is* the twist each slot picks up.
        let (source, twist) = to_cubies(&turned, pieces).expect("a turned cube has corners");

        let permutation = (0..N_PERMUTATIONS)
            .map(|rank| {
                let old = unrank_permutation(rank);
                let new = std::array::from_fn(|slot| old[source[slot] as usize]);
                rank_permutation(&new) * N_ORIENTATIONS
            })
            .collect();
        let orientation = (0..N_ORIENTATIONS)
            .map(|rank| {
                let old = unrank_orientation(rank);
                let new =
                    std::array::from_fn(|slot| (old[source[slot] as usize] + twist[slot]) % 3);
                rank_orientation(&new)
            })
            .collect();

        MoveTable { permutation, orientation }
    }

    /// The piece triples of the solved cube this state is aiming at.
    fn pieces_for(&self, state: &State) -> Result<&Pieces> {
        let corner: [u8; 3] = std::array::from_fn(|i| state[FIXED_CORNER[i]]);
        self.goals
            .iter()
            .find(|goal| goal.corner == corner)
            .map(|goal| &goal.pieces)
            .ok_or(Error::UnknownCorner(corner))
    }

    /// Apply one solve move to an encoded state.
    pub fn apply(&self, index: u32, turn: usize) -> u32 {
        let table = &self.moves[turn];
        table.permutation[(index / N_ORIENTATIONS) as usize]
            + table.orientation[(index % N_ORIENTATIONS) as usize]
    }
}

static TABLES: LazyLock<Tables> = LazyLock::new(Tables::build);

/// The move tables, built on first use (~35k entries, a few milliseconds).
pub fn tables() -> &'static Tables {
    &TABLES
}

/// The move letters of a solution given as move indices.
pub fn moves_to_string(turns: impl IntoIterator<Item = usize>) -> String {
    turns.into_iter().map(|turn| SOLVE_MOVES[turn]).collect()
}
