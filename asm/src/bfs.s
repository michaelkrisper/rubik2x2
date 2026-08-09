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
 * perfect index.  One uint16 per state is enough:
 *
 *   bits  0..3   depth within this frontier
 *   bit   4      which frontier claimed it
 *   bits  5..15  generation stamp
 *
 * Sixteen bits rather than thirty-two is the single biggest thing in this file.
 * The array is touched once per edge at a random offset, so the search is bound
 * by cache misses on it; halving it from 14.7 MB to 7.3 MB is worth more than
 * any instruction selection nearby.  Storing a *depth* instead of a parent is
 * what buys the room: the path is recovered afterwards by stepping to whichever
 * neighbour is one level shallower, a few dozen lookups against 150,000 edges
 * saved.  It also makes the store loop-invariant -- every state reached in one
 * round gets the same word -- so the inner loop writes a register it already
 * holds.
 *
 * The generation stamp keeps reuse cheap: a solve bumps the counter instead of
 * clearing the array, so marks left by earlier solves read as unvisited.  Eleven
 * bits give 2047 solves between real clears.
 */

    .intel_syntax noprefix
    .include "rubik.inc"

/* Levels cannot exceed 114,149 (the depth-8 shell) before the two sides must
 * have met, since the diameter is 14 and the search keeps them balanced. */
    .equ BFS_CAP,   131072

/* The mark array lives on 2 MB pages: 7.3 MB of random access is ~1,800
 * ordinary pages -- more than any dTLB holds, so nearly every mark load also
 * paid a page walk (perf: 47% of the process's dTLB misses were this loop).
 * Rounded up to a multiple of 2 MB it is four TLB entries. */
    .equ HUGE_2M,   0x200000
    .equ MARKS_HUGE,(NSTATES*2 + HUGE_2M - 1) & -HUGE_2M
    .equ BFS_PEAK,  MARKS_HUGE + 3*BFS_CAP*4

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
    .equ F_LEVDEPTH,188
    .equ F_FWDD,    180              /* depth reached by each frontier */
    .equ F_BWDD,    184
    .equ F_SIZE,    200

    .text

/* One child.  r10/r11 = the two move rows, r12 = marks, rdi = next-level
 * write pointer, rbp = gen|side|(depth+1) -- the loop-invariant mark value --
 * r8d = side, r9d = generation.
 *
 * Laid out so the common case is straight-line.
 *
 * Nearly every edge reaches a state nobody has claimed yet, so that path falls
 * through with no taken branch; the already-seen handling lives out of line at
 * .Lseen\m and jumps back. The room-for-six check is hoisted to once per node
 * and kept in rsi, which costs one register-to-register compare instead of six
 * reloads of the limit from the stack. Both shapes were copied from what GCC
 * does to the equivalent C, after a disassembly comparison showed the
 * hand-written version taking an extra jump and an extra stack load per edge. */
    .macro CHILD m
    mov     eax, [r10 + \m*4]
    add     eax, [r11 + \m*4]
    movzx   ecx, word ptr [r12 + rax*2]
    mov     edx, ecx
    and     edx, MARK_GENMASK
    cmp     edx, r9d
    je      .Lseen\m
    mov     [r12 + rax*2], bp               /* the round's constant mark */
    mov     [rdi], eax
    add     rdi, 4
.Lback\m:
    .endm

/* The out-of-line half of CHILD, one per move index. */
    .macro CHILD_SEEN m
.Lseen\m:
    and     ecx, MARK_SIDEBIT
    cmp     ecx, r8d
    je      .Lback\m                        /* this side has already been here */
    mov     [rsp + F_MEET], eax
    mov     dword ptr [rsp + F_BY], \m
    jmp     .Lbfs_found
    .endm

/* alloc_huge(rdi = bytes, a multiple of 2 MB) -> rax: a 2 MB-aligned, zeroed,
 * MADV_HUGEPAGE'd mapping, or 0.  mmap gives no alignment promise, so map one
 * spare huge page, trim the unaligned head and whatever tail is left, and only
 * then advise the kernel -- with THP in madvise mode the huge pages arrive at
 * first fault, exactly when the memset-free zeroed mapping is touched. */
alloc_huge:
    push    rbx
    push    r12
    push    r13
    mov     rbx, rdi                    /* bytes */
    lea     rsi, [rdi + HUGE_2M]        /* over-map to guarantee alignment */
    xor     edi, edi
    mov     edx, 3                      /* PROT_READ|PROT_WRITE */
    mov     r10d, 0x22                  /* MAP_PRIVATE|MAP_ANONYMOUS */
    mov     r8, -1
    xor     r9d, r9d
    mov     eax, 9                      /* mmap */
    syscall
    cmp     rax, -4095
    jae     8f
    mov     r12, rax                    /* raw base */
    lea     r13, [rax + HUGE_2M - 1]
    and     r13, -HUGE_2M               /* aligned base */
    mov     rsi, r13
    sub     rsi, r12                    /* unaligned head */
    je      1f
    mov     rdi, r12
    mov     eax, 11                     /* munmap the head */
    syscall
1:  lea     rdi, [r13 + rbx]            /* past the aligned block */
    lea     rsi, [r12 + HUGE_2M]
    sub     rsi, r13                    /* head + tail = one spare page */
    mov     eax, 11                     /* munmap the tail */
    syscall
    mov     rdi, r13
    mov     rsi, rbx
    mov     edx, 14                     /* MADV_HUGEPAGE */
    mov     eax, 28                     /* madvise; best effort, result ignored */
    syscall
    mov     rax, r13
9:  pop     r13
    pop     r12
    pop     rbx
    ret
8:  xor     eax, eax
    jmp     9b

/* bfs_prepare() -> rax marks, or 0.  One allocation for the life of the
 * process; the benchmark's per-solve cost must not include it. */
    .globl bfs_prepare
bfs_prepare:
    mov     rax, [rip + bfs_marks]
    test    rax, rax
    jne     2f
    sub     rsp, 8
    mov     edi, MARKS_HUGE
    call    alloc_huge
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

/* bfs_trail(edi=node, esi=tag, rdx=out) -> eax length.
 *
 * The moves from this node's frontier root down to it, root first. There is no
 * parent stored -- the mark carries only a depth -- so each step asks which of
 * the six neighbours is marked one level shallower on this side. That is at
 * most 6 lookups per move and about 40 per solve, the price of the 7.3 MB the
 * narrower mark saves in the loop that runs 150,000 times.
 *
 * back = apply(node, move^1), so if `back` is the shallower one, the move that
 * led from it to `node` is `move`.
 */
bfs_trail:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    mov     r8, [rip + bfs_marks]
    lea     r9, [rip + solve_move_chars]
    lea     r14, [rip + perm_wide]
    lea     r15, [rip + orient_child]
    mov     r13, rdx                        /* out */
    mov     r12d, esi                       /* tag = generation | side */
    xor     ecx, ecx                        /* length */

1:  movzx   eax, word ptr [r8 + rdi*2]
    and     eax, MARK_DEPTHMASK
    test    eax, eax
    je      5f                              /* reached this frontier's root */
    lea     r10d, [r12 + rax - 1]           /* the mark a parent must carry */

    /* the two move rows for the current node */
    mov     edx, edi
    mov     eax, edi
    imul    rax, rax, MAGIC729
    shr     rax, 32
    mov     ebp, eax
    imul    ebp, ebp, NORIENT
    sub     edx, ebp                        /* r */
    shl     rax, PERMROW_SH
    add     rax, r14                        /* permutation row */
    mov     ebp, edx
    shl     rbp, ORIENTROW_SH
    add     rbp, r15                        /* orientation row */

    xor     ebx, ebx                        /* move */
2:  mov     edx, ebx
    xor     edx, 1                          /* the inverse turn */
    mov     r11d, [rax + rdx*4]
    add     r11d, [rbp + rdx*4]             /* back = apply(node, move^1) */
    movzx   edx, word ptr [r8 + r11*2]
    cmp     edx, r10d
    je      3f
    inc     ebx
    cmp     ebx, NSOLVE
    jb      2b
    jmp     5f                              /* unreachable for a valid table */
3:  movzx   edx, byte ptr [r9 + rbx]
    mov     [r13 + rcx], dl
    inc     ecx
    mov     edi, r11d
    jmp     1b

5:  test    ecx, ecx                        /* a root has no trail at all */
    je      7f
    xor     edx, edx                        /* reverse in place */
    mov     eax, ecx
6:  dec     eax
    cmp     edx, eax
    jae     7f
    movzx   r10d, byte ptr [r13 + rdx]
    movzx   r11d, byte ptr [r13 + rax]
    mov     [r13 + rdx], r11b
    mov     [r13 + rax], r10b
    inc     edx
    jmp     6b
7:  mov     byte ptr [r13 + rcx], 0
    mov     eax, ecx
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
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
    mov     edx, NSTATES*2
    call    memset@PLT
    xor     eax, eax
1:  inc     eax
    mov     [rip + bfs_gen], eax
    shl     eax, MARK_GENSH
    mov     [rsp + F_GEN], eax
    mov     r9d, eax

    mov     [r12 + rbx*2], ax               /* the scramble roots the forward tree */
    mov     edx, eax
    or      edx, MARK_SIDEBIT
    mov     [r12 + SOLVED_INDEX*2], dx

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
    mov     dword ptr [rsp + F_FWDD], 0
    mov     dword ptr [rsp + F_BWDD], 0

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
    /* Every state reached this round carries the same mark, so build it once
     * here and let the inner loop store a register. */
    mov     ecx, [rsp + F_FWDD]
    test    r8d, r8d
    je      21f
    mov     ecx, [rsp + F_BWDD]
21: inc     ecx
    mov     [rsp + F_LEVDEPTH], ecx
    or      ecx, r9d
    or      ecx, r8d
    mov     [rsp + F_STAMP], ecx
    mov     rdi, [rsp + F_NXT]
    lea     rax, [rdi + BFS_CAP*4]
    mov     [rsp + F_NXTEND], rax
    mov     r13, rsi                        /* level base -- depth is done with */
    mov     ebp, [rsp + F_STAMP]            /* the round's constant mark */
    sub     rax, 24                         /* room for six children */
    mov     rsi, rax                        /* the per-node limit, in a register */
    xor     ebx, ebx

.Lbfs_node:
    cmp     ebx, [rsp + F_LEVN]
    jae     .Lbfs_endlevel
    cmp     rdi, rsi
    ja      .Lbfs_overflow
    /* Name the six mark words this loop will want a few frontier entries from
     * now. Profiling put 69% of the search in the mark load: one random word
     * of a 7.3 MB array per edge, in a machine with 3 MiB of last-level cache,
     * so the loop is waiting on memory. Recomputing an address early costs a
     * multiply and lets the misses overlap instead of stalling one at a time. */
    lea     eax, [rbx + BFS_LOOKAHEAD]
    cmp     eax, [rsp + F_LEVN]
    jae     8100f
    mov     ecx, [r13 + rax*4]
    mov     eax, ecx
    imul    rax, rax, MAGIC729
    shr     rax, 32
    mov     edx, eax
    imul    edx, edx, NORIENT
    sub     ecx, edx
    shl     rax, PERMROW_SH
    add     rax, r14
    mov     edx, ecx
    shl     rdx, ORIENTROW_SH
    add     rdx, r15
    .irp poff, 0, 4, 8, 12, 16, 20
    mov     ecx, [rax + \poff]
    add     ecx, [rdx + \poff]
    prefetcht0 [r12 + rcx*2]
    .endr
8100:
    mov     ecx, [r13 + rbx*4]
    mov     [rsp + F_NODE], ecx
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
    jmp     .Lbfs_swapped
1:  mov     rcx, [rsp + F_BWD]
    mov     [rsp + F_NXT], rcx
    mov     [rsp + F_BWD], rax
    mov     [rsp + F_BWDN], edi
.Lbfs_swapped:
    /* this side is now one level deeper. Named labels, not numbered ones: the
     * block above jumps to `2f` and would otherwise land past this. */
    mov     ecx, [rsp + F_LEVDEPTH]
    cmp     dword ptr [rsp + F_FROMGOAL], 0
    jne     .Lbfs_deeper_backward
    mov     [rsp + F_FWDD], ecx
    jmp     .Lbfs_deeper_done
.Lbfs_deeper_backward:
    mov     [rsp + F_BWDD], ecx
.Lbfs_deeper_done:
    cmp     dword ptr [rsp + F_FWDN], 0
    je      .Lbfs_nosol
    cmp     dword ptr [rsp + F_BWDN], 0
    je      .Lbfs_nosol
    inc     dword ptr [rsp + F_DEPTH]
    jmp     .Lbfs_level

    CHILD_SEEN 0
    CHILD_SEEN 1
    CHILD_SEEN 2
    CHILD_SEEN 3
    CHILD_SEEN 4
    CHILD_SEEN 5

.Lbfs_found:
    /* reached: this side's root -> the meeting point.
     * opposite: the other side's root -> the same place. */
    mov     edi, [rsp + F_NODE]
    mov     esi, [rsp + F_GEN]
    or      esi, [rsp + F_SIDE]             /* the expanding side's tag */
    lea     rdx, [rsp + F_BUFA]
    call    bfs_trail
    mov     ecx, [rsp + F_BY]
    lea     rdx, [rip + solve_move_chars]
    movzx   ecx, byte ptr [rdx + rcx]
    lea     rsi, [rsp + F_BUFA]
    mov     [rsi + rax], cl
    inc     eax
    mov     byte ptr [rsi + rax], 0
    mov     edi, [rsp + F_MEET]
    mov     esi, [rsp + F_SIDE]
    xor     esi, MARK_SIDEBIT               /* the side that got there first */
    or      esi, [rsp + F_GEN]
    lea     rdx, [rsp + F_BUFB]
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
