#include "fixtures.hpp"

#include <vector>

#include "rubik/bench.hpp"

namespace fixtures {

const rubik::DistanceTable& table() {
    static const rubik::DistanceTable built = rubik::DistanceTable::build();
    return built;
}

std::span<const rubik::State> scrambles() {
    static const std::vector<rubik::State> states = [] {
        std::vector<rubik::State> all;
        for (std::uint64_t seed = 0; seed < 40; ++seed) {
            all.push_back(rubik::scramble(rubik::solved, 14, seed));
        }
        return all;
    }();
    return states;
}

const rubik::State& hardest() {
    static const rubik::State state = rubik::parse(rubik::bench::hardest_scramble).value();
    return state;
}

}  // namespace fixtures
