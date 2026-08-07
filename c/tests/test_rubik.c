/* Everything spec section 4 requires, plus the checks that make the rest
 * trustworthy. Run with `make test`.
 *
 * The OEIS A079762 histogram is the load-bearing one: no incorrect move table
 * and no incorrect encoding can reproduce it, so it validates the whole stack
 * at once.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench.h"
#include "bfs.h"
#include "coords.h"
#include "cube.h"
#include "ida.h"
#include "table.h"

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

static bool same(cube_t a, cube_t b)
{
    return memcmp(a.facelet, b.facelet, CUBE_FACELETS) == 0;
}

/* --- the sticker model ------------------------------------------------- */

static void test_move_algebra(void)
{
    for (int move = 0; move < CUBE_MOVES; move++) {
        cube_t state = cube_solved;
        for (int i = 0; i < 4; i++)
            state = cube_apply_move(state, move);
        CHECK(same(state, cube_solved), "move '%c' does not have order 4", cube_move_chars[move]);

        /* Order exactly 4, not 1 or 2: a single turn must change something. */
        CHECK(!same(cube_apply_move(cube_solved, move), cube_solved),
              "move '%c' is the identity", cube_move_chars[move]);
    }

    /* Lowercase and uppercase are inverses, and they sit at adjacent indices. */
    for (int move = 0; move < CUBE_MOVES; move += 2) {
        /* 24 distinct markers, so cancelling is checked facelet by facelet
         * rather than only up to colour. */
        cube_t scrambled;
        for (int i = 0; i < CUBE_FACELETS; i++)
            scrambled.facelet[i] = (uint8_t)i;

        cube_t there = cube_apply_move(scrambled, move);
        CHECK(same(cube_apply_move(there, move + 1), scrambled),
              "'%c' and '%c' do not cancel", cube_move_chars[move], cube_move_chars[move + 1]);
        there = cube_apply_move(scrambled, move + 1);
        CHECK(same(cube_apply_move(there, move), scrambled),
              "'%c' and '%c' do not cancel", cube_move_chars[move + 1], cube_move_chars[move]);
    }

    /* Opposite faces commute: l/r, t/b, f/a. */
    static const int opposite[3][2] = {{0, 2}, {4, 6}, {8, 10}};
    for (int pair = 0; pair < 3; pair++)
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++) {
                const int first = opposite[pair][0] + a, second = opposite[pair][1] + b;
                const cube_t forwards =
                    cube_apply_move(cube_apply_move(cube_solved, first), second);
                const cube_t backwards =
                    cube_apply_move(cube_apply_move(cube_solved, second), first);
                CHECK(same(forwards, backwards), "'%c' and '%c' do not commute",
                      cube_move_chars[first], cube_move_chars[second]);
            }
}

static void test_fixed_corner(void)
{
    /* Exactly facelets 11, 13 and 23 survive every solve move untouched --
     * the fact the whole encoding is built on. */
    for (int facelet = 0; facelet < CUBE_FACELETS; facelet++) {
        bool fixed = true;
        for (int i = 0; i < CUBE_SOLVE_MOVES; i++) {
            const int move = cube_move_index(cube_solve_move_chars[i]);
            fixed = fixed && cube_move_perm[move][facelet] == facelet;
        }
        const bool expected = facelet == 11 || facelet == 13 || facelet == 23;
        CHECK(fixed == expected, "facelet %d: fixed=%d expected=%d", facelet, fixed, expected);
    }
}

static void test_parse_and_render(void)
{
    cube_t state;
    char text[CUBE_FACELETS + 1];
    CHECK(cube_parse("yyyyrrrrwwwwppppbbbbgggg", &state, nullptr, 0), "solved state rejected");
    CHECK(same(state, cube_solved), "solved state parsed wrongly");
    cube_to_string(cube_solved, text);
    CHECK(strcmp(text, "yyyyrrrrwwwwppppbbbbgggg") == 0, "round trip gave %s", text);

    CHECK(!cube_parse("yyyy", &state, nullptr, 0), "short string accepted");
    CHECK(!cube_parse("zyyyrrrrwwwwppppbbbbgggg", &state, nullptr, 0), "bad letter accepted");
    CHECK(!cube_parse("yyyyyrrrwwwwppppbbbbgggg", &state, nullptr, 0), "bad counts accepted");
    CHECK(cube_parse("  yyyyrrrrwwwwppppbbbbgggg\n", &state, nullptr, 0), "whitespace rejected");
}

static void test_invert(void)
{
    char inverted[16];
    cube_invert("lFtL", inverted);
    CHECK(strcmp(inverted, "lTfL") == 0, "invert(\"lFtL\") = %s", inverted);

    cube_rng_t rng = cube_rng_seed(7);
    for (int trial = 0; trial < 50; trial++) {
        char moves[9];
        for (int i = 0; i < 8; i++)
            moves[i] = cube_move_chars[cube_rng_next(&rng) % CUBE_MOVES];
        moves[8] = '\0';
        cube_invert(moves, inverted);
        cube_t there, back;
        CHECK(cube_apply_moves(cube_solved, moves, &there), "bad move string");
        CHECK(cube_apply_moves(there, inverted, &back), "bad move string");
        CHECK(same(back, cube_solved), "%s then %s did not cancel", moves, inverted);
    }
}

static void test_rotations(void)
{
    const uint8_t (*const rotations)[CUBE_FACELETS] = cube_rotation_perms();
    int distinct = 0;
    for (int i = 0; i < CUBE_ROTATIONS; i++) {
        bool duplicate = false;
        for (int j = 0; j < i; j++)
            duplicate = duplicate || memcmp(rotations[i], rotations[j], CUBE_FACELETS) == 0;
        distinct += !duplicate;
        CHECK(cube_is_solved(cube_rotate(cube_solved, rotations[i])),
              "rotation %d does not preserve solvedness", i);
    }
    CHECK(distinct == CUBE_ROTATIONS, "%d distinct rotations, expected 24", distinct);
}

/* --- the encoding ------------------------------------------------------ */

static cube_t random_state(cube_rng_t *rng)
{
    return cube_scramble(cube_solved, 14, rng);
}

/* A scramble built from the six solve moves only, so the fixed corner keeps its
 * piece and the cube stays in the standard orientation. */
static cube_t random_upright_state(cube_rng_t *rng)
{
    cube_t state = cube_solved;
    for (int i = 0; i < 14; i++)
        state = cube_apply_move(
            state, cube_move_index(cube_solve_move_chars[cube_rng_next(rng) % COORD_MOVES]));
    return state;
}

static void test_encoding(const coord_tables_t *tables)
{
    uint32_t index;
    CHECK(coords_encode(tables, cube_solved, &index) == COORD_OK, "solved cube rejected");
    CHECK(index == COORD_SOLVED_INDEX, "solved cube encodes to %u", index);

    /* index -> state -> index for a spread of indices, including the ends. */
    cube_rng_t rng = cube_rng_seed(11);
    for (int trial = 0; trial < 2000; trial++) {
        const uint32_t original = trial < 2 ? (trial ? COORD_STATES - 1 : 0)
                                            : (uint32_t)(cube_rng_next(&rng) % COORD_STATES);
        const cube_t state = coords_decode(tables, original);
        uint32_t again;
        CHECK(coords_encode(tables, state, &again) == COORD_OK, "decoded state rejected");
        CHECK(again == original, "index %u round-tripped to %u", original, again);
    }

    /* state -> index -> state. Decoding always produces a cube in the standard
     * orientation, so this holds for cubes held that way; the orientation test
     * below covers the other 23. */
    for (int trial = 0; trial < 500; trial++) {
        const cube_t state = random_upright_state(&rng);
        CHECK(coords_encode(tables, state, &index) == COORD_OK, "scramble rejected");
        CHECK(same(coords_decode(tables, index), state), "state did not round-trip");
    }

    /* Encoding is defined for scrambles in any orientation, even though the
     * index differs between orientations. */
    for (int trial = 0; trial < 500; trial++)
        CHECK(coords_encode(tables, random_state(&rng), &index) == COORD_OK,
              "scramble in an arbitrary orientation rejected");

    /* Every solved cube encodes to 0 whatever orientation it is held in --
     * that is what the goal lookup buys, and it is why the solvers can always
     * aim at index 0. */
    const uint8_t (*const rotations)[CUBE_FACELETS] = cube_rotation_perms();
    for (int r = 0; r < CUBE_ROTATIONS; r++) {
        CHECK(coords_encode(tables, cube_rotate(cube_solved, rotations[r]), &index) == COORD_OK,
              "rotated solved cube rejected");
        CHECK(index == COORD_SOLVED_INDEX, "rotation %d of solved encodes to %u", r, index);
    }

    /* Unreachable arrangements are rejected rather than silently mis-encoded. */
    cube_t broken = cube_solved;
    broken.facelet[0] = broken.facelet[4];      /* two colours swapped at one corner */
    broken.facelet[4] = cube_solved.facelet[0];
    CHECK(coords_encode(tables, broken, &index) != COORD_OK, "twisted corner accepted");
}

static void test_move_tables(const coord_tables_t *tables)
{
    /* The compact move tables must agree with the sticker model they came from. */
    cube_rng_t rng = cube_rng_seed(23);
    for (int trial = 0; trial < 300; trial++) {
        const cube_t state = random_state(&rng);
        uint32_t index;
        CHECK(coords_encode(tables, state, &index) == COORD_OK, "scramble rejected");
        for (int move = 0; move < COORD_MOVES; move++) {
            const cube_t turned =
                cube_apply_move(state, cube_move_index(cube_solve_move_chars[move]));
            uint32_t expected;
            CHECK(coords_encode(tables, turned, &expected) == COORD_OK, "turned state rejected");
            CHECK(coords_apply(tables, move, index) == expected,
                  "move '%c' disagrees with the sticker model", cube_solve_move_chars[move]);
        }
    }
}

/* --- the distance table ------------------------------------------------ */

static const uint32_t oeis_a079762[] = {
    1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508,
    930588, 1350852, 782536, 90280, 276,
};

static void test_histogram(const distance_table_t *table)
{
    uint32_t counts[TABLE_MAX_DISTANCE + 1];
    const int highest = table_histogram(table, counts);
    const int expected = (int)(sizeof oeis_a079762 / sizeof *oeis_a079762);
    CHECK(highest == expected, "maximum distance %d, expected %d", highest - 1, expected - 1);

    uint32_t total = 0;
    for (int depth = 0; depth < highest && depth < expected; depth++) {
        CHECK(counts[depth] == oeis_a079762[depth], "depth %d: %u states, expected %u", depth,
              counts[depth], oeis_a079762[depth]);
        total += counts[depth];
    }
    CHECK(total == COORD_STATES, "%u states counted, expected %d", total, COORD_STATES);
}

static void test_parallel_table(const coord_tables_t *tables, const distance_table_t *serial)
{
    for (int workers = 2; workers <= 8; workers *= 2) {
        distance_table_t *const parallel = table_create_parallel(tables, workers);
        CHECK(parallel != nullptr, "parallel build with %d workers failed", workers);
        if (!parallel)
            continue;
        CHECK(memcmp((const void *)serial->distance, (const void *)parallel->distance,
                     serial->size) == 0,
              "parallel build with %d workers differs from the serial one", workers);
        table_destroy(parallel);
    }
}

/* A cube may be handed over in any of the 24 orientations. Each one encodes to
 * a *different* index -- the fixed slot then holds a different piece, so the
 * permutation genuinely differs -- but all 24 describe the same physical
 * position, so all 24 must be the same number of quarter turns from solved and
 * every solution must actually solve them.
 */
static void test_orientation_independence(const coord_tables_t *tables,
                                          const distance_table_t *table)
{
    const uint8_t (*const rotations)[CUBE_FACELETS] = cube_rotation_perms();
    cube_rng_t rng = cube_rng_seed(41);
    for (int trial = 0; trial < 20; trial++) {
        const cube_t state = random_state(&rng);
        uint32_t index;
        CHECK(coords_encode(tables, state, &index) == COORD_OK, "scramble rejected");
        const int expected = table_distance(table, index);

        for (int r = 0; r < CUBE_ROTATIONS; r++) {
            const cube_t held = cube_rotate(state, rotations[r]);
            solution_t solution;
            CHECK(table_solve(tables, table, held, &solution) == SOLVE_OK,
                  "rotation %d rejected", r);
            CHECK(solution.length == expected, "rotation %d needs %d moves, expected %d", r,
                  solution.length, expected);
            cube_t solved;
            CHECK(cube_apply_moves(held, solution.moves, &solved), "bad move string");
            CHECK(cube_is_solved(solved), "rotation %d was not solved", r);
        }
    }
}

/* --- the solvers ------------------------------------------------------- */

static void test_solvers(const coord_tables_t *tables, const distance_table_t *table,
                         const ida_databases_t *databases)
{
    cube_rng_t rng = cube_rng_seed(31);
    cube_t states[26];
    int count = 0;
    char error[128];
    CHECK(cube_parse(bench_hardest, &states[count++], error, sizeof error),
          "the antipodal position does not parse");
    while (count < (int)(sizeof states / sizeof *states))
        states[count++] = random_state(&rng);

    for (int i = 0; i < count; i++) {
        uint32_t index;
        CHECK(coords_encode(tables, states[i], &index) == COORD_OK, "scramble rejected");
        const int distance = table_distance(table, index);

        solution_t by_bfs, by_ida, by_table;
        CHECK(bfs_solve(tables, states[i], &by_bfs) == SOLVE_OK, "bfs failed");
        CHECK(ida_solve(tables, databases, states[i], &by_ida) == SOLVE_OK, "ida failed");
        CHECK(table_solve(tables, table, states[i], &by_table) == SOLVE_OK, "table failed");

        CHECK(by_bfs.length == distance, "bfs returned %d moves, optimum is %d", by_bfs.length,
              distance);
        CHECK(by_ida.length == distance, "ida returned %d moves, optimum is %d", by_ida.length,
              distance);
        CHECK(by_table.length == distance, "table returned %d moves, optimum is %d",
              by_table.length, distance);

        const solution_t *const all[] = {&by_bfs, &by_ida, &by_table};
        static const char *const names[] = {"bfs", "ida", "table"};
        for (int which = 0; which < 3; which++) {
            cube_t solved;
            CHECK(cube_apply_moves(states[i], all[which]->moves, &solved),
                  "%s returned an unparsable move string", names[which]);
            CHECK(cube_is_solved(solved), "%s solution '%s' does not solve the cube",
                  names[which], all[which]->moves);
        }
    }

    /* The antipodal position is the point of the exercise: exactly 14 moves. */
    solution_t antipodal;
    CHECK(bfs_solve(tables, states[0], &antipodal) == SOLVE_OK, "bfs failed on the antipode");
    CHECK(antipodal.length == 14, "the antipodal position needs %d moves, expected 14",
          antipodal.length);

    /* An already-solved cube needs no moves and is not an error. */
    solution_t nothing;
    CHECK(bfs_solve(tables, cube_solved, &nothing) == SOLVE_OK, "bfs rejected a solved cube");
    CHECK(nothing.length == 0, "bfs returned %d moves for a solved cube", nothing.length);
    CHECK(table_solve(tables, table, cube_solved, &nothing) == SOLVE_OK, "table rejected solved");
    CHECK(nothing.length == 0, "table returned %d moves for a solved cube", nothing.length);
    CHECK(ida_solve(tables, databases, cube_solved, &nothing) == SOLVE_OK, "ida rejected solved");
    CHECK(nothing.length == 0, "ida returned %d moves for a solved cube", nothing.length);
}

static void test_pattern_databases(const ida_databases_t *databases)
{
    int permutation, orientation;
    ida_databases_reach(databases, &permutation, &orientation);
    /* Documented as the reason IDA* loses here: both bounds stop well short of
     * the real maximum of 14. */
    CHECK(permutation > 0 && permutation < TABLE_MAX_DISTANCE,
          "permutation database reaches %d", permutation);
    CHECK(orientation > 0 && orientation < TABLE_MAX_DISTANCE,
          "orientation database reaches %d", orientation);
}

/* ----------------------------------------------------------------------- */

typedef void (*plain_test_t)(void);

static void run(const char *name, plain_test_t test)
{
    current = name;
    const int before = failures;
    test();
    printf("%-24s %s\n", name, failures == before ? "ok" : "FAILED");
}

#define RUN(name, ...)                                                         \
    do {                                                                       \
        current = name;                                                        \
        const int before = failures;                                           \
        __VA_ARGS__;                                                           \
        printf("%-24s %s\n", name, failures == before ? "ok" : "FAILED");       \
    } while (0)

int main(void)
{
    coord_tables_t *const tables = coords_create();
    if (!tables) {
        fputs("out of memory\n", stderr);
        return 1;
    }

    run("move algebra", test_move_algebra);
    run("fixed corner", test_fixed_corner);
    run("parse and render", test_parse_and_render);
    run("invert", test_invert);
    run("rotations", test_rotations);
    RUN("encoding", test_encoding(tables));
    RUN("move tables", test_move_tables(tables));

    distance_table_t *const table = table_create(tables);
    ida_databases_t *const databases = ida_databases_create(tables);
    if (!table || !databases) {
        fputs("out of memory\n", stderr);
        table_destroy(table);
        ida_databases_destroy(databases);
        coords_destroy(tables);
        return 1;
    }

    RUN("OEIS A079762 histogram", test_histogram(table));
    RUN("parallel table build", test_parallel_table(tables, table));
    RUN("pattern databases", test_pattern_databases(databases));
    RUN("orientation independence", test_orientation_independence(tables, table));
    RUN("solvers agree", test_solvers(tables, table, databases));

    table_destroy(table);
    ida_databases_destroy(databases);
    coords_destroy(tables);

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
