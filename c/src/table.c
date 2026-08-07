#include "table.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Every state is queued exactly once, so one buffer of COORD_STATES entries
 * holds the whole search: levels are consecutive ranges in it, no growth and no
 * double buffering. */
#define QUEUE_BYTES ((size_t)COORD_STATES * sizeof(uint32_t))

static distance_table_t *table_alloc(int workers)
{
    distance_table_t *const table = malloc(sizeof *table);
    if (!table)
        return nullptr;
    table->distance = calloc(COORD_STATES, sizeof *table->distance);
    table->size = COORD_STATES;
    table->workers = workers;
    if (!table->distance) {
        free(table);
        return nullptr;
    }
    return table;
}

void table_destroy(distance_table_t *table)
{
    if (table) {
        free(table->distance);
        free(table);
    }
}

int table_default_workers(void)
{
    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    return online > 1 ? (int)online : 1;
}

distance_table_t *table_create(const coord_tables_t *tables)
{
    distance_table_t *const table = table_alloc(1);
    if (!table)
        return nullptr;
    uint32_t *const queue = malloc(QUEUE_BYTES);
    if (!queue) {
        table_destroy(table);
        return nullptr;
    }

    _Atomic uint8_t *const distance = table->distance;
    atomic_store_explicit(&distance[COORD_SOLVED_INDEX], 1, memory_order_relaxed);
    queue[0] = COORD_SOLVED_INDEX;

    uint32_t begin = 0, end = 1, tail = 1;
    for (uint8_t marker = 2; begin < end; marker++) {
        for (uint32_t i = begin; i < end; i++) {
            const uint32_t index = queue[i];
            const uint32_t *const perm = tables->perm_by_q[index / COORD_ORIENTATIONS];
            const uint16_t *const orient = tables->orient_by_r[index % COORD_ORIENTATIONS];
            for (int move = 0; move < COORD_MOVES; move++) {
                const uint32_t child = perm[move] + orient[move];
                if (!atomic_load_explicit(&distance[child], memory_order_relaxed)) {
                    atomic_store_explicit(&distance[child], marker, memory_order_relaxed);
                    queue[tail++] = child;
                }
            }
        }
        begin = end;
        end = tail;
    }

    free(queue);
    return table;
}

/* ---------------------------------------------------------------------------
 * Parallel build.
 *
 * The search is level-synchronous, so within a level the frontier can be split
 * across workers; only the shared distance array needs care, because two
 * workers can reach the same state at the same moment. The claim is settled
 * with one relaxed compare-exchange per edge: whoever turns the byte from 0 to
 * the level marker owns the state and queues it, everyone else moves on. A
 * plain relaxed load in front of it keeps the locked instruction off the ~85%
 * of edges that lead somewhere already visited.
 *
 * Queue slots are claimed in blocks of PAR_BATCH so the shared tail is touched
 * once per block rather than once per state.
 * ------------------------------------------------------------------------- */

enum { PAR_CHUNK = 4096, PAR_BATCH = 512, PAR_MAX_WORKERS = 256 };

typedef struct {
    const coord_tables_t *tables;
    _Atomic uint8_t *distance;
    uint32_t *queue;
    _Atomic uint32_t tail;
    _Atomic uint32_t cursor;
    uint32_t level_end;
    uint8_t marker;
    bool done;
    pthread_barrier_t barrier;
    /* Held by the launcher until every thread exists, so a worker cannot reach
     * the barrier before its participant count is final. */
    pthread_mutex_t launch;
} par_context_t;

static void par_work(par_context_t *context)
{
    const coord_tables_t *const tables = context->tables;
    _Atomic uint8_t *const distance = context->distance;
    const uint8_t marker = context->marker;
    uint32_t batch[PAR_BATCH];
    uint32_t held = 0;

    for (;;) {
        const uint32_t from =
            atomic_fetch_add_explicit(&context->cursor, PAR_CHUNK, memory_order_relaxed);
        if (from >= context->level_end)
            break;
        const uint32_t to =
            from + PAR_CHUNK < context->level_end ? from + PAR_CHUNK : context->level_end;

        for (uint32_t i = from; i < to; i++) {
            const uint32_t index = context->queue[i];
            const uint32_t *const perm = tables->perm_by_q[index / COORD_ORIENTATIONS];
            const uint16_t *const orient = tables->orient_by_r[index % COORD_ORIENTATIONS];
            for (int move = 0; move < COORD_MOVES; move++) {
                const uint32_t child = perm[move] + orient[move];
                if (atomic_load_explicit(&distance[child], memory_order_relaxed))
                    continue;
                uint8_t expected = 0;
                if (!atomic_compare_exchange_strong_explicit(&distance[child], &expected, marker,
                                                             memory_order_relaxed,
                                                             memory_order_relaxed))
                    continue;
                batch[held++] = child;
                if (held == PAR_BATCH) {
                    const uint32_t at = atomic_fetch_add_explicit(&context->tail, held,
                                                                  memory_order_relaxed);
                    memcpy(&context->queue[at], batch, held * sizeof *batch);
                    held = 0;
                }
            }
        }
    }

    if (held) {
        const uint32_t at =
            atomic_fetch_add_explicit(&context->tail, held, memory_order_relaxed);
        memcpy(&context->queue[at], batch, held * sizeof *batch);
    }
}

static void *par_worker(void *argument)
{
    par_context_t *const context = argument;
    pthread_mutex_lock(&context->launch);
    pthread_mutex_unlock(&context->launch);
    for (;;) {
        pthread_barrier_wait(&context->barrier);
        if (context->done)
            return nullptr;
        par_work(context);
        pthread_barrier_wait(&context->barrier);
    }
}

distance_table_t *table_create_parallel(const coord_tables_t *tables, int workers)
{
    if (workers <= 0)
        workers = table_default_workers();
    if (workers > PAR_MAX_WORKERS)
        workers = PAR_MAX_WORKERS;
    if (workers == 1)
        return table_create(tables);

    distance_table_t *const table = table_alloc(workers);
    if (!table)
        return nullptr;
    uint32_t *const queue = malloc(QUEUE_BYTES);
    pthread_t *const threads = malloc((size_t)(workers - 1) * sizeof *threads);
    if (!queue || !threads) {
        free(queue);
        free(threads);
        table_destroy(table);
        return nullptr;
    }

    par_context_t context = {
        .tables = tables,
        .distance = table->distance,
        .queue = queue,
        .tail = 1,
        .cursor = 0,
        .level_end = 1,
        .marker = 2,
        .done = false,
    };
    atomic_store_explicit(&context.distance[COORD_SOLVED_INDEX], 1, memory_order_relaxed);
    queue[0] = COORD_SOLVED_INDEX;

    if (pthread_mutex_init(&context.launch, nullptr) != 0
        || pthread_barrier_init(&context.barrier, nullptr, (unsigned)workers) != 0) {
        free(queue);
        free(threads);
        table_destroy(table);
        return nullptr;
    }

    pthread_mutex_lock(&context.launch);
    int started = 0;
    for (; started < workers - 1; started++)
        if (pthread_create(&threads[started], nullptr, par_worker, &context) != 0)
            break;
    if (started < workers - 1) {
        /* Fewer threads than asked for. No worker has passed the launch gate
         * yet, so the barrier can still be resized to match what we have. */
        workers = started + 1;
        table->workers = workers;
        pthread_barrier_destroy(&context.barrier);
        pthread_barrier_init(&context.barrier, nullptr, (unsigned)workers);
    }
    pthread_mutex_unlock(&context.launch);

    uint32_t begin = 0;
    for (;;) {
        context.done = begin >= context.level_end;
        atomic_store_explicit(&context.cursor, begin, memory_order_relaxed);
        pthread_barrier_wait(&context.barrier);
        if (context.done)
            break;
        par_work(&context);
        pthread_barrier_wait(&context.barrier);

        begin = context.level_end;
        context.level_end = atomic_load_explicit(&context.tail, memory_order_relaxed);
        context.marker++;
    }

    for (int i = 0; i < workers - 1; i++)
        pthread_join(threads[i], nullptr);
    pthread_barrier_destroy(&context.barrier);
    pthread_mutex_destroy(&context.launch);
    free(queue);
    free(threads);
    return table;
}

int table_histogram(const distance_table_t *table, uint32_t counts[static TABLE_MAX_DISTANCE + 1])
{
    memset(counts, 0, (TABLE_MAX_DISTANCE + 1) * sizeof *counts);
    int highest = 0;
    for (uint32_t i = 0; i < table->size; i++) {
        const int marker = atomic_load_explicit(&table->distance[i], memory_order_relaxed);
        if (marker < 1 || marker > TABLE_MAX_DISTANCE + 1)
            continue;
        counts[marker - 1]++;
        if (marker > highest)
            highest = marker;
    }
    return highest;
}

solve_status_t table_solve_index(const coord_tables_t *tables, const distance_table_t *table,
                                 uint32_t start, solution_t *out)
{
    *out = (solution_t){0};
    out->peak_bytes = table->size;

    int remaining = table_distance(table, start);
    if (remaining < 0)
        return SOLVE_INVALID;

    uint32_t index = start;
    while (remaining > 0) {
        const uint32_t *const perm = tables->perm_by_q[index / COORD_ORIENTATIONS];
        const uint16_t *const orient = tables->orient_by_r[index % COORD_ORIENTATIONS];
        bool stepped = false;
        for (int move = 0; move < COORD_MOVES && !stepped; move++) {
            const uint32_t child = perm[move] + orient[move];
            if (table_distance(table, child) == remaining - 1) {
                out->moves[out->length++] = cube_solve_move_chars[move];
                index = child;
                remaining--;
                stepped = true;
            }
        }
        if (!stepped)
            return SOLVE_NO_SOLUTION;   /* impossible for a correctly built table */
    }
    out->moves[out->length] = '\0';
    return SOLVE_OK;
}

solve_status_t table_solve(const coord_tables_t *tables, const distance_table_t *table,
                           cube_t state, solution_t *out)
{
    uint32_t start;
    if (coords_encode(tables, state, &start) != COORD_OK) {
        *out = (solution_t){0};
        return SOLVE_INVALID;
    }
    return table_solve_index(tables, table, start, out);
}
