#include "root_dispatch.h"
#include <stddef.h>

extern void cb_mac_context_swap(struct cb_host_context *, struct cb_host_context *);
struct cb_mac_root_call {
    void (*function)(void *);
    void *argument;
    struct cb_host_context *requester;
};

void cb_mac_dispatch_call(struct cb_mac_dispatch *dispatch,
                         void (*function)(void *), void *argument)
{
    struct cb_mac_root_call call = {function, argument, dispatch->active};
    if (dispatch->active == dispatch->root) { function(argument); return; }
    dispatch->pending = &call;
    cb_mac_context_swap(dispatch->active, dispatch->root);
}

void cb_mac_dispatch_switch(struct cb_mac_dispatch *dispatch,
                           struct cb_host_context *from,
                           struct cb_host_context *to)
{
    do {
        dispatch->active = to;
        cb_mac_context_swap(from, to);
        dispatch->active = from;
        if (from != dispatch->root || dispatch->pending == NULL) break;
        {
            struct cb_mac_root_call *call = dispatch->pending;
            dispatch->pending = NULL;
            /* The requester may be a nested child of the original target. */
            to = call->requester;
            call->function(call->argument);
        }
        /* Service requests immediately; only an ordinary context switch
         * returns to the scheduler and allows another task to run. */
    } while (1);
}
