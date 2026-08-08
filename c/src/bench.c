#include "bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bfs.h"
#include "coords.h"
#include "cube.h"
#include "ida.h"
#include "table.h"

const char bench_hardest[] = "wppryygrygrwgprbbbwpbwyg";

#ifndef RUBIK_BUILD_FLAGS
#define RUBIK_BUILD_FLAGS "unknown"
#endif

enum { BENCH_ALGORITHMS = 4, DEFAULT_SCRAMBLES = 200, SETUP_SAMPLES = 3 };

static void keep_fastest(double *best, double sample, int index)
{
    if (index == 0 || sample < *best)
        *best = sample;
}

/* Keep repeating a measurement until the clock has something to say. */
static const double target_ms = 250.0;
static const int max_repeats = 100000;

static double now_ms(void)
{
    struct timespec at;
    clock_gettime(CLOCK_MONOTONIC, &at);
    return (double)at.tv_sec * 1000.0 + (double)at.tv_nsec / 1.0e6;
}

/* One algorithm behind one interface, so the harness measures them alike. */
typedef solve_status_t (*solve_fn)(const void *context, cube_t state, solution_t *out);

typedef struct {
    const coord_tables_t *tables;
    const ida_databases_t *databases;
    const distance_table_t *table;
} bench_context_t;

static solve_status_t solve_bfs(const void *context, cube_t state, solution_t *out)
{
    const bench_context_t *const c = context;
    return bfs_solve(c->tables, state, out);
}

static solve_status_t solve_ida(const void *context, cube_t state, solution_t *out)
{
    const bench_context_t *const c = context;
    return ida_solve(c->tables, c->databases, state, out);
}

static solve_status_t solve_table(const void *context, cube_t state, solution_t *out)
{
    const bench_context_t *const c = context;
    return table_solve(c->tables, c->table, state, out);
}

typedef struct {
    const char *name;
    double setup_ms;
    double avg_ms;
    double worst_ms;
    double peak_mb;
    bool optimal;
    int workers;        /* 0 when the entry is single-threaded */
} bench_result_t;

typedef struct {
    cube_t *state;
    size_t count;
} scramble_set_t;

static void scrambles_free(scramble_set_t *set)
{
    free(set->state);
    *set = (scramble_set_t){0};
}

static bool scrambles_load(scramble_set_t *set, const char *path, int limit)
{
    *set = (scramble_set_t){0};
    size_t capacity = 256;
    set->state = malloc(capacity * sizeof *set->state);
    if (!set->state)
        return false;

    if (path) {
        FILE *const file = fopen(path, "r");
        if (!file) {
            fprintf(stderr, "cannot open %s\n", path);
            scrambles_free(set);
            return false;
        }
        char line[128];
        while (fgets(line, sizeof line, file)) {
            cube_t state;
            char error[128];
            if (line[0] == '\n' || line[0] == '#')
                continue;
            if (!cube_parse(line, &state, error, sizeof error)) {
                fprintf(stderr, "%s: %s\n", path, error);
                fclose(file);
                scrambles_free(set);
                return false;
            }
            if (set->count == capacity) {
                capacity *= 2;
                cube_t *const grown = realloc(set->state, capacity * sizeof *grown);
                if (!grown) {
                    fclose(file);
                    scrambles_free(set);
                    return false;
                }
                set->state = grown;
            }
            set->state[set->count++] = state;
            if (limit > 0 && set->count == (size_t)limit)
                break;
        }
        fclose(file);
    } else {
        /* No file: deterministic scrambles, so a run is still reproducible. */
        const size_t wanted = limit > 0 ? (size_t)limit : DEFAULT_SCRAMBLES;
        cube_t *const grown = realloc(set->state, wanted * sizeof *grown);
        if (!grown) {
            scrambles_free(set);
            return false;
        }
        set->state = grown;
        for (size_t i = 0; i < wanted; i++) {
            cube_rng_t rng = cube_rng_seed(i);
            set->state[set->count++] = cube_scramble(cube_solved, 14, &rng);
        }
    }

    if (set->count == 0) {
        fprintf(stderr, "no scrambles to benchmark\n");
        scrambles_free(set);
        return false;
    }
    return true;
}

/* Mean milliseconds per solve over the whole set, repeated until the total is
 * long enough to mean something. */
static double measure_average(solve_fn solve, const bench_context_t *context,
                              const scramble_set_t *set)
{
    double elapsed = 0.0;
    long solves = 0;
    for (int repeat = 0; repeat < max_repeats && elapsed < target_ms; repeat++) {
        const double started = now_ms();
        for (size_t i = 0; i < set->count; i++) {
            solution_t solution;
            solve(context, set->state[i], &solution);
        }
        elapsed += now_ms() - started;
        solves += (long)set->count;
    }
    return elapsed / (double)solves;
}

static double measure_one(solve_fn solve, const bench_context_t *context, cube_t state,
                          solution_t *out)
{
    double elapsed = 0.0;
    long solves = 0;
    for (int repeat = 0; repeat < max_repeats && elapsed < target_ms; repeat++) {
        const double started = now_ms();
        solve(context, state, out);
        elapsed += now_ms() - started;
        solves++;
    }
    return elapsed / (double)solves;
}

/* Every solution must be shortest *and* actually solve its cube. */
static bool check_optimal(solve_fn solve, const bench_context_t *context,
                          const scramble_set_t *set)
{
    for (size_t i = 0; i < set->count; i++) {
        solution_t solution;
        if (solve(context, set->state[i], &solution) != SOLVE_OK)
            return false;
        uint32_t index;
        if (coords_encode(context->tables, set->state[i], &index) != COORD_OK)
            return false;
        if (solution.length != table_distance(context->table, index))
            return false;
        cube_t solved;
        if (!cube_apply_moves(set->state[i], solution.moves, &solved) || !cube_is_solved(solved))
            return false;
    }
    return true;
}

static void evaluate(bench_result_t *result, solve_fn solve, const bench_context_t *context,
                     const scramble_set_t *set, cube_t hardest)
{
    solution_t worst;
    result->avg_ms = measure_average(solve, context, set);
    result->worst_ms = measure_one(solve, context, hardest, &worst);
    result->peak_mb = (double)worst.peak_bytes / 1.0e6;
    result->optimal = check_optimal(solve, context, set);
}

static void print_json(const bench_result_t *results, int count)
{
    printf("{\n");
    printf("  \"language\": \"C\",\n");
    printf("  \"version\": \"gcc %d.%d.%d\",\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
    printf("  \"build\": \"%s\",\n", RUBIK_BUILD_FLAGS);
    printf("  \"algorithms\": [\n");
    for (int i = 0; i < count; i++) {
        const bench_result_t *const r = &results[i];
        printf("    {\"name\": \"%s\", \"setup_ms\": %.3f, \"avg_ms\": %.4f, "
               "\"worst_ms\": %.4f, \"peak_mb\": %.2f, \"optimal\": %s",
               r->name, r->setup_ms, r->avg_ms, r->worst_ms, r->peak_mb,
               r->optimal ? "true" : "false");
        if (r->workers > 0)
            printf(", \"workers\": %d", r->workers);
        printf("}%s\n", i + 1 < count ? "," : "");
    }
    printf("  ]\n");
    printf("}\n");
}

static void print_table(const bench_result_t *results, int count, size_t scrambles)
{
    printf("rubik2x2 benchmark -- %zu scrambles plus the antipodal position\n", scrambles);
    printf("%d reachable states, maximum distance %d quarter turns\n\n",
           COORD_STATES, TABLE_MAX_DISTANCE);
    printf("%-11s%11s%10s%11s%10s%9s\n",
           "algorithm", "setup ms", "avg ms", "worst ms", "peak MB", "optimal");
    printf("--------------------------------------------------------------\n");
    for (int i = 0; i < count; i++)
        printf("%-11s%11.1f%10.4f%11.4f%10.2f%9s\n", results[i].name, results[i].setup_ms,
               results[i].avg_ms, results[i].worst_ms, results[i].peak_mb,
               results[i].optimal ? "yes" : "NO");

    printf("\ntotal time for a workload (setup + solves), seconds\n");
    static const long counts[] = {1, 10, 100, 1000, 10000};
    printf("%-11s", "algorithm");
    for (size_t c = 0; c < sizeof counts / sizeof *counts; c++)
        printf("%12ld", counts[c]);
    printf("\n");
    for (int i = 0; i < count; i++) {
        printf("%-11s", results[i].name);
        for (size_t c = 0; c < sizeof counts / sizeof *counts; c++)
            printf("%12.2f",
                   (results[i].setup_ms + (double)counts[c] * results[i].avg_ms) / 1000.0);
        printf("\n");
    }
}

int bench_run(const char *path, int limit, bool json, int workers)
{
    scramble_set_t set;
    if (!scrambles_load(&set, path, limit))
        return 1;

    cube_t hardest;
    char error[128];
    if (!cube_parse(bench_hardest, &hardest, error, sizeof error)) {
        scrambles_free(&set);
        return 1;
    }

    coord_tables_t *const tables = coords_create();
    if (!tables) {
        scrambles_free(&set);
        return 1;
    }

    if (workers <= 0)
        workers = table_default_workers();

    /* Setup is what each algorithm must precompute before its first solve. The
     * shared move tables above are excluded: every algorithm needs them, so
     * counting them would say nothing about the differences.
     *
     * Each build is timed several times, alternating, and the fastest run kept.
     * A single sample is not usable here: whichever table is built first pays
     * for faulting in 18 MB of fresh pages, and on a laptop turbo and thermal
     * state move the answer by a factor of two between neighbouring runs. The
     * minimum is the only statistic that survives both. */
    double setup_ida = 0.0, setup_table = 0.0, setup_parallel = 0.0;
    for (int sample = 0; sample < SETUP_SAMPLES; sample++) {
        double started = now_ms();
        ida_databases_t *const scratch_databases = ida_databases_create(tables);
        keep_fastest(&setup_ida, now_ms() - started, sample);
        ida_databases_destroy(scratch_databases);

        started = now_ms();
        distance_table_t *const scratch_table = table_create(tables);
        keep_fastest(&setup_table, now_ms() - started, sample);
        table_destroy(scratch_table);

        started = now_ms();
        distance_table_t *const scratch_parallel = table_create_parallel(tables, workers);
        keep_fastest(&setup_parallel, now_ms() - started, sample);
        table_destroy(scratch_parallel);
    }

    ida_databases_t *const databases = ida_databases_create(tables);
    distance_table_t *const table = table_create(tables);
    distance_table_t *const parallel = table_create_parallel(tables, workers);

    if (!databases || !table || !parallel) {
        fprintf(stderr, "out of memory\n");
        ida_databases_destroy(databases);
        table_destroy(table);
        table_destroy(parallel);
        coords_destroy(tables);
        scrambles_free(&set);
        return 1;
    }

    const bench_context_t context = {.tables = tables, .databases = databases, .table = table};
    bench_result_t results[BENCH_ALGORITHMS] = {
        {.name = "bfs", .setup_ms = 0.0},
        {.name = "ida", .setup_ms = setup_ida},
        {.name = "table", .setup_ms = setup_table},
        {.name = "table-par", .setup_ms = setup_parallel, .workers = parallel->workers},
    };

    evaluate(&results[0], solve_bfs, &context, &set, hardest);
    evaluate(&results[1], solve_ida, &context, &set, hardest);
    evaluate(&results[2], solve_table, &context, &set, hardest);

    /* Only the build is parallelised, so the per-solve figures are the serial
     * table's by definition. */
    results[3].avg_ms = results[2].avg_ms;
    results[3].worst_ms = results[2].worst_ms;
    results[3].peak_mb = results[2].peak_mb;
    results[3].optimal = results[2].optimal
        && memcmp((const void *)table->distance, (const void *)parallel->distance,
                  table->size) == 0;

    if (json)
        print_json(results, BENCH_ALGORITHMS);
    else
        print_table(results, BENCH_ALGORITHMS, set.count);

    bfs_release();
    ida_databases_destroy(databases);
    table_destroy(table);
    table_destroy(parallel);
    coords_destroy(tables);
    scrambles_free(&set);
    return 0;
}
