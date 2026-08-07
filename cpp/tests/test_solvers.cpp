/// The three algorithms must agree with each other and with the table.
#include <cstddef>
#include <cstdint>
#include <string>

#include "fixtures.hpp"
#include "harness.hpp"
#include "rubik/bfs.hpp"
#include "rubik/coords.hpp"
#include "rubik/cube.hpp"
#include "rubik/ida.hpp"

using namespace rubik;

namespace {

void check_solves(const State& state, const Moves& solution) {
    const Result<State> finished = turn(state, solution);
    CHECK(finished.has_value());
    CHECK(is_solved(*finished));
}

}  // namespace

TEST(the_three_algorithms_return_equal_length_solutions) {
    for (const State& state : fixtures::scrambles()) {
        const Result<Moves> bfs = solve_bfs(state);
        const Result<Moves> ida = solve_ida(state);
        const Result<Moves> table = fixtures::table().solve(state);
        CHECK(bfs.has_value());
        CHECK(ida.has_value());
        CHECK(table.has_value());
        CHECK_EQ(bfs->size(), ida->size());
        CHECK_EQ(bfs->size(), table->size());
    }
}

TEST(every_solution_solves_its_cube) {
    for (const State& state : fixtures::scrambles()) {
        check_solves(state, solve_bfs(state).value());
        check_solves(state, solve_ida(state).value());
        check_solves(state, fixtures::table().solve(state).value());
    }
}

TEST(solutions_are_as_short_as_the_table_says_is_possible) {
    for (const State& state : fixtures::scrambles()) {
        const int optimal = fixtures::table().distance(coords::encode(state).value());
        CHECK_EQ(static_cast<int>(solve_bfs(state)->size()), optimal);
        CHECK_EQ(static_cast<int>(solve_ida(state)->size()), optimal);
    }
}

TEST(the_antipodal_position_needs_exactly_fourteen_moves) {
    const State& state = fixtures::hardest();
    CHECK_EQ(fixtures::table().distance(coords::encode(state).value()), 14);
    CHECK_EQ(solve_bfs(state)->size(), std::size_t{14});
    CHECK_EQ(solve_ida(state)->size(), std::size_t{14});
    CHECK_EQ(fixtures::table().solve(state)->size(), std::size_t{14});
    check_solves(state, solve_bfs(state).value());
}

TEST(how_the_cube_is_held_does_not_change_the_answer) {
    // Rotating the whole cube moves it to a different index -- the slots and
    // the pieces are both relabelled -- but the puzzle is the same one, so
    // every orientation must come out the same number of moves from solved.
    for (std::uint64_t seed = 0; seed < 5; ++seed) {
        const State state = scramble(solved, 11, seed);
        const std::size_t length = solve_bfs(state)->size();
        for (const Facelets& rotation : rotations) {
            const State held = rotate(state, rotation);
            CHECK_EQ(fixtures::table().solve(held)->size(), length);
            check_solves(held, fixtures::table().solve(held).value());
        }
    }
}

TEST(a_solved_cube_needs_no_moves_however_it_is_held) {
    for (const Facelets& rotation : rotations) {
        const State state = rotate(solved, rotation);
        CHECK(solve_bfs(state)->empty());
        CHECK(solve_ida(state)->empty());
        CHECK(fixtures::table().solve(state)->empty());
    }
}

TEST(every_solver_rejects_an_unreachable_cube) {
    State twisted = solved;
    const auto& slot = coords::slots[2];
    const Color first = twisted[slot[0]];
    twisted[slot[0]] = twisted[slot[1]];
    twisted[slot[1]] = twisted[slot[2]];
    twisted[slot[2]] = first;
    CHECK(!solve_bfs(twisted).has_value());
    CHECK(!solve_ida(twisted).has_value());
    CHECK(!fixtures::table().solve(twisted).has_value());
}

TEST(the_pattern_database_bounds_never_overestimate) {
    const PatternDatabases& databases = PatternDatabases::shared();
    for (const State& state : fixtures::scrambles()) {
        const std::uint32_t index = coords::encode(state).value();
        CHECK(databases.bound(index) <= fixtures::table().distance(index));
    }
    // Both projections stop well short of 14, which is why IDA* prunes badly
    // here; if either ever exceeded it the heuristic would be inadmissible.
    const auto [permutation, orientation] = databases.reach();
    CHECK(permutation <= max_depth);
    CHECK(orientation <= max_depth);
}
