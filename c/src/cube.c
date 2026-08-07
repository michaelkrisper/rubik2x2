#include "cube.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

const char cube_color_chars[CUBE_COLORS + 1] = "rwbypg";
const char cube_move_chars[CUBE_MOVES + 1] = "lLrRtTbBfFaA";
const char cube_solve_move_chars[CUBE_SOLVE_MOVES + 1] = "lLtTfF";
const uint8_t cube_fixed_corner[3] = {11, 13, 23};

enum { COLOR_R, COLOR_W, COLOR_B, COLOR_Y, COLOR_P, COLOR_G };

const cube_t cube_solved = {{
    COLOR_Y, COLOR_Y, COLOR_Y, COLOR_Y,
    COLOR_R, COLOR_R, COLOR_R, COLOR_R,
    COLOR_W, COLOR_W, COLOR_W, COLOR_W,
    COLOR_P, COLOR_P, COLOR_P, COLOR_P,
    COLOR_B, COLOR_B, COLOR_B, COLOR_B,
    COLOR_G, COLOR_G, COLOR_G, COLOR_G,
}};

/* l/L  left column down/up      r/R  right column down/up
 * t/T  top layer left/right     b/B  bottom layer left/right
 * f/F  front face cw/ccw        a/A  back face cw/ccw
 */
const uint8_t cube_move_perm[CUBE_MOVES][CUBE_FACELETS] = {
    {12,  1, 14,  3,  0,  5,  2,  7,  4,  9,  6, 11,  8, 13, 10, 15, 18, 16, 19, 17, 20, 21, 22, 23},
    { 4,  1,  6,  3,  8,  5, 10,  7, 12,  9, 14, 11,  0, 13,  2, 15, 17, 19, 16, 18, 20, 21, 22, 23},
    { 0, 13,  2, 15,  4,  1,  6,  3,  8,  5, 10,  7, 12,  9, 14, 11, 16, 17, 18, 19, 21, 23, 20, 22},
    { 0,  5,  2,  7,  4,  9,  6, 11,  8, 13, 10, 15, 12,  1, 14,  3, 16, 17, 18, 19, 22, 20, 23, 21},
    { 2,  0,  3,  1, 20, 21,  6,  7,  8,  9, 10, 11, 12, 13, 17, 16,  4,  5, 18, 19, 15, 14, 22, 23},
    { 1,  3,  0,  2, 16, 17,  6,  7,  8,  9, 10, 11, 12, 13, 21, 20, 15, 14, 18, 19,  4,  5, 22, 23},
    { 0,  1,  2,  3,  4,  5, 22, 23,  9, 11,  8, 10, 19, 18, 14, 15, 16, 17,  6,  7, 20, 21, 13, 12},
    { 0,  1,  2,  3,  4,  5, 18, 19, 10,  8, 11,  9, 23, 22, 14, 15, 16, 17, 13, 12, 20, 21,  6,  7},
    { 0,  1, 19, 17,  6,  4,  7,  5, 22, 20, 10, 11, 12, 13, 14, 15, 16,  8, 18,  9,  2, 21,  3, 23},
    { 0,  1, 20, 22,  5,  7,  4,  6, 17, 19, 10, 11, 12, 13, 14, 15, 16,  3, 18,  2,  9, 21,  8, 23},
    {18, 16,  2,  3,  4,  5,  6,  7,  8,  9, 23, 21, 13, 15, 12, 14, 10, 17, 11, 19, 20,  0, 22,  1},
    {21, 23,  2,  3,  4,  5,  6,  7,  8,  9, 16, 18, 14, 12, 15, 13,  1, 17,  0, 19, 20, 11, 22, 10},
};

/* Three generators of the rotation group; the full 24 are closed over below. */
static const uint8_t rotation_generators[3][CUBE_FACELETS] = {
    {12, 13, 14, 15,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 18, 16, 19, 17, 21, 23, 20, 22},
    { 2,  0,  3,  1, 20, 21, 22, 23,  9, 11,  8, 10, 19, 18, 17, 16,  4,  5,  6,  7, 15, 14, 13, 12},
    {18, 16, 19, 17,  6,  4,  7,  5, 22, 20, 23, 21, 13, 15, 12, 14, 10,  8, 11,  9,  2,  0,  3,  1},
};

int cube_move_index(char letter)
{
    const char *found = letter ? strchr(cube_move_chars, letter) : nullptr;
    return found ? (int)(found - cube_move_chars) : -1;
}

int cube_solve_move_index(char letter)
{
    const char *found = letter ? strchr(cube_solve_move_chars, letter) : nullptr;
    return found ? (int)(found - cube_solve_move_chars) : -1;
}

cube_t cube_apply_move(cube_t state, int move)
{
    const uint8_t *const perm = cube_move_perm[move];
    cube_t next;
    for (int i = 0; i < CUBE_FACELETS; i++)
        next.facelet[i] = state.facelet[perm[i]];
    return next;
}

bool cube_apply_moves(cube_t state, const char *moves, cube_t *out)
{
    for (const char *p = moves; *p; p++) {
        const int move = cube_move_index(*p);
        if (move < 0)
            return false;
        state = cube_apply_move(state, move);
    }
    *out = state;
    return true;
}

void cube_invert(const char *moves, char *out)
{
    const size_t length = strlen(moves);
    for (size_t i = 0; i < length; i++) {
        const char letter = moves[length - 1 - i];
        /* Case is the inverse, and both cases are always present in the table. */
        out[i] = (char)(letter >= 'a' ? letter - ('a' - 'A') : letter + ('a' - 'A'));
    }
    out[length] = '\0';
}

bool cube_is_solved(cube_t state)
{
    for (int side = 0; side < CUBE_FACELETS; side += 4) {
        const uint8_t color = state.facelet[side];
        for (int i = 1; i < 4; i++)
            if (state.facelet[side + i] != color)
                return false;
    }
    return true;
}

bool cube_parse(const char *text, cube_t *out, char *error, size_t error_size)
{
    /* A trailing newline is what a pipe hands us, so trim whitespace. */
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')
        text++;
    size_t length = strlen(text);
    while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t'
                          || text[length - 1] == '\n' || text[length - 1] == '\r'))
        length--;

    if (length != CUBE_FACELETS) {
        if (error)
            snprintf(error, error_size, "expected %d colour letters, got %zu",
                     CUBE_FACELETS, length);
        return false;
    }

    cube_t state = {{0}};
    int counts[CUBE_COLORS] = {0};
    for (size_t i = 0; i < length; i++) {
        char letter = text[i];
        if (letter >= 'A' && letter <= 'Z')
            letter = (char)(letter + ('a' - 'A'));
        const char *found = strchr(cube_color_chars, letter);
        if (!found || !letter) {
            if (error)
                snprintf(error, error_size, "invalid colour letter '%c' (allowed: %s)",
                         text[i], cube_color_chars);
            return false;
        }
        const int color = (int)(found - cube_color_chars);
        state.facelet[i] = (uint8_t)color;
        counts[color]++;
    }
    for (int color = 0; color < CUBE_COLORS; color++)
        if (counts[color] != 4) {
            if (error)
                snprintf(error, error_size, "colour '%c' appears %d times, expected 4",
                         cube_color_chars[color], counts[color]);
            return false;
        }

    *out = state;
    return true;
}

void cube_to_string(cube_t state, char out[static CUBE_FACELETS + 1])
{
    for (int i = 0; i < CUBE_FACELETS; i++)
        out[i] = cube_color_chars[state.facelet[i]];
    out[CUBE_FACELETS] = '\0';
}

static uint8_t rotation_perms[CUBE_ROTATIONS][CUBE_FACELETS];
static pthread_once_t rotations_once = PTHREAD_ONCE_INIT;

static void build_rotations(void)
{
    for (int i = 0; i < CUBE_FACELETS; i++)
        rotation_perms[0][i] = (uint8_t)i;

    size_t count = 1;
    for (size_t head = 0; head < count; head++)
        for (int g = 0; g < 3; g++) {
            uint8_t composed[CUBE_FACELETS];
            for (int i = 0; i < CUBE_FACELETS; i++)
                composed[i] = rotation_perms[head][rotation_generators[g][i]];

            bool seen = false;
            for (size_t k = 0; k < count && !seen; k++)
                seen = memcmp(rotation_perms[k], composed, CUBE_FACELETS) == 0;
            if (!seen && count < CUBE_ROTATIONS)
                memcpy(rotation_perms[count++], composed, CUBE_FACELETS);
        }
}

const uint8_t (*cube_rotation_perms(void))[CUBE_FACELETS]
{
    pthread_once(&rotations_once, build_rotations);
    return rotation_perms;
}

cube_t cube_rotate(cube_t state, const uint8_t perm[static CUBE_FACELETS])
{
    cube_t rotated;
    for (int i = 0; i < CUBE_FACELETS; i++)
        rotated.facelet[i] = state.facelet[perm[i]];
    return rotated;
}

/* splitmix64: two lines, well-distributed, and identical on every platform. */
cube_rng_t cube_rng_seed(uint64_t seed)
{
    return (cube_rng_t){.state = seed + 0x9e3779b97f4a7c15u};
}

uint64_t cube_rng_next(cube_rng_t *rng)
{
    uint64_t z = (rng->state += 0x9e3779b97f4a7c15u);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9u;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebu;
    return z ^ (z >> 31);
}

cube_t cube_scramble(cube_t state, int count, cube_rng_t *rng)
{
    /* Rejecting states already seen keeps a short scramble from wandering back
     * onto its own path, which would make `-n 8` mean fewer than 8 moves. */
    enum { SEEN_MAX = 256 };
    cube_t seen[SEEN_MAX];
    size_t seen_count = 0;
    seen[seen_count++] = state;

    while (count > 0) {
        const cube_t candidate =
            cube_apply_move(state, (int)(cube_rng_next(rng) % CUBE_MOVES));
        bool known = false;
        for (size_t i = 0; i < seen_count && !known; i++)
            known = memcmp(&seen[i], &candidate, sizeof candidate) == 0;
        if (known)
            continue;
        if (seen_count < SEEN_MAX)
            seen[seen_count++] = candidate;
        state = candidate;
        count--;
    }
    return state;
}
