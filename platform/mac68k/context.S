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
    move.l 0x0110,4(%a0)
    /* The VBL sniffer must be off while A7 is in transition. Heap-backed
     * contexts keep it off; the original application context restores it. */
    clr.l 0x0110
    move.l (%a1),%sp
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    move.l 4(%a1),0x0110
    rts

/* Inherit the enclosing application's address registers, especially A5,
 * when priming a fresh stack for its first return into C. */
    .globl cb_mac_context_seed
cb_mac_context_seed:
    move.l 4(%sp),%a0
    movem.l %d2-%d7/%a2-%a6,(%a0)
    rts
