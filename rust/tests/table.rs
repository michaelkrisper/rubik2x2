//! The distance table, and the one check no wrong implementation survives.

use std::sync::LazyLock;

use rubik2x2::coords::N_STATES;
use rubik2x2::table::{self, DistanceTable};

/// [OEIS A079762](https://oeis.org/A079762): pocket cube positions at exactly
/// *n* quarter turns.
const A079762: [u32; 15] =
    [1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508, 930588, 1350852, 782536, 90280, 276];

static TABLE: LazyLock<DistanceTable> = LazyLock::new(DistanceTable::build);

/// The strongest check available: reproducing this sequence exercises the move
/// tables, the encoding and the search together. An incorrect move table or a
/// flawed encoding cannot produce it.
///
/// Takes a few seconds in a debug build; `cargo test --release` is much faster.
#[test]
fn the_histogram_is_oeis_a079762() {
    assert_eq!(TABLE.histogram(), A079762);
}

#[test]
fn every_state_is_reachable_within_fourteen_moves() {
    let histogram = TABLE.histogram();
    assert_eq!(histogram.iter().sum::<u32>(), N_STATES);
    assert_eq!(histogram.len(), 15, "distances 0 through 14");
}

/// The parallel build races on the visited array by design; the compare-exchange
/// is what keeps the outcome deterministic.
#[test]
fn the_parallel_build_produces_an_identical_table() {
    let workers = table::workers().max(2);
    assert!(DistanceTable::build_parallel(workers) == *TABLE, "{workers} workers disagreed");
}

#[test]
fn the_solved_state_is_at_distance_zero_and_the_antipode_at_fourteen() {
    let solved = rubik2x2::coords::encode(&rubik2x2::cube::SOLVED).unwrap();
    assert_eq!(TABLE.distance(solved), 0);

    let hardest = rubik2x2::cube::parse(rubik2x2::HARDEST).unwrap();
    assert_eq!(TABLE.distance(rubik2x2::coords::encode(&hardest).unwrap()), 14);
}
