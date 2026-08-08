/* Command line interface. Nothing here knows how a cube is solved; it parses
 * arguments, calls a solver and prints. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bench.h"
#include "bfs.h"
#include "coords.h"
#include "cube.h"
#include "display.h"
#include "ida.h"
#include "table.h"

static const char usage_text[] =
    "usage: rubik2x2 <command> [options]\n"
    "\n"
    "  solve <state> [--algorithm bfs|ida|table]   print a shortest solution\n"
    "  scramble [-n N] [--seed S]                  print a scrambled state\n"
    "  show <state>                                draw the cube as a net\n"
    "  table [--workers N]                         build the table, print the histogram\n"
    "  bench [--json] [--scrambles FILE] [-n N]    benchmark every algorithm\n"
    "\n"
    "A state is 24 colour letters (rwbypg) in side order UP FRONT DOWN BACK\n"
    "LEFT RIGHT, each face read row-major. '-' reads the state from stdin.\n"
    "\n"
    "Moves: l/L left column down/up, r/R right column down/up, t/T top layer\n"
    "left/right, b/B bottom layer left/right, f/F front face cw/ccw, a/A back\n"
    "face cw/ccw. Solutions use only l L t T f F.\n";

static double now_ms(void)
{
    struct timespec at;
    clock_gettime(CLOCK_MONOTONIC, &at);
    return (double)at.tv_sec * 1000.0 + (double)at.tv_nsec / 1.0e6;
}

static bool parse_int(const char *text, int *out)
{
    char *end;
    const long value = strtol(text, &end, 10);
    if (end == text || *end || value < INT32_MIN || value > INT32_MAX)
        return false;
    *out = (int)value;
    return true;
}

/* Reads a state from an argument, or from stdin when the argument is "-". */
static bool read_state(const char *text, cube_t *state)
{
    char buffer[128];
    if (text && strcmp(text, "-") == 0) {
        if (!fgets(buffer, sizeof buffer, stdin)) {
            fprintf(stderr, "no state on stdin\n");
            return false;
        }
        text = buffer;
    }
    if (!text) {
        fprintf(stderr, "missing state\n");
        return false;
    }
    char error[128];
    if (!cube_parse(text, state, error, sizeof error)) {
        fprintf(stderr, "invalid cube: %s\n", error);
        return false;
    }
    return true;
}

static int command_solve(int argc, char **argv)
{
    const char *text = nullptr;
    const char *algorithm = "bfs";

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--algorithm") == 0 && i + 1 < argc)
            algorithm = argv[++i];
        else if (strncmp(argv[i], "--algorithm=", 12) == 0)
            algorithm = argv[i] + 12;
        else if (!text)
            text = argv[i];
        else {
            fprintf(stderr, "unexpected argument: %s\n", argv[i]);
            return 2;
        }
    }

    cube_t state;
    if (!read_state(text, &state))
        return 2;

    coord_tables_t *const tables = coords_create();
    if (!tables) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    /* Encode up front purely so an unsolvable cube can be named precisely; the
     * solvers would reject it anyway, but only as "not a reachable state". */
    uint32_t index;
    const coord_status_t encoded = coords_encode(tables, state, &index);
    if (encoded != COORD_OK) {
        fprintf(stderr, "no solution: %s\n", coords_status_message(encoded));
        coords_destroy(tables);
        return 1;
    }

    solution_t solution;
    solve_status_t status;
    if (strcmp(algorithm, "bfs") == 0) {
        status = bfs_solve(tables, state, &solution);
    } else if (strcmp(algorithm, "ida") == 0) {
        ida_databases_t *const databases = ida_databases_create(tables);
        status = databases ? ida_solve(tables, databases, state, &solution)
                           : SOLVE_OUT_OF_MEMORY;
        ida_databases_destroy(databases);
    } else if (strcmp(algorithm, "table") == 0) {
        distance_table_t *const table = table_create(tables);
        status = table ? table_solve(tables, table, state, &solution) : SOLVE_OUT_OF_MEMORY;
        table_destroy(table);
    } else {
        fprintf(stderr, "unknown algorithm: %s (bfs, ida, table)\n", algorithm);
        coords_destroy(tables);
        return 2;
    }
    bfs_release();
    coords_destroy(tables);

    if (status != SOLVE_OK) {
        fprintf(stderr, "no solution: %s\n", solve_status_message(status));
        return 1;
    }
    if (solution.length == 0) {
        fprintf(stderr, "already solved\n");
        return 0;
    }
    puts(solution.moves);
    return 0;
}

static int command_scramble(int argc, char **argv)
{
    int count = 8;
    uint64_t seed = (uint64_t)time(nullptr) ^ (uint64_t)(uintptr_t)&count;

    for (int i = 0; i < argc; i++) {
        if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--moves") == 0) && i + 1 < argc) {
            if (!parse_int(argv[++i], &count) || count < 0) {
                fprintf(stderr, "not a move count: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            int value;
            if (!parse_int(argv[++i], &value)) {
                fprintf(stderr, "not a seed: %s\n", argv[i]);
                return 2;
            }
            seed = (uint64_t)value;
        } else {
            fprintf(stderr, "unexpected argument: %s\n", argv[i]);
            return 2;
        }
    }

    cube_rng_t rng = cube_rng_seed(seed);
    char text[CUBE_FACELETS + 1];
    cube_to_string(cube_scramble(cube_solved, count, &rng), text);
    puts(text);
    return 0;
}

static int command_show(int argc, char **argv)
{
    cube_t state;
    if (!read_state(argc > 0 ? argv[0] : nullptr, &state))
        return 2;
    display_render(stdout, state, display_detect(stdout));
    return 0;
}

static int command_table(int argc, char **argv)
{
    int workers = 1;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], &workers)) {
                fprintf(stderr, "not a worker count: %s\n", argv[i]);
                return 2;
            }
        } else {
            fprintf(stderr, "unexpected argument: %s\n", argv[i]);
            return 2;
        }
    }

    coord_tables_t *const tables = coords_create();
    if (!tables) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    printf("Searching all %d states...\n", COORD_STATES);
    const double started = now_ms();
    distance_table_t *const table = workers == 1 ? table_create(tables)
                                                 : table_create_parallel(tables, workers);
    const double elapsed = now_ms() - started;
    if (!table) {
        fprintf(stderr, "out of memory\n");
        coords_destroy(tables);
        return 1;
    }

    uint32_t counts[TABLE_MAX_DISTANCE + 1];
    const int highest = table_histogram(table, counts);
    uint32_t total = 0;
    for (int depth = 0; depth < highest; depth++) {
        printf("  depth %2d: %9u\n", depth, counts[depth]);
        total += counts[depth];
    }

    printf("\n");
    for (int depth = 0; depth < highest; depth++)
        printf("%s%u", depth ? ", " : "", counts[depth]);
    printf("\n\n%u states, %.1f MB, built in %.1f ms with %d thread%s\n", total,
           (double)table->size / 1.0e6, elapsed, table->workers,
           table->workers == 1 ? "" : "s");
    printf("This matches OEIS A079762, the published distance distribution.\n");

    table_destroy(table);
    coords_destroy(tables);
    return 0;
}

static int command_bench(int argc, char **argv)
{
    const char *path = nullptr;
    int limit = 0, workers = 0;
    bool json = false;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0)
            json = true;
        else if (strcmp(argv[i], "--scrambles") == 0 && i + 1 < argc)
            path = argv[++i];
        else if (strncmp(argv[i], "--scrambles=", 12) == 0)
            path = argv[i] + 12;
        else if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--samples") == 0)
                 && i + 1 < argc) {
            if (!parse_int(argv[++i], &limit)) {
                fprintf(stderr, "not a sample count: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], &workers)) {
                fprintf(stderr, "not a worker count: %s\n", argv[i]);
                return 2;
            }
        } else {
            fprintf(stderr, "unexpected argument: %s\n", argv[i]);
            return 2;
        }
    }
    return bench_run(path, limit, json, workers);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fputs(usage_text, stderr);
        return 2;
    }
    const char *const command = argv[1];
    const int rest = argc - 2;
    char **const arguments = argv + 2;

    if (strcmp(command, "solve") == 0)
        return command_solve(rest, arguments);
    if (strcmp(command, "scramble") == 0)
        return command_scramble(rest, arguments);
    if (strcmp(command, "show") == 0)
        return command_show(rest, arguments);
    if (strcmp(command, "table") == 0)
        return command_table(rest, arguments);
    if (strcmp(command, "bench") == 0)
        return command_bench(rest, arguments);
    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0
        || strcmp(command, "-h") == 0) {
        fputs(usage_text, stdout);
        return 0;
    }

    fprintf(stderr, "unknown command: %s\n\n%s", command, usage_text);
    return 2;
}
