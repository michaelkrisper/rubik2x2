//! The sticker model: the algebra the move tables must obey.
//!
//! The tables are data typed in from a specification, so they are checked
//! rather than trusted.

use rubik2x2::cube::{
    self, FIXED_CORNER, MOVE_NAMES, MOVE_PERMUTATIONS, SOLVE_MOVES, SOLVED, State,
};

fn turn(state: &State, moves: &str) -> State {
    cube::apply_moves(state, moves).expect("valid moves")
}

#[test]
fn every_move_is_a_permutation_of_all_24_facelets() {
    for permutation in &MOVE_PERMUTATIONS {
        let mut seen = *permutation;
        seen.sort_unstable();
        assert_eq!(seen, std::array::from_fn(|i| i as u8));
    }
}

#[test]
fn every_move_has_order_four() {
    let scrambled = turn(&SOLVED, "lFtrBa");
    for name in MOVE_NAMES {
        let mut state = scrambled;
        for repeat in 1..=4 {
            state = cube::apply_move(&state, name).unwrap();
            assert_eq!(state == scrambled, repeat == 4, "move {name} at repeat {repeat}");
        }
    }
}

#[test]
fn a_move_and_its_inverse_cancel() {
    let scrambled = turn(&SOLVED, "lFtrBa");
    for pair in MOVE_NAMES.chunks_exact(2) {
        let (down, up) = (pair[0], pair[1]);
        assert_eq!(turn(&scrambled, &format!("{down}{up}")), scrambled);
        assert_eq!(turn(&scrambled, &format!("{up}{down}")), scrambled);
    }
}

#[test]
fn opposite_faces_commute() {
    let scrambled = turn(&SOLVED, "lFtrBa");
    for (one, other) in [('l', 'r'), ('l', 'R'), ('t', 'b'), ('t', 'B'), ('f', 'a'), ('f', 'A')] {
        assert_eq!(
            turn(&scrambled, &format!("{one}{other}")),
            turn(&scrambled, &format!("{other}{one}")),
            "{one} and {other} must commute"
        );
    }
}

#[test]
fn inverting_a_sequence_undoes_it() {
    let moves = "lFtLLaBrft";
    assert_eq!(cube::invert(moves), "TFRbAllTfL");
    assert_eq!(turn(&turn(&SOLVED, moves), &cube::invert(moves)), SOLVED);
}

/// Exactly the down-back-right corner is fixed by the six solve moves — the
/// fact the whole encoding rests on.
#[test]
fn the_solve_moves_fix_exactly_one_corner() {
    let fixed: Vec<usize> = (0..24)
        .filter(|&facelet| {
            SOLVE_MOVES.iter().all(|&name| {
                MOVE_PERMUTATIONS[cube::move_index(name).unwrap()][facelet] as usize == facelet
            })
        })
        .collect();
    assert_eq!(fixed, FIXED_CORNER);
}

#[test]
fn there_are_exactly_24_whole_cube_rotations() {
    assert_eq!(cube::rotations().len(), 24);
    let orientations = cube::orientations(&SOLVED);
    assert_eq!(orientations.len(), 24);
    assert!(orientations.iter().all(cube::is_solved));
    let mut distinct = orientations.clone();
    distinct.sort_unstable();
    distinct.dedup();
    assert_eq!(distinct.len(), 24, "every rotation of a solved cube is distinct");
}

#[test]
fn parsing_round_trips() {
    let text = "wppryygrygrwgprbbbwpbwyg";
    assert_eq!(cube::to_string(&cube::parse(text).unwrap()), text);
    assert_eq!(cube::to_string(&SOLVED), "yyyyrrrrwwwwppppbbbbgggg");
}

#[test]
fn parsing_rejects_nonsense() {
    use rubik2x2::Error;
    assert_eq!(cube::parse("yyy"), Err(Error::Length(3)));
    assert_eq!(cube::parse(&"x".repeat(24)), Err(Error::Color('x')));
    assert_eq!(
        cube::parse("yyyyyrrrwwwwppppbbbbgggg"),
        Err(Error::ColorCount { color: 'r', count: 3 })
    );
}

#[test]
fn scrambling_moves_the_cube_and_repeats_for_a_seed() {
    let mut rng = rubik2x2::rng::Rng::new(42);
    let scrambled = cube::scramble(&SOLVED, 14, &mut rng);
    assert!(!cube::is_solved(&scrambled));
    let mut same_seed = rubik2x2::rng::Rng::new(42);
    assert_eq!(cube::scramble(&SOLVED, 14, &mut same_seed), scrambled);
}

#[test]
fn rendering_shows_every_facelet() {
    let picture = rubik2x2::display::render(&SOLVED);
    assert!(picture.contains("UP") && picture.contains("DOWN"), "{picture}");
    // 'Y' is the one colour letter that no face label also contains.
    assert_eq!(picture.matches('Y').count(), 4, "{picture}");
}
