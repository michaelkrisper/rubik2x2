/// The compact encoding, checked move by move against the readable model.
#include <cstdint>
#include <random>
#include <string>

#include "harness.hpp"
#include "rubik/coords.hpp"
#include "rubik/cube.hpp"

using namespace rubik;

namespace {

/// The pieces of whichever solved cube `state`'s fixed corner points at.
const coords::Pieces& goal_pieces(const State& state) {
    const std::int8_t goal = coords::goal_lookup[coords::goal_key(state)];
    return coords::goals[static_cast<std::size_t>(goal)].pieces;
}

}  // namespace

TEST(the_state_space_has_the_expected_size) {
    CHECK_EQ(coords::state_count, 3'674'160u);
    CHECK_EQ(coords::permutation_count * coords::orientation_count, coords::state_count);
}

TEST(a_solved_cube_encodes_to_zero) {
    CHECK_EQ(coords::encode(solved).value(), coords::solved_index);
    for (const Facelets& rotation : rotations) {
        CHECK_EQ(coords::encode(rotate(solved, rotation)).value(), coords::solved_index);
    }
}

TEST(ranking_round_trips) {
    for (std::uint16_t rank = 0; rank < coords::permutation_count; ++rank) {
        CHECK_EQ(coords::rank_permutation(coords::unrank_permutation(rank)), rank);
    }
    for (std::uint16_t rank = 0; rank < coords::orientation_count; ++rank) {
        CHECK_EQ(coords::rank_orientation(coords::unrank_orientation(rank)), rank);
    }
}

TEST(the_seventh_twist_is_forced) {
    for (std::uint16_t rank = 0; rank < coords::orientation_count; ++rank) {
        const coords::Orientation orientation = coords::unrank_orientation(rank);
        int total = 0;
        for (std::uint8_t twist : orientation) {
            total += twist;
        }
        CHECK_EQ(total % 3, 0);
    }
}

TEST(states_round_trip_through_the_index) {
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        const State state = scramble(solved, 11, seed);
        const Result<std::uint32_t> index = coords::encode(state);
        CHECK(index.has_value());
        CHECK_EQ(to_string(coords::decode(*index, goal_pieces(state))), to_string(state));
    }
}

TEST(indices_round_trip_through_the_state) {
    for (std::uint32_t index :
         {0u, 1u, 728u, 729u, 12'345u, 1'000'000u, coords::state_count - 1u}) {
        CHECK_EQ(coords::encode(coords::decode(index)).value(), index);
    }
}

TEST(moves_on_the_index_agree_with_moves_on_the_stickers) {
    std::mt19937 rng(7);
    State state = solved;
    std::uint32_t index = coords::solved_index;
    for (int step = 0; step < 2000; ++step) {
        const std::size_t position = rng() % solve_moves.size();
        state = turn(state, solve_moves[position]);
        index = coords::apply(index, position);
        CHECK_EQ(coords::encode(state).value(), index);
    }
}

TEST(every_way_of_holding_a_cube_encodes) {
    // The index itself is *not* invariant: a whole-cube rotation relabels both
    // the slots and the pieces, which conjugates the permutation coordinate.
    // What has to hold is that every orientation encodes at all, aiming at its
    // own goal, and that the solved cube always lands on 0 (checked above).
    // Equal solution lengths across the 24 orientations are checked in
    // test_solvers.cpp, which is where the claim actually matters.
    const State state = scramble(solved, 9, 42);
    for (const Facelets& rotation : rotations) {
        CHECK(coords::encode(rotate(state, rotation)).has_value());
    }
}

TEST(the_wide_tables_are_the_plain_ones_scaled) {
    const coords::MoveTables& tables = coords::move_tables();
    for (std::uint32_t rank = 0; rank < coords::permutation_count; ++rank) {
        for (std::size_t move = 0; move < solve_moves.size(); ++move) {
            CHECK_EQ(tables.permutation_wide[rank][move],
                     tables.permutation[rank][move] * coords::orientation_count);
        }
    }
}

TEST(a_twisted_corner_is_rejected) {
    // Rotating one corner's three stickers in place keeps the colour counts
    // right but takes the twists off a multiple of three.
    State twisted = solved;
    const auto& slot = coords::slots[0];
    const Color first = twisted[slot[0]];
    twisted[slot[0]] = twisted[slot[1]];
    twisted[slot[1]] = twisted[slot[2]];
    twisted[slot[2]] = first;
    CHECK(!coords::encode(twisted).has_value());
    CHECK_EQ(coords::encode(twisted).error().kind, ErrorKind::unsolvable);
}

TEST(an_impossible_fixed_corner_is_rejected) {
    State broken = solved;
    for (std::uint8_t facelet : fixed_corner) {
        broken[facelet] = Color::yellow;
    }
    CHECK(!coords::encode(broken).has_value());
}

TEST(swapped_corners_are_rejected) {
    // Two slots showing the same piece cannot happen on a real cube.
    State broken = solved;
    for (std::size_t i = 0; i < 3; ++i) {
        broken[coords::slots[0][i]] = broken[coords::slots[1][i]];
    }
    CHECK(!coords::encode(broken).has_value());
}
