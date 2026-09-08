#ifndef CB_HOST_MAC_H
#define CB_HOST_MAC_H

#include "internal.h"

const struct cb_host_ops_v1 *cb_mac_host_ops(void);
int cb_mac_initialize(void);
void cb_mac_pump(int timeout_ms);
void cb_mac_text(const char *text);
void cb_mac_capture_begin(void);
int cb_mac_capture_matches(const char *expected);
int cb_mac_write_result(const char *text);
int cb_mac_autorun_requested(void);
int cb_mac_capture_screen(void);
int cb_mac_write_done(const char *text);
int cb_mac_context_check(void);
int cb_mac_quitting(void);
void cb_mac_shutdown(void);

#endif
