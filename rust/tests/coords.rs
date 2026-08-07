//! The compact encoding, checked against the readable sticker model.

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

#[test]
fn unreachable_sticker_arrangements_are_rejected() {
    // Two stickers of a solved cube swapped: the colour counts still pass, but
    // three of the corners no longer exist.
    let nonsense = cube::parse("ryyyyrrrwwwwppppbbbbgggg").unwrap();
    assert!(coords::encode(&nonsense).is_err());
}
