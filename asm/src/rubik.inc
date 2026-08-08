/* rubik.inc -- sizes shared by every translation unit.
 *
 * Nothing here is a policy decision; these are the numbers the problem hands
 * us.  Keeping them in one place means the assembler, not the reader, is the
 * one who has to remember that a permutation row is eight slots wide.
 */

    .equ FACELETS,      24          /* stickers on a 2x2x2 */
    .equ NCOLORS,       6
    .equ NMOVES,        12          /* l L r R t T b B f F a A */
    .equ NSOLVE,        6           /* l L t T f F -- the solver alphabet */
    .equ NFREE,         7           /* corner slots that are not the fixed one */
    .equ NPERM,         5040        /* 7! */
    .equ NORIENT,       729         /* 3^6 */
    .equ NSTATES,       3674160     /* 5040 * 729 */
    .equ NROT,          24          /* whole-cube rotations */
    .equ SOLVED_INDEX,  0
    .equ MAXDEPTH,      14          /* God's number, quarter-turn metric */
    .equ MOVEBUF,       32          /* room for a solution string + NUL */

/* q = index / 729 by reciprocal multiply: floor(index * MAGIC729 / 2^32).
 * MAGIC729 = floor(2^32 / 729) + 1; the error term (356) times the largest
 * index (3674159) stays below 2^32, so the result is exact over our range. */
    .equ MAGIC729,      5891588

/* Rows are padded from six entries to eight so that a row address is a shift
 * of the coordinate rather than a multiply, and so a whole row is one or two
 * aligned SSE loads. */
    .equ PERMROW_SH,    5           /* perm_wide:    32 bytes = 8 x uint32 */
    .equ CHILDROW_SH,   4           /* perm_child:   16 bytes = 8 x uint16 */
    .equ ORIENTROW_SH,  5           /* orient_child: 32 bytes = 8 x uint32 */

/* coords_encode status codes. */
    .equ CE_OK,         0
    .equ CE_CORNER,     1
    .equ CE_PIECE,      2
    .equ CE_REPEAT,     3
    .equ CE_TWIST,      4

/* A solution: the move string, its length, and the bytes the algorithm had to
 * keep live to produce it (what the benchmark reports as peak_mb). */
    .equ SOL_MOVES,     0
    .equ SOL_LEN,       32
    .equ SOL_PEAK,      40
    .equ SOL_SIZE,      48

/* solver status codes. */
    .equ SV_OK,         0
    .equ SV_INVALID,    1
    .equ SV_NOSOL,      2
    .equ SV_NOMEM,      3

/* BFS mark word layout (see bfs.s). */
    .equ MARK_MOVEMASK, 7
    .equ MARK_ROOT,     7
    .equ MARK_PARSH,    3
    .equ MARK_PARMASK,  0x3FFFFF
    .equ MARK_SIDEBIT,  0x02000000
    .equ MARK_GENMASK,  0xFC000000
    .equ MARK_GENSH,    26
    .equ MARK_MAXGEN,   62
