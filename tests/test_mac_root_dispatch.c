#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include "root_dispatch.h"

struct cb_host_context { ucontext_t native; };
static struct cb_host_context root, child, first, second;
static struct cb_mac_dispatch dispatch = {&root, &root, NULL};
static unsigned calls, child_turns;
static char child_stack[65536], first_stack[65536], second_stack[65536];
static unsigned stage, nested_calls;

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

static void nested_service(void *argument)
{
    char marker;
    uintptr_t where = (uintptr_t)&marker;
    assert(dispatch.active == &root && dispatch.pending == NULL);
    assert(where < (uintptr_t)first_stack ||
           where >= (uintptr_t)first_stack + sizeof(first_stack));
    assert(where < (uintptr_t)second_stack ||
           where >= (uintptr_t)second_stack + sizeof(second_stack));
    *(unsigned *)argument = ++nested_calls;
}

static void outer_service(void *argument)
{
    assert(dispatch.active == &root && dispatch.pending == NULL);
    cb_mac_dispatch_call(&dispatch, nested_service, argument);
}

static void second_entry(void)
{
    volatile unsigned cookie[16];
    unsigned index, result = 0;
    assert(stage == 1);
    for (index = 0; index < 16; ++index) cookie[index] = index ^ 0x5a5a;
    stage = 2;
    cb_mac_dispatch_call(&dispatch, outer_service, &result);
    assert(dispatch.active == &second && result == 1);
    for (index = 0; index < 16; ++index) assert(cookie[index] == (index ^ 0x5a5a));
    stage = 3;
    cb_mac_dispatch_switch(&dispatch, &second, &root);
    assert(stage == 7 && dispatch.active == &second);
    for (index = 0; index < 16; ++index) assert(cookie[index] == (index ^ 0x5a5a));
    stage = 8;
    cb_mac_dispatch_switch(&dispatch, &second, &root);
    abort();
}

static void first_entry(void)
{
    volatile unsigned cookie = 0x1234;
    unsigned result = 0;
    stage = 1;
    cb_mac_dispatch_switch(&dispatch, &first, &second);
    /* Only the scheduler's later explicit resume may continue this caller. */
    assert(stage == 4 && dispatch.active == &first);
    assert(cookie == 0x1234);
    stage = 5;
    cb_mac_dispatch_call(&dispatch, outer_service, &result);
    assert(dispatch.active == &first && result == 2 && cookie == 0x1234);
    stage = 6;
    cb_mac_dispatch_switch(&dispatch, &first, &root);
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
    assert(getcontext(&first.native) == 0);
    first.native.uc_stack.ss_sp = first_stack;
    first.native.uc_stack.ss_size = sizeof(first_stack);
    first.native.uc_link = NULL;
    makecontext(&first.native, first_entry, 0);
    assert(getcontext(&second.native) == 0);
    second.native.uc_stack.ss_sp = second_stack;
    second.native.uc_stack.ss_size = sizeof(second_stack);
    second.native.uc_link = NULL;
    makecontext(&second.native, second_entry, 0);
    cb_mac_dispatch_switch(&dispatch, &root, &first);
    assert(stage == 3 && dispatch.active == &root && dispatch.pending == NULL);
    stage = 4;
    cb_mac_dispatch_switch(&dispatch, &root, &first);
    assert(stage == 6 && dispatch.active == &root && dispatch.pending == NULL);
    stage = 7;
    cb_mac_dispatch_switch(&dispatch, &root, &second);
    assert(stage == 8 && dispatch.active == &root && dispatch.pending == NULL);
    assert(nested_calls == 2);
    puts("Mac root dispatch tests passed");
    return 0;
}
