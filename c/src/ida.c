#include "ida.h"

#include <stdlib.h>
#include <string.h>

/* Exact distance-to-solved within one coordinate, by breadth-first search over
 * a graph small enough to sit in cache. Stores distance + 1, so 0 still means
 * "not yet reached" while the search runs. */
static bool project(uint8_t *distance, uint32_t size, const uint16_t *table, uint32_t stride)
{
    uint32_t *const scratch = malloc((size_t)size * 2 * sizeof *scratch);
    if (!scratch)
        return false;
    uint32_t *current = scratch, *next = scratch + size;

    memset(distance, 0, size);
    distance[0] = 1;
    current[0] = 0;
    uint32_t count = 1;

    for (uint8_t depth = 1; count; depth++) {
        uint32_t produced = 0;
        for (uint32_t i = 0; i < count; i++)
            for (uint32_t move = 0; move < COORD_MOVES; move++) {
                const uint32_t child = table[move * stride + current[i]];
                if (!distance[child]) {
                    distance[child] = (uint8_t)(depth + 1);
                    next[produced++] = child;
                }
            }
        uint32_t *const swap = current;
        current = next;
        next = swap;
        count = produced;
    }
    free(scratch);
    return true;
}

ida_databases_t *ida_databases_create(const coord_tables_t *tables)
{
    ida_databases_t *const databases = malloc(sizeof *databases);
    if (!databases)
        return nullptr;
    if (!project(databases->permutation, COORD_PERMUTATIONS,
                 &tables->perm_raw[0][0], COORD_PERMUTATIONS)
        || !project(databases->orientation, COORD_ORIENTATIONS,
                    &tables->orient[0][0], COORD_ORIENTATIONS)) {
        free(databases);
        return nullptr;
    }
    return databases;
}

void ida_databases_destroy(ida_databases_t *databases)
{
    free(databases);
}

static int reach_of(const uint8_t *distance, uint32_t size)
{
    int reach = 0;
    for (uint32_t i = 0; i < size; i++)
        if (distance[i] > reach)
            reach = distance[i];
    return reach - 1;
}

void ida_databases_reach(const ida_databases_t *databases, int *permutation, int *orientation)
{
    *permutation = reach_of(databases->permutation, COORD_PERMUTATIONS);
    *orientation = reach_of(databases->orientation, COORD_ORIENTATIONS);
}

typedef struct {
    const coord_tables_t *tables;
    const ida_databases_t *databases;
    char path[SOLVER_MOVES_SIZE];
    int length;
} search_t;

static inline int bound_of(const search_t *search, uint32_t quotient, uint32_t remainder)
{
    const int permutation = search->databases->permutation[quotient];
    const int orientation = search->databases->orientation[remainder];
    return (permutation > orientation ? permutation : orientation) - 1;
}

static bool descend(search_t *search, uint32_t index, int depth, int limit, int last)
{
    const uint32_t quotient = index / COORD_ORIENTATIONS;
    const uint32_t remainder = index % COORD_ORIENTATIONS;
    if (depth + bound_of(search, quotient, remainder) > limit)
        return false;
    if (index == COORD_SOLVED_INDEX) {
        search->length = depth;
        return true;
    }

    for (int move = 0; move < COORD_MOVES; move++) {
        /* The solve moves pair each turn with its inverse at adjacent indices,
         * so `move ^ 1` is the one that would undo the previous turn. */
        if ((move ^ 1) == last)
            continue;
        search->path[depth] = cube_solve_move_chars[move];
        const uint32_t child = search->tables->perm[move][quotient]
                             + search->tables->orient[move][remainder];
        if (descend(search, child, depth + 1, limit, move))
            return true;
    }
    return false;
}

solve_status_t ida_solve_index(const coord_tables_t *tables, const ida_databases_t *databases,
                               uint32_t start, solution_t *out)
{
    *out = (solution_t){0};
    out->peak_bytes = sizeof *databases;
    if (start == COORD_SOLVED_INDEX)
        return SOLVE_OK;

    search_t search = {.tables = tables, .databases = databases, .path = {0}, .length = 0};
    const int first = bound_of(&search, start / COORD_ORIENTATIONS, start % COORD_ORIENTATIONS);

    for (int limit = first; limit <= SOLVER_MAX_DEPTH; limit++)
        if (descend(&search, start, 0, limit, -1)) {
            memcpy(out->moves, search.path, (size_t)search.length);
            out->moves[search.length] = '\0';
            out->length = search.length;
            return SOLVE_OK;
        }
    return SOLVE_NO_SOLUTION;
}

solve_status_t ida_solve(const coord_tables_t *tables, const ida_databases_t *databases,
                         cube_t state, solution_t *out)
{
    uint32_t start;
    if (coords_encode(tables, state, &start) != COORD_OK) {
        *out = (solution_t){0};
        return SOLVE_INVALID;
    }
    return ida_solve_index(tables, databases, start, out);
}
