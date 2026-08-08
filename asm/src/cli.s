/* cli.s -- argument parsing and printing.  Nothing here knows how a cube is
 * solved; it reads arguments, calls a solver and formats the answer.
 *
 * stderr is reached with dprintf(2, ...) rather than through libc's `stderr`
 * FILE*, which spares this file a GOT dance for no loss.
 */

    .intel_syntax noprefix
    .include "rubik.inc"

    .text

/* streq(rdi, rsi) -> ZF set when equal */
streq:
    sub     rsp, 8
    call    strcmp@PLT
    lea     rsp, [rsp + 8]                  /* lea, not add: the flags below
                                             * are the return value */
    test    eax, eax
    ret

/* parse_int(rdi=text, rsi=out int) -> eax 1 ok / 0 bad */
parse_int:
    push    rbx
    sub     rsp, 16
    mov     rbx, rsi
    mov     rsi, rsp
    mov     edx, 10
    call    strtol@PLT
    mov     rcx, [rsp]
    cmp     byte ptr [rcx], 0
    jne     1f
    mov     [rbx], eax
    mov     eax, 1
    jmp     2f
1:  xor     eax, eax
2:  add     rsp, 16
    pop     rbx
    ret

/* read_state(rdi=text or 0, rsi=out24, rdx=scratch128) -> eax 1 ok / 0 bad.
 * "-" reads the state from stdin so solves can be piped. */
read_state:
    push    rbx
    push    r12
    push    r13
    sub     rsp, 8
    mov     rbx, rdi
    mov     r12, rsi
    mov     r13, rdx
    test    rbx, rbx
    je      .Lrs_missing
    lea     rsi, [rip + .Lstdin_dash]
    mov     rdi, rbx
    call    streq
    jne     1f
    mov     rdi, r13
    mov     esi, 128
    mov     rdx, qword ptr [rip + stdin@GOTPCREL]
    mov     rdx, [rdx]
    call    fgets@PLT
    test    rax, rax
    je      .Lrs_nostdin
    mov     rbx, r13
1:  mov     rdi, rbx
    mov     rsi, r12
    call    cube_parse
    test    eax, eax
    jne     .Lrs_bad
    mov     eax, 1
    jmp     .Lrs_out
.Lrs_bad:
    mov     edi, eax
    call    cube_parse_message
    mov     rdx, rax
    lea     rsi, [rip + .Lfmt_invalid]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    xor     eax, eax
    jmp     .Lrs_out
.Lrs_missing:
    lea     rsi, [rip + .Lmsg_nostate]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    xor     eax, eax
    jmp     .Lrs_out
.Lrs_nostdin:
    lea     rsi, [rip + .Lmsg_nostdin]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    xor     eax, eax
.Lrs_out:
    add     rsp, 8
    pop     r13
    pop     r12
    pop     rbx
    ret

/* ---------------------------------------------------------------- solve */

    .equ S_STATE,   0
    .equ S_SOL,     32
    .equ S_LINE,    96
    .equ S_TEXT,    224
    .equ S_ALGO,    232
    .equ S_IDX,     240
    .equ S_TABLE,   248
    .equ S_SIZE,    264

command_solve:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbp
    sub     rsp, S_SIZE
    mov     r14d, edi
    mov     r15, rsi
    mov     qword ptr [rsp + S_TEXT], 0
    lea     rax, [rip + .Lalg_bfs]
    mov     [rsp + S_ALGO], rax
    xor     ebx, ebx
1:  cmp     ebx, r14d
    jae     4f
    mov     r12, [r15 + rbx*8]
    mov     rdi, r12
    lea     rsi, [rip + .Lopt_algorithm]
    call    streq
    jne     2f
    lea     eax, [rbx + 1]
    cmp     eax, r14d
    jae     3f
    inc     ebx
    mov     rax, [r15 + rbx*8]
    mov     [rsp + S_ALGO], rax
    jmp     3f
2:  mov     rdi, r12
    lea     rsi, [rip + .Lopt_algorithm_eq]
    mov     edx, 12
    call    strncmp@PLT
    test    eax, eax
    jne     21f
    lea     rax, [r12 + 12]
    mov     [rsp + S_ALGO], rax
    jmp     3f
21: cmp     qword ptr [rsp + S_TEXT], 0
    jne     .Lsolve_unexpected
    mov     [rsp + S_TEXT], r12
3:  inc     ebx
    jmp     1b
4:  mov     rdi, [rsp + S_TEXT]
    lea     rsi, [rsp + S_STATE]
    lea     rdx, [rsp + S_LINE]
    call    read_state
    test    eax, eax
    je      .Lsolve_argfail

    call    coords_init
    lea     rdi, [rsp + S_STATE]
    lea     rsi, [rsp + S_IDX]
    call    coords_encode
    test    eax, eax
    je      5f
    mov     edi, eax
    call    coords_status_message
    mov     rdx, rax
    lea     rsi, [rip + .Lfmt_nosolution]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    mov     eax, 1
    jmp     .Lsolve_out

5:  mov     rdi, [rsp + S_ALGO]
    lea     rsi, [rip + .Lalg_bfs]
    call    streq
    jne     6f
    mov     edi, [rsp + S_IDX]
    lea     rsi, [rsp + S_SOL]
    call    bfs_solve_index
    jmp     9f
6:  mov     rdi, [rsp + S_ALGO]
    lea     rsi, [rip + .Lalg_ida]
    call    streq
    jne     7f
    call    ida_prepare
    mov     edi, [rsp + S_IDX]
    lea     rsi, [rsp + S_SOL]
    call    ida_solve_index
    jmp     9f
7:  mov     rdi, [rsp + S_ALGO]
    lea     rsi, [rip + .Lalg_table]
    call    streq
    jne     8f
    mov     edi, 1
    call    table_create
    test    rax, rax
    je      .Lsolve_nomem
    mov     [rsp + S_TABLE], rax
    mov     edi, [rsp + S_IDX]
    lea     rsi, [rsp + S_SOL]
    mov     rdx, rax
    call    table_solve_index
    mov     ebp, eax
    mov     rdi, [rsp + S_TABLE]
    call    table_free
    mov     eax, ebp
    jmp     9f
8:  mov     rdx, [rsp + S_ALGO]
    lea     rsi, [rip + .Lfmt_badalgorithm]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    mov     eax, 2
    jmp     .Lsolve_out

9:  test    eax, eax
    jne     .Lsolve_nosol
    cmp     dword ptr [rsp + S_SOL + SOL_LEN], 0
    jne     10f
    lea     rsi, [rip + .Lmsg_alreadysolved]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    xor     eax, eax
    jmp     .Lsolve_out
10: lea     rdi, [rsp + S_SOL + SOL_MOVES]
    call    puts@PLT
    xor     eax, eax
    jmp     .Lsolve_out
.Lsolve_nosol:
    lea     rsi, [rip + .Lmsg_nosolution]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    mov     eax, 1
    jmp     .Lsolve_out
.Lsolve_nomem:
    lea     rsi, [rip + .Lmsg_nomem]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    mov     eax, 1
    jmp     .Lsolve_out
.Lsolve_unexpected:
    mov     rdx, r12
    lea     rsi, [rip + .Lfmt_unexpected]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
.Lsolve_argfail:
    mov     eax, 2
.Lsolve_out:
    add     rsp, S_SIZE
    pop     rbp
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

/* ------------------------------------------------------------- scramble */

command_scramble:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbp
    sub     rsp, 104
    mov     r14d, edi
    mov     r15, rsi
    mov     dword ptr [rsp + 64], 8         /* moves */
    mov     edi, 0
    call    time@PLT
    mov     [rsp + 72], rax                 /* seed */
    xor     ebx, ebx
1:  cmp     ebx, r14d
    jae     6f
    mov     r12, [r15 + rbx*8]
    mov     rdi, r12
    lea     rsi, [rip + .Lopt_n]
    call    streq
    je      2f
    mov     rdi, r12
    lea     rsi, [rip + .Lopt_moves]
    call    streq
    jne     3f
2:  lea     eax, [rbx + 1]
    cmp     eax, r14d
    jae     5f
    inc     ebx
    mov     rdi, [r15 + rbx*8]
    lea     rsi, [rsp + 64]
    call    parse_int
    test    eax, eax
    je      .Lscr_bad
    jmp     5f
3:  mov     rdi, r12
    lea     rsi, [rip + .Lopt_seed]
    call    streq
    jne     .Lscr_unexpected
    lea     eax, [rbx + 1]
    cmp     eax, r14d
    jae     5f
    inc     ebx
    mov     rdi, [r15 + rbx*8]
    lea     rsi, [rsp + 80]
    call    parse_int
    test    eax, eax
    je      .Lscr_bad
    movsxd  rax, dword ptr [rsp + 80]
    mov     [rsp + 72], rax
5:  inc     ebx
    jmp     1b
6:  lea     rdi, [rsp + 88]
    mov     rsi, [rsp + 72]
    call    cube_rng_seed
    lea     rdi, [rsp]
    lea     rsi, [rip + cube_solved]
    mov     rax, [rsi]
    mov     [rdi], rax
    mov     rax, [rsi + 8]
    mov     [rdi + 8], rax
    mov     rax, [rsi + 16]
    mov     [rdi + 16], rax
    lea     rdi, [rsp]
    mov     esi, [rsp + 64]
    lea     rdx, [rsp + 88]
    call    cube_scramble
    lea     rdi, [rsp + 32]
    lea     rsi, [rsp]
    call    cube_to_string
    lea     rdi, [rsp + 32]
    call    puts@PLT
    xor     eax, eax
    jmp     .Lscr_out
.Lscr_bad:
    mov     rdx, r12
    lea     rsi, [rip + .Lfmt_badnumber]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    mov     eax, 2
    jmp     .Lscr_out
.Lscr_unexpected:
    mov     rdx, r12
    lea     rsi, [rip + .Lfmt_unexpected]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    mov     eax, 2
.Lscr_out:
    add     rsp, 104
    pop     rbp
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

/* ---------------------------------------------------------------- table */

command_table:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbp
    sub     rsp, 136
    mov     r14d, edi
    mov     r15, rsi
    mov     dword ptr [rsp + 64], 1         /* workers */
    xor     ebx, ebx
1:  cmp     ebx, r14d
    jae     3f
    mov     r12, [r15 + rbx*8]
    mov     rdi, r12
    lea     rsi, [rip + .Lopt_workers]
    call    streq
    jne     .Ltab_unexpected
    lea     eax, [rbx + 1]
    cmp     eax, r14d
    jae     2f
    inc     ebx
    mov     rdi, [r15 + rbx*8]
    lea     rsi, [rsp + 64]
    call    parse_int
    test    eax, eax
    je      .Ltab_unexpected
2:  inc     ebx
    jmp     1b
3:  call    coords_init
    mov     esi, NSTATES
    lea     rdi, [rip + .Lfmt_searching]
    xor     eax, eax
    call    printf@PLT
    call    now_ms
    movsd   [rsp + 72], xmm0
    mov     edi, [rsp + 64]
    call    table_create
    test    rax, rax
    je      .Ltab_nomem
    mov     r13, rax
    call    now_ms
    subsd   xmm0, [rsp + 72]
    movsd   [rsp + 80], xmm0
    mov     rdi, r13
    lea     rsi, [rsp]
    call    table_histogram
    mov     r12d, eax                       /* highest marker */
    xor     ebx, ebx
    xor     ebp, ebp                        /* total */
4:  cmp     ebx, r12d
    jae     5f
    mov     edx, [rsp + rbx*4]
    add     ebp, edx
    mov     esi, ebx
    lea     rdi, [rip + .Lfmt_depth]
    xor     eax, eax
    call    printf@PLT
    inc     ebx
    jmp     4b
5:  lea     rdi, [rip + .Lnewline]
    xor     eax, eax
    call    printf@PLT
    xor     ebx, ebx
6:  cmp     ebx, r12d
    jae     7f
    test    ebx, ebx
    je      61f
    lea     rdi, [rip + .Lcomma]
    xor     eax, eax
    call    printf@PLT
61: mov     esi, [rsp + rbx*4]
    lea     rdi, [rip + .Lfmt_u]
    xor     eax, eax
    call    printf@PLT
    inc     ebx
    jmp     6b
7:  mov     esi, ebp
    mov     edx, [rip + table_last_workers]
    movsd   xmm0, [rsp + 80]
    lea     rdi, [rip + .Lfmt_tablesummary]
    mov     eax, 1
    call    printf@PLT
    mov     rdi, r13
    call    table_free
    xor     eax, eax
    jmp     .Ltab_out
.Ltab_nomem:
    lea     rsi, [rip + .Lmsg_nomem]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    mov     eax, 1
    jmp     .Ltab_out
.Ltab_unexpected:
    mov     rdx, r12
    lea     rsi, [rip + .Lfmt_unexpected]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    mov     eax, 2
.Ltab_out:
    add     rsp, 136
    pop     rbp
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

/* ---------------------------------------------------------------- bench */

command_bench:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbp
    sub     rsp, 56
    mov     r14d, edi
    mov     r15, rsi
    mov     qword ptr [rsp + 0], 0          /* scrambles path */
    mov     dword ptr [rsp + 8], 0          /* limit */
    mov     dword ptr [rsp + 12], 0         /* workers */
    mov     dword ptr [rsp + 16], 0         /* json */
    xor     ebx, ebx
1:  cmp     ebx, r14d
    jae     9f
    mov     r12, [r15 + rbx*8]
    mov     rdi, r12
    lea     rsi, [rip + .Lopt_json]
    call    streq
    jne     2f
    mov     dword ptr [rsp + 16], 1
    jmp     8f
2:  mov     rdi, r12
    lea     rsi, [rip + .Lopt_scrambles]
    call    streq
    jne     3f
    lea     eax, [rbx + 1]
    cmp     eax, r14d
    jae     8f
    inc     ebx
    mov     rax, [r15 + rbx*8]
    mov     [rsp + 0], rax
    jmp     8f
3:  mov     rdi, r12
    lea     rsi, [rip + .Lopt_scrambles_eq]
    mov     edx, 12
    call    strncmp@PLT
    test    eax, eax
    jne     4f
    lea     rax, [r12 + 12]
    mov     [rsp + 0], rax
    jmp     8f
4:  mov     rdi, r12
    lea     rsi, [rip + .Lopt_n]
    call    streq
    je      5f
    mov     rdi, r12
    lea     rsi, [rip + .Lopt_samples]
    call    streq
    jne     6f
5:  lea     eax, [rbx + 1]
    cmp     eax, r14d
    jae     8f
    inc     ebx
    mov     rdi, [r15 + rbx*8]
    lea     rsi, [rsp + 8]
    call    parse_int
    jmp     8f
6:  mov     rdi, r12
    lea     rsi, [rip + .Lopt_workers]
    call    streq
    jne     .Lben_unexpected
    lea     eax, [rbx + 1]
    cmp     eax, r14d
    jae     8f
    inc     ebx
    mov     rdi, [r15 + rbx*8]
    lea     rsi, [rsp + 12]
    call    parse_int
8:  inc     ebx
    jmp     1b
9:  mov     rdi, [rsp + 0]
    mov     esi, [rsp + 8]
    mov     edx, [rsp + 16]
    mov     ecx, [rsp + 12]
    call    bench_run
    jmp     .Lben_out
.Lben_unexpected:
    mov     rdx, r12
    lea     rsi, [rip + .Lfmt_unexpected]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    mov     eax, 2
.Lben_out:
    add     rsp, 56
    pop     rbp
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

/* ----------------------------------------------------------------- main */

    .globl main
main:
    push    rbx
    push    r12
    sub     rsp, 8
    mov     ebx, edi
    mov     r12, rsi
    cmp     ebx, 2
    jge     1f
    lea     rsi, [rip + .Lusage]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    mov     eax, 2
    jmp     .Lmain_out
1:  mov     rdi, [r12 + 8]
    lea     rsi, [rip + .Lcmd_solve]
    call    streq
    jne     2f
    lea     edi, [rbx - 2]
    lea     rsi, [r12 + 16]
    call    command_solve
    jmp     .Lmain_out
2:  mov     rdi, [r12 + 8]
    lea     rsi, [rip + .Lcmd_scramble]
    call    streq
    jne     3f
    lea     edi, [rbx - 2]
    lea     rsi, [r12 + 16]
    call    command_scramble
    jmp     .Lmain_out
3:  mov     rdi, [r12 + 8]
    lea     rsi, [rip + .Lcmd_table]
    call    streq
    jne     4f
    lea     edi, [rbx - 2]
    lea     rsi, [r12 + 16]
    call    command_table
    jmp     .Lmain_out
4:  mov     rdi, [r12 + 8]
    lea     rsi, [rip + .Lcmd_bench]
    call    streq
    jne     5f
    lea     edi, [rbx - 2]
    lea     rsi, [r12 + 16]
    call    command_bench
    jmp     .Lmain_out
5:  mov     rdi, [r12 + 8]
    lea     rsi, [rip + .Lcmd_help]
    call    streq
    je      6f
    mov     rdi, [r12 + 8]
    lea     rsi, [rip + .Lcmd_help2]
    call    streq
    je      6f
    mov     rdi, [r12 + 8]
    lea     rsi, [rip + .Lcmd_help3]
    call    streq
    jne     7f
6:  lea     rdi, [rip + .Lusage]
    xor     eax, eax
    call    printf@PLT
    xor     eax, eax
    jmp     .Lmain_out
7:  mov     rdx, [r12 + 8]
    lea     rsi, [rip + .Lfmt_unknowncmd]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    lea     rsi, [rip + .Lusage]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    mov     eax, 2
.Lmain_out:
    add     rsp, 8
    pop     r12
    pop     rbx
    ret

    .section .rodata
.Lstdin_dash:       .asciz "-"
.Lopt_algorithm:    .asciz "--algorithm"
.Lopt_algorithm_eq: .asciz "--algorithm="
.Lopt_workers:      .asciz "--workers"
.Lopt_json:         .asciz "--json"
.Lopt_scrambles:    .asciz "--scrambles"
.Lopt_scrambles_eq: .asciz "--scrambles="
.Lopt_n:            .asciz "-n"
.Lopt_moves:        .asciz "--moves"
.Lopt_samples:      .asciz "--samples"
.Lopt_seed:         .asciz "--seed"
.Lalg_bfs:          .asciz "bfs"
.Lalg_ida:          .asciz "ida"
.Lalg_table:        .asciz "table"
.Lcmd_solve:        .asciz "solve"
.Lcmd_scramble:     .asciz "scramble"
.Lcmd_table:        .asciz "table"
.Lcmd_bench:        .asciz "bench"
.Lcmd_help:         .asciz "help"
.Lcmd_help2:        .asciz "--help"
.Lcmd_help3:        .asciz "-h"
.Lfmt_invalid:      .asciz "invalid cube: %s\n"
.Lfmt_nosolution:   .asciz "no solution: %s\n"
.Lfmt_unexpected:   .asciz "unexpected argument: %s\n"
.Lfmt_badnumber:    .asciz "not a number: %s\n"
.Lfmt_badalgorithm: .asciz "unknown algorithm: %s (bfs, ida, table)\n"
.Lfmt_unknowncmd:   .asciz "unknown command: %s\n\n"
.Lmsg_nostate:      .asciz "missing state\n"
.Lmsg_nostdin:      .asciz "no state on stdin\n"
.Lmsg_nosolution:   .asciz "no solution\n"
.Lmsg_nomem:        .asciz "out of memory\n"
.Lmsg_alreadysolved:.asciz "already solved\n"
.Lfmt_searching:    .asciz "Searching all %d states...\n"
.Lfmt_depth:        .asciz "  depth %2d: %9u\n"
.Lfmt_u:            .asciz "%u"
.Lcomma:            .asciz ", "
.Lnewline:          .asciz "\n"
.Lfmt_tablesummary: .asciz "\n\n%u states, 3.7 MB, built in %.1f ms with %d thread(s)\nThis matches OEIS A079762, the published distance distribution.\n"
.Lusage:
    .ascii "usage: rubik2x2 <command> [options]\n"
    .ascii "\n"
    .ascii "  solve <state> [--algorithm bfs|ida|table]   print a shortest solution\n"
    .ascii "  scramble [-n N] [--seed S]                  print a scrambled state\n"
    .ascii "  table [--workers N]                         build the table, print the histogram\n"
    .ascii "  bench [--json] [--scrambles FILE] [-n N]    benchmark every algorithm\n"
    .ascii "\n"
    .ascii "A state is 24 colour letters (rwbypg) in side order UP FRONT DOWN BACK\n"
    .ascii "LEFT RIGHT, each face read row-major. '-' reads the state from stdin.\n"
    .ascii "\n"
    .ascii "Moves: l/L left column down/up, r/R right column down/up, t/T top layer\n"
    .ascii "left/right, b/B bottom layer left/right, f/F front face cw/ccw, a/A back\n"
    .asciz "face cw/ccw. Solutions use only l L t T f F.\n"

    .section .note.GNU-stack,"",@progbits
