/* coords.s -- the compact layer: sticker state <-> a single integer 0..3674159.
 *
 * Holding the down-back-right corner still removes the 24-fold whole-cube
 * rotation symmetry, so a state is exactly "which of the seven free pieces
 * sits in which of the seven free slots" (7! = 5040) crossed with "how each is
 * twisted" (3^6 = 729, the seventh twist being forced).  index = q*729 + r.
 *
 * A move acts on those two coordinates independently, which is the whole
 * reason the split is worth making: applying a move is two array lookups, and
 * each coordinate on its own is a genuine quotient of the state graph, hence an
 * admissible heuristic for IDA*.
 *
 * Memory layout chosen here, and relied on by every solver:
 *
 *   perm_wide[q][m]    uint32, = new_q * 729           row stride 32 bytes
 *   perm_child[q][m]   uint16, = new_q                 row stride 16 bytes
 *   orient_child[r][m] uint32, = new_r                 row stride 32 bytes
 *
 * Six moves padded to eight per row, so a row address is (coord << 5) rather
 * than a multiply, and a row is two aligned 16-byte loads.  The child index is
 * then perm_wide[q][m] + orient_child[r][m]: one add, no multiply.
 */

    .intel_syntax noprefix
    .include "rubik.inc"

    .text

/* ------------------------------------------------------------- ranking */

/* rank_perm(rdi=7 bytes) -> eax, the Lehmer rank; identity ranks 0.
 *
 * Left to right rather than the textbook O(n^2) scan: the digit at position i
 * is perm[i] minus how many smaller values have already been consumed, which a
 * popcount of a used-value bitmask answers in one instruction.
 * Clobbers rax rcx rdx rsi r8 r9 r10 r11. */
    .globl rank_perm
rank_perm:
    xor     eax, eax
    xor     r8d, r8d                        /* values already placed */
    xor     r11d, r11d
    lea     r10, [rip + factorial16]
1:  movzx   r9d, byte ptr [rdi + r11]
    mov     ecx, r9d
    mov     edx, 1
    shl     edx, cl
    lea     esi, [rdx - 1]
    and     esi, r8d
    popcnt  esi, esi
    or      r8d, edx
    sub     r9d, esi
    movzx   edx, word ptr [r10 + r11*2]
    imul    r9d, edx
    add     eax, r9d
    inc     r11
    cmp     r11d, NFREE
    jb      1b
    ret

/* rank_orient(rdi=7 bytes) -> eax.  Six base-3 digits; the seventh is implied
 * by the twists summing to zero mod three. */
    .globl rank_orient
rank_orient:
    xor     eax, eax
    mov     ecx, NFREE - 2
1:  lea     eax, [rax + rax*2]
    movzx   edx, byte ptr [rdi + rcx]
    add     eax, edx
    dec     ecx
    jns     1b
    ret

/* ------------------------------------------------------- state -> cubies */

/* to_cubies(rdi=state24, rsi=lut216, rdx=perm_out7, rcx=twist_out7) -> eax
 *
 * The lut maps a slot's three colours, packed as c0*36 + c1*6 + c2, to
 * piece*4 + twist + 1, or 0 for a triple that is no piece of this cube.  It is
 * built per goal orientation, so this works for a cube handed over any way up.
 *
 * Rejects: colours that form no piece, a piece seen twice, and twists that do
 * not sum to a multiple of three.  The last one matters more than it looks:
 * only six twists are ranked and the seventh is derived, so one corner turned
 * in place would otherwise encode silently as a different, perfectly legal
 * cube and get "solved". */
    .globl to_cubies
to_cubies:
    push    rbx
    push    r12
    push    r13
    mov     r13, rcx
    lea     r10, [rip + coord_slots]
    xor     r8d, r8d                        /* pieces used */
    xor     r9d, r9d                        /* total twist */
    xor     r11d, r11d
1:  lea     rbx, [r11 + r11*2]
    add     rbx, r10
    movzx   eax, byte ptr [rbx]
    movzx   eax, byte ptr [rdi + rax]
    movzx   r12d, byte ptr [rbx + 1]
    movzx   r12d, byte ptr [rdi + r12]
    movzx   ebx, byte ptr [rbx + 2]
    movzx   ebx, byte ptr [rdi + rbx]
    imul    eax, eax, 36
    lea     r12d, [r12 + r12*2]
    lea     eax, [rax + r12*2]
    add     eax, ebx
    movzx   eax, byte ptr [rsi + rax]
    test    eax, eax
    je      .Lcub_piece
    dec     eax
    mov     ebx, eax
    and     ebx, 3                          /* twist */
    shr     eax, 2                          /* piece */
    bt      r8d, eax
    jc      .Lcub_repeat
    bts     r8d, eax
    mov     [rdx + r11], al
    mov     [r13 + r11], bl
    add     r9d, ebx
    inc     r11
    cmp     r11d, NFREE
    jb      1b
    /* total <= 14, so test divisibility by three with a bit mask */
    mov     ecx, r9d
    mov     eax, 0x1249
    shr     eax, cl
    test    al, 1
    je      .Lcub_twist
    xor     eax, eax
    jmp     .Lcub_out
.Lcub_piece:
    mov     eax, CE_PIECE
    jmp     .Lcub_out
.Lcub_repeat:
    mov     eax, CE_REPEAT
    jmp     .Lcub_out
.Lcub_twist:
    mov     eax, CE_TWIST
.Lcub_out:
    pop     r13
    pop     r12
    pop     rbx
    ret

/* ------------------------------------------------------------ encoding */

/* coords_encode(rdi=state24, rsi=out_uint32) -> eax status */
    .globl coords_encode
coords_encode:
    push    rbx
    push    r14
    sub     rsp, 24
    mov     r14, rsi
    /* The fixed corner's three colours name which of the 24 solved
     * orientations this cube is aiming at. */
    movzx   eax, byte ptr [rdi + 11]
    imul    eax, eax, 36
    movzx   ecx, byte ptr [rdi + 13]
    lea     ecx, [rcx + rcx*2]
    lea     eax, [rax + rcx*2]
    movzx   ecx, byte ptr [rdi + 23]
    add     eax, ecx
    lea     rcx, [rip + goal_valid]
    cmp     byte ptr [rcx + rax], 0
    je      .Lenc_corner
    imul    rax, rax, 216
    lea     rsi, [rip + goal_lut]
    add     rsi, rax
    mov     rdx, rsp
    lea     rcx, [rsp + 8]
    call    to_cubies
    test    eax, eax
    jne     .Lenc_out
    mov     rdi, rsp
    call    rank_perm
    mov     ebx, eax
    lea     rdi, [rsp + 8]
    call    rank_orient
    imul    ebx, ebx, NORIENT
    add     eax, ebx
    mov     [r14], eax
    xor     eax, eax
    jmp     .Lenc_out
.Lenc_corner:
    mov     eax, CE_CORNER
.Lenc_out:
    add     rsp, 24
    pop     r14
    pop     rbx
    ret

/* coords_decode(edi=index, rsi=out24).  Inverse of encode for a cube in the
 * canonical orientation; for any other it yields that state's canonical
 * representative, which is the same cube held the standard way up. */
    .globl coords_decode
coords_decode:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbp
    mov     eax, edi
    imul    rax, rax, MAGIC729
    shr     rax, 32                         /* q */
    mov     ecx, eax
    imul    ecx, ecx, NORIENT
    mov     edx, edi
    sub     edx, ecx                        /* r */
    lea     r12, [rip + perm_all]
    lea     r12, [r12 + rax*8]              /* the permutation */
    lea     r13, [rip + orient_all]
    lea     r13, [r13 + rdx*8]              /* the twists */
    lea     r14, [rip + canonical_pieces]
    lea     r15, [rip + coord_slots]
    lea     rbp, [rip + mod3tab]
    xor     r8d, r8d                        /* slot */
1:  lea     r9, [r8 + r8*2]
    add     r9, r15                         /* &coord_slots[slot] */
    cmp     r8d, NFREE
    jae     3f
    movzx   r10d, byte ptr [r12 + r8]       /* piece */
    lea     r10, [r10 + r10*2]
    add     r10, r14                        /* &canonical_pieces[piece] */
    movzx   r11d, byte ptr [r13 + r8]       /* twist */
    xor     ecx, ecx
2:  lea     eax, [r11 + rcx]
    movzx   eax, byte ptr [rbp + rax]
    movzx   eax, byte ptr [r10 + rax]
    movzx   ebx, byte ptr [r9 + rcx]
    mov     [rsi + rbx], al
    inc     ecx
    cmp     ecx, 3
    jb      2b
    jmp     4f
3:  /* the fixed corner is itself, always */
    lea     r10, [r14 + NFREE*3]
    xor     ecx, ecx
31: movzx   eax, byte ptr [r10 + rcx]
    movzx   ebx, byte ptr [r9 + rcx]
    mov     [rsi + rbx], al
    inc     ecx
    cmp     ecx, 3
    jb      31b
4:  inc     r8d
    cmp     r8d, 8
    jb      1b
    pop     rbp
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

/* coords_status_message(edi=status) -> rax */
    .globl coords_status_message
coords_status_message:
    lea     rax, [rip + .Lst_corner]
    cmp     edi, CE_CORNER
    je      1f
    lea     rax, [rip + .Lst_piece]
    cmp     edi, CE_PIECE
    je      1f
    lea     rax, [rip + .Lst_repeat]
    cmp     edi, CE_REPEAT
    je      1f
    lea     rax, [rip + .Lst_twist]
    cmp     edi, CE_TWIST
    je      1f
    lea     rax, [rip + .Lst_ok]
1:  ret

/* --------------------------------------------------------------- set-up */

/* coords_init() -- derive everything from the sticker model.  Idempotent. */
    .globl coords_init
coords_init:
    cmp     dword ptr [rip + coords_ready], 0
    jne     .Linit_done
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbp
    sub     rsp, 136
    /* frame: [rsp+0] cube scratch 32, [rsp+32] source8, [rsp+40] twist8,
     *        [rsp+48] next8, [rsp+56] work8, [rsp+64] goal24 (32 bytes) */

    /* 1. the canonical piece of each slot: what that slot shows when solved */
    lea     r12, [rip + canonical_pieces]
    lea     r13, [rip + coord_slots]
    lea     r14, [rip + cube_solved]
    xor     ecx, ecx
1:  movzx   eax, byte ptr [r13 + rcx]
    movzx   eax, byte ptr [r14 + rax]
    mov     [r12 + rcx], al
    inc     ecx
    cmp     ecx, 24
    jb      1b

    /* 2. all 24 whole-cube rotations */
    call    cube_rotations
    mov     r15, rax

    /* 3. one colour-triple lookup per solved orientation */
    xor     ebx, ebx                        /* rotation */
2:  lea     rdi, [rsp + 64]
    lea     rsi, [rip + cube_solved]
    lea     rdx, [rbx + rbx*2]
    lea     rdx, [r15 + rdx*8]
    call    cube_rotate
    lea     r12, [rsp + 64]                 /* the goal, this way up */
    movzx   eax, byte ptr [r12 + 11]
    imul    eax, eax, 36
    movzx   ecx, byte ptr [r12 + 13]
    lea     ecx, [rcx + rcx*2]
    lea     eax, [rax + rcx*2]
    movzx   ecx, byte ptr [r12 + 23]
    add     eax, ecx                        /* goal key */
    lea     rcx, [rip + goal_valid]
    mov     byte ptr [rcx + rax], 1
    imul    rax, rax, 216
    lea     r13, [rip + goal_lut]
    add     r13, rax                        /* this goal's lut */
    lea     r14, [rip + coord_slots]
    lea     rbp, [rip + mod3tab]
    xor     r8d, r8d                        /* piece */
21: xor     r9d, r9d                        /* twist */
22: lea     r10, [r8 + r8*2]
    add     r10, r14                        /* &coord_slots[piece] */
    xor     r11d, r11d
    xor     esi, esi                        /* colour key */
23: lea     eax, [r9 + r11]
    movzx   eax, byte ptr [rbp + rax]
    movzx   eax, byte ptr [r10 + rax]
    movzx   eax, byte ptr [r12 + rax]       /* colour */
    lea     esi, [rsi + rsi*2]
    add     esi, esi                        /* key *= 6 */
    add     esi, eax
    inc     r11d
    cmp     r11d, 3
    jb      23b
    lea     eax, [r8*4]
    add     eax, r9d
    inc     eax                             /* piece*4 + twist + 1 */
    mov     [r13 + rsi], al
    inc     r9d
    cmp     r9d, 3
    jb      22b
    inc     r8d
    cmp     r8d, NFREE
    jb      21b
    inc     ebx
    cmp     ebx, NROT
    jb      2b

    /* 4. every permutation of 0..6 in lexicographic order, which is Lehmer
     *    order, so perm_all[rank] is the unranking table. */
    lea     r12, [rip + perm_all]
    xor     ecx, ecx
3:  mov     byte ptr [rsp + 56 + rcx], cl
    inc     ecx
    cmp     ecx, NFREE
    jb      3b
    mov     byte ptr [rsp + 63], 0
    xor     ebx, ebx
31: mov     rax, [rsp + 56]
    mov     [r12 + rbx*8], rax
    inc     ebx
    cmp     ebx, NPERM
    jae     36f
    /* next permutation in place */
    mov     ecx, NFREE - 2
32: movzx   eax, byte ptr [rsp + 56 + rcx]
    movzx   edx, byte ptr [rsp + 57 + rcx]
    cmp     eax, edx
    jb      33f
    dec     ecx
    jns     32b
33: mov     edx, NFREE - 1
34: movzx   eax, byte ptr [rsp + 56 + rcx]
    cmp     byte ptr [rsp + 56 + rdx], al
    ja      35f
    dec     edx
    jmp     34b
35: movzx   r8d, byte ptr [rsp + 56 + rdx]
    mov     byte ptr [rsp + 56 + rcx], r8b
    mov     byte ptr [rsp + 56 + rdx], al
    /* reverse the tail */
    lea     edx, [rcx + 1]
    mov     r8d, NFREE - 1
351:cmp     edx, r8d
    jae     31b
    movzx   eax, byte ptr [rsp + 56 + rdx]
    movzx   r9d, byte ptr [rsp + 56 + r8]
    mov     byte ptr [rsp + 56 + rdx], r9b
    mov     byte ptr [rsp + 56 + r8], al
    inc     edx
    dec     r8d
    jmp     351b
36:

    /* 5. every orientation: six base-3 digits, the seventh forced */
    lea     r12, [rip + orient_all]
    xor     ebx, ebx
4:  mov     qword ptr [rsp + 56], 0
    mov     eax, ebx
    xor     ecx, ecx
    xor     r9d, r9d                        /* twist sum */
41: xor     edx, edx
    mov     r8d, 3
    div     r8d                             /* eax/3 -> eax, edx = digit */
    mov     byte ptr [rsp + 56 + rcx], dl
    add     r9d, edx
    inc     ecx
    cmp     ecx, NFREE - 1
    jb      41b
    mov     eax, r9d
    xor     edx, edx
    mov     r8d, 3
    div     r8d
    mov     eax, 3
    sub     eax, edx
    xor     edx, edx
    mov     r8d, 3
    div     r8d
    mov     byte ptr [rsp + 62], dl         /* twist[6] */
    mov     rax, [rsp + 56]
    mov     [r12 + rbx*8], rax
    inc     ebx
    cmp     ebx, NORIENT
    jb      4b

    /* 6. the move tables.  A move on the identity state *is* the slot-to-slot
     *    map and the twist each slot picks up, so read them straight off. The
     *    six solve moves come in inverse pairs at adjacent indices, so only
     *    the even ones need a full pass: the odd one is the inverse map, which
     *    the same loop can write for free. */
    mov     ebx, 0                          /* move, stepping by two */
5:  lea     rax, [rip + solve_move_chars]
    movzx   edi, byte ptr [rax + rbx]
    call    cube_move_index
    mov     edx, eax
    mov     rdi, rsp
    lea     rsi, [rip + cube_solved]
    call    cube_apply_move
    lea     rdi, [rip + cube_solved]
    movzx   eax, byte ptr [rdi + 11]
    imul    eax, eax, 36
    movzx   ecx, byte ptr [rdi + 13]
    lea     ecx, [rcx + rcx*2]
    lea     eax, [rax + rcx*2]
    movzx   ecx, byte ptr [rdi + 23]
    add     eax, ecx
    imul    rax, rax, 216
    lea     rsi, [rip + goal_lut]
    add     rsi, rax
    mov     rdi, rsp                        /* the turned cube */
    lea     rdx, [rsp + 32]                 /* source[] */
    lea     rcx, [rsp + 40]                 /* twist[]  */
    call    to_cubies

    /* 6a. permutation coordinate */
    lea     r12, [rip + perm_all]
    lea     r13, [rip + perm_child]
    lea     r14, [rip + perm_wide]
    xor     r15d, r15d                      /* q */
51: lea     rbp, [r12 + r15*8]
    xor     ecx, ecx
52: movzx   eax, byte ptr [rsp + 32 + rcx]  /* source[slot] */
    movzx   eax, byte ptr [rbp + rax]
    mov     byte ptr [rsp + 48 + rcx], al
    inc     ecx
    cmp     ecx, NFREE
    jb      52b
    lea     rdi, [rsp + 48]
    call    rank_perm                       /* -> eax = new q */
    mov     rcx, r15
    shl     rcx, CHILDROW_SH
    add     rcx, r13
    mov     [rcx + rbx*2], ax
    mov     rcx, r15
    shl     rcx, PERMROW_SH
    add     rcx, r14
    mov     edx, eax
    imul    edx, edx, NORIENT
    mov     [rcx + rbx*4], edx
    /* and the inverse move, one index up: the six solve moves come in
     * inverse pairs at adjacent indices, so this pass writes both. */
    mov     rcx, rax
    shl     rcx, CHILDROW_SH
    add     rcx, r13
    mov     [rcx + rbx*2 + 2], r15w
    mov     rcx, rax
    shl     rcx, PERMROW_SH
    add     rcx, r14
    mov     edx, r15d
    imul    edx, edx, NORIENT
    mov     [rcx + rbx*4 + 4], edx
    inc     r15d
    cmp     r15d, NPERM
    jb      51b

    /* 6b. orientation coordinate */
    lea     r12, [rip + orient_all]
    lea     r13, [rip + orient_child]
    lea     r14, [rip + orient_child16]
    lea     rbp, [rip + mod3tab]
    xor     r15d, r15d                      /* r */
53: lea     r8, [r12 + r15*8]
    xor     ecx, ecx
54: movzx   eax, byte ptr [rsp + 32 + rcx]
    movzx   eax, byte ptr [r8 + rax]
    movzx   edx, byte ptr [rsp + 40 + rcx]
    add     eax, edx
    movzx   eax, byte ptr [rbp + rax]
    mov     byte ptr [rsp + 48 + rcx], al
    inc     ecx
    cmp     ecx, NFREE
    jb      54b
    lea     rdi, [rsp + 48]
    call    rank_orient                     /* -> eax = new r */
    mov     rcx, r15
    shl     rcx, ORIENTROW_SH
    add     rcx, r13
    mov     [rcx + rbx*4], eax
    mov     rcx, rax
    shl     rcx, ORIENTROW_SH
    add     rcx, r13
    mov     [rcx + rbx*4 + 4], r15d
    /* the same map again as uint16: IDA* wants a row in one cache line, and
     * 729 * 16 bytes stays in L1 where 729 * 32 does not */
    mov     rcx, r15
    shl     rcx, CHILDROW_SH
    add     rcx, r14
    mov     [rcx + rbx*2], ax
    mov     rcx, rax
    shl     rcx, CHILDROW_SH
    add     rcx, r14
    mov     [rcx + rbx*2 + 2], r15w
    inc     r15d
    cmp     r15d, NORIENT
    jb      53b

    add     ebx, 2
    cmp     ebx, NSOLVE
    jb      5b

    mov     dword ptr [rip + coords_ready], 1
    add     rsp, 136
    pop     rbp
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
.Linit_done:
    ret

    .section .rodata
.Lst_ok:        .asciz "ok"
.Lst_corner:    .asciz "the fixed corner's colours occur on no solved cube"
.Lst_piece:     .asciz "a corner's colours form no piece of this cube"
.Lst_repeat:    .asciz "the same corner piece appears in two slots"
.Lst_twist:     .asciz "a corner is twisted in place; the twists do not sum to a multiple of three"

    .bss
    .balign 64
    .globl perm_wide
perm_wide:          .space NPERM * 32
    .globl perm_child
perm_child:         .space NPERM * 16
    .globl orient_child
orient_child:       .space NORIENT * 32
    .globl orient_child16
orient_child16:     .space NORIENT * 16
    .globl perm_all
perm_all:           .space NPERM * 8
    .globl orient_all
orient_all:         .space NORIENT * 8
    .globl canonical_pieces
canonical_pieces:   .space 24
    .balign 16
goal_valid:         .space 216
goal_lut:           .space 216 * 216
coords_ready:       .space 4

    .section .note.GNU-stack,"",@progbits
