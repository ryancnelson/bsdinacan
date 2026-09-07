/* Retro68's C ABI: stack arguments, d2-d7/a2-a6 callee-saved.
 * The build uses the default 68000 software-float ABI; no FPU state is used.
 * saved_sp is the first field of cb_host_context. */
    .text
    .globl cb_mac_context_swap
cb_mac_context_swap:
    move.l 4(%sp),%a0
    move.l 8(%sp),%a1
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.l %sp,(%a0)
    move.l (%a1),%sp
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

/* Inherit the enclosing application's address registers, especially A5,
 * when priming a fresh stack for its first return into C. */
    .globl cb_mac_context_seed
cb_mac_context_seed:
    move.l 4(%sp),%a0
    movem.l %d2-%d7/%a2-%a6,(%a0)
    rts
