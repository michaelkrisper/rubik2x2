/* table.s -- God's algorithm: the distance from every one of the 3,674,160
 * states to solved, in one byte each, then solving by walking downhill.
 *
 * Breadth-first from index 0 reaches every state exactly once, so a single
 * NSTATES-entry queue holds the whole search: each level is a consecutive
 * range in it and nothing ever grows.  Distances are stored as distance+1, so
 * a zero byte still means "not reached yet" while the search runs.
 *
 * The build is the one place in this program where parallelism can pay.  The
 * search is level-synchronous, so a level's frontier can be cut into chunks
 * across threads; the only shared mutable state is the distance array, and two
 * threads can reach the same child in the same instant.  Settled here with one
 * relaxed `lock cmpxchg` per edge, guarded by a plain load: about 85% of edges
 * lead somewhere already visited, and those never execute a locked
 * instruction.  Queue slots are claimed in blocks of 512 so the shared tail is
 * touched once per block instead of once per state.
 */

    .intel_syntax noprefix
    .include "rubik.inc"

    .equ PAR_CHUNK,     4096
    .equ PAR_BATCH,     512
    .equ PAR_MAXWORK,   64

/* par_ctx field offsets */
    .equ PC_DIST,       0
    .equ PC_QUEUE,      8
    .equ PC_TAIL,       16
    .equ PC_CURSOR,     20
    .equ PC_LEVELEND,   24
    .equ PC_MARKER,     28
    .equ PC_DONE,       32
    .equ PC_WORKERS,    36
    .equ PC_BARRIER,    64
    .equ PC_MUTEX,      128
    .equ PC_SIZE,       192

    .text

/* Expand one node: child = perm_wide[q][m] + orient_child[r][m].
 * r10 = permutation row, r11 = orientation row, r12 = distance array,
 * r13 = queue, r8 = tail, r9b = level marker. */
    .macro PREFETCH_AHEAD
    /* Name the cache lines this loop will want a few frontier entries from now.
     * Every STEP touches one random byte of a 3.7 MB array that does not fit in
     * this machine's 3 MiB L3, so the loop waits on memory, not on arithmetic.
     * Recomputing an address early costs a multiply and lets the miss overlap
     * with real work instead of stalling on it. Measured: 155 -> 140 ms. */
    lea     esi, [rbx + PREFETCH_LOOKAHEAD]
    cmp     esi, ebp
    jae     9100f
    mov     ecx, [r13 + rsi*4]
    mov     eax, ecx
    imul    rax, rax, MAGIC729
    shr     rax, 32
    mov     edx, eax
    imul    edx, edx, NORIENT
    sub     ecx, edx
    shl     rax, PERMROW_SH
    add     rax, r14
    mov     esi, ecx
    shl     rsi, ORIENTROW_SH
    add     rsi, r15
    .irp poff, 0, 4, 8, 12, 16, 20
    mov     ecx, [rax + \poff]
    add     ecx, [rsi + \poff]
    prefetcht0 [r12 + rcx]
    .endr
9100:
    .endm

    .macro STEP off
    mov     eax, [r10 + \off]
    add     eax, [r11 + \off]
    cmp     byte ptr [r12 + rax], 0
    jne     9000f
    mov     byte ptr [r12 + rax], r9b
    mov     [r13 + r8*4], eax
    inc     r8
9000:
    .endm

/* table_default_workers() -> eax */
    .globl table_default_workers
table_default_workers:
    sub     rsp, 8
    mov     edi, 84                         /* _SC_NPROCESSORS_ONLN */
    call    sysconf@PLT
    cmp     rax, 1
    jg      1f
    mov     eax, 1
1:  cmp     eax, PAR_MAXWORK
    jle     2f
    mov     eax, PAR_MAXWORK
2:  add     rsp, 8
    ret

/* table_build(rdi=distance, rsi=queue) -- serial breadth-first from solved. */
    .globl table_build
table_build:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    mov     r12, rdi
    mov     r13, rsi
    lea     r14, [rip + perm_wide]
    lea     r15, [rip + orient_child]
    mov     byte ptr [r12 + SOLVED_INDEX], 1
    mov     dword ptr [r13], SOLVED_INDEX
    xor     ebx, ebx                        /* begin */
    mov     ebp, 1                          /* end */
    mov     r8d, 1                          /* tail */
    mov     r9d, 2                          /* marker = distance + 1 */
1:  cmp     ebx, ebp
    jae     9f
2:  PREFETCH_AHEAD
    mov     ecx, [r13 + rbx*4]
    mov     eax, ecx
    imul    rax, rax, MAGIC729
    shr     rax, 32                         /* q */
    mov     edx, eax
    imul    edx, edx, NORIENT
    sub     ecx, edx                        /* r */
    shl     rax, PERMROW_SH
    add     rax, r14
    mov     r10, rax
    mov     eax, ecx
    shl     rax, ORIENTROW_SH
    lea     r11, [r15 + rax]
    STEP 0
    STEP 4
    STEP 8
    STEP 12
    STEP 16
    STEP 20
    inc     ebx
    cmp     ebx, ebp
    jb      2b
    mov     ebp, r8d
    inc     r9d
    jmp     1b
9:  pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret

/* table_create(edi=workers) -> rax distance array, or 0.
 * workers <= 1 builds serially. */
    .globl table_create
table_create:
    push    rbx
    push    r12
    push    r13
    sub     rsp, 16
    mov     ebx, edi
    mov     edi, NSTATES
    mov     esi, 1
    call    calloc@PLT
    test    rax, rax
    je      .Ltc_fail
    mov     r12, rax                        /* distance */
    mov     edi, NSTATES
    mov     esi, 4
    call    calloc@PLT
    test    rax, rax
    je      .Ltc_freedist
    mov     r13, rax                        /* queue */
    mov     rdi, r12
    mov     rsi, r13
    cmp     ebx, 1
    jle     1f
    mov     edx, ebx
    call    table_build_parallel
    mov     [rip + table_last_workers], eax
    jmp     2f
1:  mov     dword ptr [rip + table_last_workers], 1
    call    table_build
2:  mov     rdi, r13
    call    free@PLT
    mov     rax, r12
    jmp     .Ltc_out
.Ltc_freedist:
    mov     rdi, r12
    call    free@PLT
.Ltc_fail:
    xor     eax, eax
.Ltc_out:
    add     rsp, 16
    pop     r13
    pop     r12
    pop     rbx
    ret

/* table_free(rdi) */
    .globl table_free
table_free:
    test    rdi, rdi
    je      1f
    jmp     free@PLT
1:  ret

/* table_histogram(rdi=distance, rsi=uint32 counts[16]) -> eax highest marker */
    .globl table_histogram
table_histogram:
    xor     ecx, ecx
1:  mov     dword ptr [rsi + rcx*4], 0
    inc     ecx
    cmp     ecx, 16
    jb      1b
    xor     r8d, r8d                        /* highest */
    xor     ecx, ecx
2:  movzx   eax, byte ptr [rdi + rcx]
    test    eax, eax
    je      3f
    cmp     eax, 15
    ja      3f
    inc     dword ptr [rsi + rax*4 - 4]
    cmp     eax, r8d
    jbe     3f
    mov     r8d, eax
3:  inc     ecx
    cmp     ecx, NSTATES
    jb      2b
    mov     eax, r8d
    ret

/* table_distance(rdi=distance, esi=index) -> eax, or -1 if unreached */
    .globl table_distance
table_distance:
    movzx   eax, byte ptr [rdi + rsi]
    dec     eax
    ret

/* table_solve_index(edi=index, rsi=solution, rdx=distance) -> eax status
 *
 * At distance d exactly one neighbour is at d-1, so the walk never searches:
 * it reads six bytes and takes the one that goes down. */
    .globl table_solve_index
table_solve_index:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    mov     r13, rsi
    mov     qword ptr [r13 + SOL_PEAK], NSTATES
    mov     dword ptr [r13 + SOL_LEN], 0
    mov     byte ptr [r13 + SOL_MOVES], 0
    mov     r12, rdx
    mov     ebx, edi                        /* index */
    movzx   ebp, byte ptr [r12 + rbx]
    test    ebp, ebp
    je      .Lts_invalid
    dec     ebp                             /* remaining */
    lea     r14, [rip + perm_wide]
    lea     r15, [rip + orient_child]
    xor     r9d, r9d                        /* length */
1:  test    ebp, ebp
    je      .Lts_done
    mov     ecx, ebx
    mov     eax, ebx
    imul    rax, rax, MAGIC729
    shr     rax, 32
    mov     edx, eax
    imul    edx, edx, NORIENT
    sub     ecx, edx
    shl     rax, PERMROW_SH
    add     rax, r14
    mov     r10, rax
    mov     eax, ecx
    shl     rax, ORIENTROW_SH
    lea     r11, [r15 + rax]
    lea     edi, [rbp - 1]                  /* the marker we want, +1 */
    xor     ecx, ecx
2:  mov     eax, [r10 + rcx*4]
    add     eax, [r11 + rcx*4]
    movzx   edx, byte ptr [r12 + rax]
    dec     edx
    cmp     edx, edi
    je      3f
    inc     ecx
    cmp     ecx, NSOLVE
    jb      2b
    mov     eax, SV_NOSOL
    jmp     .Lts_out
3:  lea     rdx, [rip + solve_move_chars]
    movzx   edx, byte ptr [rdx + rcx]
    mov     [r13 + r9], dl
    inc     r9d
    mov     ebx, eax
    dec     ebp
    jmp     1b
.Lts_done:
    mov     byte ptr [r13 + r9], 0
    mov     [r13 + SOL_LEN], r9d
    xor     eax, eax
    jmp     .Lts_out
.Lts_invalid:
    mov     eax, SV_INVALID
.Lts_out:
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret

/* table_solve(rdi=state24, rsi=solution, rdx=distance) -> eax status */
    .globl table_solve
table_solve:
    push    rbx
    push    r12
    sub     rsp, 24
    mov     rbx, rsi
    mov     r12, rdx
    lea     rsi, [rsp]
    call    coords_encode
    test    eax, eax
    jne     1f
    mov     edi, [rsp]
    mov     rsi, rbx
    mov     rdx, r12
    call    table_solve_index
    jmp     2f
1:  mov     eax, SV_INVALID
2:  add     rsp, 24
    pop     r12
    pop     rbx
    ret

/* ------------------------------------------------------- parallel build */

/* par_work(rdi=ctx) -- claim chunks of the current level until it is used up */
par_work:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 2072
    mov     r15, rdi                        /* ctx */
    mov     r12, [rdi + PC_DIST]
    mov     r13, [rdi + PC_QUEUE]
    movzx   r9d, byte ptr [rdi + PC_MARKER]
    lea     r14, [rip + perm_wide]
    mov     qword ptr [rsp + 2056], 0       /* held */
    lea     rbp, [rip + orient_child]
.Lpw_chunk:
    mov     eax, PAR_CHUNK
    lock xadd dword ptr [r15 + PC_CURSOR], eax
    mov     ebx, eax                        /* from */
    mov     edx, [r15 + PC_LEVELEND]
    cmp     ebx, edx
    jae     .Lpw_flush
    lea     ecx, [rbx + PAR_CHUNK]
    cmp     ecx, edx
    jbe     1f
    mov     ecx, edx
1:  mov     [rsp + 2064], ecx               /* to */
.Lpw_node:
    mov     ecx, [r13 + rbx*4]
    mov     eax, ecx
    imul    rax, rax, MAGIC729
    shr     rax, 32
    mov     edx, eax
    imul    edx, edx, NORIENT
    sub     ecx, edx
    shl     rax, PERMROW_SH
    lea     r10, [r14 + rax]
    mov     eax, ecx
    shl     rax, ORIENTROW_SH
    lea     r11, [rbp + rax]
    xor     r8d, r8d                        /* move -- not rdi: the publish
                                             * helper below uses rdi */
.Lpw_move:
    mov     eax, [r10 + r8*4]
    add     eax, [r11 + r8*4]
    cmp     byte ptr [r12 + rax], 0
    jne     .Lpw_next
    mov     ecx, eax
    xor     eax, eax
    lock cmpxchg byte ptr [r12 + rcx], r9b
    jne     .Lpw_next
    mov     rax, [rsp + 2056]
    mov     [rsp + rax*4], ecx
    inc     rax
    mov     [rsp + 2056], rax
    cmp     rax, PAR_BATCH
    jb      .Lpw_next
    call    .Lpw_publish
.Lpw_next:
    inc     r8d
    cmp     r8d, NSOLVE
    jb      .Lpw_move
    inc     ebx
    cmp     ebx, [rsp + 2064]
    jb      .Lpw_node
    jmp     .Lpw_chunk
.Lpw_flush:
    cmp     qword ptr [rsp + 2056], 0
    je      1f
    call    .Lpw_publish
1:  add     rsp, 2072
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret

/* Local helper: append the held batch to the shared queue.  Called, not
 * inlined, so the hot path stays short; the call is taken once per 512
 * newly claimed states. */
.Lpw_publish:
    mov     eax, [rsp + 8 + 2056]           /* held (return address pushed) */
    mov     edx, eax
    lock xadd dword ptr [r15 + PC_TAIL], eax
    lea     rdi, [r13 + rax*4]
    lea     rsi, [rsp + 8]
    xor     ecx, ecx
1:  mov     eax, [rsi + rcx*4]
    mov     [rdi + rcx*4], eax
    inc     ecx
    cmp     ecx, edx
    jb      1b
    mov     qword ptr [rsp + 8 + 2056], 0
    ret

/* par_worker(rdi=ctx) -- pthread entry */
par_worker:
    push    rbx
    sub     rsp, 16
    mov     rbx, rdi
    lea     rdi, [rbx + PC_MUTEX]
    call    pthread_mutex_lock@PLT
    lea     rdi, [rbx + PC_MUTEX]
    call    pthread_mutex_unlock@PLT
1:  lea     rdi, [rbx + PC_BARRIER]
    call    pthread_barrier_wait@PLT
    cmp     dword ptr [rbx + PC_DONE], 0
    jne     2f
    mov     rdi, rbx
    call    par_work
    lea     rdi, [rbx + PC_BARRIER]
    call    pthread_barrier_wait@PLT
    jmp     1b
2:  xor     eax, eax
    add     rsp, 16
    pop     rbx
    ret

/* table_build_parallel(rdi=distance, rsi=queue, edx=workers) */
    .globl table_build_parallel
table_build_parallel:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 8 + PAR_MAXWORK*8
    mov     r12, rdi
    mov     r13, rsi
    mov     r14d, edx
    cmp     r14d, PAR_MAXWORK
    jle     1f
    mov     r14d, PAR_MAXWORK
1:  lea     r15, [rip + par_ctx]
    mov     [r15 + PC_DIST], r12
    mov     [r15 + PC_QUEUE], r13
    mov     dword ptr [r15 + PC_TAIL], 1
    mov     dword ptr [r15 + PC_CURSOR], 0
    mov     dword ptr [r15 + PC_LEVELEND], 1
    mov     dword ptr [r15 + PC_MARKER], 2
    mov     dword ptr [r15 + PC_DONE], 0
    mov     [r15 + PC_WORKERS], r14d
    mov     byte ptr [r12 + SOLVED_INDEX], 1
    mov     dword ptr [r13], SOLVED_INDEX

    lea     rdi, [r15 + PC_MUTEX]
    xor     esi, esi
    call    pthread_mutex_init@PLT
    lea     rdi, [r15 + PC_BARRIER]
    xor     esi, esi
    mov     edx, r14d
    call    pthread_barrier_init@PLT

    /* Hold the gate until every thread exists: a worker must not reach the
     * barrier before its participant count is final. */
    lea     rdi, [r15 + PC_MUTEX]
    call    pthread_mutex_lock@PLT
    xor     ebx, ebx
2:  lea     eax, [r14 - 1]
    cmp     ebx, eax
    jae     3f
    lea     rdi, [rsp + 8]
    lea     rdi, [rdi + rbx*8]
    xor     esi, esi
    lea     rdx, [rip + par_worker]
    mov     rcx, r15
    call    pthread_create@PLT
    test    eax, eax
    jne     3f
    inc     ebx
    jmp     2b
3:  lea     eax, [r14 - 1]
    cmp     ebx, eax
    je      4f
    /* fewer threads than asked for; none has passed the gate, so resize */
    lea     r14d, [rbx + 1]
    mov     [r15 + PC_WORKERS], r14d
    lea     rdi, [r15 + PC_BARRIER]
    call    pthread_barrier_destroy@PLT
    lea     rdi, [r15 + PC_BARRIER]
    xor     esi, esi
    mov     edx, r14d
    call    pthread_barrier_init@PLT
4:  lea     rdi, [r15 + PC_MUTEX]
    call    pthread_mutex_unlock@PLT

    xor     ebp, ebp                        /* begin */
5:  mov     eax, [r15 + PC_LEVELEND]
    xor     ecx, ecx
    cmp     ebp, eax
    jb      6f
    mov     ecx, 1
6:  mov     [r15 + PC_DONE], ecx
    mov     [r15 + PC_CURSOR], ebp
    lea     rdi, [r15 + PC_BARRIER]
    call    pthread_barrier_wait@PLT
    cmp     dword ptr [r15 + PC_DONE], 0
    jne     7f
    mov     rdi, r15
    call    par_work
    lea     rdi, [r15 + PC_BARRIER]
    call    pthread_barrier_wait@PLT
    mov     ebp, [r15 + PC_LEVELEND]
    mov     eax, [r15 + PC_TAIL]
    mov     [r15 + PC_LEVELEND], eax
    inc     dword ptr [r15 + PC_MARKER]
    jmp     5b

7:  xor     r13d, r13d
8:  cmp     r13d, ebx
    jae     9f
    lea     rax, [rsp + 8]
    mov     rdi, [rax + r13*8]
    xor     esi, esi
    call    pthread_join@PLT
    inc     r13d
    jmp     8b
9:  lea     rdi, [r15 + PC_BARRIER]
    call    pthread_barrier_destroy@PLT
    lea     rdi, [r15 + PC_MUTEX]
    call    pthread_mutex_destroy@PLT
    mov     eax, [r15 + PC_WORKERS]
    add     rsp, 8 + PAR_MAXWORK*8
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret

    .bss
    .balign 64
par_ctx:    .space PC_SIZE
    .globl table_last_workers
table_last_workers: .space 4

    .section .note.GNU-stack,"",@progbits
