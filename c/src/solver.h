/* What every solver returns, and how it reports failure.
 *
 * All three algorithms return a *shortest* solution; they differ only in what
 * they spend to get there. God's number for the pocket cube is 14 quarter
 * turns, so a solution never needs more room than that.
 */
#ifndef RUBIK_SOLVER_H
#define RUBIK_SOLVER_H

#include <stddef.h>

enum {
    SOLVER_MAX_DEPTH = 14,          /* God's number, quarter-turn metric */
    SOLVER_MOVES_SIZE = SOLVER_MAX_DEPTH + 2,
};

typedef struct {
    char moves[SOLVER_MOVES_SIZE];  /* NUL-terminated; empty when already solved */
    int length;
    size_t peak_bytes;              /* memory the solver held at its peak */
} solution_t;

typedef enum {
    SOLVE_OK = 0,
    SOLVE_INVALID,        /* the sticker arrangement is not a real cube state */
    SOLVE_NO_SOLUTION,    /* no solution within the depth limit -- cannot happen
                             for a valid state, but the search says so rather
                             than looping */
    SOLVE_OUT_OF_MEMORY,
} solve_status_t;

const char *solve_status_message(solve_status_t status);

#endif /* RUBIK_SOLVER_H */
