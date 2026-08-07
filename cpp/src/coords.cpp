#include "rubik/coords.hpp"

#include <format>

namespace rubik::coords {
namespace {

/// Rotate a piece's colour triple left by `twist`.
[[nodiscard]] constexpr Piece twisted(const Piece& piece, std::size_t twist) {
    return {piece[twist % 3], piece[(twist + 1) % 3], piece[(twist + 2) % 3]};
}

}  // namespace

Result<Cubies> to_cubies(const State& state, const Pieces& pieces) {
    Cubies cubies{};
    std::array<bool, slots.size()> used{};
    int total_twist = 0;

    for (std::size_t slot = 0; slot < free_slots; ++slot) {
        const Piece colors{state[slots[slot][0]], state[slots[slot][1]], state[slots[slot][2]]};
        bool found = false;
        for (std::size_t piece = 0; piece < pieces.size() && !found; ++piece) {
            for (std::size_t twist = 0; twist < 3; ++twist) {
                if (colors == twisted(pieces[piece], twist)) {
                    if (piece >= free_slots || used[piece]) {
                        return fail(ErrorKind::unsolvable,
                                    std::format("corner piece {} occupies two slots", piece));
                    }
                    used[piece] = true;
                    cubies.permutation[slot] = static_cast<std::uint8_t>(piece);
                    cubies.orientation[slot] = static_cast<std::uint8_t>(twist);
                    total_twist += static_cast<int>(twist);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return fail(ErrorKind::unsolvable,
                        std::format("facelets {},{},{} do not hold a corner of this cube",
                                    slots[slot][0], slots[slot][1], slots[slot][2]));
        }
    }

    // The eighth corner is held still and therefore untwisted, so the twists of
    // the seven free corners must already sum to a multiple of three. A cube
    // that fails this is one corner rotated in place: well formed, unreachable.
    if (total_twist % 3 != 0) {
        return fail(ErrorKind::unsolvable, "corner twists do not sum to a multiple of three");
    }
    return cubies;
}

State from_cubies(const Cubies& cubies, const Pieces& pieces) {
    State state{};
    for (std::size_t slot = 0; slot < free_slots; ++slot) {
        const Piece colors = twisted(pieces[cubies.permutation[slot]], cubies.orientation[slot]);
        for (std::size_t i = 0; i < 3; ++i) {
            state[slots[slot][i]] = colors[i];
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        state[slots[free_slots][i]] = pieces[free_slots][i];
    }
    return state;
}

Result<std::uint32_t> encode(const State& state) {
    const std::int8_t goal = goal_lookup[goal_key(state)];
    if (goal < 0) {
        return fail(ErrorKind::unsolvable,
                    std::format("corner colours {}{}{} do not occur on a solved cube",
                                color_chars[std::to_underlying(state[fixed_corner[0]])],
                                color_chars[std::to_underlying(state[fixed_corner[1]])],
                                color_chars[std::to_underlying(state[fixed_corner[2]])]));
    }
    return to_cubies(state, goals[static_cast<std::size_t>(goal)].pieces)
        .transform([](const Cubies& cubies) {
            return rank_permutation(cubies.permutation) * orientation_count +
                   rank_orientation(cubies.orientation);
        });
}

State decode(std::uint32_t index, const Pieces& pieces) {
    return from_cubies({unrank_permutation(static_cast<std::uint16_t>(index / orientation_count)),
                        unrank_orientation(static_cast<std::uint16_t>(index % orientation_count))},
                       pieces);
}

namespace {

MoveTables build_move_tables() {
    MoveTables tables{};

    Cubies identity_cubies{};
    for (std::uint8_t slot = 0; slot < free_slots; ++slot) {
        identity_cubies.permutation[slot] = slot;
    }
    const State identity = from_cubies(identity_cubies, solved_pieces);

    for (std::size_t position = 0; position < solve_moves.size(); ++position) {
        // On the identity state the permutation *is* the slot-to-slot mapping
        // and the orientation *is* the twist each slot picks up. A failure here
        // would mean the move tables themselves are wrong, which the tests
        // exclude, so a hard failure is the right response.
        const Cubies moved =
            to_cubies(turn(identity, solve_moves[position]), solved_pieces).value();
        const Permutation& source = moved.permutation;
        const Orientation& twist = moved.orientation;
        MoveTable& table = tables[position];

        for (std::uint32_t rank = 0; rank < permutation_count; ++rank) {
            const Permutation before = unrank_permutation(static_cast<std::uint16_t>(rank));
            Permutation after{};
            for (std::size_t slot = 0; slot < free_slots; ++slot) {
                after[slot] = before[source[slot]];
            }
            const std::uint16_t moved_rank = rank_permutation(after);
            table.permutation[rank] = moved_rank;
            table.permutation_wide[rank] = moved_rank * orientation_count;
        }

        for (std::uint32_t rank = 0; rank < orientation_count; ++rank) {
            const Orientation before = unrank_orientation(static_cast<std::uint16_t>(rank));
            Orientation after{};
            for (std::size_t slot = 0; slot < free_slots; ++slot) {
                after[slot] = static_cast<std::uint8_t>((before[source[slot]] + twist[slot]) % 3);
            }
            table.orientation[rank] = rank_orientation(after);
        }
    }
    return tables;
}

}  // namespace

const MoveTables& move_tables() {
    static const MoveTables tables = build_move_tables();
    return tables;
}

}  // namespace rubik::coords
