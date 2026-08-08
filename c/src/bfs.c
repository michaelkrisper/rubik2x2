#include "bfs.h"

#include <stdlib.h>
#include <string.h>

/* Frontier entries of lookahead for the mark prefetch. Measured. */
#define BFS_LOOKAHEAD 6

/* How a visited state is recorded, packed into one uint32_t:
 *
 *     bits  0..3   its distance from this frontier's root
 *     bit   4      which frontier claimed it
 *     bits  5..31  generation stamp
 *
 * A cube state is already a dense integer below COORD_STATES, so the visited set
 * wants no hash table: one flat array subscripted by the state itself answers
 * "seen, by which side, how far in" in a single load, and records a child in a
 * single store of a value that is constant for the whole level. The previous
 * version kept two open-addressed sets and a node pool per solve; hashing and
 * probing a number that was already a perfect index dominated the run time.
 *
 * The array is 14.7 MB, far too big to clear per solve, so marks carry a
 * generation stamp: a solve bumps a counter and everything left by earlier
 * solves reads as unvisited. That is why the mark stores a *distance* and not a
 * parent state: a parent needs 22 bits and leaves only 6 for the stamp, so every
 * 63rd solve pays to zero the whole array -- a fifth of the average solve, all
 * of it memset. A distance needs 4 bits, leaves 27, and the clear happens once
 * in 134 million solves, which is to say never.
 *
 * Distances are enough to rebuild the path because breadth-first marks are exact:
 * from a node at distance d, exactly the neighbours at d-1 on the same side are
 * parents, and there are only six neighbours to try. bfs_trail walks back that
 * way -- a few dozen lookups once per solve, against a saved store on every one
 * of ~150,000 edges.
 */
enum {
    MARK_DEPTH_BITS = 4,             /* SOLVER_MAX_DEPTH fits, and levels stop at 8 */
    MARK_DEPTH_MASK = (1 << MARK_DEPTH_BITS) - 1,
    MARK_SIDE_POS = MARK_DEPTH_BITS,
    MARK_GEN_POS = MARK_SIDE_POS + 1,
};

#define MARK_SIDE_BIT ((uint16_t)1 << MARK_SIDE_POS)
#define MARK_GEN_MASK ((uint16_t)~(MARK_SIDE_BIT | (uint16_t)MARK_DEPTH_MASK))
#define MARK_MAX_GEN ((UINT16_MAX >> MARK_GEN_POS) - 1)

/* One breadth-first level: a plain array of states, reused across solves. Three
 * of them rotate -- the two frontiers and the level being built. */
typedef struct {
    uint32_t *at;
    uint32_t count;
    uint32_t capacity;
} level_t;

/* The working set of one solve, kept alive between solves so the mark array is
 * allocated once. bfs_solve is not thread-safe; nothing in this program solves
 * concurrently, and a 14.7 MB allocation per solve would cost far more than the
 * search it serves. bfs_release hands it all back. */
static uint16_t *bfs_marks;
static uint32_t bfs_generation;
static level_t bfs_level[3];

static bool level_reserve(level_t *level, uint32_t wanted)
{
    if (wanted <= level->capacity)
        return true;
    uint32_t capacity = level->capacity ? level->capacity : 4096;
    while (capacity < wanted)
        capacity *= 2;
    uint32_t *const grown = realloc(level->at, (size_t)capacity * sizeof *grown);
    if (!grown)
        return false;
    level->at = grown;
    level->capacity = capacity;
    return true;
}

/* Begin a solve; yields the stamp this solve's marks carry. */
static bool bfs_stamp(uint32_t *generation)
{
    if (!bfs_marks) {
        bfs_marks = calloc(COORD_STATES, sizeof *bfs_marks);
        if (!bfs_marks)
            return false;
    }
    if (bfs_generation >= MARK_MAX_GEN) {
        memset(bfs_marks, 0, (size_t)COORD_STATES * sizeof *bfs_marks);
        bfs_generation = 0;
    }
    bfs_generation++;
    *generation = bfs_generation << MARK_GEN_POS;
    return true;
}

void bfs_release(void)
{
    free(bfs_marks);
    bfs_marks = nullptr;
    bfs_generation = 0;
    for (int i = 0; i < 3; i++) {
        free(bfs_level[i].at);
        bfs_level[i] = (level_t){0};
    }
}

static size_t bfs_bytes(void)
{
    size_t bytes = (size_t)COORD_STATES * sizeof *bfs_marks;
    for (int i = 0; i < 3; i++)
        bytes += (size_t)bfs_level[i].capacity * sizeof *bfs_level[i].at;
    return bytes;
}

/* The moves from `node`'s frontier root down to `node`, in root-first order.
 *
 * `tag` is the generation and side bit the frontier's marks carry. A neighbour
 * one step shallower under the same tag is a parent by construction, and the
 * move reaching `node` from it is the inverse of the one that got there --
 * adjacent indices, which is why the six solve moves are ordered as they are.
 */
static int bfs_trail(const coord_tables_t *tables, uint32_t node, uint32_t tag, char *out)
{
    char reversed[SOLVER_MOVES_SIZE];
    int length = 0;
    for (uint32_t depth = bfs_marks[node] & MARK_DEPTH_MASK; depth > 0; depth--) {
        const uint32_t parent_mark = tag | (depth - 1);
        for (int move = 0; move < COORD_MOVES; move++) {
            const uint32_t back = coords_apply(tables, move ^ 1, node);
            if (bfs_marks[back] == parent_mark) {
                reversed[length++] = cube_solve_move_chars[move];
                node = back;
                break;
            }
        }
    }
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

    uint32_t generation;
    if (!bfs_stamp(&generation) || !level_reserve(&bfs_level[0], 1)
        || !level_reserve(&bfs_level[1], 1))
        return SOLVE_OUT_OF_MEMORY;

    uint16_t *const marks = bfs_marks;
    marks[start] = generation;
    marks[COORD_SOLVED_INDEX] = generation | MARK_SIDE_BIT;

    level_t *forward = &bfs_level[0], *backward = &bfs_level[1], *next = &bfs_level[2];
    forward->count = 0;
    forward->at[forward->count++] = start;
    backward->count = 0;
    backward->at[backward->count++] = COORD_SOLVED_INDEX;

    /* The two sides advance independently -- each round grows only one of them --
     * so each carries its own level depth, which is what the marks record. */
    uint32_t forward_depth = 0, backward_depth = 0;

    solve_status_t status = SOLVE_NO_SOLUTION;
    for (int round = 0; round < SOLVER_MAX_DEPTH; round++) {
        /* Always grow the cheaper side, which keeps both halves shallow. */
        const bool from_goal = backward->count < forward->count;
        level_t *const level = from_goal ? backward : forward;
        uint32_t *const level_depth = from_goal ? &backward_depth : &forward_depth;
        const uint32_t side = from_goal ? MARK_SIDE_BIT : 0;
        /* Every state reached this round gets the same mark, so the store in the
         * inner loop is a constant. */
        const uint16_t reached = (uint16_t)(generation | side | (*level_depth + 1));

        next->count = 0;
        uint32_t meeting = 0, from = 0;
        int by = COORD_MOVES;

        /* Six children per node at most, reserved once so the inner loop can
         * store without checking. */
        if (!level_reserve(next, level->count * COORD_MOVES)) {
            status = SOLVE_OUT_OF_MEMORY;
            goto done;
        }

        for (uint32_t i = 0; i < level->count && by == COORD_MOVES; i++) {
            /* Name the mark words this loop wants a few entries from now: one
             * random word of a 14.7 MB array per edge, against 3 MiB of cache. */
            if (i + BFS_LOOKAHEAD < level->count) {
                const uint32_t ahead = level->at[i + BFS_LOOKAHEAD];
                const uint32_t *const p = tables->perm_by_q[ahead / COORD_ORIENTATIONS];
                const uint16_t *const o = tables->orient_by_r[ahead % COORD_ORIENTATIONS];
                for (int m = 0; m < COORD_MOVES; m++)
                    __builtin_prefetch(&marks[p[m] + o[m]], 1, 1);
            }
            const uint32_t node = level->at[i];
            const uint32_t *const perm = tables->perm_by_q[node / COORD_ORIENTATIONS];
            const uint16_t *const orient = tables->orient_by_r[node % COORD_ORIENTATIONS];
            for (int move = 0; move < COORD_MOVES; move++) {
                const uint32_t child = perm[move] + orient[move];
                const uint16_t mark = marks[child];
                if ((mark & MARK_GEN_MASK) == generation) {
                    if ((mark & MARK_SIDE_BIT) == side)
                        continue; /* this side has already been here */
                    meeting = child;
                    from = node;
                    by = move;
                    break;
                }
                marks[child] = reached;
                next->at[next->count++] = child;
            }
        }

        if (by != COORD_MOVES) {
            /* One trail runs from each root to the meeting point; the move that
             * joined them belongs to whichever side was expanding. */
            const uint32_t here = generation | side;
            const uint32_t there = generation | (side ^ MARK_SIDE_BIT);
            char from_start[SOLVER_MOVES_SIZE], to_goal[SOLVER_MOVES_SIZE];
            int start_length, goal_length;
            if (from_goal) {
                start_length = bfs_trail(tables, meeting, there, from_start);
                goal_length = bfs_trail(tables, from, here, to_goal);
                to_goal[goal_length++] = cube_solve_move_chars[by];
            } else {
                start_length = bfs_trail(tables, from, here, from_start);
                from_start[start_length++] = cube_solve_move_chars[by];
                goal_length = bfs_trail(tables, meeting, there, to_goal);
            }
            from_start[start_length] = '\0';
            to_goal[goal_length] = '\0';

            memcpy(out->moves, from_start, (size_t)start_length);
            cube_invert(to_goal, out->moves + start_length);
            out->length = start_length + goal_length;
            status = SOLVE_OK;
            break;
        }

        level_t *const spent = level;
        if (from_goal)
            backward = next;
        else
            forward = next;
        next = spent;
        (*level_depth)++;
        if (forward->count == 0 || backward->count == 0)
            break;
    }

done:
    out->peak_bytes = bfs_bytes();
    /* The three levels must return to their slots: they are swapped by pointer
     * above, and the next solve starts again from bfs_level[0] and [1]. */
    const level_t kept[3] = {*forward, *backward, *next};
    memcpy(bfs_level, kept, sizeof kept);
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
