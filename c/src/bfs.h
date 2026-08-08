/* Bidirectional breadth-first search: the solver that needs no setup.
 *
 * Two frontiers grow a level at a time -- one from the scramble, one from the
 * goal -- and the search stops where they touch. The levels peak at 1,350,852
 * states at depth 11, so a one-sided search would have to walk most of the
 * space; meeting in the middle needs depth 7 from each side, where only 33,058
 * states live.
 *
 * Expanding by whole levels is what makes the result optimal. Every state in
 * the opposite side's map is at most as deep as its last completed level, so
 * the first meeting point found is already a shortest solution -- no heuristic
 * and no tie-breaking required.
 */
#ifndef RUBIK_BFS_H
#define RUBIK_BFS_H

#include "coords.h"
#include "solver.h"

solve_status_t bfs_solve(const coord_tables_t *tables, cube_t state, solution_t *out);

/* The same search from an already-encoded index, for callers that have one. */
solve_status_t bfs_solve_index(const coord_tables_t *tables, uint32_t start, solution_t *out);

/* Release the mark array and level buffers the search keeps alive between
 * solves. Optional -- the process would hand them back anyway -- but it keeps
 * the allocation ledger balanced for a leak checker. */
void bfs_release(void);

#endif /* RUBIK_BFS_H */
