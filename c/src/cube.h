/* The 2x2x2 pocket cube as 24 stickers -- the readable model, nothing else.
 *
 * A state is 24 colour ids indexed `side * 4 + row * 2 + column`, sides in the
 * order UP FRONT DOWN BACK LEFT RIGHT and each side read row-major:
 *
 *     -------
 *     |0  1 |
 *     |2  3 |
 *     -------
 *
 * Every move is a facelet permutation applied as `new[i] = old[PERM[i]]`.
 * The tables are checked rather than trusted -- tests/ pins the algebra they
 * must obey, and building the distance table reproduces OEIS A079762, which no
 * wrong table could do.
 *
 * No search and no drawing live here: cube_coords.h re-encodes states for the
 * solvers, cube_display.h draws them.
 */
#ifndef RUBIK_CUBE_H
#define RUBIK_CUBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    CUBE_FACELETS = 24,
    CUBE_SIDES = 6,
    CUBE_COLORS = 6,
    CUBE_MOVES = 12,        /* l L r R t T b B f F a A */
    CUBE_SOLVE_MOVES = 6,   /* l L t T f F -- the generators the solvers use */
    CUBE_ROTATIONS = 24,    /* whole-cube rotations */
};

/* Sides, in the order a state string is written. */
enum { CUBE_UP, CUBE_FRONT, CUBE_DOWN, CUBE_BACK, CUBE_LEFT, CUBE_RIGHT };

/* One cube state. Small enough to pass and return by value. */
typedef struct {
    uint8_t facelet[CUBE_FACELETS];
} cube_t;

/* Colour letter per colour id: red, white, blue, yellow, purple, green. */
extern const char cube_color_chars[CUBE_COLORS + 1];

/* Move letters, inverses adjacent: "lLrRtTbBfFaA". */
extern const char cube_move_chars[CUBE_MOVES + 1];

/* The quarter-turn generators the solvers use: "lLtTfF".
 *
 * A fourth face would be redundant -- turning a face equals turning the
 * opposite face plus a whole-cube rotation. Inverses sit at adjacent indices,
 * which the IDA* pruning relies on, so keep them paired.
 */
extern const char cube_solve_move_chars[CUBE_SOLVE_MOVES + 1];

/* Facelets of the corner the solve moves leave in place (down-back-right).
 * Holding one corner still is what collapses the 24-fold rotation symmetry.
 */
extern const uint8_t cube_fixed_corner[3];

/* Solved cube in the standard orientation: yellow up, red front. */
extern const cube_t cube_solved;

/* The move permutations, indexed by the position of the letter in
 * cube_move_chars. Exposed so tests can exercise them directly.
 */
extern const uint8_t cube_move_perm[CUBE_MOVES][CUBE_FACELETS];

/* Index of `letter` in cube_move_chars, or -1. */
int cube_move_index(char letter);

/* Index of `letter` in cube_solve_move_chars, or -1. */
int cube_solve_move_index(char letter);

cube_t cube_apply_move(cube_t state, int move);

/* Apply a chain such as "lFtL". Returns false (leaving *out untouched) if the
 * string contains a letter that is not a move.
 */
bool cube_apply_moves(cube_t state, const char *moves, cube_t *out);

/* Undo a sequence: reverse the order, flip every turn. `out` needs room for
 * strlen(moves) + 1 bytes.
 */
void cube_invert(const char *moves, char *out);

bool cube_is_solved(cube_t state);

/* Parse 24 colour letters. On failure returns false and, when `error` is not
 * NULL, writes a human-readable reason of at most `error_size` bytes.
 */
bool cube_parse(const char *text, cube_t *out, char *error, size_t error_size);

/* The 24 colour letters of a state; writes 25 bytes including the terminator. */
void cube_to_string(cube_t state, char out[static CUBE_FACELETS + 1]);

/* The 24 whole-cube rotations as facelet permutations, generated once on first
 * use. Used to recognise which of the 24 orientations a scramble is held in.
 */
const uint8_t (*cube_rotation_perms(void))[CUBE_FACELETS];

cube_t cube_rotate(cube_t state, const uint8_t perm[static CUBE_FACELETS]);

/* Deterministic 64-bit generator, so `scramble --seed` is reproducible
 * everywhere rather than at the mercy of the platform's rand(). */
typedef struct {
    uint64_t state;
} cube_rng_t;

cube_rng_t cube_rng_seed(uint64_t seed);
uint64_t cube_rng_next(cube_rng_t *rng);

/* Apply `count` random moves, each reaching a state not seen before. */
cube_t cube_scramble(cube_t state, int count, cube_rng_t *rng);

#endif /* RUBIK_CUBE_H */
