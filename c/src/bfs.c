#include "bfs.h"

#include <stdlib.h>
#include <string.h>

#define NO_NODE UINT32_MAX

/* One search direction: a node pool holding the tree of reached states, plus an
 * open-addressed set of the states themselves so membership is a probe rather
 * than a scan. Both grow on demand; a side never holds more than about 160,000
 * nodes, which is why a hash set beats a 3.7 MB flag array per solve.
 *
 * The set stores the state, not the node id: a probe is then one load with no
 * detour through the node pool, and the loop that filters six children per node
 * touches nothing but the slot array. The node id for a state is only ever
 * wanted once per solve -- when the two searches meet -- and side_locate finds
 * it by scanning the pool, which is linear but runs exactly once. */
typedef struct {
    uint32_t *state;
    uint32_t *parent;
    uint8_t *move;
    uint32_t count;
    uint32_t capacity;

    uint32_t *slot;     /* state + 1; 0 means empty */
    uint32_t mask;      /* slot count - 1, always a power of two minus one */
} side_t;

static void side_free(side_t *side)
{
    free(side->state);
    free(side->parent);
    free(side->move);
    free(side->slot);
    *side = (side_t){0};
}

static size_t side_bytes(const side_t *side)
{
    return (size_t)side->capacity * (sizeof *side->state + sizeof *side->parent
                                     + sizeof *side->move)
         + ((size_t)side->mask + 1) * sizeof *side->slot;
}

static inline uint32_t hash_state(uint32_t state)
{
    return state * 0x9e3779b1u;
}

static bool side_rehash(side_t *side, uint32_t slots)
{
    uint32_t *const fresh = calloc(slots, sizeof *fresh);
    if (!fresh)
        return false;
    free(side->slot);
    side->slot = fresh;
    side->mask = slots - 1;
    for (uint32_t node = 0; node < side->count; node++) {
        uint32_t at = hash_state(side->state[node]) & side->mask;
        while (side->slot[at])
            at = (at + 1) & side->mask;
        side->slot[at] = node + 1;
    }
    return true;
}

static bool side_init(side_t *side)
{
    *side = (side_t){0};
    side->capacity = 1024;
    side->state = malloc(side->capacity * sizeof *side->state);
    side->parent = malloc(side->capacity * sizeof *side->parent);
    side->move = malloc(side->capacity * sizeof *side->move);
    if (!side->state || !side->parent || !side->move || !side_rehash(side, 2048)) {
        side_free(side);
        return false;
    }
    return true;
}

static bool side_grow(side_t *side)
{
    const uint32_t capacity = side->capacity * 2;
    uint32_t *const state = realloc(side->state, capacity * sizeof *state);
    if (state)
        side->state = state;
    uint32_t *const parent = realloc(side->parent, capacity * sizeof *parent);
    if (parent)
        side->parent = parent;
    uint8_t *const move = realloc(side->move, capacity * sizeof *move);
    if (move)
        side->move = move;
    if (!state || !parent || !move)
        return false;
    side->capacity = capacity;
    return true;
}

/* Node holding `state`, or NO_NODE. */
static inline uint32_t side_find(const side_t *side, uint32_t state)
{
    uint32_t at = hash_state(state) & side->mask;
    for (uint32_t entry; (entry = side->slot[at]); at = (at + 1) & side->mask)
        if (side->state[entry - 1] == state)
            return entry - 1;
    return NO_NODE;
}

static bool side_insert(side_t *side, uint32_t state, uint32_t parent, uint8_t move)
{
    if (side->count == side->capacity && !side_grow(side))
        return false;
    /* Keep the load factor under a half: linear probing degrades fast above it
     * and the table is tiny compared to the node pool. */
    if ((side->count + 1) * 2 > side->mask + 1 && !side_rehash(side, (side->mask + 1) * 2))
        return false;

    const uint32_t node = side->count++;
    side->state[node] = state;
    side->parent[node] = parent;
    side->move[node] = move;

    uint32_t at = hash_state(state) & side->mask;
    while (side->slot[at])
        at = (at + 1) & side->mask;
    side->slot[at] = node + 1;
    return true;
}

/* The moves from a side's root down to `node`, in root-first order. */
static int side_path(const side_t *side, uint32_t node, const char *alphabet, char *out)
{
    char reversed[SOLVER_MOVES_SIZE];
    int length = 0;
    for (uint32_t at = node; side->parent[at] != NO_NODE; at = side->parent[at])
        reversed[length++] = alphabet[side->move[at]];
    for (int i = 0; i < length; i++)
        out[i] = reversed[length - 1 - i];
    out[length] = '\0';
    return length;
}

solve_status_t bfs_solve_index(const coord_tables_t *tables, uint32_t start, solution_t *out)
{
    *out = (solution_t){0};
    if (start == COORD_SOLVED_INDEX)
        return SOLVE_OK;

    side_t forward, backward;
    if (!side_init(&forward))
        return SOLVE_OUT_OF_MEMORY;
    if (!side_init(&backward)) {
        side_free(&forward);
        return SOLVE_OUT_OF_MEMORY;
    }

    solve_status_t status = SOLVE_NO_SOLUTION;
    if (!side_insert(&forward, start, NO_NODE, 0)
        || !side_insert(&backward, COORD_SOLVED_INDEX, NO_NODE, 0))
        status = SOLVE_OUT_OF_MEMORY;

    /* Half-open node ranges [begin, end) delimit the current level of each side. */
    uint32_t forward_begin = 0, forward_end = 1;
    uint32_t backward_begin = 0, backward_end = 1;

    for (int depth = 0; status == SOLVE_NO_SOLUTION && depth < SOLVER_MAX_DEPTH; depth++) {
        /* Always grow the cheaper side, which keeps both halves shallow. */
        const bool from_goal =
            (backward_end - backward_begin) < (forward_end - forward_begin);
        side_t *const side = from_goal ? &backward : &forward;
        const side_t *const other = from_goal ? &forward : &backward;
        const uint32_t begin = from_goal ? backward_begin : forward_begin;
        const uint32_t end = from_goal ? backward_end : forward_end;

        uint32_t meeting_here = NO_NODE, meeting_there = NO_NODE;
        uint8_t meeting_move = 0;

        for (uint32_t node = begin; node < end && meeting_here == NO_NODE; node++) {
            const uint32_t index = side->state[node];
            const uint32_t *const perm = tables->perm_by_q[index / COORD_ORIENTATIONS];
            const uint16_t *const orient = tables->orient_by_r[index % COORD_ORIENTATIONS];
            for (uint8_t move = 0; move < COORD_MOVES; move++) {
                const uint32_t child = perm[move] + orient[move];
                if (side_find(side, child) != NO_NODE)
                    continue;
                const uint32_t there = side_find(other, child);
                if (there != NO_NODE) {
                    meeting_here = node;
                    meeting_there = there;
                    meeting_move = move;
                    break;
                }
                if (!side_insert(side, child, node, move)) {
                    status = SOLVE_OUT_OF_MEMORY;
                    goto done;
                }
            }
        }

        if (meeting_here != NO_NODE) {
            char from_start[SOLVER_MOVES_SIZE], to_goal[SOLVER_MOVES_SIZE];
            int start_length, goal_length;
            if (from_goal) {
                start_length = side_path(&forward, meeting_there, cube_solve_move_chars,
                                         from_start);
                goal_length = side_path(&backward, meeting_here, cube_solve_move_chars, to_goal);
                to_goal[goal_length++] = cube_solve_move_chars[meeting_move];
            } else {
                start_length = side_path(&forward, meeting_here, cube_solve_move_chars,
                                         from_start);
                from_start[start_length++] = cube_solve_move_chars[meeting_move];
                goal_length = side_path(&backward, meeting_there, cube_solve_move_chars, to_goal);
            }
            from_start[start_length] = '\0';
            to_goal[goal_length] = '\0';

            memcpy(out->moves, from_start, (size_t)start_length);
            cube_invert(to_goal, out->moves + start_length);
            out->length = start_length + goal_length;
            status = SOLVE_OK;
            break;
        }

        if (from_goal) {
            backward_begin = backward_end;
            backward_end = backward.count;
            if (backward_begin == backward_end)
                break;
        } else {
            forward_begin = forward_end;
            forward_end = forward.count;
            if (forward_begin == forward_end)
                break;
        }
    }

done:
    out->peak_bytes = side_bytes(&forward) + side_bytes(&backward);
    side_free(&forward);
    side_free(&backward);
    return status;
}

solve_status_t bfs_solve(const coord_tables_t *tables, cube_t state, solution_t *out)
{
    uint32_t start;
    if (coords_encode(tables, state, &start) != COORD_OK) {
        *out = (solution_t){0};
        return SOLVE_INVALID;
    }
    return bfs_solve_index(tables, start, out);
}
