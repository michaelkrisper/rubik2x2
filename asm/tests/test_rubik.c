/* test_rubik.c -- everything SPEC section 4 requires of the assembly build.
 *
 * The runner is C; every routine it exercises is the hand-written assembly in
 * ../src.  The spec asks for the *algorithms* to be in assembly, not the
 * harness, and a C harness can say what it expected when a check fails, which
 * an assembly one would have to spell out by hand for each assertion.
 *
 * There are no headers in this project -- the assembly is the interface -- so
 * every symbol below is declared here against the calling convention written
 * at the top of each .s file.  That duplication is deliberate: if a routine's
 * signature ever drifts, this file is where the mismatch shows up.
 *
 * The OEIS A079762 histogram is the load-bearing check.  No wrong move table
 * and no wrong encoding can reproduce those fifteen numbers, so it validates
 * the sticker model, the coordinate split and the breadth-first build in one
 * go.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    FACELETS = 24,
    NMOVES = 12,
    NSOLVE = 6,
    NFREE = 7,
    NPERM = 5040,
    NORIENT = 729,
    NSTATES = 3674160,
    NROT = 24,
    MAXDEPTH = 14,
};

/* coords_encode status codes (rubik.inc). */
enum { CE_OK = 0, CE_CORNER = 1, CE_PIECE = 2, CE_REPEAT = 3, CE_TWIST = 4 };
/* solver status codes. */
enum { SV_OK = 0, SV_INVALID = 1, SV_NOSOL = 2, SV_NOMEM = 3 };

/* SOL_MOVES 0, SOL_LEN 32, SOL_PEAK 40, SOL_SIZE 48. */
typedef struct {
    char moves[32];
    uint32_t length;
    uint32_t reserved;
    uint64_t peak;
} solution_t;

_Static_assert(sizeof(solution_t) == 48, "solution_t must match SOL_SIZE");
_Static_assert(offsetof(solution_t, length) == 32, "SOL_LEN");
_Static_assert(offsetof(solution_t, peak) == 40, "SOL_PEAK");

/* --- cube.s ------------------------------------------------------------ */
extern const uint8_t cube_solved[FACELETS];
extern const char color_chars[], move_chars[], solve_move_chars[];
extern const uint8_t move_perm[NMOVES][FACELETS];
extern const uint8_t coord_slots[8][3];

void cube_apply_move(uint8_t *dst, const uint8_t *src, int move);
void cube_rotate(uint8_t *dst, const uint8_t *src, const uint8_t *perm);
int cube_move_index(int letter);
int cube_solve_move_index(int letter);
int cube_apply_moves(uint8_t *state, const char *moves);
void cube_invert(char *out, const char *moves);
int cube_is_solved(const uint8_t *state);
int cube_parse(const char *text, uint8_t *out);
const char *cube_parse_message(int code);
void cube_to_string(char *out, const uint8_t *state);
const uint8_t (*cube_rotations(void))[FACELETS];
void cube_rng_seed(uint64_t *rng, uint64_t seed);
uint64_t cube_rng_next(uint64_t *rng);
void cube_scramble(uint8_t *state, int count, uint64_t *rng);

/* --- coords.s ---------------------------------------------------------- */
extern const uint8_t canonical_pieces[24];
extern const uint32_t perm_wide[NPERM * 8];     /* [q][m] = new_q * 729 */
extern const uint32_t orient_child[NORIENT * 8]; /* [r][m] = new_r */
extern const uint16_t perm_child[NPERM * 8];
extern const uint8_t perm_all[NPERM * 8];
extern const uint8_t orient_all[NORIENT * 8];

int rank_perm(const uint8_t *perm7);
int rank_orient(const uint8_t *twist7);
int coords_encode(const uint8_t *state, uint32_t *index);
void coords_decode(uint32_t index, uint8_t *out);
const char *coords_status_message(int status);
void coords_init(void);

/* --- bfs.s ------------------------------------------------------------- */
void *bfs_prepare(void);
int bfs_solve_index(uint32_t index, solution_t *solution);
int bfs_solve(const uint8_t *state, solution_t *solution);

/* --- ida.s ------------------------------------------------------------- */
extern const uint8_t ida_perm_dist[NPERM];
extern const uint8_t ida_orient_dist[NORIENT];
void ida_prepare(void);
int ida_solve_index(uint32_t index, solution_t *solution);
int ida_solve(const uint8_t *state, solution_t *solution);

/* --- table.s ----------------------------------------------------------- */
extern int table_last_workers;
int table_default_workers(void);
void table_build(uint8_t *distance, uint32_t *queue);
void table_build_parallel(uint8_t *distance, uint32_t *queue, int workers);
uint8_t *table_create(int workers);
void table_free(void *distance);
int table_histogram(const uint8_t *distance, uint32_t *counts);
int table_distance(const uint8_t *distance, uint32_t index);
int table_solve_index(uint32_t index, solution_t *solution, const uint8_t *distance);
int table_solve(const uint8_t *state, solution_t *solution, const uint8_t *distance);

/* ----------------------------------------------------------------------- */

static int checks = 0;
static int failures = 0;
static const char *current = "";

#define CHECK(condition, ...)                                                  \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            failures++;                                                        \
            printf("  FAIL %s:%d in %s: ", __FILE__, __LINE__, current);       \
            printf(__VA_ARGS__);                                               \
            putchar('\n');                                                     \
        }                                                                      \
    } while (0)

typedef uint8_t cube_t[FACELETS];

static bool same(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, FACELETS) == 0;
}

static void turn(uint8_t *state, int move)
{
    cube_t next;
    cube_apply_move(next, state, move);
    memcpy(state, next, FACELETS);
}

/* The antipodal position: the one distance-14 class the benchmark leans on. */
static const char *const hardest_text = "wppryygrygrwgprbbbwpbwyg";

/* --- the sticker model ------------------------------------------------- */

static void test_move_algebra(void)
{
    for (int move = 0; move < NMOVES; move++) {
        cube_t state;
        memcpy(state, cube_solved, FACELETS);
        for (int i = 0; i < 4; i++)
            turn(state, move);
        CHECK(same(state, cube_solved), "move '%c' does not have order 4", move_chars[move]);

        /* Order exactly 4, not 1 or 2: one turn must change something, and two
         * must not already be back. */
        memcpy(state, cube_solved, FACELETS);
        turn(state, move);
        CHECK(!same(state, cube_solved), "move '%c' is the identity", move_chars[move]);
        turn(state, move);
        CHECK(!same(state, cube_solved), "move '%c' squares to the identity", move_chars[move]);
    }

    /* Lowercase and uppercase are inverses, and they sit at adjacent indices.
     * Checked on 24 distinct markers rather than on colours, so cancelling is
     * facelet by facelet and not merely up to colour. */
    for (int move = 0; move < NMOVES; move += 2) {
        cube_t marked;
        for (int i = 0; i < FACELETS; i++)
            marked[i] = (uint8_t)i;

        cube_t there;
        memcpy(there, marked, FACELETS);
        turn(there, move);
        turn(there, move + 1);
        CHECK(same(there, marked), "'%c' and '%c' do not cancel", move_chars[move],
              move_chars[move + 1]);

        memcpy(there, marked, FACELETS);
        turn(there, move + 1);
        turn(there, move);
        CHECK(same(there, marked), "'%c' and '%c' do not cancel", move_chars[move + 1],
              move_chars[move]);

        /* Three turns equal the inverse. */
        cube_t thrice, once;
        memcpy(thrice, marked, FACELETS);
        for (int i = 0; i < 3; i++)
            turn(thrice, move);
        memcpy(once, marked, FACELETS);
        turn(once, move + 1);
        CHECK(same(thrice, once), "'%c' three times is not '%c'", move_chars[move],
              move_chars[move + 1]);
    }

    /* Opposite faces commute: l/r, t/b, f/a. */
    static const int opposite[3][2] = {{0, 2}, {4, 6}, {8, 10}};
    for (int pair = 0; pair < 3; pair++)
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++) {
                const int first = opposite[pair][0] + a, second = opposite[pair][1] + b;
                cube_t forwards, backwards;
                memcpy(forwards, cube_solved, FACELETS);
                turn(forwards, first);
                turn(forwards, second);
                memcpy(backwards, cube_solved, FACELETS);
                turn(backwards, second);
                turn(backwards, first);
                CHECK(same(forwards, backwards), "'%c' and '%c' do not commute",
                      move_chars[first], move_chars[second]);
            }

    /* The move alphabet and the solve alphabet index each other consistently. */
    for (int move = 0; move < NMOVES; move++)
        CHECK(cube_move_index(move_chars[move]) == move, "move_index('%c') is wrong",
              move_chars[move]);
    for (int i = 0; i < NSOLVE; i++) {
        CHECK(cube_solve_move_index(solve_move_chars[i]) == i, "solve_move_index('%c') is wrong",
              solve_move_chars[i]);
        CHECK(cube_move_index(solve_move_chars[i]) >= 0, "'%c' is not a move",
              solve_move_chars[i]);
    }
    CHECK(cube_move_index('z') == -1, "an unknown letter was accepted as a move");
    CHECK(cube_solve_move_index('r') == -1, "'r' was accepted into the solve alphabet");
}

static void test_fixed_corner(void)
{
    /* Exactly facelets 11, 13 and 23 survive every solve move untouched -- the
     * fact the whole encoding is built on, since coords_encode reads precisely
     * those three to decide which solved orientation the cube is aiming at. */
    for (int facelet = 0; facelet < FACELETS; facelet++) {
        bool fixed = true;
        for (int i = 0; i < NSOLVE; i++) {
            const int move = cube_move_index(solve_move_chars[i]);
            fixed = fixed && move_perm[move][facelet] == facelet;
        }
        const bool expected = facelet == 11 || facelet == 13 || facelet == 23;
        CHECK(fixed == expected, "facelet %d: fixed=%d expected=%d", facelet, (int)fixed,
              (int)expected);
    }

    /* Those three are slot 7, the corner the coordinates hold still. */
    CHECK(coord_slots[7][0] == 23 && coord_slots[7][1] == 11 && coord_slots[7][2] == 13,
          "slot 7 is {%d,%d,%d}, expected {23,11,13}", coord_slots[7][0], coord_slots[7][1],
          coord_slots[7][2]);

    /* Every facelet belongs to exactly one slot. */
    int seen[FACELETS] = {0};
    for (int slot = 0; slot < 8; slot++)
        for (int k = 0; k < 3; k++)
            seen[coord_slots[slot][k]]++;
    for (int facelet = 0; facelet < FACELETS; facelet++)
        CHECK(seen[facelet] == 1, "facelet %d appears in %d slots", facelet, seen[facelet]);
}

static void test_parse_and_render(void)
{
    cube_t state;
    char text[FACELETS + 1];
    CHECK(cube_parse("yyyyrrrrwwwwppppbbbbgggg", state) == 0, "solved state rejected");
    CHECK(same(state, cube_solved), "solved state parsed wrongly");
    cube_to_string(text, cube_solved);
    CHECK(strcmp(text, "yyyyrrrrwwwwppppbbbbgggg") == 0, "round trip gave %s", text);

    CHECK(cube_parse("yyyy", state) == 1, "short string not reported as a length error");
    CHECK(cube_parse("zyyyrrrrwwwwppppbbbbgggg", state) == 2, "bad letter not reported");
    CHECK(cube_parse("yyyyyrrrwwwwppppbbbbgggg", state) == 3, "bad colour counts not reported");
    CHECK(cube_parse("  yyyyrrrrwwwwppppbbbbgggg\n", state) == 0, "whitespace rejected");

    for (int code = 1; code <= 3; code++)
        CHECK(cube_parse_message(code) && cube_parse_message(code)[0],
              "parse code %d has no message", code);

    /* Every colour letter is accepted and renders back to itself. */
    for (int c = 0; c < 6; c++)
        CHECK(strchr("rwbypg", color_chars[c]) != NULL, "colour %d is '%c'", c, color_chars[c]);

    /* A scramble round-trips through its text form. */
    uint64_t rng;
    cube_rng_seed(&rng, 5);
    for (int trial = 0; trial < 100; trial++) {
        cube_t scrambled, again;
        memcpy(scrambled, cube_solved, FACELETS);
        cube_scramble(scrambled, 14, &rng);
        cube_to_string(text, scrambled);
        CHECK(cube_parse(text, again) == 0, "a scramble did not parse back");
        CHECK(same(again, scrambled), "%s did not round-trip", text);
    }
}

static void test_invert(void)
{
    char inverted[32];
    cube_invert(inverted, "lFtL");
    CHECK(strcmp(inverted, "lTfL") == 0, "invert(\"lFtL\") = %s", inverted);
    cube_invert(inverted, "");
    CHECK(inverted[0] == '\0', "invert(\"\") is not empty");

    uint64_t rng;
    cube_rng_seed(&rng, 7);
    for (int trial = 0; trial < 50; trial++) {
        char moves[9];
        for (int i = 0; i < 8; i++)
            moves[i] = move_chars[cube_rng_next(&rng) % NMOVES];
        moves[8] = '\0';
        cube_invert(inverted, moves);
        cube_t state;
        memcpy(state, cube_solved, FACELETS);
        CHECK(cube_apply_moves(state, moves), "bad move string %s", moves);
        CHECK(cube_apply_moves(state, inverted), "bad move string %s", inverted);
        CHECK(same(state, cube_solved), "%s then %s did not cancel", moves, inverted);
    }

    cube_t state;
    memcpy(state, cube_solved, FACELETS);
    CHECK(!cube_apply_moves(state, "lZ"), "an unknown move letter was applied");
}

static void test_rotations(void)
{
    const uint8_t (*const rotations)[FACELETS] = cube_rotations();
    int distinct = 0;
    for (int i = 0; i < NROT; i++) {
        bool duplicate = false;
        for (int j = 0; j < i; j++)
            duplicate = duplicate || memcmp(rotations[i], rotations[j], FACELETS) == 0;
        distinct += !duplicate;
        cube_t rotated;
        cube_rotate(rotated, cube_solved, rotations[i]);
        CHECK(cube_is_solved(rotated), "rotation %d does not preserve solvedness", i);

        /* A rotation is a permutation of the 24 facelets. */
        int seen[FACELETS] = {0};
        for (int k = 0; k < FACELETS; k++)
            seen[rotations[i][k]]++;
        bool bijection = true;
        for (int k = 0; k < FACELETS; k++)
            bijection = bijection && seen[k] == 1;
        CHECK(bijection, "rotation %d is not a permutation", i);
    }
    CHECK(distinct == NROT, "%d distinct rotations, expected 24", distinct);

    /* A scrambled cube is solved in exactly one of its 24 presentations. */
    cube_t scrambled;
    memcpy(scrambled, cube_solved, FACELETS);
    CHECK(cube_apply_moves(scrambled, "ltf"), "bad move string");
    CHECK(!cube_is_solved(scrambled), "a scrambled cube reads as solved");
}

/* --- the encoding ------------------------------------------------------ */

static void random_state(uint8_t *state, uint64_t *rng)
{
    memcpy(state, cube_solved, FACELETS);
    cube_scramble(state, 14, rng);
}

/* A scramble built from the six solve moves only, so the fixed corner keeps
 * its piece and the cube stays in the standard orientation. */
static void random_upright_state(uint8_t *state, uint64_t *rng)
{
    memcpy(state, cube_solved, FACELETS);
    for (int i = 0; i < 14; i++)
        turn(state, cube_move_index(solve_move_chars[cube_rng_next(rng) % NSOLVE]));
}

static void test_encoding(void)
{
    uint32_t index;
    CHECK(coords_encode(cube_solved, &index) == CE_OK, "solved cube rejected");
    CHECK(index == 0, "solved cube encodes to %u", index);

    /* The two ranks agree with their definitions at the identity. */
    static const uint8_t identity[NFREE] = {0, 1, 2, 3, 4, 5, 6};
    static const uint8_t untwisted[NFREE] = {0, 0, 0, 0, 0, 0, 0};
    CHECK(rank_perm(identity) == 0, "the identity permutation does not rank 0");
    CHECK(rank_orient(untwisted) == 0, "the untwisted orientation does not rank 0");
    static const uint8_t reversed[NFREE] = {6, 5, 4, 3, 2, 1, 0};
    CHECK(rank_perm(reversed) == NPERM - 1, "the reversed permutation ranks %d, expected %d",
          rank_perm(reversed), NPERM - 1);
    static const uint8_t all_twos[NFREE] = {2, 2, 2, 2, 2, 2, 2};
    CHECK(rank_orient(all_twos) == NORIENT - 1, "the fully twisted orientation ranks %d",
          rank_orient(all_twos));

    /* index -> state -> index for a spread of indices, including the ends. */
    uint64_t rng;
    cube_rng_seed(&rng, 11);
    for (int trial = 0; trial < 2000; trial++) {
        const uint32_t original = trial < 2 ? (trial ? NSTATES - 1 : 0)
                                            : (uint32_t)(cube_rng_next(&rng) % NSTATES);
        cube_t state;
        coords_decode(original, state);
        uint32_t again;
        CHECK(coords_encode(state, &again) == CE_OK, "decoded state %u rejected", original);
        CHECK(again == original, "index %u round-tripped to %u", original, again);

        /* A decoded state is a real cube: four stickers of every colour. */
        int counts[6] = {0};
        for (int i = 0; i < FACELETS; i++)
            counts[state[i]]++;
        bool balanced = true;
        for (int c = 0; c < 6; c++)
            balanced = balanced && counts[c] == 4;
        CHECK(balanced, "index %u decoded to unbalanced colours", original);
    }

    /* state -> index -> state.  Decoding always produces a cube in the standard
     * orientation, so this holds for cubes held that way; the rotation loop
     * below covers the other 23. */
    for (int trial = 0; trial < 500; trial++) {
        cube_t state, back;
        random_upright_state(state, &rng);
        CHECK(coords_encode(state, &index) == CE_OK, "upright scramble rejected");
        coords_decode(index, back);
        CHECK(same(back, state), "an upright state did not round-trip");
    }

    /* Encoding is defined for scrambles in any orientation. */
    for (int trial = 0; trial < 500; trial++) {
        cube_t state;
        random_state(state, &rng);
        CHECK(coords_encode(state, &index) == CE_OK,
              "a scramble in an arbitrary orientation was rejected");
    }

    /* Every solved cube encodes to 0 whatever way up it is held -- that is what
     * the per-orientation goal lookup buys, and why the solvers can always aim
     * at index 0. */
    const uint8_t (*const rotations)[FACELETS] = cube_rotations();
    for (int r = 0; r < NROT; r++) {
        cube_t rotated;
        cube_rotate(rotated, cube_solved, rotations[r]);
        CHECK(coords_encode(rotated, &index) == CE_OK, "rotated solved cube rejected");
        CHECK(index == 0, "rotation %d of solved encodes to %u", r, index);
    }

    /* --- the four rejections ------------------------------------------
     *
     * These matter more than they look.  The index space is smaller than the
     * space of sticker arrangements -- six twists stored and the seventh
     * derived, the permutation ranked as a bijection -- so an illegal cube
     * that slipped through would normalise into some *other* legal state and
     * be cheerfully "solved" while still scrambled.  Each check asserts its
     * own status code, not merely that something failed. */

    /* 1. The fixed corner's colours occur on no solved cube. */
    cube_t stranger;
    memcpy(stranger, cube_solved, FACELETS);
    stranger[coord_slots[7][0]] = stranger[coord_slots[7][1]];
    CHECK(coords_encode(stranger, &index) == CE_CORNER,
          "an impossible fixed corner was accepted (status %d)",
          coords_encode(stranger, &index));

    /* 2. A slot whose colours form no piece: front and back on one corner. */
    cube_t broken;
    memcpy(broken, cube_solved, FACELETS);
    broken[0] = cube_solved[4];
    broken[4] = cube_solved[0];
    CHECK(coords_encode(broken, &index) == CE_PIECE,
          "an arrangement holding no valid piece was accepted (status %d)",
          coords_encode(broken, &index));

    /* 3. The same piece in two slots: the permutation is not a bijection.
     * Built by hand, because the colour counts are wrong and it would never
     * parse. */
    cube_t twice;
    memcpy(twice, cube_solved, FACELETS);
    for (int k = 0; k < 3; k++)
        twice[coord_slots[1][k]] = cube_solved[coord_slots[0][k]];
    CHECK(coords_encode(twice, &index) == CE_REPEAT,
          "the same piece in two slots was accepted (status %d)",
          coords_encode(twice, &index));

    /* 4. One corner turned in place: legal-looking pieces everywhere, but the
     * twists sum to 1 rather than 0 mod 3. */
    cube_t twisted;
    CHECK(cube_parse("pyyyrrrrwwwwppbpybbbgggg", twisted) == 0,
          "the twisted-corner state does not parse");
    CHECK(coords_encode(twisted, &index) == CE_TWIST,
          "a corner twisted in place was accepted (status %d)",
          coords_encode(twisted, &index));

    /* The near miss that keeps check 4 honest: a twist on its own is not
     * enough to be illegal.  Three corners turned the same way sum to 0 mod 3,
     * are perfectly reachable, and must be ACCEPTED. */
    cube_t turnable;
    memcpy(turnable, cube_solved, FACELETS);
    for (int slot = 0; slot < 3; slot++) {
        const uint8_t *const facelets = coord_slots[slot];
        const uint8_t first = turnable[facelets[0]];
        turnable[facelets[0]] = turnable[facelets[1]];
        turnable[facelets[1]] = turnable[facelets[2]];
        turnable[facelets[2]] = first;
    }
    CHECK(coords_encode(turnable, &index) == CE_OK,
          "three corners twisted alike were rejected (status %d)",
          coords_encode(turnable, &index));
    CHECK(index != 0, "three corners twisted alike encoded to the solved index");
    /* It is not merely accepted -- it is genuinely reachable. */
    solution_t reachable;
    CHECK(bfs_solve(turnable, &reachable) == SV_OK,
          "three corners twisted alike could not be solved");
    cube_t settled;
    memcpy(settled, turnable, FACELETS);
    CHECK(cube_apply_moves(settled, reachable.moves), "bad move string");
    CHECK(cube_is_solved(settled), "the three-twist solution '%s' does not solve it",
          reachable.moves);

    /* Each status has its own message. */
    for (int a = CE_OK; a <= CE_TWIST; a++) {
        CHECK(coords_status_message(a) && coords_status_message(a)[0], "status %d has no message",
              a);
        for (int b = a + 1; b <= CE_TWIST; b++)
            CHECK(strcmp(coords_status_message(a), coords_status_message(b)) != 0,
                  "statuses %d and %d share a message", a, b);
    }
}

static void test_move_tables(void)
{
    /* The compact tables must agree with the sticker model they came from:
     *   child = perm_wide[q][m] + orient_child[r][m]. */
    uint64_t rng;
    cube_rng_seed(&rng, 23);
    for (int trial = 0; trial < 300; trial++) {
        cube_t state;
        random_state(state, &rng);
        uint32_t index;
        CHECK(coords_encode(state, &index) == CE_OK, "scramble rejected");
        const uint32_t q = index / NORIENT, r = index % NORIENT;
        for (int move = 0; move < NSOLVE; move++) {
            cube_t turned;
            memcpy(turned, state, FACELETS);
            turn(turned, cube_move_index(solve_move_chars[move]));
            uint32_t expected;
            CHECK(coords_encode(turned, &expected) == CE_OK, "turned state rejected");
            const uint32_t child = perm_wide[q * 8 + move] + orient_child[r * 8 + move];
            CHECK(child == expected, "move '%c' disagrees with the sticker model: %u vs %u",
                  solve_move_chars[move], child, expected);
            /* The narrow tables the solvers also read must say the same thing. */
            CHECK(perm_child[q * 8 + move] * NORIENT == perm_wide[q * 8 + move],
                  "perm_child and perm_wide disagree at q=%u m=%d", q, move);
        }
    }

    /* Each move is a bijection on both coordinates, which is what makes the
     * breadth-first build reach every state exactly once. */
    for (int move = 0; move < NSOLVE; move++) {
        static uint8_t hit[NPERM];
        memset(hit, 0, sizeof hit);
        for (uint32_t q = 0; q < NPERM; q++)
            hit[perm_child[q * 8 + move]] = 1;
        int reached = 0;
        for (int q = 0; q < NPERM; q++)
            reached += hit[q];
        CHECK(reached == NPERM, "move '%c' is not onto the permutation coordinate (%d)",
              solve_move_chars[move], reached);

        static uint8_t hit_r[NORIENT];
        memset(hit_r, 0, sizeof hit_r);
        for (uint32_t r = 0; r < NORIENT; r++)
            hit_r[orient_child[r * 8 + move]] = 1;
        reached = 0;
        for (int r = 0; r < NORIENT; r++)
            reached += hit_r[r];
        CHECK(reached == NORIENT, "move '%c' is not onto the orientation coordinate (%d)",
              solve_move_chars[move], reached);
    }
}

/* --- the distance table ------------------------------------------------ */

static const uint32_t oeis_a079762[] = {
    1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508,
    930588, 1350852, 782536, 90280, 276,
};

static void test_histogram(const uint8_t *table)
{
    uint32_t counts[16];
    const int highest = table_histogram(table, counts);
    const int expected = (int)(sizeof oeis_a079762 / sizeof *oeis_a079762);
    CHECK(highest == expected, "maximum distance %d, expected %d", highest - 1, expected - 1);

    uint32_t total = 0;
    for (int depth = 0; depth < highest && depth < expected; depth++) {
        CHECK(counts[depth] == oeis_a079762[depth], "depth %d: %u states, expected %u", depth,
              counts[depth], oeis_a079762[depth]);
        total += counts[depth];
    }
    CHECK(total == NSTATES, "%u states counted, expected %d", total, NSTATES);

    /* Nothing is unreachable, and nothing sits past God's number. */
    CHECK(table_distance(table, 0) == 0, "the solved state is not at distance 0");
    for (uint32_t index = 0; index < NSTATES; index += 4093) {
        const int d = table_distance(table, index);
        if (d < 0 || d > MAXDEPTH) {
            CHECK(false, "state %u is at distance %d", index, d);
            break;
        }
    }
    checks++; /* the sweep above counts as one check when it passes */
}

static void test_parallel_table(const uint8_t *serial)
{
    for (int workers = 2; workers <= 8; workers *= 2) {
        uint8_t *const parallel = table_create(workers);
        CHECK(parallel != NULL, "parallel build with %d workers failed", workers);
        if (!parallel)
            continue;
        CHECK(table_last_workers == workers, "asked for %d workers, got %d", workers,
              table_last_workers);
        CHECK(memcmp(serial, parallel, NSTATES) == 0,
              "the %d-worker build differs from the serial one", workers);
        table_free(parallel);
    }
    CHECK(table_default_workers() >= 1, "table_default_workers() returned %d",
          table_default_workers());
}

/* --- the solvers ------------------------------------------------------- */

/* A cube may be handed over in any of the 24 orientations.  Each encodes to a
 * *different* index -- the fixed slot then holds a different piece -- but all
 * 24 describe the same physical position, so all 24 must be the same number of
 * quarter turns from solved and every solution must actually solve them. */
static void test_orientation_independence(const uint8_t *table)
{
    const uint8_t (*const rotations)[FACELETS] = cube_rotations();
    uint64_t rng;
    cube_rng_seed(&rng, 41);
    for (int trial = 0; trial < 20; trial++) {
        cube_t state;
        random_state(state, &rng);
        uint32_t index;
        CHECK(coords_encode(state, &index) == CE_OK, "scramble rejected");
        const int expected = table_distance(table, index);

        for (int r = 0; r < NROT; r++) {
            cube_t held;
            cube_rotate(held, state, rotations[r]);
            solution_t solution;
            CHECK(table_solve(held, &solution, table) == SV_OK, "rotation %d rejected", r);
            CHECK((int)solution.length == expected, "rotation %d needs %u moves, expected %d", r,
                  solution.length, expected);
            cube_t solved;
            memcpy(solved, held, FACELETS);
            CHECK(cube_apply_moves(solved, solution.moves), "bad move string");
            CHECK(cube_is_solved(solved), "rotation %d was not solved", r);
        }
    }
}

static void test_solvers(const uint8_t *table)
{
    uint64_t rng;
    cube_rng_seed(&rng, 31);
    cube_t states[26];
    int count = 0;
    CHECK(cube_parse(hardest_text, states[count++]) == 0,
          "the antipodal position does not parse");
    while (count < (int)(sizeof states / sizeof *states))
        random_state(states[count++], &rng);

    for (int i = 0; i < count; i++) {
        uint32_t index;
        CHECK(coords_encode(states[i], &index) == CE_OK, "scramble rejected");
        const int distance = table_distance(table, index);

        solution_t by_bfs, by_ida, by_table;
        CHECK(bfs_solve(states[i], &by_bfs) == SV_OK, "bfs failed");
        CHECK(ida_solve(states[i], &by_ida) == SV_OK, "ida failed");
        CHECK(table_solve(states[i], &by_table, table) == SV_OK, "table failed");

        CHECK((int)by_bfs.length == distance, "bfs returned %u moves, optimum is %d",
              by_bfs.length, distance);
        CHECK((int)by_ida.length == distance, "ida returned %u moves, optimum is %d",
              by_ida.length, distance);
        CHECK((int)by_table.length == distance, "table returned %u moves, optimum is %d",
              by_table.length, distance);

        const solution_t *const all[] = {&by_bfs, &by_ida, &by_table};
        static const char *const names[] = {"bfs", "ida", "table"};
        for (int which = 0; which < 3; which++) {
            CHECK(strlen(all[which]->moves) == all[which]->length,
                  "%s: length %u does not match '%s'", names[which], all[which]->length,
                  all[which]->moves);
            /* Solutions use only the six solve moves. */
            bool alphabet = true;
            for (const char *p = all[which]->moves; *p; p++)
                alphabet = alphabet && cube_solve_move_index(*p) >= 0;
            CHECK(alphabet, "%s used a move outside the solve alphabet: '%s'", names[which],
                  all[which]->moves);
            cube_t solved;
            memcpy(solved, states[i], FACELETS);
            CHECK(cube_apply_moves(solved, all[which]->moves),
                  "%s returned an unparsable move string", names[which]);
            CHECK(cube_is_solved(solved), "%s solution '%s' does not solve the cube",
                  names[which], all[which]->moves);
        }
    }

    /* The antipodal position is the point of the exercise: exactly 14 moves,
     * and all three agree on that. */
    solution_t antipodal;
    CHECK(bfs_solve(states[0], &antipodal) == SV_OK, "bfs failed on the antipode");
    CHECK(antipodal.length == 14, "bfs says the antipodal position needs %u moves, expected 14",
          antipodal.length);
    CHECK(ida_solve(states[0], &antipodal) == SV_OK, "ida failed on the antipode");
    CHECK(antipodal.length == 14, "ida says the antipodal position needs %u moves, expected 14",
          antipodal.length);
    CHECK(table_solve(states[0], &antipodal, table) == SV_OK, "table failed on the antipode");
    CHECK(antipodal.length == 14, "table says the antipodal position needs %u moves, expected 14",
          antipodal.length);

    /* An already-solved cube needs no moves and is not an error. */
    solution_t nothing;
    CHECK(bfs_solve(cube_solved, &nothing) == SV_OK, "bfs rejected a solved cube");
    CHECK(nothing.length == 0, "bfs returned %u moves for a solved cube", nothing.length);
    CHECK(table_solve(cube_solved, &nothing, table) == SV_OK, "table rejected a solved cube");
    CHECK(nothing.length == 0, "table returned %u moves for a solved cube", nothing.length);
    CHECK(ida_solve(cube_solved, &nothing) == SV_OK, "ida rejected a solved cube");
    CHECK(nothing.length == 0, "ida returned %u moves for a solved cube", nothing.length);

    /* Every solver must refuse an unsolvable cube rather than return moves that
     * leave it scrambled. */
    cube_t twisted;
    CHECK(cube_parse("pyyyrrrrwwwwppbpybbbgggg", twisted) == 0,
          "the twisted-corner state does not parse");
    CHECK(bfs_solve(twisted, &nothing) == SV_INVALID, "bfs accepted a twisted corner");
    CHECK(ida_solve(twisted, &nothing) == SV_INVALID, "ida accepted a twisted corner");
    CHECK(table_solve(twisted, &nothing, table) == SV_INVALID,
          "table accepted a twisted corner");

    /* By index, too: the three agree over a spread of the whole space, which
     * covers depths the random scrambles above rarely reach. */
    for (uint32_t index = 0; index < NSTATES; index += 262144) {
        solution_t a, b, c;
        CHECK(bfs_solve_index(index, &a) == SV_OK, "bfs failed on index %u", index);
        CHECK(ida_solve_index(index, &b) == SV_OK, "ida failed on index %u", index);
        CHECK(table_solve_index(index, &c, table) == SV_OK, "table failed on index %u", index);
        const int distance = table_distance(table, index);
        CHECK((int)a.length == distance && a.length == b.length && b.length == c.length,
              "index %u: bfs %u, ida %u, table %u, optimum %d", index, a.length, b.length,
              c.length, distance);
        cube_t state, solved;
        coords_decode(index, state);
        const solution_t *const all[] = {&a, &b, &c};
        static const char *const names[] = {"bfs", "ida", "table"};
        for (int which = 0; which < 3; which++) {
            memcpy(solved, state, FACELETS);
            CHECK(cube_apply_moves(solved, all[which]->moves), "bad move string");
            CHECK(cube_is_solved(solved), "%s solution '%s' does not solve index %u",
                  names[which], all[which]->moves, index);
        }
    }
}

static void test_pattern_databases(void)
{
    /* Both databases store distance + 1, so a zero byte would mean "never
     * reached" -- and every coordinate value is reachable. */
    int perm_reach = 0, orient_reach = 0;
    for (int q = 0; q < NPERM; q++) {
        if (ida_perm_dist[q] == 0) {
            CHECK(false, "permutation coordinate %d was never reached", q);
            break;
        }
        if (ida_perm_dist[q] > perm_reach)
            perm_reach = ida_perm_dist[q];
    }
    for (int r = 0; r < NORIENT; r++) {
        if (ida_orient_dist[r] == 0) {
            CHECK(false, "orientation coordinate %d was never reached", r);
            break;
        }
        if (ida_orient_dist[r] > orient_reach)
            orient_reach = ida_orient_dist[r];
    }
    checks += 2;
    perm_reach--;
    orient_reach--;
    CHECK(ida_perm_dist[0] == 1 && ida_orient_dist[0] == 1,
          "the solved coordinate is not at distance 0");
    /* Documented as the reason IDA* loses here: both bounds stop well short of
     * the real maximum of 14, so the search still has to walk. */
    CHECK(perm_reach > 0 && perm_reach < MAXDEPTH, "permutation database reaches %d",
          perm_reach);
    CHECK(orient_reach > 0 && orient_reach < MAXDEPTH, "orientation database reaches %d",
          orient_reach);
}

/* ----------------------------------------------------------------------- */

#define RUN(name, ...)                                                         \
    do {                                                                       \
        current = name;                                                        \
        const int before = failures;                                           \
        __VA_ARGS__;                                                           \
        printf("%-26s %s\n", name, failures == before ? "ok" : "FAILED");      \
        fflush(stdout);                                                        \
    } while (0)

int main(void)
{
    coords_init();
    if (!bfs_prepare()) {
        fputs("out of memory\n", stderr);
        return 1;
    }
    ida_prepare();

    RUN("move algebra", test_move_algebra());
    RUN("fixed corner", test_fixed_corner());
    RUN("parse and render", test_parse_and_render());
    RUN("invert", test_invert());
    RUN("rotations", test_rotations());
    RUN("encoding", test_encoding());
    RUN("move tables", test_move_tables());

    uint8_t *const table = table_create(1);
    if (!table) {
        fputs("out of memory\n", stderr);
        return 1;
    }

    RUN("OEIS A079762 histogram", test_histogram(table));
    RUN("parallel table build", test_parallel_table(table));
    RUN("pattern databases", test_pattern_databases());
    RUN("orientation independence", test_orientation_independence(table));
    RUN("solvers agree", test_solvers(table));

    table_free(table);

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
