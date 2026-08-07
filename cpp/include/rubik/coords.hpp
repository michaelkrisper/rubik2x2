/// Cubie coordinates: the compact state encoding every solver runs on.
///
/// `rubik::cube` models a state as 24 stickers, which is what a human reads but
/// not what a search wants. A pocket cube with one corner held still is
/// described by far less: which of the 7 free corner *pieces* sits in each of
/// the 7 free *slots* (7! = 5040 arrangements) and how each is twisted (3^7,
/// minus the constraint that the twists sum to a multiple of 3, so 3^6 = 729).
/// That is 5040 * 729 = 3,674,160 states.
///
/// The encoding splits in two because a move acts on the two halves
/// independently:
///
///     new_permutation[slot] = old_permutation[source[slot]]
///     new_orientation[slot] = old_orientation[source[slot]] + twist[slot]  (mod 3)
///
/// So a move is two lookups in tables of 5040 and 729 entries, and a whole
/// state is a single integer below 3,674,160. That dense bounded index is what
/// makes the full distance table possible at all, and what hands IDA* its two
/// pattern databases for free.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "rubik/cube.hpp"
#include "rubik/error.hpp"

namespace rubik::coords {

/// Slots that actually move; the eighth is the corner held still.
inline constexpr std::size_t free_slots = 7;

inline constexpr std::uint32_t orientation_count = 729;   // 3^6
inline constexpr std::uint32_t permutation_count = 5040;  // 7!
inline constexpr std::uint32_t state_count = permutation_count * orientation_count;

/// A solved cube is the identity permutation with no twists, which ranks as 0.
inline constexpr std::uint32_t solved_index = 0;

/// Each corner slot as its three facelets, listed in a consistent rotational
/// sense so that "twist" means the same thing at every slot: all eight triples
/// agree in handedness. Slot 7 is the corner the solve moves never touch.
inline constexpr std::array<std::array<std::uint8_t, 3>, 8> slots{{
    {16, 0, 14},   // left-up-back
    {21, 15, 1},   // right-up-back
    {17, 4, 2},    // left-up-front
    {20, 3, 5},    // right-up-front
    {19, 8, 6},    // left-down-front
    {22, 7, 9},    // right-down-front
    {18, 12, 10},  // left-down-back
    {23, 11, 13},  // right-down-back -- fixed
}};

/// The three colours a corner piece shows, in slot order.
using Piece = std::array<Color, 3>;
using Pieces = std::array<Piece, slots.size()>;
using Permutation = std::array<std::uint8_t, free_slots>;
using Orientation = std::array<std::uint8_t, free_slots>;

struct Cubies {
    Permutation permutation;
    Orientation orientation;
};

/// The canonical colour triple of each corner piece, read off a solved cube.
[[nodiscard]] constexpr Pieces pieces_of(const State& goal) {
    Pieces pieces{};
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        for (std::size_t i = 0; i < 3; ++i) {
            pieces[slot][i] = goal[slots[slot][i]];
        }
    }
    return pieces;
}

/// Lehmer code. Any bijection would do as long as the identity ranks 0; this
/// one also happens to be lexicographic, which makes it easy to check by hand.
[[nodiscard]] constexpr std::uint16_t rank_permutation(const Permutation& permutation) {
    std::uint32_t rank = 0;
    for (std::size_t i = 0; i < free_slots; ++i) {
        std::uint32_t smaller = 0;
        for (std::size_t j = i + 1; j < free_slots; ++j) {
            smaller += permutation[j] < permutation[i] ? 1u : 0u;
        }
        rank = rank * static_cast<std::uint32_t>(free_slots - i) + smaller;
    }
    return static_cast<std::uint16_t>(rank);
}

[[nodiscard]] constexpr Permutation unrank_permutation(std::uint16_t rank) {
    Permutation available{};
    for (std::uint8_t i = 0; i < free_slots; ++i) {
        available[i] = i;
    }
    Permutation permutation{};
    for (std::size_t i = 0; i < free_slots; ++i) {
        const std::size_t remaining = free_slots - i;
        std::uint32_t factorial = 1;
        for (std::size_t f = 2; f < remaining; ++f) {
            factorial *= static_cast<std::uint32_t>(f);
        }
        const std::size_t choice = rank / factorial;
        rank = static_cast<std::uint16_t>(rank % factorial);
        permutation[i] = available[choice];
        for (std::size_t j = choice; j + 1 < remaining; ++j) {
            available[j] = available[j + 1];
        }
    }
    return permutation;
}

/// Only six twists are stored; the seventh is forced by the parity constraint.
[[nodiscard]] constexpr std::uint16_t rank_orientation(const Orientation& orientation) {
    std::uint16_t rank = 0;
    for (std::size_t i = free_slots - 1; i-- > 0;) {
        rank = static_cast<std::uint16_t>(rank * 3 + orientation[i]);
    }
    return rank;
}

[[nodiscard]] constexpr Orientation unrank_orientation(std::uint16_t rank) {
    Orientation orientation{};
    int total = 0;
    for (std::size_t i = 0; i + 1 < free_slots; ++i) {
        orientation[i] = static_cast<std::uint8_t>(rank % 3);
        total += orientation[i];
        rank = static_cast<std::uint16_t>(rank / 3);
    }
    orientation[free_slots - 1] = static_cast<std::uint8_t>((3 - total % 3) % 3);
    return orientation;
}

/// A solved cube, plus the piece colours derived from it. The fixed corner's
/// three sticker colours identify which of the 24 rotations of a solved cube a
/// scramble is aiming at, so a cube handed over any which way still encodes
/// correctly and the goal index is always 0.
struct Goal {
    State state;
    Pieces pieces;
};

consteval std::array<Goal, rotations.size()> make_goals() {
    std::array<Goal, rotations.size()> all{};
    for (std::size_t i = 0; i < rotations.size(); ++i) {
        all[i].state = rotate(solved, rotations[i]);
        all[i].pieces = pieces_of(all[i].state);
    }
    return all;
}

inline constexpr std::array<Goal, rotations.size()> goals = make_goals();

/// Colour triple at the fixed corner, packed into 0..215.
[[nodiscard]] constexpr std::size_t goal_key(const State& state) {
    std::size_t key = 0;
    for (std::uint8_t facelet : fixed_corner) {
        key = key * 6 + std::to_underlying(state[facelet]);
    }
    return key;
}

consteval std::array<std::int8_t, 216> make_goal_lookup() {
    std::array<std::int8_t, 216> lookup{};
    for (std::int8_t& entry : lookup) {
        entry = -1;
    }
    for (std::size_t i = 0; i < goals.size(); ++i) {
        lookup[goal_key(goals[i].state)] = static_cast<std::int8_t>(i);
    }
    return lookup;
}

inline constexpr std::array<std::int8_t, 216> goal_lookup = make_goal_lookup();

/// The piece colours of a cube in the standard orientation.
inline constexpr Pieces solved_pieces = pieces_of(solved);

/// Split a sticker state into (piece per slot, twist per slot).
[[nodiscard]] Result<Cubies> to_cubies(const State& state, const Pieces& pieces);

/// Rebuild a sticker state. Inverse of `to_cubies`.
[[nodiscard]] State from_cubies(const Cubies& cubies, const Pieces& pieces);

/// Sticker state -> dense index below `state_count`, using the cube's own goal
/// orientation so all 24 ways of holding one scramble encode alike.
[[nodiscard]] Result<std::uint32_t> encode(const State& state);

/// Dense index -> sticker state. Inverse of `encode` up to orientation.
[[nodiscard]] State decode(std::uint32_t index, const Pieces& pieces = solved_pieces);

/// One (permutation table, orientation table) pair per solve move.
///
/// `permutation_wide` is the permutation table pre-multiplied by the row width,
/// so a search computes a child as `permutation_wide[q] + orientation[r]` --
/// one add rather than a multiply and an add, in a loop that runs tens of
/// millions of times. `permutation` is the same table unscaled, which is what
/// the IDA* pattern database needs.
struct MoveTable {
    std::array<std::uint16_t, permutation_count> permutation;
    std::array<std::uint32_t, permutation_count> permutation_wide;
    std::array<std::uint16_t, orientation_count> orientation;
};

using MoveTables = std::array<MoveTable, solve_moves.size()>;

/// Built on first use -- roughly 35k entries, a fraction of a millisecond.
[[nodiscard]] const MoveTables& move_tables();

/// Apply one solve move (by its index in `solve_moves`) to an encoded state.
[[nodiscard]] inline std::uint32_t apply(std::uint32_t index, std::size_t move_position) {
    const MoveTable& table = move_tables()[move_position];
    return table.permutation_wide[index / orientation_count] +
           table.orientation[index % orientation_count];
}

}  // namespace rubik::coords
