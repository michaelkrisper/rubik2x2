//! The three algorithms, cross-checked against each other and against the
//! sticker model: same lengths, and every answer actually solves its cube.

use std::sync::LazyLock;

use rubik2x2::cube::{self, SOLVED, State};
use rubik2x2::rng::Rng;
use rubik2x2::table::DistanceTable;
use rubik2x2::{HARDEST, bfs, coords, ida};

static TABLE: LazyLock<DistanceTable> = LazyLock::new(DistanceTable::build);

/// The shared benchmark scrambles when they are around, reproducible random
/// ones when they are not, so the crate tests on its own too.
fn scrambles(count: usize) -> Vec<State> {
    let shared = std::fs::read_to_string("../bench/scrambles.txt").ok();
    match shared {
        Some(text) => text.lines().take(count).map(|line| cube::parse(line).unwrap()).collect(),
        None => {
            (0..count).map(|seed| cube::scramble(&SOLVED, 14, &mut Rng::new(seed as u64))).collect()
        }
    }
}

fn solves(state: &State, solution: &str) -> bool {
    cube::apply_moves(state, solution).is_ok_and(|state| cube::is_solved(&state))
}

#[test]
fn all_three_agree_on_length_and_actually_solve() {
    for state in scrambles(40) {
        let optimal = TABLE.distance(coords::encode(&state).unwrap()) as usize;
        for (name, solution) in [
            ("bfs", bfs::solve(&state).unwrap()),
            ("ida", ida::solve(&state).unwrap()),
            ("table", TABLE.solve(&state).unwrap()),
        ] {
            assert_eq!(
                solution.len(),
                optimal,
                "{name} was not optimal on {}",
                cube::to_string(&state)
            );
            assert!(
                solves(&state, &solution),
                "{name} returned {solution:?}, which does not solve"
            );
        }
    }
}

/// The antipodal position: as far from solved as a pocket cube gets.
#[test]
fn the_hardest_position_needs_exactly_fourteen_moves() {
    let hardest = cube::parse(HARDEST).unwrap();
    for (name, solution) in [
        ("bfs", bfs::solve(&hardest).unwrap()),
        ("ida", ida::solve(&hardest).unwrap()),
        ("table", TABLE.solve(&hardest).unwrap()),
    ] {
        assert_eq!(solution.len(), 14, "{name} returned {solution:?}");
        assert!(solves(&hardest, &solution), "{name} returned {solution:?}, which does not solve");
    }
}

#[test]
fn a_solved_cube_needs_no_moves() {
    assert_eq!(bfs::solve(&SOLVED).unwrap(), "");
    assert_eq!(ida::solve(&SOLVED).unwrap(), "");
    assert_eq!(TABLE.solve(&SOLVED).unwrap(), "");
}

/// Holding the cube differently changes the index — the rotation is conjugated
/// into the permutation — but it is an automorphism of the state graph, so it
/// must never change how many moves the answer takes.
#[test]
fn solving_works_whatever_way_the_cube_is_held() {
    let scrambled = cube::apply_moves(&SOLVED, "lFtLTfL").unwrap();
    let expected = TABLE.distance(coords::encode(&scrambled).unwrap()) as usize;
    assert_eq!(expected, 7, "the sample scramble should need all seven of its moves");
    for held in cube::orientations(&scrambled) {
        let solution = bfs::solve(&held).unwrap();
        assert!(solves(&held, &solution));
        assert_eq!(solution.len(), expected);
    }
}

#[test]
fn every_depth_is_solved_optimally() {
    // One state at each distance 0..=14, found by walking away from solved.
    let mut rng = Rng::new(99);
    let mut found = vec![None; 15];
    let mut state = SOLVED;
    for _ in 0..4000 {
        state = cube::scramble(&state, 1, &mut rng);
        let distance = TABLE.distance(coords::encode(&state).unwrap()) as usize;
        found[distance].get_or_insert(state);
    }
    for (distance, state) in found.iter().enumerate() {
        let Some(state) = state else { continue };
        assert_eq!(bfs::solve(state).unwrap().len(), distance);
        assert_eq!(ida::solve(state).unwrap().len(), distance);
        assert_eq!(TABLE.solve(state).unwrap().len(), distance);
    }
}

#[test]
fn the_pattern_databases_are_admissible_but_loose() {
    let (permutation, orientation) = ida::databases().reach();
    assert!(permutation <= 14 && orientation <= 14);
    // Both top out well below God's number, which is why IDA* prunes badly here.
    assert!(permutation.max(orientation) < 14);
}

#[test]
fn an_unreachable_cube_is_reported_not_solved() {
    let nonsense = cube::parse("ryyyyrrrwwwwppppbbbbgggg").unwrap();
    assert!(bfs::solve(&nonsense).is_err());
    assert!(ida::solve(&nonsense).is_err());
    assert!(TABLE.solve(&nonsense).is_err());
}
