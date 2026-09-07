#include "host_mac.h"

struct probe {
    const struct cb_host_ops_v1 *host;
    struct cb_host_context *root, *child;
    unsigned turns;
    int failed;
};

/* Apple develop 16, p129: StkLowPt=0 suppresses the VBL sniffer only while
 * A7 points into a private heap stack. These are low-memory reads, no traps. */
static uint32_t stack_low(void) { return *(volatile uint32_t *)0x0110; }
static uint32_t ticks(void) { return *(volatile uint32_t *)0x016a; }

static void child(void *argument)
{
    struct probe *probe = argument;
    volatile unsigned cookie[32];
    unsigned turn, index;
    for (index = 0; index < 32; ++index) cookie[index] = index ^ 0x5a5a;
    /* Keep each private stack active across several real VBL interrupts.
     * The old implementation can pass quick yield loops by missing a tick. */
    {
        uint32_t start = ticks();
        while ((uint32_t)(ticks() - start) < 8)
            if (stack_low() != 0) probe->failed = 1;
    }
    for (turn = 0; turn < 256; ++turn) {
        unsigned char *memory, *resized;
        if (stack_low() != 0) probe->failed = 1;
        memory = probe->host->allocate(16);
        if (memory == NULL) { probe->failed = 1; }
        else {
            memory[0] = (unsigned char)turn;
            resized = probe->host->resize(memory, 32);
            if (resized == NULL) { probe->failed = 1; }
            else { memory = resized; if (memory[0] != (unsigned char)turn) probe->failed = 1; }
            probe->host->release(memory);
        }
        if (stack_low() != 0) probe->failed = 1;
        for (index = 0; index < 32; ++index)
            if (cookie[index] != (index ^ 0x5a5a)) probe->failed = 1;
        probe->turns = turn + 1;
        probe->host->context_switch(probe->child, probe->root);
    }
    probe->failed = 1; /* the caller destroys this suspended context. */
    for (;;) probe->host->context_switch(probe->child, probe->root);
}

int cb_mac_context_check(void)
{
    struct probe first = {0}, second = {0};
    unsigned turn;
    uint32_t original_stack_low = stack_low();
    int passed = 0;
    first.host = second.host = cb_mac_host_ops();
    first.root = second.root = first.host->context_root();
    first.child = first.host->context_create(child, &first, 16384);
    second.child = first.host->context_create(child, &second, 16384);
    if (!first.root || !first.child || !second.child) goto done;
    for (turn = 0; turn < 256; ++turn) {
        first.host->context_switch(first.root, first.child);
        second.host->context_switch(second.root, second.child);
        if (original_stack_low != 0 && stack_low() == 0) goto done;
        if (first.turns != turn + 1 || second.turns != turn + 1) goto done;
    }
    passed = !first.failed && !second.failed;
done:
    first.host->context_destroy(first.child);
    second.host->context_destroy(second.child);
    first.host->context_destroy(first.root);
    return passed ? 0 : -1;
}
