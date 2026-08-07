/* God's algorithm: the exact distance of every reachable state.
 *
 * The pocket cube has 3,674,160 positions -- few enough to hold every one's
 * exact distance to solved in a flat byte array of 3.7 MB. Then there is
 * nothing left to search, only a table to read:
 *
 *     at a state d moves from solved, exactly one neighbour is d-1 away;
 *     step to it, repeat.
 *
 * A solve costs d lookups: constant time in the difficulty of the scramble, and
 * optimal by construction. The distribution it produces is OEIS A079762, which
 * the test suite pins exactly -- no wrong move table or encoding could
 * reproduce it.
 *
 * Distances are stored as distance + 1 so that 0 can mean "not yet reached"
 * while the breadth-first build runs. The array element type is _Atomic so the
 * same storage serves both the serial and the parallel build; on every target
 * that matters a relaxed load or store of a byte is a plain move, so the serial
 * path pays nothing for it.
 */
#ifndef RUBIK_TABLE_H
#define RUBIK_TABLE_H

#include <stdatomic.h>

#include "coords.h"
#include "solver.h"

enum { TABLE_MAX_DISTANCE = SOLVER_MAX_DEPTH };

typedef struct {
    _Atomic uint8_t *distance;   /* distance + 1, indexed by encoded state */
    uint32_t size;
    int workers;                 /* how many threads built it; 1 when serial */
} distance_table_t;

/* Breadth-first over the whole space on one thread. */
distance_table_t *table_create(const coord_tables_t *tables);

/* The same table, built with `workers` threads (<= 0 picks one per online CPU).
 * Only the build differs; the result is bit-for-bit identical. */
distance_table_t *table_create_parallel(const coord_tables_t *tables, int workers);

void table_destroy(distance_table_t *table);

int table_default_workers(void);

/* Quarter turns from the encoded state to solved. */
static inline int table_distance(const distance_table_t *table, uint32_t index)
{
    return (int)atomic_load_explicit(&table->distance[index], memory_order_relaxed) - 1;
}

/* States per distance, index 0 being the solved state. Writes at most
 * TABLE_MAX_DISTANCE + 1 entries and returns how many it wrote. */
int table_histogram(const distance_table_t *table, uint32_t counts[static TABLE_MAX_DISTANCE + 1]);

solve_status_t table_solve(const coord_tables_t *tables, const distance_table_t *table,
                           cube_t state, solution_t *out);
solve_status_t table_solve_index(const coord_tables_t *tables, const distance_table_t *table,
                                 uint32_t start, solution_t *out);

#endif /* RUBIK_TABLE_H */
