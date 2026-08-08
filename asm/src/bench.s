/* bench.s -- the measurement harness.
 *
 * Scrambles come from a file, not a generator, so every language in the
 * repository measures the same work.  Each algorithm is timed the same way:
 * repeat the whole set until at least 250 ms of wall clock has passed, then
 * divide.  Setup costs are sampled three times, alternating between the
 * builds, and the fastest kept -- whichever table is built first pays to fault
 * in ~18 MB of fresh pages, and a single sample would hand that bill to
 * whichever ran first.
 *
 * peak_mb is not sampled from the OS.  It is the exact number of bytes each
 * algorithm keeps live from the end of its setup onwards, computed from the
 * sizes this program allocates: allocators do not return freed pages, so a
 * resident-set delta would read as zero for every algorithm after the first.
 */

    .intel_syntax noprefix
    .include "rubik.inc"

    .equ MAXSCRAMBLES,  8192
    .equ R_NAME,        0
    .equ R_SETUP,       8
    .equ R_AVG,         16
    .equ R_WORST,       24
    .equ R_PEAK,        32
    .equ R_OPT,         40
    .equ R_WORKERS,     44
    .equ R_SIZE,        48

    .text

/* now_ms() -> xmm0 */
    .globl now_ms
now_ms:
    sub     rsp, 24
    mov     edi, 1                          /* CLOCK_MONOTONIC */
    mov     rsi, rsp
    call    clock_gettime@PLT
    cvtsi2sd xmm0, qword ptr [rsp]
    mulsd   xmm0, [rip + .Lthousand]
    cvtsi2sd xmm1, qword ptr [rsp + 8]
    divsd   xmm1, [rip + .Lmillion]
    addsd   xmm0, xmm1
    add     rsp, 24
    ret

/* solve_one(edi=algo, rsi=state24, rdx=solution) -> eax status */
solve_one:
    cmp     edi, 0
    jne     1f
    mov     rdi, rsi
    mov     rsi, rdx
    jmp     bfs_solve
1:  cmp     edi, 1
    jne     2f
    mov     rdi, rsi
    mov     rsi, rdx
    jmp     ida_solve
2:  mov     rdi, rsi
    mov     rsi, rdx
    mov     rdx, [rip + bench_table]
    jmp     table_solve

/* bench_avg(edi=algo) -> xmm0, mean milliseconds per solve */
bench_avg:
    push    rbx
    push    r12
    push    r13
    sub     rsp, 96
    mov     ebx, edi
    pxor    xmm0, xmm0
    movsd   [rsp + 56], xmm0                /* elapsed */
    mov     qword ptr [rsp + 64], 0         /* solves */
    xor     r13d, r13d
1:  cmp     r13d, 100000
    jae     4f
    movsd   xmm0, [rsp + 56]
    ucomisd xmm0, [rip + .Ltarget]
    jae     4f
    call    now_ms
    movsd   [rsp + 72], xmm0
    xor     r12d, r12d
2:  cmp     r12d, [rip + bench_count]
    jae     3f
    mov     edi, ebx
    mov     rsi, [rip + bench_states]
    imul    rax, r12, 24
    add     rsi, rax
    mov     rdx, rsp
    call    solve_one
    inc     r12d
    jmp     2b
3:  call    now_ms
    subsd   xmm0, [rsp + 72]
    addsd   xmm0, [rsp + 56]
    movsd   [rsp + 56], xmm0
    mov     eax, [rip + bench_count]
    add     [rsp + 64], rax
    inc     r13d
    jmp     1b
4:  movsd   xmm0, [rsp + 56]
    cvtsi2sd xmm1, qword ptr [rsp + 64]
    divsd   xmm0, xmm1
    add     rsp, 96
    pop     r13
    pop     r12
    pop     rbx
    ret

/* bench_worst(edi=algo) -> xmm0; also leaves peak_bytes in bench_peak */
bench_worst:
    push    rbx
    push    r12
    push    r13
    sub     rsp, 96
    mov     ebx, edi
    pxor    xmm0, xmm0
    movsd   [rsp + 56], xmm0
    mov     qword ptr [rsp + 64], 0
    xor     r13d, r13d
1:  cmp     r13d, 100000
    jae     4f
    movsd   xmm0, [rsp + 56]
    ucomisd xmm0, [rip + .Ltarget]
    jae     4f
    call    now_ms
    movsd   [rsp + 72], xmm0
    mov     edi, ebx
    lea     rsi, [rip + bench_hardest]
    mov     rdx, rsp
    call    solve_one
    call    now_ms
    subsd   xmm0, [rsp + 72]
    addsd   xmm0, [rsp + 56]
    movsd   [rsp + 56], xmm0
    add     qword ptr [rsp + 64], 1
    inc     r13d
    jmp     1b
4:  mov     rax, [rsp + SOL_PEAK]
    mov     [rip + bench_peak], rax
    movsd   xmm0, [rsp + 56]
    cvtsi2sd xmm1, qword ptr [rsp + 64]
    divsd   xmm0, xmm1
    add     rsp, 96
    pop     r13
    pop     r12
    pop     rbx
    ret

/* bench_optimal(edi=algo) -> eax.  Every solution must be shortest *and*
 * actually solve its cube; the distance table is the arbiter of shortest. */
bench_optimal:
    push    rbx
    push    r12
    push    r13
    sub     rsp, 112
    mov     ebx, edi
    xor     r12d, r12d
1:  cmp     r12d, [rip + bench_count]
    jae     .Lopt_yes
    mov     rax, [rip + bench_states]
    imul    rcx, r12, 24
    lea     r13, [rax + rcx]
    mov     edi, ebx
    mov     rsi, r13
    mov     rdx, rsp
    call    solve_one
    test    eax, eax
    jne     .Lopt_no
    mov     rdi, r13
    lea     rsi, [rsp + 56]
    call    coords_encode
    test    eax, eax
    jne     .Lopt_no
    mov     rdi, [rip + bench_table]
    mov     esi, [rsp + 56]
    call    table_distance
    cmp     eax, [rsp + SOL_LEN]
    jne     .Lopt_no
    /* replay the moves on the sticker model: an independent check that does
     * not go through the coordinate at all */
    mov     rax, [r13]
    mov     [rsp + 64], rax
    mov     rax, [r13 + 8]
    mov     [rsp + 72], rax
    mov     rax, [r13 + 16]
    mov     [rsp + 80], rax
    lea     rdi, [rsp + 64]
    mov     rsi, rsp
    call    cube_apply_moves
    test    eax, eax
    je      .Lopt_no
    lea     rdi, [rsp + 64]
    call    cube_is_solved
    test    eax, eax
    je      .Lopt_no
    inc     r12d
    jmp     1b
.Lopt_yes:
    mov     eax, 1
    jmp     .Lopt_out
.Lopt_no:
    xor     eax, eax
.Lopt_out:
    add     rsp, 112
    pop     r13
    pop     r12
    pop     rbx
    ret

/* scrambles_load(rdi=path or 0, esi=limit) -> eax count */
scrambles_load:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbp
    sub     rsp, 152
    mov     r14, rdi
    mov     r15d, esi
    mov     edi, MAXSCRAMBLES
    mov     esi, 24
    call    calloc@PLT
    test    rax, rax
    je      .Lsl_fail
    mov     [rip + bench_states], rax
    mov     r12, rax
    xor     ebx, ebx                        /* count */
    test    r14, r14
    je      .Lsl_generate
    mov     rdi, r14
    lea     rsi, [rip + .Lmode_r]
    call    fopen@PLT
    test    rax, rax
    je      .Lsl_noopen
    mov     r13, rax
1:  lea     rdi, [rsp + 16]
    mov     esi, 128
    mov     rdx, r13
    call    fgets@PLT
    test    rax, rax
    je      2f
    movzx   eax, byte ptr [rsp + 16]
    cmp     al, 10
    je      1b
    cmp     al, '#'
    je      1b
    cmp     ebx, MAXSCRAMBLES
    jae     2f
    lea     rdi, [rsp + 16]
    imul    rax, rbx, 24
    lea     rsi, [r12 + rax]
    call    cube_parse
    test    eax, eax
    jne     .Lsl_badline
    inc     ebx
    test    r15d, r15d
    jle     1b
    cmp     ebx, r15d
    jb      1b
2:  mov     rdi, r13
    call    fclose@PLT
    jmp     .Lsl_done
.Lsl_generate:
    mov     ebp, 200
    test    r15d, r15d
    jle     3f
    mov     ebp, r15d
3:  cmp     ebx, ebp
    jae     .Lsl_done
    lea     rdi, [rsp + 8]
    movsxd  rsi, ebx
    call    cube_rng_seed
    imul    rax, rbx, 24
    lea     rdi, [r12 + rax]
    lea     rsi, [rip + cube_solved]
    mov     rax, [rsi]
    mov     [rdi], rax
    mov     rax, [rsi + 8]
    mov     [rdi + 8], rax
    mov     rax, [rsi + 16]
    mov     [rdi + 16], rax
    imul    rax, rbx, 24
    lea     rdi, [r12 + rax]
    mov     esi, 14
    lea     rdx, [rsp + 8]
    call    cube_scramble
    inc     ebx
    jmp     3b
.Lsl_done:
    mov     [rip + bench_count], ebx
    mov     eax, ebx
    jmp     .Lsl_out
.Lsl_badline:
    mov     rdi, r13
    call    fclose@PLT
    lea     rsi, [rip + .Lmsg_badscramble]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
    xor     eax, eax
    jmp     .Lsl_out
.Lsl_noopen:
    mov     rdx, r14
    lea     rsi, [rip + .Lmsg_noopen]
    mov     edi, 2
    xor     eax, eax
    call    dprintf@PLT
.Lsl_fail:
    xor     eax, eax
.Lsl_out:
    add     rsp, 152
    pop     rbp
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

/* keep_fastest(rdi=slot, esi=index, xmm0=sample) */
keep_fastest:
    test    esi, esi
    je      1f
    movsd   xmm1, [rdi]
    ucomisd xmm1, xmm0
    jbe     2f
1:  movsd   [rdi], xmm0
2:  ret

/* bench_run(rdi=path, esi=limit, edx=json, ecx=workers) -> eax */
    .globl bench_run
bench_run:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbp
    sub     rsp, 88
    mov     [rsp + 8], edx                  /* json */
    mov     [rsp + 12], ecx                 /* workers */
    call    scrambles_load
    test    eax, eax
    je      .Lbr_fail

    lea     rdi, [rip + .Lhardest_text]
    lea     rsi, [rip + bench_hardest]
    call    cube_parse
    test    eax, eax
    jne     .Lbr_fail

    call    coords_init
    mov     eax, [rsp + 12]
    test    eax, eax
    jg      1f
    call    table_default_workers
    mov     [rsp + 12], eax
1:
    /* setup: three alternating samples, fastest kept */
    xor     r15d, r15d
2:  call    ida_invalidate
    call    now_ms
    movsd   [rsp + 16], xmm0
    call    ida_prepare
    call    now_ms
    subsd   xmm0, [rsp + 16]
    lea     rdi, [rip + bench_results + 1*R_SIZE + R_SETUP]
    mov     esi, r15d
    call    keep_fastest

    call    now_ms
    movsd   [rsp + 16], xmm0
    mov     edi, 1
    call    table_create
    mov     rbx, rax
    call    now_ms
    subsd   xmm0, [rsp + 16]
    lea     rdi, [rip + bench_results + 2*R_SIZE + R_SETUP]
    mov     esi, r15d
    call    keep_fastest
    mov     rdi, rbx
    call    table_free

    call    now_ms
    movsd   [rsp + 16], xmm0
    mov     edi, [rsp + 12]
    call    table_create
    mov     rbx, rax
    call    now_ms
    subsd   xmm0, [rsp + 16]
    lea     rdi, [rip + bench_results + 3*R_SIZE + R_SETUP]
    mov     esi, r15d
    call    keep_fastest
    mov     rdi, rbx
    call    table_free

    inc     r15d
    cmp     r15d, 3
    jb      2b

    /* the copies that stay live for the solve measurements */
    call    ida_prepare
    mov     edi, 1
    call    table_create
    test    rax, rax
    je      .Lbr_fail
    mov     [rip + bench_table], rax
    mov     r13, rax
    mov     edi, [rsp + 12]
    call    table_create
    test    rax, rax
    je      .Lbr_fail
    mov     r14, rax
    mov     eax, [rip + table_last_workers]
    mov     [rip + bench_workers], eax

    /* names */
    lea     rax, [rip + .Lname_bfs]
    mov     [rip + bench_results + 0*R_SIZE + R_NAME], rax
    lea     rax, [rip + .Lname_ida]
    mov     [rip + bench_results + 1*R_SIZE + R_NAME], rax
    lea     rax, [rip + .Lname_table]
    mov     [rip + bench_results + 2*R_SIZE + R_NAME], rax
    lea     rax, [rip + .Lname_tablepar]
    mov     [rip + bench_results + 3*R_SIZE + R_NAME], rax
    pxor    xmm0, xmm0
    movsd   [rip + bench_results + 0*R_SIZE + R_SETUP], xmm0
    mov     eax, [rip + bench_workers]
    mov     [rip + bench_results + 3*R_SIZE + R_WORKERS], eax

    /* one pass per algorithm */
    xor     ebx, ebx
3:  cmp     ebx, 3
    jae     4f
    lea     rbp, [rip + bench_results]
    imul    rax, rbx, R_SIZE
    add     rbp, rax
    mov     edi, ebx
    call    bench_avg
    movsd   [rbp + R_AVG], xmm0
    mov     edi, ebx
    call    bench_worst
    movsd   [rbp + R_WORST], xmm0
    cvtsi2sd xmm0, qword ptr [rip + bench_peak]
    divsd   xmm0, [rip + .Lmillion]
    movsd   [rbp + R_PEAK], xmm0
    mov     edi, ebx
    call    bench_optimal
    mov     [rbp + R_OPT], eax
    inc     ebx
    jmp     3b

4:  /* only the build is parallelised, so the per-solve figures are the serial
     * table's by definition; what must be checked is that the two builds
     * agree byte for byte. */
    lea     rbp, [rip + bench_results]
    movsd   xmm0, [rbp + 2*R_SIZE + R_AVG]
    movsd   [rbp + 3*R_SIZE + R_AVG], xmm0
    movsd   xmm0, [rbp + 2*R_SIZE + R_WORST]
    movsd   [rbp + 3*R_SIZE + R_WORST], xmm0
    movsd   xmm0, [rbp + 2*R_SIZE + R_PEAK]
    movsd   [rbp + 3*R_SIZE + R_PEAK], xmm0
    mov     rdi, r13
    mov     rsi, r14
    mov     edx, NSTATES
    call    memcmp@PLT
    test    eax, eax
    jne     5f
    mov     eax, [rbp + 2*R_SIZE + R_OPT]
    jmp     6f
5:  xor     eax, eax
6:  mov     [rbp + 3*R_SIZE + R_OPT], eax

    cmp     dword ptr [rsp + 8], 0
    je      7f
    call    print_json
    jmp     8f
7:  call    print_human
8:  xor     eax, eax
    jmp     .Lbr_out
.Lbr_fail:
    mov     eax, 1
.Lbr_out:
    add     rsp, 88
    pop     rbp
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

print_json:
    push    rbx
    push    r12
    sub     rsp, 8
    lea     rdi, [rip + .Ljson_head]
    lea     rsi, [rip + bench_version]
    lea     rdx, [rip + bench_build]
    xor     eax, eax
    call    printf@PLT
    xor     ebx, ebx
1:  cmp     ebx, 4
    jae     4f
    lea     r12, [rip + bench_results]
    imul    rax, rbx, R_SIZE
    add     r12, rax
    mov     rsi, [r12 + R_NAME]
    lea     rdx, [rip + .Ltrue]
    cmp     dword ptr [r12 + R_OPT], 0
    jne     2f
    lea     rdx, [rip + .Lfalse]
2:  movsd   xmm0, [r12 + R_SETUP]
    movsd   xmm1, [r12 + R_AVG]
    movsd   xmm2, [r12 + R_WORST]
    movsd   xmm3, [r12 + R_PEAK]
    lea     rdi, [rip + .Ljson_row]
    mov     eax, 4
    call    printf@PLT
    cmp     dword ptr [r12 + R_WORKERS], 0
    je      3f
    mov     esi, [r12 + R_WORKERS]
    lea     rdi, [rip + .Ljson_workers]
    xor     eax, eax
    call    printf@PLT
3:  lea     rsi, [rip + .Lcomma_nl]
    lea     rax, [rip + .Lnl]
    cmp     ebx, 3
    jne     31f
    mov     rsi, rax
31: lea     rdi, [rip + .Ljson_rowend]
    xor     eax, eax
    call    printf@PLT
    inc     ebx
    jmp     1b
4:  lea     rdi, [rip + .Ljson_tail]
    xor     eax, eax
    call    printf@PLT
    add     rsp, 8
    pop     r12
    pop     rbx
    ret

print_human:
    push    rbx
    push    r12
    sub     rsp, 8
    mov     esi, [rip + bench_count]
    lea     rdi, [rip + .Lhuman_head]
    xor     eax, eax
    call    printf@PLT
    xor     ebx, ebx
1:  cmp     ebx, 4
    jae     2f
    lea     r12, [rip + bench_results]
    imul    rax, rbx, R_SIZE
    add     r12, rax
    mov     rsi, [r12 + R_NAME]
    lea     rdx, [rip + .Lyes]
    cmp     dword ptr [r12 + R_OPT], 0
    jne     11f
    lea     rdx, [rip + .Lno]
11: movsd   xmm0, [r12 + R_SETUP]
    movsd   xmm1, [r12 + R_AVG]
    movsd   xmm2, [r12 + R_WORST]
    movsd   xmm3, [r12 + R_PEAK]
    lea     rdi, [rip + .Lhuman_row]
    mov     eax, 4
    call    printf@PLT
    inc     ebx
    jmp     1b
2:  add     rsp, 8
    pop     r12
    pop     rbx
    ret

    .section .rodata
    .balign 8
.Lthousand:     .double 1000.0
.Lmillion:      .double 1000000.0
.Ltarget:       .double 250.0
.Lmode_r:       .asciz "r"
.Lhardest_text: .asciz "wppryygrygrwgprbbbwpbwyg"
.Lname_bfs:     .asciz "bfs"
.Lname_ida:     .asciz "ida"
.Lname_table:   .asciz "table"
.Lname_tablepar:.asciz "table-par"
.Ltrue:         .asciz "true"
.Lfalse:        .asciz "false"
.Lyes:          .asciz "yes"
.Lno:           .asciz "NO"
.Lcomma_nl:     .asciz ",\n"
.Lnl:           .asciz "\n"
.Lmsg_noopen:   .asciz "cannot open %s\n"
.Lmsg_badscramble: .asciz "malformed scramble in the scrambles file\n"
.Ljson_head:
    .ascii "{\n"
    .ascii "  \"language\": \"Assembly\",\n"
    .ascii "  \"version\": \"%s\",\n"
    .ascii "  \"build\": \"%s\",\n"
    .asciz "  \"algorithms\": [\n"
.Ljson_row:
    .asciz "    {\"name\": \"%s\", \"setup_ms\": %.3f, \"avg_ms\": %.4f, \"worst_ms\": %.4f, \"peak_mb\": %.2f, \"optimal\": %s"
.Ljson_workers: .asciz ", \"workers\": %d"
.Ljson_rowend:  .asciz "}%s"
.Ljson_tail:    .asciz "  ]\n}\n"
.Lhuman_head:
    .ascii "rubik2x2 benchmark -- %d scrambles plus the antipodal position\n"
    .ascii "3674160 reachable states, maximum distance 14 quarter turns\n\n"
    .ascii "algorithm     setup ms    avg ms   worst ms   peak MB  optimal\n"
    .asciz "--------------------------------------------------------------\n"
.Lhuman_row:    .asciz "%-11s%11.1f%10.4f%11.4f%10.2f%9s\n"

    .bss
    .balign 64
    .globl bench_states
bench_states:   .space 8
bench_count:    .space 4
bench_workers:  .space 4
bench_table:    .space 8
bench_peak:     .space 8
bench_hardest:  .space 32
bench_results:  .space 4 * R_SIZE

    .section .note.GNU-stack,"",@progbits
