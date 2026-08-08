/* cube.s -- the readable layer: 24 stickers, twelve quarter turns, parsing,
 * printing, whole-cube rotations and the scramble RNG.
 *
 * Nothing in this file knows about coordinates or solving.  It is the model a
 * human can check by hand, and every table the solvers use is derived from it
 * at start-up rather than being pasted in as a magic constant.
 *
 * Convention for this file (and the rest of the program): arguments follow the
 * System V ABI (rdi, rsi, rdx, rcx, r8, r9), a cube is a pointer to 24 bytes,
 * and any routine that says it returns a status returns it in eax.
 */

    .intel_syntax noprefix
    .include "rubik.inc"

/* ------------------------------------------------------------------ data */

    .section .rodata
    .balign 16

    .globl cube_solved
cube_solved:
    .byte 3,3,3,3, 0,0,0,0, 1,1,1,1, 4,4,4,4, 2,2,2,2, 5,5,5,5

    .globl color_chars
color_chars:        .asciz "rwbypg"
    .globl move_chars
move_chars:         .asciz "lLrRtTbBfFaA"
    .globl solve_move_chars
solve_move_chars:   .asciz "lLtTfF"

/* new[i] = old[move_perm[m][i]].  l/L left column down/up, r/R right column,
 * t/T top layer, b/B bottom layer, f/F front face, a/A back face. */
    .balign 16
    .globl move_perm
move_perm:
    .byte 12, 1,14, 3, 0, 5, 2, 7, 4, 9, 6,11, 8,13,10,15,18,16,19,17,20,21,22,23
    .byte  4, 1, 6, 3, 8, 5,10, 7,12, 9,14,11, 0,13, 2,15,17,19,16,18,20,21,22,23
    .byte  0,13, 2,15, 4, 1, 6, 3, 8, 5,10, 7,12, 9,14,11,16,17,18,19,21,23,20,22
    .byte  0, 5, 2, 7, 4, 9, 6,11, 8,13,10,15,12, 1,14, 3,16,17,18,19,22,20,23,21
    .byte  2, 0, 3, 1,20,21, 6, 7, 8, 9,10,11,12,13,17,16, 4, 5,18,19,15,14,22,23
    .byte  1, 3, 0, 2,16,17, 6, 7, 8, 9,10,11,12,13,21,20,15,14,18,19, 4, 5,22,23
    .byte  0, 1, 2, 3, 4, 5,22,23, 9,11, 8,10,19,18,14,15,16,17, 6, 7,20,21,13,12
    .byte  0, 1, 2, 3, 4, 5,18,19,10, 8,11, 9,23,22,14,15,16,17,13,12,20,21, 6, 7
    .byte  0, 1,19,17, 6, 4, 7, 5,22,20,10,11,12,13,14,15,16, 8,18, 9, 2,21, 3,23
    .byte  0, 1,20,22, 5, 7, 4, 6,17,19,10,11,12,13,14,15,16, 3,18, 2, 9,21, 8,23
    .byte 18,16, 2, 3, 4, 5, 6, 7, 8, 9,23,21,13,15,12,14,10,17,11,19,20, 0,22, 1
    .byte 21,23, 2, 3, 4, 5, 6, 7, 8, 9,16,18,14,12,15,13, 1,17, 0,19,20,11,22,10

/* Three generators; cube_rotations() closes them into all 24. */
    .balign 16
rot_gen:
    .byte 12,13,14,15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,18,16,19,17,21,23,20,22
    .byte  2, 0, 3, 1,20,21,22,23, 9,11, 8,10,19,18,17,16, 4, 5, 6, 7,15,14,13,12
    .byte 18,16,19,17, 6, 4, 7, 5,22,20,23,21,13,15,12,14,10, 8,11, 9, 2, 0, 3, 1

/* Each slot's three facelets, in a rotational order shared by all eight, so
 * that "twist" means the same thing at every slot.  Slot 7 is the fixed
 * down-back-right corner. */
    .balign 16
    .globl coord_slots
coord_slots:
    .byte 16, 0,14
    .byte 21,15, 1
    .byte 17, 4, 2
    .byte 20, 3, 5
    .byte 19, 8, 6
    .byte 22, 7, 9
    .byte 18,12,10
    .byte 23,11,13

    .globl factorial16
factorial16:    .word 720,120,24,6,2,1,1     /* 6! .. 0!, Lehmer place values */
    .globl pow3_16
pow3_16:        .word 1,3,9,27,81,243,729
    .globl mod3tab
mod3tab:        .byte 0,1,2,0,1,2,0,1        /* (a+b) mod 3 for a+b < 6 */

    .balign 8
count_unit:     .quad 1, 0x100, 0x10000, 0x1000000, 0x100000000, 0x10000000000

    .equ GOLDEN_LO, 0x7f4a7c15
    .equ GOLDEN_HI, 0x9e3779b9

/* ------------------------------------------------------------------ code */

    .text

/* cube_apply_move(rdi=dst24, rsi=src24, edx=move).  dst must not alias src. */
    .globl cube_apply_move
cube_apply_move:
    movzx   edx, dl
    lea     rax, [rdx + rdx*2]              /* move*3 */
    lea     rcx, [rip + move_perm]
    lea     rax, [rcx + rax*8]              /* + move*24 */
    xor     ecx, ecx
1:  movzx   r8d, byte ptr [rax + rcx]
    movzx   r9d, byte ptr [rsi + r8]
    mov     [rdi + rcx], r9b
    inc     ecx
    cmp     ecx, FACELETS
    jb      1b
    ret

/* cube_rotate(rdi=dst24, rsi=src24, rdx=perm24) */
    .globl cube_rotate
cube_rotate:
    xor     ecx, ecx
1:  movzx   r8d, byte ptr [rdx + rcx]
    movzx   r9d, byte ptr [rsi + r8]
    mov     [rdi + rcx], r9b
    inc     ecx
    cmp     ecx, FACELETS
    jb      1b
    ret

/* cube_move_index(edi=letter) -> eax, or -1 */
    .globl cube_move_index
cube_move_index:
    lea     rcx, [rip + move_chars]
    mov     edx, NMOVES
    jmp     letter_index

/* cube_solve_move_index(edi=letter) -> eax, or -1 */
    .globl cube_solve_move_index
cube_solve_move_index:
    lea     rcx, [rip + solve_move_chars]
    mov     edx, NSOLVE
letter_index:
    xor     eax, eax
1:  cmp     eax, edx
    jae     2f
    cmp     dil, byte ptr [rcx + rax]
    je      3f
    inc     eax
    jmp     1b
2:  mov     eax, -1
3:  ret

/* cube_apply_moves(rdi=state24 in/out, rsi=moves) -> eax 1 ok / 0 bad letter */
    .globl cube_apply_moves
cube_apply_moves:
    push    rbx
    push    r12
    push    r13
    sub     rsp, 32
    mov     rbx, rdi
    mov     r12, rsi
1:  movzx   edi, byte ptr [r12]
    test    edi, edi
    je      9f
    call    cube_move_index
    test    eax, eax
    js      8f
    mov     rdi, rsp
    mov     rsi, rbx
    mov     edx, eax
    call    cube_apply_move
    mov     rax, [rsp]
    mov     [rbx], rax
    mov     rax, [rsp + 8]
    mov     [rbx + 8], rax
    mov     rax, [rsp + 16]
    mov     [rbx + 16], rax
    inc     r12
    jmp     1b
9:  mov     eax, 1
    jmp     10f
8:  xor     eax, eax
10: add     rsp, 32
    pop     r13
    pop     r12
    pop     rbx
    ret

/* cube_invert(rdi=out, rsi=moves).  Reverse the sequence and flip each case:
 * both cases of every move exist, and they are each other's inverse. */
    .globl cube_invert
cube_invert:
    xor     ecx, ecx
1:  cmp     byte ptr [rsi + rcx], 0          /* length */
    je      2f
    inc     ecx
    jmp     1b
2:  xor     edx, edx
    mov     r8d, ecx
3:  cmp     edx, ecx
    jae     4f
    dec     r8d
    movzx   eax, byte ptr [rsi + r8]
    xor     eax, 0x20                       /* 'a' <-> 'A' */
    mov     [rdi + rdx], al
    inc     edx
    jmp     3b
4:  mov     byte ptr [rdi + rcx], 0
    ret

/* cube_is_solved(rdi=state24) -> eax bool.  Every face one colour. */
    .globl cube_is_solved
cube_is_solved:
    xor     ecx, ecx
1:  movzx   eax, byte ptr [rdi + rcx]
    cmp     al, byte ptr [rdi + rcx + 1]
    jne     9f
    cmp     al, byte ptr [rdi + rcx + 2]
    jne     9f
    cmp     al, byte ptr [rdi + rcx + 3]
    jne     9f
    add     ecx, 4
    cmp     ecx, FACELETS
    jb      1b
    mov     eax, 1
    ret
9:  xor     eax, eax
    ret

/* cube_parse(rdi=text, rsi=out24) -> eax
 *      0 ok, 1 wrong length, 2 bad letter, 3 a colour is not used four times
 *
 * Whitespace is trimmed because a piped state arrives with a newline. */
    .globl cube_parse
cube_parse:
    push    rbx
    push    r12
    push    r13
    push    r14
1:  movzx   eax, byte ptr [rdi]             /* skip leading blanks */
    cmp     al, ' '
    je      2f
    cmp     al, 9
    je      2f
    cmp     al, 10
    je      2f
    cmp     al, 13
    je      2f
    jmp     3f
2:  inc     rdi
    jmp     1b
3:  xor     ecx, ecx                        /* trimmed length */
4:  movzx   eax, byte ptr [rdi + rcx]
    test    al, al
    je      5f
    inc     ecx
    jmp     4b
5:  test    ecx, ecx
    je      6f
    movzx   eax, byte ptr [rdi + rcx - 1]
    cmp     al, ' '
    je      7f
    cmp     al, 9
    je      7f
    cmp     al, 10
    je      7f
    cmp     al, 13
    jne     6f
7:  dec     ecx
    jmp     5b
6:  cmp     ecx, FACELETS
    jne     .Lparse_len

    /* Six colour tallies live packed one per byte of r8: no colour can occur
     * more than 24 times, so a byte never carries into its neighbour. */
    xor     r8, r8
    xor     ecx, ecx
    lea     r9, [rip + color_chars]
    lea     r11, [rip + count_unit]
8:  movzx   eax, byte ptr [rdi + rcx]
    or      al, 0x20                        /* accept upper case */
    xor     edx, edx
9:  cmp     al, byte ptr [r9 + rdx]
    je      10f
    inc     edx
    cmp     edx, NCOLORS
    jb      9b
    jmp     .Lparse_letter
10: mov     [rsi + rcx], dl
    add     r8, [r11 + rdx*8]
    inc     ecx
    cmp     ecx, FACELETS
    jb      8b

    mov     rax, 0x0000040404040404
    cmp     r8, rax
    jne     .Lparse_counts
    xor     eax, eax
    jmp     .Lparse_out
.Lparse_len:
    mov     eax, 1
    jmp     .Lparse_out
.Lparse_letter:
    mov     eax, 2
    jmp     .Lparse_out
.Lparse_counts:
    mov     eax, 3
.Lparse_out:
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

/* cube_parse_message(edi=code) -> rax, a static explanation */
    .globl cube_parse_message
cube_parse_message:
    lea     rax, [rip + .Lmsg_len]
    cmp     edi, 1
    je      1f
    lea     rax, [rip + .Lmsg_letter]
    cmp     edi, 2
    je      1f
    lea     rax, [rip + .Lmsg_counts]
1:  ret

/* cube_to_string(rdi=out25, rsi=state24) */
    .globl cube_to_string
cube_to_string:
    lea     rdx, [rip + color_chars]
    xor     ecx, ecx
1:  movzx   eax, byte ptr [rsi + rcx]
    movzx   eax, byte ptr [rdx + rax]
    mov     [rdi + rcx], al
    inc     ecx
    cmp     ecx, FACELETS
    jb      1b
    mov     byte ptr [rdi + FACELETS], 0
    ret

/* cube_rotations() -> rax, the 24 rotation permutations, built once.
 * Closure of the three generators under composition. */
    .globl cube_rotations
cube_rotations:
    lea     rax, [rip + rot_perms]
    cmp     dword ptr [rip + rot_ready], 0
    jne     9f
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbp
    mov     r15, rax                        /* rot_perms */
    xor     ecx, ecx
1:  mov     [r15 + rcx], cl                 /* identity */
    inc     ecx
    cmp     ecx, FACELETS
    jb      1b
    mov     r12d, 1                         /* count */
    xor     r13d, r13d                      /* head */
2:  cmp     r13d, r12d
    jae     8f
    xor     r14d, r14d                      /* generator */
3:  cmp     r14d, 3
    jae     7f
    /* composed[i] = rot_perms[head][ rot_gen[g][i] ] */
    lea     rbx, [r13 + r13*2]
    lea     rbx, [r15 + rbx*8]              /* &rot_perms[head] */
    lea     rbp, [rip + rot_gen]
    lea     rax, [r14 + r14*2]
    lea     rbp, [rbp + rax*8]              /* &rot_gen[g] */
    lea     rdi, [rip + rot_scratch]
    xor     ecx, ecx
4:  movzx   eax, byte ptr [rbp + rcx]
    movzx   eax, byte ptr [rbx + rax]
    mov     [rdi + rcx], al
    inc     ecx
    cmp     ecx, FACELETS
    jb      4b
    /* seen? */
    xor     ecx, ecx
5:  cmp     ecx, r12d
    jae     6f
    lea     rax, [rcx + rcx*2]
    lea     rax, [r15 + rax*8]
    mov     rdx, [rdi]
    cmp     rdx, [rax]
    jne     51f
    mov     rdx, [rdi + 8]
    cmp     rdx, [rax + 8]
    jne     51f
    mov     rdx, [rdi + 16]
    cmp     rdx, [rax + 16]
    je      52f
51: inc     ecx
    jmp     5b
52: jmp     61f                             /* already known */
6:  cmp     r12d, NROT
    jae     61f
    lea     rax, [r12 + r12*2]
    lea     rax, [r15 + rax*8]
    mov     rdx, [rdi]
    mov     [rax], rdx
    mov     rdx, [rdi + 8]
    mov     [rax + 8], rdx
    mov     rdx, [rdi + 16]
    mov     [rax + 16], rdx
    inc     r12d
61: inc     r14d
    jmp     3b
7:  inc     r13d
    jmp     2b
8:  mov     dword ptr [rip + rot_ready], 1
    mov     rax, r15
    pop     rbp
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
9:  ret

/* cube_rng_seed(rdi=state_ptr, rsi=seed) */
    .globl cube_rng_seed
cube_rng_seed:
    mov     rax, 0x9e3779b97f4a7c15
    add     rax, rsi
    mov     [rdi], rax
    ret

/* cube_rng_next(rdi=state_ptr) -> rax.  splitmix64. */
    .globl cube_rng_next
cube_rng_next:
    mov     rax, 0x9e3779b97f4a7c15
    add     rax, [rdi]
    mov     [rdi], rax
    mov     rdx, rax
    shr     rdx, 30
    xor     rax, rdx
    mov     rcx, 0xbf58476d1ce4e5b9
    imul    rax, rcx
    mov     rdx, rax
    shr     rdx, 27
    xor     rax, rdx
    mov     rcx, 0x94d049bb133111eb
    imul    rax, rcx
    mov     rdx, rax
    shr     rdx, 31
    xor     rax, rdx
    ret

/* cube_scramble(rdi=state24 in/out, esi=count, rdx=rng_ptr)
 *
 * Rejecting states already visited keeps a short scramble from wandering back
 * onto its own path, so "-n 8" really means eight moves of distance. */
    .globl cube_scramble
cube_scramble:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbp
    sub     rsp, 6184                       /* 256 * 24 seen + scratch cube */
    mov     rbx, rdi                        /* state */
    mov     r12d, esi                       /* remaining */
    mov     r13, rdx                        /* rng */
    lea     r14, [rsp + 32]                 /* seen[] */
    mov     r15d, 1                         /* seen count */
    mov     rax, [rbx]
    mov     [r14], rax
    mov     rax, [rbx + 8]
    mov     [r14 + 8], rax
    mov     rax, [rbx + 16]
    mov     [r14 + 16], rax
1:  test    r12d, r12d
    jle     9f
    mov     rdi, r13
    call    cube_rng_next
    xor     edx, edx
    mov     ecx, NMOVES
    div     rcx                             /* remainder -> rdx = the move */
    mov     rdi, rsp
    mov     rsi, rbx
    call    cube_apply_move
    xor     ecx, ecx
2:  cmp     ecx, r15d
    jae     4f
    lea     rax, [rcx + rcx*2]
    lea     rax, [r14 + rax*8]
    mov     rdx, [rsp]
    cmp     rdx, [rax]
    jne     3f
    mov     rdx, [rsp + 8]
    cmp     rdx, [rax + 8]
    jne     3f
    mov     rdx, [rsp + 16]
    cmp     rdx, [rax + 16]
    je      1b                              /* seen: draw again */
3:  inc     ecx
    jmp     2b
4:  cmp     r15d, 256
    jae     5f
    lea     rax, [r15 + r15*2]
    lea     rax, [r14 + rax*8]
    mov     rdx, [rsp]
    mov     [rax], rdx
    mov     rdx, [rsp + 8]
    mov     [rax + 8], rdx
    mov     rdx, [rsp + 16]
    mov     [rax + 16], rdx
    inc     r15d
5:  mov     rax, [rsp]
    mov     [rbx], rax
    mov     rax, [rsp + 8]
    mov     [rbx + 8], rax
    mov     rax, [rsp + 16]
    mov     [rbx + 16], rax
    dec     r12d
    jmp     1b
9:  add     rsp, 6184
    pop     rbp
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

    .section .rodata
.Lmsg_len:      .asciz "expected 24 colour letters"
.Lmsg_letter:   .asciz "invalid colour letter (allowed: rwbypg)"
.Lmsg_counts:   .asciz "every colour must appear exactly four times"

    .bss
    .balign 32
rot_perms:      .space NROT * FACELETS
rot_scratch:    .space FACELETS
rot_ready:      .space 4

    .section .note.GNU-stack,"",@progbits
