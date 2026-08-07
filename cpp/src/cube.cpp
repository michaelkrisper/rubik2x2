#include "rubik/cube.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <random>
#include <ranges>
#include <set>

namespace rubik {

Result<State> turn(const State& state, std::string_view moves) {
    State current = state;
    for (char c : moves) {
        const std::optional<Move> move = move_from_char(c);
        if (!move) {
            return fail(ErrorKind::usage, std::format("'{}' is not a move letter ({})", c, move_chars));
        }
        current = turn(current, *move);
    }
    return current;
}

Moves invert(std::string_view moves) {
    Moves inverted;
    inverted.reserve(moves.size());
    for (char c : moves | std::views::reverse) {
        // Case is the only difference between a turn and its inverse.
        inverted.push_back(static_cast<char>(std::islower(static_cast<unsigned char>(c))
                                                 ? std::toupper(static_cast<unsigned char>(c))
                                                 : std::tolower(static_cast<unsigned char>(c))));
    }
    return inverted;
}

Result<State> parse(std::string_view text) {
    const auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!text.empty() && is_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(text.back())) {
        text.remove_suffix(1);
    }
    if (text.size() != facelet_count) {
        return fail(ErrorKind::invalid_state,
                    std::format("expected 24 colour letters, got {}", text.size()));
    }

    State state{};
    std::array<int, 6> counts{};
    for (std::size_t i = 0; i < facelet_count; ++i) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        const std::size_t color = color_chars.find(c);
        if (color == std::string_view::npos) {
            return fail(ErrorKind::invalid_state,
                        std::format("invalid colour letter '{}' (allowed: {})", text[i], color_chars));
        }
        state[i] = static_cast<Color>(color);
        ++counts[color];
    }
    for (std::size_t color = 0; color < counts.size(); ++color) {
        if (counts[color] != 4) {
            return fail(ErrorKind::invalid_state,
                        std::format("colour '{}' appears {} times, expected 4", color_chars[color],
                                    counts[color]));
        }
    }
    return state;
}

std::string to_string(const State& state) {
    std::string text;
    text.reserve(facelet_count);
    for (Color color : state) {
        text.push_back(color_chars[std::to_underlying(color)]);
    }
    return text;
}

State scramble(const State& state, int count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::size_t> pick(0, move_permutations.size() - 1);

    // Rejecting states already seen keeps a short scramble from wandering back
    // over its own tracks, so "n moves" really means n moves of distance.
    std::set<State> seen{state};
    State current = state;
    while (count > 0) {
        const State candidate = turn(current, static_cast<Move>(pick(rng)));
        if (seen.insert(candidate).second) {
            current = candidate;
            --count;
        }
    }
    return current;
}

}  // namespace rubik
