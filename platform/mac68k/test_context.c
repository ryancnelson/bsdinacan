#include "host_mac.h"

struct probe {
    const struct cb_host_ops_v1 *host;
    struct cb_host_context *root, *child;
    unsigned turns;
    int failed;
};

static void child(void *argument)
{
    struct probe *probe = argument;
    volatile unsigned cookie[32];
    unsigned turn, index;
    for (index = 0; index < 32; ++index) cookie[index] = index ^ 0x5a5a;
    for (turn = 0; turn < 256; ++turn) {
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
    int passed = 0;
    first.host = second.host = cb_mac_host_ops();
    first.root = second.root = first.host->context_root();
    first.child = first.host->context_create(child, &first, 16384);
    second.child = first.host->context_create(child, &second, 16384);
    if (!first.root || !first.child || !second.child) goto done;
    for (turn = 0; turn < 256; ++turn) {
        first.host->context_switch(first.root, first.child);
        second.host->context_switch(second.root, second.child);
        if (first.turns != turn + 1 || second.turns != turn + 1) goto done;
    }
    passed = !first.failed && !second.failed;
done:
    first.host->context_destroy(first.child);
    second.host->context_destroy(second.child);
    first.host->context_destroy(first.root);
    return passed ? 0 : -1;
}
