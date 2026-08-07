//! The compact encoding, checked against the readable sticker model.

use rubik2x2::Error;
use rubik2x2::coords::{self, N_ORIENTATIONS, N_PERMUTATIONS, N_STATES, SOLVED_INDEX};
use rubik2x2::cube::{self, SOLVE_MOVES, SOLVED};
use rubik2x2::rng::Rng;

#[test]
fn the_space_is_the_size_the_encoding_claims() {
    assert_eq!(N_PERMUTATIONS * N_ORIENTATIONS, N_STATES);
    assert_eq!(N_STATES, 3_674_160);
}

#[test]
fn the_solved_cube_encodes_to_zero() {
    assert_eq!(coords::encode(&SOLVED).unwrap(), SOLVED_INDEX);
}

/// A cube may be handed over any way round. Reading the goal off the fixed
/// corner is what makes every orientation encodable *and* keeps the goal at
/// index 0 — the rotation itself is not erased (a rotated scramble is a
/// different index, conjugated by the rotation), but it is an automorphism of
/// the state graph, so the distance to solved is unchanged. `tests/solvers.rs`
/// pins that consequence.
#[test]
fn every_orientation_encodes_and_the_goal_is_always_zero() {
    let scrambled = cube::apply_moves(&SOLVED, "lFtLTf").unwrap();
    for held in cube::orientations(&scrambled) {
        assert!(coords::encode(&held).unwrap() < N_STATES);
    }
    for held in cube::orientations(&SOLVED) {
        assert_eq!(coords::encode(&held).unwrap(), SOLVED_INDEX);
    }
}

#[test]
fn encoding_round_trips() {
    let mut rng = Rng::new(2024);
    for _ in 0..500 {
        let state = cube::scramble(&SOLVED, 14, &mut rng);
        let index = coords::encode(&state).unwrap();
        assert!(index < N_STATES);
        // decode() answers in the standard orientation, so compare through the
        // index rather than the stickers: the rotation is deliberately lost.
        assert_eq!(coords::encode(&coords::decode(index)).unwrap(), index);
    }
}

#[test]
fn every_index_decodes_and_encodes_back() {
    for index in (0..N_STATES).step_by(1237) {
        assert_eq!(coords::encode(&coords::decode(index)).unwrap(), index);
    }
}

/// The one that matters: moving in the compact model must agree with moving in
/// the sticker model, for every move, over a long random walk.
#[test]
fn the_move_tables_agree_with_the_sticker_model() {
    let tables = coords::tables();
    let mut rng = Rng::new(7);
    let mut state = SOLVED;
    let mut index = SOLVED_INDEX;
    for _ in 0..20_000 {
        let turn = rng.below(6) as usize;
        state = cube::apply_move(&state, SOLVE_MOVES[turn]).unwrap();
        index = tables.apply(index, turn);
        assert_eq!(coords::encode(&state).unwrap(), index);
    }
}

#[test]
fn solve_moves_pair_each_turn_with_its_inverse() {
    let tables = coords::tables();
    let mut rng = Rng::new(11);
    for _ in 0..200 {
        let index = rng.below(u64::from(N_STATES)) as u32;
        for turn in 0..6 {
            assert_eq!(tables.apply(tables.apply(index, turn), turn ^ 1), index);
        }
    }
}

/// Twist the corner sitting in `slot` in place, by rotating its own three
/// stickers among its own three facelets. Colour counts survive that untouched,
/// so the result still parses — which is exactly why the encoding, not the
/// parser, has to be the one to notice.
fn twist_in_place(state: &mut cube::State, slot: usize, amount: usize) {
    let facelets = coords::SLOTS[slot];
    let colors: [u8; 3] = std::array::from_fn(|i| state[facelets[i]]);
    for (i, &facelet) in facelets.iter().enumerate() {
        state[facelet] = colors[(i + amount) % 3];
    }
}

/// A single corner twisted in place: the cube looks almost solved and no
/// sequence of turns can reach it, because turning a face cannot change the sum
/// of the twists. Only six twists are ranked and the seventh is derived, so
/// without this check the state would normalise into a *different*, legal cube
/// and every solver would answer confidently about the wrong puzzle.
#[test]
fn a_corner_twisted_in_place_is_rejected() {
    let twisted = cube::parse("pyyyrrrrwwwwppbpybbbgggg").unwrap();
    assert_eq!(coords::encode(&twisted), Err(Error::TwistedCorner(1)));

    // The same thing built by hand, both ways round.
    for amount in [1, 2] {
        let mut state = SOLVED;
        twist_in_place(&mut state, 3, amount);
        assert_eq!(coords::encode(&state), Err(Error::TwistedCorner(amount as u8)));
    }
}

/// The near miss: three corners twisted the same way *do* sum to a multiple of
/// three, so this cube is genuinely reachable and must be accepted. A check
/// that simply rejected twisted corners would pass the test above and fail
/// here.
#[test]
fn three_corners_twisted_the_same_way_are_accepted() {
    let mut state = SOLVED;
    for slot in [0, 1, 2] {
        twist_in_place(&mut state, slot, 1);
    }
    assert!(!cube::is_solved(&state), "the cube really is scrambled");
    let index = coords::encode(&state).expect("three equal twists sum to zero mod three");
    assert!(index < N_STATES);
}

/// One piece in two slots is not a permutation, so another piece is missing and
/// the cube cannot exist. Colour counting cannot see this — the arrangement is
/// caught where the pieces are identified.
#[test]
fn one_piece_in_two_slots_is_rejected() {
    let mut cloned = SOLVED;
    let (source, target) = (coords::SLOTS[1], coords::SLOTS[0]);
    for (&from, &to) in source.iter().zip(&target) {
        cloned[to] = SOLVED[from];
    }
    assert_eq!(coords::encode(&cloned), Err(Error::RepeatedPiece(1)));

    // The fixed corner counts as occupied too: cloning it into a free slot is
    // the same fault, and the piece it duplicates is slot 7's.
    let mut cloned = SOLVED;
    for (&from, &to) in coords::SLOTS[7].iter().zip(&coords::SLOTS[0]) {
        cloned[to] = SOLVED[from];
    }
    assert_eq!(coords::encode(&cloned), Err(Error::RepeatedPiece(7)));
}

#[test]
fn unreachable_sticker_arrangements_are_rejected() {
    // Two stickers of a solved cube swapped: the colour counts still pass, but
    // three of the corners no longer exist.
    let nonsense = cube::parse("ryyyyrrrwwwwppppbbbbgggg").unwrap();
    assert!(coords::encode(&nonsense).is_err());
}
