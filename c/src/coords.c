#include "coords.h"

#include <stdlib.h>

const uint8_t coord_slots[8][3] = {
    {16,  0, 14},   /* left-up-back */
    {21, 15,  1},   /* right-up-back */
    {17,  4,  2},   /* left-up-front */
    {20,  3,  5},   /* right-up-front */
    {19,  8,  6},   /* left-down-front */
    {22,  7,  9},   /* right-down-front */
    {18, 12, 10},   /* left-down-back */
    {23, 11, 13},   /* right-down-back -- fixed */
};

/* 6! .. 0!, the place values of a Lehmer code over seven symbols. */
static const uint16_t factorial[COORD_FREE] = {720, 120, 24, 6, 2, 1, 1};

const char *coords_status_message(coord_status_t status)
{
    switch (status) {
    case COORD_OK:          return "ok";
    case COORD_ERR_CORNER:  return "the fixed corner's colours occur on no solved cube";
    case COORD_ERR_PIECE:   return "a corner's colours form no piece of this cube";
    case COORD_ERR_REPEAT:  return "the same corner piece appears in two slots";
    case COORD_ERR_TWIST:   return "a corner is twisted in place; the twists do not sum to "
                                   "a multiple of three";
    }
    return "unknown";
}

/* Lehmer code. The identity ranks 0, which is all the encoding needs of it. */
static uint16_t rank_permutation(const uint8_t perm[static COORD_FREE])
{
    uint16_t rank = 0;
    for (int i = 0; i < COORD_FREE; i++) {
        uint16_t smaller = 0;
        for (int j = i + 1; j < COORD_FREE; j++)
            smaller += perm[j] < perm[i];
        rank += smaller * factorial[i];
    }
    return rank;
}

static void unrank_permutation(uint16_t rank, uint8_t perm[static COORD_FREE])
{
    uint8_t pool[COORD_FREE];
    for (int i = 0; i < COORD_FREE; i++)
        pool[i] = (uint8_t)i;

    int remaining = COORD_FREE;
    for (int i = 0; i < COORD_FREE; i++) {
        const int pick = rank / factorial[i];
        rank = (uint16_t)(rank % factorial[i]);
        perm[i] = pool[pick];
        remaining--;
        for (int j = pick; j < remaining; j++)
            pool[j] = pool[j + 1];
    }
}

static uint16_t rank_orientation(const uint8_t twist[static COORD_FREE])
{
    uint16_t rank = 0;
    for (int i = COORD_FREE - 2; i >= 0; i--)
        rank = (uint16_t)(rank * 3 + twist[i]);
    return rank;
}

static void unrank_orientation(uint16_t rank, uint8_t twist[static COORD_FREE])
{
    int sum = 0;
    for (int i = 0; i < COORD_FREE - 1; i++) {
        twist[i] = (uint8_t)(rank % 3);
        sum += twist[i];
        rank = (uint16_t)(rank / 3);
    }
    twist[COORD_FREE - 1] = (uint8_t)((3 - sum % 3) % 3);   /* twists sum to 0 mod 3 */
}

static void pieces_of(cube_t goal, uint8_t pieces[8][3])
{
    for (int slot = 0; slot < 8; slot++)
        for (int k = 0; k < 3; k++)
            pieces[slot][k] = goal.facelet[coord_slots[slot][k]];
}

static int goal_key(cube_t state)
{
    return state.facelet[cube_fixed_corner[0]] * 36
         + state.facelet[cube_fixed_corner[1]] * 6
         + state.facelet[cube_fixed_corner[2]];
}

/* Split a sticker state into (piece per slot, twist per slot), rejecting
 * anything no sequence of turns could have produced. */
static coord_status_t to_cubies(cube_t state, const uint8_t pieces[8][3],
                                uint8_t perm[static COORD_FREE],
                                uint8_t twist[static COORD_FREE])
{
    bool used[8] = {false};
    int total_twist = 0;
    for (int slot = 0; slot < COORD_FREE; slot++) {
        const uint8_t *const facelets = coord_slots[slot];
        const uint8_t colors[3] = {
            state.facelet[facelets[0]], state.facelet[facelets[1]], state.facelet[facelets[2]],
        };

        int found_piece = -1, found_twist = 0;
        for (int piece = 0; piece < COORD_FREE && found_piece < 0; piece++)
            for (int t = 0; t < 3; t++)
                if (colors[0] == pieces[piece][t]
                    && colors[1] == pieces[piece][(t + 1) % 3]
                    && colors[2] == pieces[piece][(t + 2) % 3]) {
                    found_piece = piece;
                    found_twist = t;
                    break;
                }

        if (found_piece < 0)
            return COORD_ERR_PIECE;
        if (used[found_piece])
            return COORD_ERR_REPEAT;
        used[found_piece] = true;
        perm[slot] = (uint8_t)found_piece;
        twist[slot] = (uint8_t)found_twist;
        total_twist += found_twist;
    }

    /* The fixed corner cannot absorb a twist, so on a reachable cube the seven
     * free twists already sum to a multiple of three. Checking it here is what
     * catches a single corner turned in place -- otherwise only six twists are
     * ranked and the seventh is silently rewritten to whatever makes the sum
     * come out right. */
    if (total_twist % 3 != 0)
        return COORD_ERR_TWIST;
    return COORD_OK;
}

/* Rebuild a sticker state. Inverse of to_cubies. */
static cube_t from_cubies(const uint8_t perm[static COORD_FREE],
                          const uint8_t twist[static COORD_FREE],
                          const uint8_t pieces[8][3])
{
    cube_t state = {{0}};
    for (int slot = 0; slot < COORD_FREE; slot++)
        for (int k = 0; k < 3; k++)
            state.facelet[coord_slots[slot][k]] = pieces[perm[slot]][(twist[slot] + k) % 3];
    for (int k = 0; k < 3; k++)
        state.facelet[coord_slots[COORD_FREE][k]] = pieces[COORD_FREE][k];
    return state;
}

static void build_goals(coord_tables_t *tables)
{
    const uint8_t (*const rotations)[CUBE_FACELETS] = cube_rotation_perms();
    for (int r = 0; r < CUBE_ROTATIONS; r++) {
        const cube_t goal = cube_rotate(cube_solved, rotations[r]);
        coord_goal_t *const entry = &tables->goal[goal_key(goal)];
        entry->valid = true;
        pieces_of(goal, entry->pieces);
    }
}

static void build_move_tables(coord_tables_t *tables)
{
    uint8_t identity_perm[COORD_FREE], identity_twist[COORD_FREE] = {0};
    for (int i = 0; i < COORD_FREE; i++)
        identity_perm[i] = (uint8_t)i;
    const cube_t identity = from_cubies(identity_perm, identity_twist, tables->canonical);

    for (int move = 0; move < COORD_MOVES; move++) {
        /* On the identity state the permutation *is* the slot-to-slot mapping
         * and the orientation *is* the twist each slot picks up. */
        const cube_t turned =
            cube_apply_move(identity, cube_move_index(cube_solve_move_chars[move]));
        uint8_t source[COORD_FREE], twist[COORD_FREE];
        (void)to_cubies(turned, tables->canonical, source, twist);

        for (uint16_t rank = 0; rank < COORD_PERMUTATIONS; rank++) {
            uint8_t old[COORD_FREE], next[COORD_FREE];
            unrank_permutation(rank, old);
            for (int slot = 0; slot < COORD_FREE; slot++)
                next[slot] = old[source[slot]];
            const uint16_t mapped = rank_permutation(next);
            tables->perm_raw[move][rank] = mapped;
            tables->perm[move][rank] = (uint32_t)mapped * COORD_ORIENTATIONS;
            tables->perm_by_q[rank][move] = (uint32_t)mapped * COORD_ORIENTATIONS;
            tables->permq_by_q[rank][move] = mapped;
        }

        for (uint16_t rank = 0; rank < COORD_ORIENTATIONS; rank++) {
            uint8_t old[COORD_FREE], next[COORD_FREE];
            unrank_orientation(rank, old);
            for (int slot = 0; slot < COORD_FREE; slot++)
                next[slot] = (uint8_t)((old[source[slot]] + twist[slot]) % 3);
            tables->orient[move][rank] = rank_orientation(next);
            tables->orient_by_r[rank][move] = tables->orient[move][rank];
        }
    }
}

coord_tables_t *coords_create(void)
{
    coord_tables_t *const tables = calloc(1, sizeof *tables);
    if (!tables)
        return nullptr;
    pieces_of(cube_solved, tables->canonical);
    build_goals(tables);
    build_move_tables(tables);
    return tables;
}

void coords_destroy(coord_tables_t *tables)
{
    free(tables);
}

coord_status_t coords_encode(const coord_tables_t *tables, cube_t state, uint32_t *index)
{
    const coord_goal_t *const goal = &tables->goal[goal_key(state)];
    if (!goal->valid)
        return COORD_ERR_CORNER;

    uint8_t perm[COORD_FREE], twist[COORD_FREE];
    const coord_status_t status = to_cubies(state, goal->pieces, perm, twist);
    if (status != COORD_OK)
        return status;

    *index = (uint32_t)rank_permutation(perm) * COORD_ORIENTATIONS + rank_orientation(twist);
    return COORD_OK;
}

cube_t coords_decode(const coord_tables_t *tables, uint32_t index)
{
    uint8_t perm[COORD_FREE], twist[COORD_FREE];
    unrank_permutation((uint16_t)(index / COORD_ORIENTATIONS), perm);
    unrank_orientation((uint16_t)(index % COORD_ORIENTATIONS), twist);
    return from_cubies(perm, twist, tables->canonical);
}
