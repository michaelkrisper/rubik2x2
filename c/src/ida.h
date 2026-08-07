/* Korf's IDA* with pattern databases -- the textbook optimal cube solver.
 *
 * Iterative-deepening A* runs depth-first passes with a rising limit on g + h,
 * where h is a lower bound on the moves still needed. Korf's contribution was
 * to get h from *pattern databases*: exact distances for a relaxed version of
 * the puzzle, precomputed once.
 *
 * Here the relaxations fall out of the encoding for free. A move acts on the
 * permutation coordinate and the orientation coordinate independently, so each
 * coordinate on its own is a genuine quotient of the state graph -- solving the
 * cube necessarily solves both projections. Their exact distances (5040 and 729
 * entries, computed in microseconds) are therefore admissible, and the larger
 * of the two is the better bound.
 *
 * This is here for comparison, not because it wins. Both databases top out at
 * distance 8 while real positions reach 14, so the bound is too loose to prune
 * a puzzle this small: pattern databases pay off when you cannot afford to
 * track every piece, and a pocket cube is nothing *but* the pieces the
 * databases already cover.
 */
#ifndef RUBIK_IDA_H
#define RUBIK_IDA_H

#include "coords.h"
#include "solver.h"

/* Exact distances for the two coordinate projections, stored as distance + 1
 * so that 0 can mean "not yet reached" during the build.
 */
typedef struct {
    uint8_t permutation[COORD_PERMUTATIONS];
    uint8_t orientation[COORD_ORIENTATIONS];
} ida_databases_t;

ida_databases_t *ida_databases_create(const coord_tables_t *tables);
void ida_databases_destroy(ida_databases_t *databases);

/* The largest bound each database can ever return. */
void ida_databases_reach(const ida_databases_t *databases, int *permutation, int *orientation);

solve_status_t ida_solve(const coord_tables_t *tables, const ida_databases_t *databases,
                         cube_t state, solution_t *out);
solve_status_t ida_solve_index(const coord_tables_t *tables, const ida_databases_t *databases,
                               uint32_t start, solution_t *out);

#endif /* RUBIK_IDA_H */
