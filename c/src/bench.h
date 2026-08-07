/* Compare the algorithms with their setup cost included.
 *
 * Per-solve time alone flatters the lookup table, because it hides the
 * exhaustive search that produced it. What decides which algorithm to reach for
 * is total time for the workload you have, so setup, average and worst case are
 * all reported side by side.
 */
#ifndef RUBIK_BENCH_H
#define RUBIK_BENCH_H

#include <stdbool.h>

/* An antipodal position: 14 quarter turns from solved, the hardest there is. */
extern const char bench_hardest[];

/* `path` may be NULL, in which case deterministic scrambles are generated.
 * `limit` caps how many are used (<= 0 means all). `workers` is the thread
 * count for the table-par entry (<= 0 means one per online CPU).
 * Returns an exit status. */
int bench_run(const char *path, int limit, bool json, int workers);

#endif /* RUBIK_BENCH_H */
