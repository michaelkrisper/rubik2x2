/// The sticker model's algebra. Everything else is built on these tables being
/// right, so they are checked rather than trusted.
#include <array>
#include <set>
#include <string>
#include <utility>

#include "harness.hpp"
#include "rubik/cube.hpp"

using namespace rubik;

namespace {

State sample() { return scramble(solved, 12, 1234); }

/// Opposite faces, as the index of the first of each inverse pair:
/// left/right, top/bottom, front/back.
constexpr std::array<std::pair<std::size_t, std::size_t>, 3> opposite_faces{
    {{0, 2}, {4, 6}, {8, 10}}};

}  // namespace

TEST(every_move_is_a_permutation_of_the_facelets) {
    for (const Facelets& permutation : move_permutations) {
        const std::set<std::uint8_t> distinct(permutation.begin(), permutation.end());
        CHECK_EQ(distinct.size(), facelet_count);
    }
}

TEST(every_move_has_order_four) {
    const State start = sample();
    for (std::size_t i = 0; i < move_permutations.size(); ++i) {
        State state = start;
        for (int quarter = 1; quarter <= 4; ++quarter) {
            state = turn(state, static_cast<Move>(i));
            if (quarter < 4) {
                CHECK_NE(to_string(state), to_string(start));
            }
        }
        CHECK_EQ(to_string(state), to_string(start));
    }
}

TEST(a_move_and_its_inverse_cancel) {
    const State start = sample();
    for (std::size_t i = 0; i < move_permutations.size(); ++i) {
        const State there = turn(start, static_cast<Move>(i));
        const State back = turn(there, static_cast<Move>(i ^ 1u));
        CHECK_EQ(to_string(back), to_string(start));
    }
}

TEST(opposite_faces_commute) {
    const State start = sample();
    for (const auto& [first, second] : opposite_faces) {
        for (std::size_t a = 0; a < 2; ++a) {
            for (std::size_t b = 0; b < 2; ++b) {
                const Move one = static_cast<Move>(first + a);
                const Move other = static_cast<Move>(second + b);
                CHECK_EQ(to_string(turn(turn(start, one), other)),
                         to_string(turn(turn(start, other), one)));
            }
        }
    }
}

TEST(adjacent_faces_do_not_commute) {
    const State start = sample();
    CHECK_NE(to_string(turn(turn(start, Move::l), Move::t)),
             to_string(turn(turn(start, Move::t), Move::l)));
}

TEST(the_solve_moves_fix_exactly_three_facelets) {
    std::set<std::uint8_t> fixed;
    for (std::uint8_t facelet = 0; facelet < facelet_count; ++facelet) {
        fixed.insert(facelet);
    }
    for (Move move : solve_moves) {
        const Facelets& permutation = move_permutations[std::to_underlying(move)];
        std::erase_if(fixed, [&](std::uint8_t facelet) { return permutation[facelet] != facelet; });
    }
    CHECK_EQ(fixed, (std::set<std::uint8_t>{11, 13, 23}));
    CHECK_EQ(fixed, (std::set<std::uint8_t>{fixed_corner.begin(), fixed_corner.end()}));
}

TEST(inverting_a_sequence_undoes_it) {
    const State start = sample();
    const Moves sequence = "lFtLraBT";
    const Result<State> there = turn(start, sequence);
    CHECK(there.has_value());
    const Result<State> back = turn(*there, invert(sequence));
    CHECK(back.has_value());
    CHECK_EQ(to_string(*back), to_string(start));
}

TEST(unknown_move_letters_are_rejected) {
    CHECK(!turn(solved, "lFz").has_value());
    CHECK(turn(solved, "").has_value());
}

TEST(parsing_round_trips) {
    const State state = sample();
    const Result<State> again = parse(to_string(state));
    CHECK(again.has_value());
    CHECK_EQ(to_string(*again), to_string(state));
    CHECK_EQ(to_string(solved), std::string{"yyyyrrrrwwwwppppbbbbgggg"});
}

TEST(parsing_rejects_malformed_states) {
    CHECK(!parse("").has_value());
    CHECK(!parse("yyyyrrrrwwwwppppbbbbggg").has_value());    // 23 letters
    CHECK(!parse("xyyyrrrrwwwwppppbbbbgggg").has_value());   // not a colour
    CHECK(!parse("yyyyyrrrwwwwppppbbbbgggg").has_value());   // five yellows, three reds
    CHECK(parse("  yyyyrrrrwwwwppppbbbbgggg \n").has_value());  // surrounding space is fine
}

TEST(there_are_twenty_four_ways_of_holding_a_cube) {
    std::set<std::string> seen;
    for (const Facelets& rotation : rotations) {
        seen.insert(to_string(rotate(solved, rotation)));
        CHECK(is_solved(rotate(solved, rotation)));
    }
    CHECK_EQ(seen.size(), std::size_t{24});
}

TEST(solved_detection) {
    CHECK(is_solved(solved));
    CHECK(!is_solved(sample()));
}

TEST(scrambling_moves_the_cube) {
    CHECK(!is_solved(scramble(solved, 1, 5)));
    CHECK_EQ(to_string(scramble(solved, 7, 99)), to_string(scramble(solved, 7, 99)));
}
