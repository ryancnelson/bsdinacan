#ifndef CANNEDBSD_TERMINAL_H
#define CANNEDBSD_TERMINAL_H

#include "cannedbsd/abi.h"

/* Isolated canonical engine. Caller-owned, no host calls or allocation.
   TERM-04 owns configurable attributes and raw/canonical transitions. */
#define CB_TERM_BYTES CB_PATH_MAX
#define CB_TERM_RECORDS 16
struct cb_terminal_engine {
    unsigned char edit[CB_TERM_BYTES];
    unsigned char fifo[CB_TERM_BYTES];
    size_t edit_used, fifo_head, fifo_used;
    size_t records[CB_TERM_RECORDS];
    size_t record_head, record_count;
    unsigned char echo[3];
    size_t echo_offset, echo_used;
    int pending, source_eof;
};

void cb_terminal_init(struct cb_terminal_engine *t);
int cb_terminal_can_accept(const struct cb_terminal_engine *t);
/* 1 consumes the byte (including deliberate overflow discard), 0 defers it.
   Fixed defaults: canonical, echo, CR->NL, erase=8, VEOF=4. */
int cb_terminal_feed(struct cb_terminal_engine *t, unsigned char byte);
/* Explicit physical EOF notification, not a byte or transient unavailability. */
void cb_terminal_source_eof(struct cb_terminal_engine *t);
int cb_terminal_ready(const struct cb_terminal_engine *t);
/* Non-NULL t and count are required by all operations taking them.
   Read additionally rejects a NULL count without mutation.
   1: completed read, including zero count/EOF; 0: not ready; -1: invalid
   destination. On zero count no event is consumed. No errno or host binding. */
int cb_terminal_read(struct cb_terminal_engine *t, void *buffer, size_t capacity,
                     size_t *count);
/* Borrowed suffix stays valid until the next mutating operation. Acknowledge
   actual bytes only: zero preserves it; over-ack returns -1 without mutation.
   This is not a host write result or a host error policy. */
const unsigned char *cb_terminal_echo(const struct cb_terminal_engine *t,
                                      size_t *count);
int cb_terminal_echo_ack(struct cb_terminal_engine *t, size_t count);

#endif
