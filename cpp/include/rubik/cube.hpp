/// The 2x2x2 pocket cube, modelled as 24 stickers.
///
/// This is the readable model and nothing else -- no search, no drawing. A
/// state is 24 sticker colour ids ordered side by side (UP, FRONT, DOWN, BACK,
/// LEFT, RIGHT), each side read row-major, so the flat index is
/// `side * 4 + row * 2 + column`:
///
///     -------
///     |0  1 |
///     |2  3 |
///     -------
///
/// Every move is a permutation applied as `new[i] = old[PERM[i]]`. The tables
/// are `constexpr` because they are pure data known at compile time, and they
/// are checked rather than trusted: tests/test_cube.cpp pins the algebra they
/// must obey, and building the distance table reproduces OEIS A079762 exactly,
/// which no wrong table could do.
///
/// `rubik::coords` re-encodes states for the solvers; `rubik::display` draws
/// them.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "rubik/error.hpp"

namespace rubik {

inline constexpr std::size_t facelet_count = 24;

/// Sticker colours: red, white, blue, yellow, purple, green.
enum class Color : std::uint8_t { red, white, blue, yellow, purple, green };

inline constexpr std::string_view color_chars = "rwbypg";

enum class Side : std::uint8_t { up, front, down, back, left, right };

/// A cube state: one colour per facelet.
using State = std::array<Color, facelet_count>;
/// A permutation of the 24 facelets -- what a move or a whole-cube rotation is.
using Facelets = std::array<std::uint8_t, facelet_count>;
/// A move sequence in the notation below, e.g. "lFtL".
using Moves = std::string;

/// Move notation. Lowercase and uppercase are inverse quarter turns of one
/// face, and inverses sit at adjacent indices so that `index ^ 1` inverts:
///   l/L  left column down/up        r/R  right column down/up
///   t/T  top layer left/right       b/B  bottom layer left/right
///   f/F  front face cw/ccw          a/A  back face cw/ccw
enum class Move : std::uint8_t { l, L, r, R, t, T, b, B, f, F, a, A };

inline constexpr std::string_view move_chars = "lLrRtTbBfFaA";

/// The quarter-turn generators the solvers use. A fourth face would be
/// redundant: turning a face equals turning the opposite face plus a whole-cube
/// rotation. Keep the pairing -- `rubik::ida` prunes with `index ^ 1`.
inline constexpr std::array<Move, 6> solve_moves{Move::l, Move::L, Move::t,
                                                 Move::T, Move::f, Move::F};
inline constexpr std::string_view solve_move_chars = "lLtTfF";

/// Facelets of the corner the solve moves leave in place (down-back-right).
/// Holding one corner still is what collapses the 24-fold rotation symmetry.
inline constexpr std::array<std::uint8_t, 3> fixed_corner{11, 13, 23};

// clang-format off
inline constexpr std::array<Facelets, 12> move_permutations{{
    /* l */ {12,  1, 14,  3,  0,  5,  2,  7,  4,  9,  6, 11,  8, 13, 10, 15, 18, 16, 19, 17, 20, 21, 22, 23},
    /* L */ { 4,  1,  6,  3,  8,  5, 10,  7, 12,  9, 14, 11,  0, 13,  2, 15, 17, 19, 16, 18, 20, 21, 22, 23},
    /* r */ { 0, 13,  2, 15,  4,  1,  6,  3,  8,  5, 10,  7, 12,  9, 14, 11, 16, 17, 18, 19, 21, 23, 20, 22},
    /* R */ { 0,  5,  2,  7,  4,  9,  6, 11,  8, 13, 10, 15, 12,  1, 14,  3, 16, 17, 18, 19, 22, 20, 23, 21},
    /* t */ { 2,  0,  3,  1, 20, 21,  6,  7,  8,  9, 10, 11, 12, 13, 17, 16,  4,  5, 18, 19, 15, 14, 22, 23},
    /* T */ { 1,  3,  0,  2, 16, 17,  6,  7,  8,  9, 10, 11, 12, 13, 21, 20, 15, 14, 18, 19,  4,  5, 22, 23},
    /* b */ { 0,  1,  2,  3,  4,  5, 22, 23,  9, 11,  8, 10, 19, 18, 14, 15, 16, 17,  6,  7, 20, 21, 13, 12},
    /* B */ { 0,  1,  2,  3,  4,  5, 18, 19, 10,  8, 11,  9, 23, 22, 14, 15, 16, 17, 13, 12, 20, 21,  6,  7},
    /* f */ { 0,  1, 19, 17,  6,  4,  7,  5, 22, 20, 10, 11, 12, 13, 14, 15, 16,  8, 18,  9,  2, 21,  3, 23},
    /* F */ { 0,  1, 20, 22,  5,  7,  4,  6, 17, 19, 10, 11, 12, 13, 14, 15, 16,  3, 18,  2,  9, 21,  8, 23},
    /* a */ {18, 16,  2,  3,  4,  5,  6,  7,  8,  9, 23, 21, 13, 15, 12, 14, 10, 17, 11, 19, 20,  0, 22,  1},
    /* A */ {21, 23,  2,  3,  4,  5,  6,  7,  8,  9, 16, 18, 14, 12, 15, 13,  1, 17,  0, 19, 20, 11, 22, 10},
}};

/// Three generators of the whole-cube rotation group, used to enumerate the 24
/// ways of holding one state.
inline constexpr std::array<Facelets, 3> rotation_generators{{
    {12, 13, 14, 15,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 18, 16, 19, 17, 21, 23, 20, 22},
    { 2,  0,  3,  1, 20, 21, 22, 23,  9, 11,  8, 10, 19, 18, 17, 16,  4,  5,  6,  7, 15, 14, 13, 12},
    {18, 16, 19, 17,  6,  4,  7,  5, 22, 20, 23, 21, 13, 15, 12, 14, 10,  8, 11,  9,  2,  0,  3,  1},
}};
// clang-format on

/// Solved cube in the standard orientation: yellow up, red front.
inline constexpr State solved = [] {
    constexpr std::array<Color, 6> sides{Color::yellow, Color::red,    Color::white,
                                         Color::purple, Color::blue,   Color::green};
    State state{};
    for (std::size_t side = 0; side < sides.size(); ++side) {
        for (std::size_t cell = 0; cell < 4; ++cell) {
            state[side * 4 + cell] = sides[side];
        }
    }
    return state;
}();

/// Compose two facelet permutations: `out[i] = first[second[i]]`.
[[nodiscard]] constexpr Facelets compose(const Facelets& first, const Facelets& second) {
    Facelets out{};
    for (std::size_t i = 0; i < facelet_count; ++i) {
        out[i] = first[second[i]];
    }
    return out;
}

/// The 24 ways of holding a cube, closed under the generators above.
/// Computed at compile time; the `throw` is a constant-evaluation assertion
/// that the group really has 24 elements.
consteval std::array<Facelets, 24> make_rotations() {
    std::array<Facelets, 24> found{};
    for (std::uint8_t i = 0; i < facelet_count; ++i) {
        found[0][i] = i;
    }
    std::size_t count = 1;
    for (std::size_t head = 0; head < count; ++head) {
        for (const Facelets& generator : rotation_generators) {
            const Facelets candidate = compose(found[head], generator);
            bool seen = false;
            for (std::size_t j = 0; j < count && !seen; ++j) {
                seen = found[j] == candidate;
            }
            if (!seen) {
                if (count == found.size()) {
                    throw "the rotation group has more than 24 elements";
                }
                found[count++] = candidate;
            }
        }
    }
    if (count != found.size()) {
        throw "the rotation group has fewer than 24 elements";
    }
    return found;
}

inline constexpr std::array<Facelets, 24> rotations = make_rotations();

/// Turn one face.
[[nodiscard]] constexpr State turn(const State& state, Move move) {
    const Facelets& permutation = move_permutations[std::to_underlying(move)];
    State out{};
    for (std::size_t i = 0; i < facelet_count; ++i) {
        out[i] = state[permutation[i]];
    }
    return out;
}

/// Hold the same cube a different way round.
[[nodiscard]] constexpr State rotate(const State& state, const Facelets& rotation) {
    State out{};
    for (std::size_t i = 0; i < facelet_count; ++i) {
        out[i] = state[rotation[i]];
    }
    return out;
}

[[nodiscard]] constexpr std::optional<Move> move_from_char(char c) {
    const std::size_t position = move_chars.find(c);
    return position == std::string_view::npos ? std::nullopt
                                              : std::optional{static_cast<Move>(position)};
}

[[nodiscard]] constexpr char to_char(Move move) { return move_chars[std::to_underlying(move)]; }

/// Turn faces in sequence.
[[nodiscard]] constexpr State turn(const State& state, std::span<const Move> moves) {
    State current = state;
    for (Move move : moves) {
        current = turn(current, move);
    }
    return current;
}

/// Turn faces in sequence, rejecting unknown letters.
[[nodiscard]] Result<State> turn(const State& state, std::string_view moves);

/// Undo a sequence: reverse the order, flip every turn.
[[nodiscard]] Moves invert(std::string_view moves);

/// True when every face shows a single colour.
[[nodiscard]] constexpr bool is_solved(const State& state) {
    for (std::size_t side = 0; side < facelet_count; side += 4) {
        const Color color = state[side];
        for (std::size_t cell = 1; cell < 4; ++cell) {
            if (state[side + cell] != color) {
                return false;
            }
        }
    }
    return true;
}

/// Read 24 colour letters in side order UP FRONT DOWN BACK LEFT RIGHT.
/// Rejects anything that is not 24 letters from "rwbypg" with each colour
/// appearing exactly four times.
[[nodiscard]] Result<State> parse(std::string_view text);

/// The 24 colour letters of a state. Inverse of `parse`.
[[nodiscard]] std::string to_string(const State& state);

/// Apply `count` random moves, each reaching a state not seen before.
[[nodiscard]] State scramble(const State& state, int count, std::uint64_t seed);

}  // namespace rubik
