#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include "root_dispatch.h"

struct cb_host_context { ucontext_t native; };
static struct cb_host_context root, child;
static struct cb_mac_dispatch dispatch = {&root, &root, NULL};
static unsigned calls, child_turns;
static char child_stack[65536];

void cb_mac_context_swap(struct cb_host_context *from, struct cb_host_context *to)
{
    assert(swapcontext(&from->native, &to->native) == 0);
}

static void service(void *argument)
{
    char marker;
    uintptr_t where = (uintptr_t)&marker;
    assert(dispatch.active == &root);
    assert(where < (uintptr_t)child_stack ||
           where >= (uintptr_t)child_stack + sizeof(child_stack));
    assert(child_turns == calls);
    ++calls;
    *(unsigned *)argument = calls;
}

static void child_entry(void)
{
    unsigned result, turn;
    for (turn = 0; turn < 32; ++turn) {
        result = 0;
        assert(dispatch.active == &child);
        cb_mac_dispatch_call(&dispatch, service, &result);
        assert(dispatch.active == &child);
        assert(result == turn + 1);
        ++child_turns;
    }
    cb_mac_dispatch_switch(&dispatch, &child, &root);
    abort();
}

int main(void)
{
    assert(getcontext(&child.native) == 0);
    child.native.uc_stack.ss_sp = child_stack;
    child.native.uc_stack.ss_size = sizeof(child_stack);
    child.native.uc_link = NULL;
    makecontext(&child.native, child_entry, 0);
    cb_mac_dispatch_switch(&dispatch, &root, &child);
    assert(dispatch.active == &root && dispatch.pending == NULL);
    /* Requests never returned to the scheduler between the child's turns. */
    assert(calls == 32 && child_turns == 32);
    puts("Mac root dispatch tests passed");
    return 0;
}
