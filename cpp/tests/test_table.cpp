/// The strongest check available: no incorrect move table or encoding can
/// reproduce the published distance distribution.
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

#include "fixtures.hpp"
#include "harness.hpp"
#include "rubik/coords.hpp"
#include "rubik/table.hpp"

using namespace rubik;

namespace {

/// OEIS A079762 -- positions at exactly n quarter turns from solved.
const std::vector<std::uint64_t> oeis_a079762{1,      6,       27,     120,   534,
                                              2256,   8969,    33058,  114149, 360508,
                                              930588, 1350852, 782536, 90280, 276};

}  // namespace

TEST(the_histogram_matches_oeis_a079762) {
    CHECK(fixtures::table().histogram() == oeis_a079762);
}

TEST(the_histogram_accounts_for_every_state) {
    const std::vector<std::uint64_t> histogram = fixtures::table().histogram();
    CHECK_EQ(std::accumulate(histogram.begin(), histogram.end(), std::uint64_t{0}),
             std::uint64_t{coords::state_count});
    CHECK_EQ(histogram.size(), std::size_t{15});  // distances 0..14, so God's number is 14
    CHECK_EQ(histogram.front(), std::uint64_t{1});
}

TEST(every_state_is_reachable) {
    const std::span<const std::uint8_t> distances = fixtures::table().data();
    CHECK_EQ(distances.size(), std::size_t{coords::state_count});
    CHECK(std::ranges::none_of(distances, [](std::uint8_t stored) { return stored == 0; }));
}

TEST(the_parallel_build_produces_the_same_table) {
    for (unsigned workers : {1u, 2u, 4u}) {
        const DistanceTable parallel = DistanceTable::build_parallel(workers);
        CHECK(std::ranges::equal(parallel.data(), fixtures::table().data()));
    }
}

TEST(distances_fall_by_one_along_a_solution) {
    for (const State& state : fixtures::scrambles()) {
        std::uint32_t index = coords::encode(state).value();
        const Moves solution = fixtures::table().solve(state).value();
        int expected = fixtures::table().distance(index);
        for (char letter : solution) {
            const std::size_t position = solve_move_chars.find(letter);
            CHECK(position != std::string_view::npos);
            index = coords::apply(index, position);
            --expected;
            CHECK_EQ(fixtures::table().distance(index), expected);
        }
        CHECK_EQ(index, coords::solved_index);
    }
}
