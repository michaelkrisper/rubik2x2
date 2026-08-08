/* ida.s -- iterative-deepening A* with two exact pattern databases.
 *
 * Because a move acts on the permutation and the orientation coordinates
 * independently, each coordinate on its own is a genuine quotient of the state
 * graph.  The exact distance to solved inside a quotient can never exceed the
 * distance in the full graph, so both are admissible, and h = max of the two.
 * They are small enough to sit in cache: 5040 bytes and 729 bytes.
 *
 * The solve alphabet lists each turn next to its inverse, so the move that
 * would undo the previous one is exactly `move ^ 1` -- one xor instead of a
 * table.
 */

    .intel_syntax noprefix
    .include "rubik.inc"

    .equ IDA_PEAK,  NPERM + NORIENT

    .text

/* ida_prepare() -- breadth-first over each coordinate.  Idempotent. */
    .globl ida_prepare
ida_prepare:
    cmp     dword ptr [rip + ida_ready], 0
    jne     .Lidp_done
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15

    /* --- permutation coordinate, 5040 nodes, uint16 rows of 16 bytes --- */
    lea     r12, [rip + ida_perm_dist]
    xor     ecx, ecx
1:  mov     byte ptr [r12 + rcx], 0
    inc     ecx
    cmp     ecx, NPERM
    jb      1b
    mov     byte ptr [r12], 1
    lea     r13, [rip + proj_a]
    lea     r14, [rip + proj_b]
    mov     dword ptr [r13], 0
    mov     ebp, 1                          /* how many in the current level */
    mov     r15d, 2                         /* marker */
2:  test    ebp, ebp
    je      6f
    xor     r9d, r9d                        /* produced */
    xor     ebx, ebx
3:  mov     eax, [r13 + rbx*4]
    shl     rax, CHILDROW_SH
    lea     r10, [rip + perm_child]
    add     r10, rax
    xor     ecx, ecx
4:  movzx   eax, word ptr [r10 + rcx*2]
    cmp     byte ptr [r12 + rax], 0
    jne     5f
    mov     [r12 + rax], r15b
    mov     [r14 + r9*4], eax
    inc     r9d
5:  inc     ecx
    cmp     ecx, NSOLVE
    jb      4b
    inc     ebx
    cmp     ebx, ebp
    jb      3b
    mov     rax, r13
    mov     r13, r14
    mov     r14, rax
    mov     ebp, r9d
    inc     r15d
    jmp     2b
6:

    /* --- orientation coordinate, 729 nodes, uint32 rows of 32 bytes --- */
    lea     r12, [rip + ida_orient_dist]
    xor     ecx, ecx
7:  mov     byte ptr [r12 + rcx], 0
    inc     ecx
    cmp     ecx, NORIENT
    jb      7b
    mov     byte ptr [r12], 1
    lea     r13, [rip + proj_a]
    lea     r14, [rip + proj_b]
    mov     dword ptr [r13], 0
    mov     ebp, 1
    mov     r15d, 2
8:  test    ebp, ebp
    je      12f
    xor     r9d, r9d
    xor     ebx, ebx
9:  mov     eax, [r13 + rbx*4]
    shl     rax, ORIENTROW_SH
    lea     r10, [rip + orient_child]
    add     r10, rax
    xor     ecx, ecx
10: mov     eax, [r10 + rcx*4]
    cmp     byte ptr [r12 + rax], 0
    jne     11f
    mov     [r12 + rax], r15b
    mov     [r14 + r9*4], eax
    inc     r9d
11: inc     ecx
    cmp     ecx, NSOLVE
    jb      10b
    inc     ebx
    cmp     ebx, ebp
    jb      9b
    mov     rax, r13
    mov     r13, r14
    mov     r14, rax
    mov     ebp, r9d
    inc     r15d
    jmp     8b
12:
    mov     dword ptr [rip + ida_ready], 1
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
.Lidp_done:
    ret

/* ida_invalidate() -- force the next ida_prepare() to rebuild.  Only the
 * benchmark needs this, to time the build more than once. */
    .globl ida_invalidate
ida_invalidate:
    mov     dword ptr [rip + ida_ready], 0
    ret

/* ida_descend(edi=q, esi=r, edx=depth, ecx=last) -> eax found
 *
 * The caller has already accepted this node's bound, so entry means it is
 * worth expanding.  Testing each child's bound here, before recursing, keeps
 * the two database loads in one tight loop and spares a call frame for the
 * majority of children, which are cut.
 *
 * The limit is a global rather than an argument: it does not change inside one
 * deepening pass, and leaving it out of the frame is what lets the six live
 * values fit in six callee-saved registers with no stack traffic beyond the
 * pushes -- which, at fifteen instructions per child, is worth having.
 */
ida_descend:
    mov     eax, edi
    or      eax, esi
    jne     1f
    mov     [rip + ida_len], edx
    mov     eax, 1
    ret
1:  push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    mov     r12d, edx                       /* depth */
    mov     ebp, ecx                        /* the move that got us here */
    mov     eax, edi
    shl     rax, CHILDROW_SH
    lea     r14, [rip + perm_child]
    add     r14, rax
    mov     eax, esi
    shl     rax, CHILDROW_SH
    lea     r15, [rip + orient_child16]
    add     r15, rax
    /* one base for both databases: ida_orient_dist sits immediately after
     * ida_perm_dist in .bss, so the second is a fixed displacement away */
    lea     r13, [rip + ida_perm_dist]
    xor     ebx, ebx
2:  mov     eax, ebx
    xor     eax, 1                          /* the move that would undo `last` */
    cmp     eax, ebp
    je      5f
    movzx   edi, word ptr [r14 + rbx*2]
    movzx   esi, word ptr [r15 + rbx*2]
    movzx   eax, byte ptr [r13 + rdi]
    movzx   ecx, byte ptr [r13 + NPERM + rsi]
    cmp     ecx, eax
    cmovg   eax, ecx                        /* max, still carrying the +1 */
    lea     edx, [r12 + 1]
    add     eax, edx
    cmp     eax, [rip + ida_limit1]
    jg      5f
    lea     rax, [rip + solve_move_chars]
    movzx   eax, byte ptr [rax + rbx]
    lea     rcx, [rip + ida_path]
    mov     [rcx + r12], al
    mov     ecx, ebx
    call    ida_descend                     /* edx is still depth + 1 */
    test    eax, eax
    jne     6f
5:  inc     ebx
    cmp     ebx, NSOLVE
    jb      2b
    xor     eax, eax
6:  pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret

/* ida_solve_index(edi=index, rsi=solution) -> eax status */
    .globl ida_solve_index
ida_solve_index:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 8
    mov     r13, rsi
    mov     qword ptr [r13 + SOL_PEAK], IDA_PEAK
    mov     dword ptr [r13 + SOL_LEN], 0
    mov     byte ptr [r13 + SOL_MOVES], 0
    test    edi, edi
    je      .Lids_ok
    mov     eax, edi
    imul    rax, rax, MAGIC729
    shr     rax, 32
    mov     r14d, eax                       /* q */
    mov     edx, eax
    imul    edx, edx, NORIENT
    mov     r15d, edi
    sub     r15d, edx                       /* r */
    lea     rax, [rip + ida_perm_dist]
    movzx   eax, byte ptr [rax + r14]
    lea     rcx, [rip + ida_orient_dist]
    movzx   ecx, byte ptr [rcx + r15]
    cmp     ecx, eax
    cmovg   eax, ecx
    dec     eax
    mov     ebx, eax                        /* first limit worth trying */
1:  cmp     ebx, MAXDEPTH
    jg      .Lids_nosol
    lea     eax, [rbx + 1]
    mov     [rip + ida_limit1], eax
    mov     edi, r14d
    mov     esi, r15d
    xor     edx, edx
    mov     ecx, -1
    call    ida_descend
    test    eax, eax
    jne     2f
    inc     ebx
    jmp     1b
2:  mov     ecx, [rip + ida_len]
    mov     [r13 + SOL_LEN], ecx
    lea     rsi, [rip + ida_path]
    xor     edx, edx
3:  cmp     edx, ecx
    jae     4f
    movzx   eax, byte ptr [rsi + rdx]
    mov     [r13 + rdx], al
    inc     edx
    jmp     3b
4:  mov     byte ptr [r13 + rcx], 0
.Lids_ok:
    xor     eax, eax
    jmp     .Lids_out
.Lids_nosol:
    mov     eax, SV_NOSOL
.Lids_out:
    add     rsp, 8
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret

/* ida_solve(rdi=state24, rsi=solution) -> eax status */
    .globl ida_solve
ida_solve:
    push    rbx
    sub     rsp, 32
    mov     rbx, rsi
    lea     rsi, [rsp]
    call    coords_encode
    test    eax, eax
    jne     1f
    mov     edi, [rsp]
    mov     rsi, rbx
    call    ida_solve_index
    jmp     2f
1:  mov     dword ptr [rbx + SOL_LEN], 0
    mov     byte ptr [rbx + SOL_MOVES], 0
    mov     eax, SV_INVALID
2:  add     rsp, 32
    pop     rbx
    ret

    .bss
    .balign 64
    .globl ida_perm_dist
ida_perm_dist:      .space NPERM
    .globl ida_orient_dist
ida_orient_dist:    .space NORIENT          /* must follow ida_perm_dist: the
                                             * search addresses both from one
                                             * base register */
    .balign 64
proj_a:             .space NPERM * 4
proj_b:             .space NPERM * 4
ida_path:           .space MOVEBUF
ida_len:            .space 4
ida_limit1:         .space 4
ida_ready:          .space 4

    .section .note.GNU-stack,"",@progbits
