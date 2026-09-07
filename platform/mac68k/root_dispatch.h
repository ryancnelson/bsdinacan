#ifndef CB_MAC_ROOT_DISPATCH_H
#define CB_MAC_ROOT_DISPATCH_H

struct cb_host_context;
struct cb_mac_root_call;
struct cb_mac_dispatch {
    struct cb_host_context *root, *active;
    struct cb_mac_root_call *pending;
};
void cb_mac_dispatch_call(struct cb_mac_dispatch *, void (*)(void *), void *);
void cb_mac_dispatch_switch(struct cb_mac_dispatch *, struct cb_host_context *,
                            struct cb_host_context *);

#endif
