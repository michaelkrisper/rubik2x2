/* bfs.s -- bidirectional breadth-first search.  No precomputation at all.
 *
 * Level sizes peak at 1,350,852 states at depth 11, so a one-sided search
 * would have to walk most of the space; meeting in the middle needs depth 7
 * from each side, where only 33,058 states live.  Expanding by whole levels is
 * what makes the answer optimal: when the frontiers touch, every shorter
 * pairing has already been tried.
 *
 * The visited set is a flat array indexed by the state itself -- a state is
 * already a dense integer below 3,674,160, so hashing it would be hashing a
 * perfect index.  One uint32 per state carries everything the search needs:
 *
 *   bits  0..2   the move that arrived here (7 = a frontier root)
 *   bits  3..24  the state it arrived from (3,674,160 < 2^22)
 *   bit   25     which frontier claimed it
 *   bits 26..31  generation stamp
 *
 * The generation stamp is what makes reusing the 14.7 MB array cheap: a solve
 * bumps the counter instead of clearing the array, so marks left by earlier
 * solves read as unvisited.  Six bits give 62 solves between real clears, and
 * a clear costs about as much as one solve.
 */

    .intel_syntax noprefix
    .include "rubik.inc"

/* Levels cannot exceed 114,149 (the depth-8 shell) before the two sides must
 * have met, since the diameter is 14 and the search keeps them balanced. */
    .equ BFS_CAP,   131072
    .equ BFS_PEAK,  NSTATES*4 + 3*BFS_CAP*4

/* frame offsets inside bfs_solve_index */
    .equ F_BUFA,    0
    .equ F_BUFB,    32
    .equ F_BUFC,    64
    .equ F_FWD,     96
    .equ F_BWD,     104
    .equ F_NXT,     112
    .equ F_FWDN,    120
    .equ F_BWDN,    124
    .equ F_LEVN,    128
    .equ F_NODE,    132
    .equ F_MEET,    136
    .equ F_BY,      140
    .equ F_STAMP,   144
    .equ F_FROMGOAL,148
    .equ F_NXTEND,  152
    .equ F_SOL,     160
    .equ F_GEN,     168
    .equ F_SIDE,    172
    .equ F_DEPTH,   176
    .equ F_SIZE,    200

    .text

/* One child.  r10/r11 = the two move rows, r12 = marks, rdi = next-level
 * write pointer, rbp = gen|side|parent<<3, r8d = side, r9d = generation. */
    .macro CHILD m
    mov     eax, [r10 + \m*4]
    add     eax, [r11 + \m*4]
    mov     ecx, [r12 + rax*4]
    mov     edx, ecx
    and     edx, MARK_GENMASK
    cmp     edx, r9d
    je      7000f
    mov     edx, ebp
    or      edx, \m
    mov     [r12 + rax*4], edx
    cmp     rdi, [rsp + F_NXTEND]
    jae     .Lbfs_overflow
    mov     [rdi], eax
    add     rdi, 4
    jmp     7001f
7000:
    and     ecx, MARK_SIDEBIT
    cmp     ecx, r8d
    je      7001f                           /* this side has already been here */
    mov     [rsp + F_MEET], eax
    mov     dword ptr [rsp + F_BY], \m
    jmp     .Lbfs_found
7001:
    .endm

/* bfs_prepare() -> rax marks, or 0.  One allocation for the life of the
 * process; the benchmark's per-solve cost must not include it. */
    .globl bfs_prepare
bfs_prepare:
    mov     rax, [rip + bfs_marks]
    test    rax, rax
    jne     2f
    sub     rsp, 8
    mov     edi, NSTATES
    mov     esi, 4
    call    calloc@PLT
    test    rax, rax
    je      1f
    mov     [rip + bfs_marks], rax
    mov     edi, 3*BFS_CAP
    mov     esi, 4
    call    calloc@PLT
    test    rax, rax
    je      1f
    mov     [rip + bfs_levels], rax
    mov     rax, [rip + bfs_marks]
1:  add     rsp, 8
2:  ret

/* bfs_trail(edi=node, rsi=out) -> eax length.  The moves from this node's
 * frontier root down to it, in root-first order. */
bfs_trail:
    mov     r8, [rip + bfs_marks]
    lea     r9, [rip + solve_move_chars]
    xor     ecx, ecx
1:  mov     eax, [r8 + rdi*4]
    mov     edx, eax
    and     edx, MARK_MOVEMASK
    cmp     edx, MARK_ROOT
    je      2f
    movzx   edx, byte ptr [r9 + rdx]
    mov     [rsi + rcx], dl
    inc     ecx
    shr     eax, MARK_PARSH
    and     eax, MARK_PARMASK
    mov     edi, eax
    jmp     1b
2:  xor     edx, edx                        /* reverse in place */
    mov     eax, ecx
3:  dec     eax
    cmp     edx, eax
    jae     4f
    movzx   r10d, byte ptr [rsi + rdx]
    movzx   r11d, byte ptr [rsi + rax]
    mov     [rsi + rdx], r11b
    mov     [rsi + rax], r10b
    inc     edx
    jmp     3b
4:  mov     byte ptr [rsi + rcx], 0
    mov     eax, ecx
    ret

/* bfs_solve_index(edi=index, rsi=solution) -> eax status */
    .globl bfs_solve_index
bfs_solve_index:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, F_SIZE
    mov     [rsp + F_SOL], rsi
    mov     qword ptr [rsi + SOL_PEAK], BFS_PEAK
    mov     dword ptr [rsi + SOL_LEN], 0
    mov     byte ptr [rsi + SOL_MOVES], 0
    mov     ebx, edi
    test    ebx, ebx
    je      .Lbfs_ok                        /* already solved */

    call    bfs_prepare
    test    rax, rax
    je      .Lbfs_nomem
    mov     r12, rax

    /* stamp: bump the generation, clearing only when six bits run out */
    mov     eax, [rip + bfs_gen]
    cmp     eax, MARK_MAXGEN
    jb      1f
    mov     rdi, r12
    xor     esi, esi
    mov     edx, NSTATES*4
    call    memset@PLT
    xor     eax, eax
1:  inc     eax
    mov     [rip + bfs_gen], eax
    shl     eax, MARK_GENSH
    mov     [rsp + F_GEN], eax
    mov     r9d, eax

    mov     edx, eax
    or      edx, MARK_ROOT
    mov     [r12 + rbx*4], edx              /* the scramble roots the forward tree */
    mov     edx, eax
    or      edx, MARK_ROOT | MARK_SIDEBIT
    mov     [r12 + SOLVED_INDEX*4], edx

    mov     rax, [rip + bfs_levels]
    mov     [rsp + F_FWD], rax
    mov     [rax], ebx                      /* forward[0] = start */
    lea     rcx, [rax + BFS_CAP*4]
    mov     [rsp + F_BWD], rcx
    mov     dword ptr [rcx], SOLVED_INDEX
    lea     rcx, [rax + 2*BFS_CAP*4]
    mov     [rsp + F_NXT], rcx
    mov     dword ptr [rsp + F_FWDN], 1
    mov     dword ptr [rsp + F_BWDN], 1

    lea     r14, [rip + perm_wide]
    lea     r15, [rip + orient_child]
    mov     dword ptr [rsp + F_MEET], -1
    mov     dword ptr [rsp + F_DEPTH], 0

.Lbfs_level:
    cmp     dword ptr [rsp + F_DEPTH], MAXDEPTH
    jae     .Lbfs_nosol
    /* always grow the cheaper side, which keeps both halves shallow */
    mov     eax, [rsp + F_BWDN]
    cmp     eax, [rsp + F_FWDN]
    jb      1f
    xor     ecx, ecx                        /* forward */
    mov     rsi, [rsp + F_FWD]
    mov     eax, [rsp + F_FWDN]
    xor     r8d, r8d
    jmp     2f
1:  mov     ecx, 1                          /* backward */
    mov     rsi, [rsp + F_BWD]
    mov     eax, [rsp + F_BWDN]
    mov     r8d, MARK_SIDEBIT
2:  mov     [rsp + F_FROMGOAL], ecx
    mov     [rsp + F_LEVN], eax
    mov     [rsp + F_SIDE], r8d
    mov     r9d, [rsp + F_GEN]
    mov     ecx, r9d
    or      ecx, r8d
    mov     [rsp + F_STAMP], ecx
    mov     rdi, [rsp + F_NXT]
    lea     rax, [rdi + BFS_CAP*4]
    mov     [rsp + F_NXTEND], rax
    mov     r13, rsi                        /* level base -- depth is done with */
    xor     ebx, ebx

.Lbfs_node:
    cmp     ebx, [rsp + F_LEVN]
    jae     .Lbfs_endlevel
    mov     ecx, [r13 + rbx*4]
    mov     [rsp + F_NODE], ecx
    mov     ebp, [rsp + F_STAMP]
    mov     eax, ecx
    shl     eax, MARK_PARSH
    or      ebp, eax
    mov     eax, ecx
    imul    rax, rax, MAGIC729
    shr     rax, 32                         /* q */
    mov     edx, eax
    imul    edx, edx, NORIENT
    sub     ecx, edx                        /* r */
    shl     rax, PERMROW_SH
    lea     r10, [r14 + rax]
    mov     eax, ecx
    shl     rax, ORIENTROW_SH
    lea     r11, [r15 + rax]
    CHILD 0
    CHILD 1
    CHILD 2
    CHILD 3
    CHILD 4
    CHILD 5
    inc     ebx
    jmp     .Lbfs_node

.Lbfs_endlevel:
    /* swap the produced level in, and hand the old one back as scratch */
    mov     rax, [rsp + F_NXT]
    sub     rdi, rax
    shr     rdi, 2                          /* how many were produced */
    cmp     dword ptr [rsp + F_FROMGOAL], 0
    jne     1f
    mov     rcx, [rsp + F_FWD]
    mov     [rsp + F_NXT], rcx
    mov     [rsp + F_FWD], rax
    mov     [rsp + F_FWDN], edi
    jmp     2f
1:  mov     rcx, [rsp + F_BWD]
    mov     [rsp + F_NXT], rcx
    mov     [rsp + F_BWD], rax
    mov     [rsp + F_BWDN], edi
2:  cmp     dword ptr [rsp + F_FWDN], 0
    je      .Lbfs_nosol
    cmp     dword ptr [rsp + F_BWDN], 0
    je      .Lbfs_nosol
    inc     dword ptr [rsp + F_DEPTH]
    jmp     .Lbfs_level

.Lbfs_found:
    /* reached: this side's root -> the meeting point.
     * opposite: the other side's root -> the same place. */
    mov     edi, [rsp + F_NODE]
    lea     rsi, [rsp + F_BUFA]
    call    bfs_trail
    mov     ecx, [rsp + F_BY]
    lea     rdx, [rip + solve_move_chars]
    movzx   ecx, byte ptr [rdx + rcx]
    lea     rsi, [rsp + F_BUFA]
    mov     [rsi + rax], cl
    inc     eax
    mov     byte ptr [rsi + rax], 0
    mov     edi, [rsp + F_MEET]
    lea     rsi, [rsp + F_BUFB]
    call    bfs_trail

    mov     r13, [rsp + F_SOL]
    cmp     dword ptr [rsp + F_FROMGOAL], 0
    jne     1f
    /* forward side met: start -> meeting, then back out to solved */
    lea     rdi, [r13 + SOL_MOVES]
    lea     rsi, [rsp + F_BUFA]
    call    .Lbfs_append
    lea     rdi, [rsp + F_BUFC]
    lea     rsi, [rsp + F_BUFB]
    call    cube_invert
    lea     rdi, [r13 + SOL_MOVES]
    lea     rsi, [rsp + F_BUFC]
    call    .Lbfs_append
    jmp     2f
1:  lea     rdi, [r13 + SOL_MOVES]
    lea     rsi, [rsp + F_BUFB]
    call    .Lbfs_append
    lea     rdi, [rsp + F_BUFC]
    lea     rsi, [rsp + F_BUFA]
    call    cube_invert
    lea     rdi, [r13 + SOL_MOVES]
    lea     rsi, [rsp + F_BUFC]
    call    .Lbfs_append
2:  lea     rdi, [r13 + SOL_MOVES]
    xor     ecx, ecx
3:  cmp     byte ptr [rdi + rcx], 0
    je      4f
    inc     ecx
    jmp     3b
4:  mov     [r13 + SOL_LEN], ecx
.Lbfs_ok:
    xor     eax, eax
    jmp     .Lbfs_out
.Lbfs_nosol:
    mov     eax, SV_NOSOL
    jmp     .Lbfs_out
.Lbfs_overflow:
.Lbfs_nomem:
    mov     eax, SV_NOMEM
.Lbfs_out:
    add     rsp, F_SIZE
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret

/* append the NUL-terminated rsi onto the NUL-terminated rdi */
.Lbfs_append:
    xor     ecx, ecx
1:  cmp     byte ptr [rdi + rcx], 0
    je      2f
    inc     ecx
    jmp     1b
2:  xor     edx, edx
3:  movzx   eax, byte ptr [rsi + rdx]
    mov     [rdi + rcx], al
    test    al, al
    je      4f
    inc     ecx
    inc     edx
    jmp     3b
4:  ret

/* bfs_solve(rdi=state24, rsi=solution) -> eax status */
    .globl bfs_solve
bfs_solve:
    push    rbx
    sub     rsp, 32
    mov     rbx, rsi
    lea     rsi, [rsp]
    call    coords_encode
    test    eax, eax
    jne     1f
    mov     edi, [rsp]
    mov     rsi, rbx
    call    bfs_solve_index
    jmp     2f
1:  mov     dword ptr [rbx + SOL_LEN], 0
    mov     byte ptr [rbx + SOL_MOVES], 0
    mov     eax, SV_INVALID
2:  add     rsp, 32
    pop     rbx
    ret

    .bss
    .balign 64
    .globl bfs_marks
bfs_marks:      .space 8
bfs_levels:     .space 8
bfs_gen:        .space 4

    .section .note.GNU-stack,"",@progbits
