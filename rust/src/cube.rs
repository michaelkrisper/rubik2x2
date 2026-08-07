//! The 2x2x2 pocket cube, modelled as 24 stickers.
//!
//! A state is 24 sticker colour ids, one per facelet, ordered side by side:
//! UP, FRONT, DOWN, BACK, LEFT, RIGHT. Within a side the four stickers are
//! row-major:
//!
//! ```text
//! -------
//! |0  1 |
//! |2  3 |
//! -------
//! ```
//!
//! so the flat index is `side * 4 + row * 2 + column`.
//!
//! Every move is a permutation applied as `new[i] = old[PERM[i]]`. The tables
//! are checked rather than trusted: `tests/cube.rs` pins the algebra they must
//! obey (each move has order four, inverses cancel, opposite faces commute) and
//! building the distance table reproduces OEIS A079762 exactly, which no wrong
//! table could do.
//!
//! This module is the readable model and nothing else — no search, no display.
//! [`crate::coords`] re-encodes states for the solvers, [`crate::display`]
//! draws them.

use std::collections::HashSet;
use std::sync::LazyLock;

use crate::error::{Error, Result};
use crate::rng::Rng;

/// A cube state: 24 sticker colour ids, values 0..6.
///
/// A fixed-size array rather than a `Vec<u8>` — the length is part of the type,
/// the whole thing is `Copy`, and it still derefs to `&[u8]` where that is what
/// a function wants.
pub type State = [u8; 24];

/// A permutation of the 24 facelets.
pub type Permutation = [u8; 24];

pub const UP: usize = 0;
pub const FRONT: usize = 1;
pub const DOWN: usize = 2;
pub const BACK: usize = 3;
pub const LEFT: usize = 4;
pub const RIGHT: usize = 5;

/// Colour letter per colour id: red, white, blue, yellow, purple, green.
pub const COLORS: &str = "rwbypg";

const R: u8 = 0;
const W: u8 = 1;
const B: u8 = 2;
const Y: u8 = 3;
const P: u8 = 4;
const G: u8 = 5;

/// Solved cube in the standard orientation: yellow up, red front.
pub const SOLVED: State = [Y, Y, Y, Y, R, R, R, R, W, W, W, W, P, P, P, P, B, B, B, B, G, G, G, G];

/// Move notation. Lowercase and uppercase are inverse quarter turns of one face:
///
/// ```text
/// l/L  left column down/up        r/R  right column down/up
/// t/T  top layer left/right       b/B  bottom layer left/right
/// f/F  front face cw/ccw          a/A  back face cw/ccw
/// ```
pub const MOVE_NAMES: [char; 12] = ['l', 'L', 'r', 'R', 't', 'T', 'b', 'B', 'f', 'F', 'a', 'A'];

/// One facelet permutation per entry of [`MOVE_NAMES`], in the same order.
pub const MOVE_PERMUTATIONS: [Permutation; 12] = [
    [12, 1, 14, 3, 0, 5, 2, 7, 4, 9, 6, 11, 8, 13, 10, 15, 18, 16, 19, 17, 20, 21, 22, 23],
    [4, 1, 6, 3, 8, 5, 10, 7, 12, 9, 14, 11, 0, 13, 2, 15, 17, 19, 16, 18, 20, 21, 22, 23],
    [0, 13, 2, 15, 4, 1, 6, 3, 8, 5, 10, 7, 12, 9, 14, 11, 16, 17, 18, 19, 21, 23, 20, 22],
    [0, 5, 2, 7, 4, 9, 6, 11, 8, 13, 10, 15, 12, 1, 14, 3, 16, 17, 18, 19, 22, 20, 23, 21],
    [2, 0, 3, 1, 20, 21, 6, 7, 8, 9, 10, 11, 12, 13, 17, 16, 4, 5, 18, 19, 15, 14, 22, 23],
    [1, 3, 0, 2, 16, 17, 6, 7, 8, 9, 10, 11, 12, 13, 21, 20, 15, 14, 18, 19, 4, 5, 22, 23],
    [0, 1, 2, 3, 4, 5, 22, 23, 9, 11, 8, 10, 19, 18, 14, 15, 16, 17, 6, 7, 20, 21, 13, 12],
    [0, 1, 2, 3, 4, 5, 18, 19, 10, 8, 11, 9, 23, 22, 14, 15, 16, 17, 13, 12, 20, 21, 6, 7],
    [0, 1, 19, 17, 6, 4, 7, 5, 22, 20, 10, 11, 12, 13, 14, 15, 16, 8, 18, 9, 2, 21, 3, 23],
    [0, 1, 20, 22, 5, 7, 4, 6, 17, 19, 10, 11, 12, 13, 14, 15, 16, 3, 18, 2, 9, 21, 8, 23],
    [18, 16, 2, 3, 4, 5, 6, 7, 8, 9, 23, 21, 13, 15, 12, 14, 10, 17, 11, 19, 20, 0, 22, 1],
    [21, 23, 2, 3, 4, 5, 6, 7, 8, 9, 16, 18, 14, 12, 15, 13, 1, 17, 0, 19, 20, 11, 22, 10],
];

/// The quarter-turn generators the solvers use. A fourth face would be
/// redundant: turning a face equals turning the opposite face plus a whole-cube
/// rotation. Inverses sit at adjacent indices — [`crate::ida`] prunes with
/// `index ^ 1` — so keep them paired.
pub const SOLVE_MOVES: [char; 6] = ['l', 'L', 't', 'T', 'f', 'F'];

/// Facelets of the corner [`SOLVE_MOVES`] leaves in place (down-back-right).
/// Holding one corner still is what collapses the 24-fold rotation symmetry.
pub const FIXED_CORNER: [usize; 3] = [11, 13, 23];

/// Whole-cube rotations, used to enumerate the 24 ways of holding one state.
const ROTATION_GENERATORS: [Permutation; 3] = [
    [12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 18, 16, 19, 17, 21, 23, 20, 22],
    [2, 0, 3, 1, 20, 21, 22, 23, 9, 11, 8, 10, 19, 18, 17, 16, 4, 5, 6, 7, 15, 14, 13, 12],
    [18, 16, 19, 17, 6, 4, 7, 5, 22, 20, 23, 21, 13, 15, 12, 14, 10, 8, 11, 9, 2, 0, 3, 1],
];

/// The 24 ways of holding a cube, generated once from three rotations.
static ROTATIONS: LazyLock<Vec<Permutation>> = LazyLock::new(|| {
    let identity: Permutation = std::array::from_fn(|i| i as u8);
    let mut seen = HashSet::from([identity]);
    let mut frontier = vec![identity];
    while let Some(current) = frontier.pop() {
        for generator in &ROTATION_GENERATORS {
            let composed = apply_permutation(&current, generator);
            if seen.insert(composed) {
                frontier.push(composed);
            }
        }
    }
    let mut all: Vec<Permutation> = seen.into_iter().collect();
    all.sort_unstable(); // deterministic order, so goal lookup is reproducible
    all
});

/// The 24 whole-cube rotations as facelet permutations.
pub fn rotations() -> &'static [Permutation] {
    &ROTATIONS
}

/// The colour letter of a colour id.
pub fn color_char(color: u8) -> char {
    COLORS.as_bytes()[color as usize] as char
}

/// Rearrange stickers: `new[i] = old[perm[i]]`.
pub fn apply_permutation(state: &State, perm: &Permutation) -> State {
    std::array::from_fn(|i| state[perm[i] as usize])
}

/// Position of a move letter in [`MOVE_NAMES`].
pub fn move_index(name: char) -> Result<usize> {
    MOVE_NAMES.iter().position(|&m| m == name).ok_or(Error::UnknownMove(name))
}

/// Turn one face.
pub fn apply_move(state: &State, name: char) -> Result<State> {
    Ok(apply_permutation(state, &MOVE_PERMUTATIONS[move_index(name)?]))
}

/// Turn faces in sequence: `"lFtL"` applies `l`, then `F`, then `t`, then `L`.
pub fn apply_moves(state: &State, moves: &str) -> Result<State> {
    moves.chars().try_fold(*state, |state, name| apply_move(&state, name))
}

/// Undo a sequence: reverse the order, flip every turn.
pub fn invert(moves: &str) -> String {
    moves
        .chars()
        .rev()
        .map(|name| {
            if name.is_ascii_uppercase() {
                name.to_ascii_lowercase()
            } else {
                name.to_ascii_uppercase()
            }
        })
        .collect()
}

/// All 24 whole-cube rotations of `state`.
pub fn orientations(state: &State) -> Vec<State> {
    rotations().iter().map(|rotation| apply_permutation(state, rotation)).collect()
}

/// True when every face shows a single colour.
pub fn is_solved(state: &State) -> bool {
    state.chunks_exact(4).all(|face| face.iter().all(|&color| color == face[0]))
}

/// Read 24 colour letters in side order UP FRONT DOWN BACK LEFT RIGHT.
///
/// Rejects anything that is not 24 letters from `rwbypg` with each colour
/// appearing exactly four times. That is a necessary condition, not a
/// sufficient one — a sticker arrangement can pass and still be unreachable,
/// which [`crate::coords::encode`] is the one to notice.
pub fn parse(text: &str) -> Result<State> {
    let letters: Vec<char> = text.trim().to_ascii_lowercase().chars().collect();
    if letters.len() != 24 {
        return Err(Error::Length(letters.len()));
    }

    let mut state = [0u8; 24];
    for (slot, &letter) in state.iter_mut().zip(&letters) {
        *slot = COLORS.find(letter).ok_or(Error::Color(letter))? as u8;
    }
    for (color, letter) in COLORS.chars().enumerate() {
        let count = state.iter().filter(|&&c| c as usize == color).count();
        if count != 4 {
            return Err(Error::ColorCount { color: letter, count });
        }
    }
    Ok(state)
}

/// The 24 colour letters of a state. Inverse of [`parse`].
pub fn to_string(state: &State) -> String {
    state.iter().map(|&color| color_char(color)).collect()
}

/// Apply `count` random moves, each reaching a state not seen before.
pub fn scramble(state: &State, count: usize, rng: &mut Rng) -> State {
    let mut state = *state;
    let mut seen = HashSet::from([state]);
    let mut left = count;
    while left > 0 {
        let turn = rng.below(MOVE_PERMUTATIONS.len() as u64) as usize;
        let candidate = apply_permutation(&state, &MOVE_PERMUTATIONS[turn]);
        if seen.insert(candidate) {
            state = candidate;
            left -= 1;
        }
    }
    state
}
