/* Cubie coordinates: the compact state encoding every solver runs on.
 *
 * cube.h models a state as 24 stickers, which is what a human reads but not
 * what a search wants. A pocket cube with one corner held still is described by
 * far less: which of the 7 free corner *pieces* sits in each of the 7 free
 * *slots* (7! = 5040 arrangements) and how each is twisted (3^7, minus the
 * constraint that the twists sum to a multiple of 3, so 3^6 = 729). That is
 * 5040 * 729 = 3,674,160 states -- exactly the reachable space.
 *
 * The payoff is that a move acts on the two halves independently:
 *
 *     new_permutation[slot] = old_permutation[source[slot]]
 *     new_orientation[slot] = old_orientation[source[slot]] + twist[slot]  (mod 3)
 *
 * so a move is two array lookups and an add, and a whole state is one uint32_t
 * below 3,674,160. A dense bounded index is what makes the full distance table
 * possible at all, and it hands IDA* its two pattern databases for free.
 *
 * The tables are heap-allocated and owned by the caller -- no hidden globals,
 * so a test can build, use and free several independently.
 */
#ifndef RUBIK_COORDS_H
#define RUBIK_COORDS_H

#include "cube.h"

enum {
    COORD_FREE = 7,                  /* slots the solve moves actually move */
    COORD_ORIENTATIONS = 729,        /* 3^6; the 7th twist is implied */
    COORD_PERMUTATIONS = 5040,       /* 7! */
    COORD_STATES = 3674160,          /* 5040 * 729 */
    COORD_SOLVED_INDEX = 0,
    COORD_MOVES = CUBE_SOLVE_MOVES,
    COORD_GOAL_KEYS = CUBE_COLORS * CUBE_COLORS * CUBE_COLORS,
};

/* Each corner slot as its three facelets, listed in a consistent rotational
 * sense so that "twist" means the same thing at every slot. Slot 7 is the
 * corner the solve moves never touch.
 */
extern const uint8_t coord_slots[8][3];

/* One goal orientation: the colour triple of each piece on a solved cube held
 * that way. A scramble may arrive in any of the 24 orientations, so the fixed
 * corner's colours pick the goal and the encoding is correct regardless.
 */
typedef struct {
    bool valid;
    uint8_t pieces[8][3];
} coord_goal_t;

typedef struct {
    /* child = perm[move][index / 729] + orient[move][index % 729].
     * The permutation side is pre-multiplied by the row width so the child is
     * one addition, in a loop that runs tens of millions of times. */
    uint32_t perm[COORD_MOVES][COORD_PERMUTATIONS];
    uint16_t orient[COORD_MOVES][COORD_ORIENTATIONS];

    /* The same tables unscaled, which is what the two pattern databases are
     * breadth-first searched over. */
    uint16_t perm_raw[COORD_MOVES][COORD_PERMUTATIONS];

    uint8_t canonical[8][3];                 /* pieces of the standard solved cube */
    coord_goal_t goal[COORD_GOAL_KEYS];      /* keyed by the fixed corner's colours */
} coord_tables_t;

typedef enum {
    COORD_OK = 0,
    COORD_ERR_CORNER,   /* fixed corner colours occur on no solved cube */
    COORD_ERR_PIECE,    /* a slot's colours form no corner of this cube */
    COORD_ERR_REPEAT,   /* the same piece appears in two slots */
} coord_status_t;

const char *coords_status_message(coord_status_t status);

/* Build the move tables and goal lookup (~35k entries, well under a
 * millisecond). Returns NULL only if allocation fails. */
coord_tables_t *coords_create(void);
void coords_destroy(coord_tables_t *tables);

/* Sticker state -> dense index. All 24 ways of holding the same scramble
 * encode to the same index, and a solved cube always encodes to 0. */
coord_status_t coords_encode(const coord_tables_t *tables, cube_t state, uint32_t *index);

/* Dense index -> sticker state in the standard orientation. Inverse of
 * coords_encode for cubes held that way. */
cube_t coords_decode(const coord_tables_t *tables, uint32_t index);

static inline uint32_t coords_apply(const coord_tables_t *tables, int move, uint32_t index)
{
    return tables->perm[move][index / COORD_ORIENTATIONS]
         + tables->orient[move][index % COORD_ORIENTATIONS];
}

#endif /* RUBIK_COORDS_H */
