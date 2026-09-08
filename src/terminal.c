#include "terminal.h"
#include <string.h>

/* Move one whole pending record, only when both fixed queues have room. */
static void commit_pending(struct cb_terminal_engine *t)
{
    size_t i, tail;
    if (!t->pending || t->record_count == CB_TERM_RECORDS ||
        t->edit_used > CB_TERM_BYTES - t->fifo_used) return;
    tail = (t->fifo_head + t->fifo_used) % CB_TERM_BYTES;
    for (i = 0; i < t->edit_used; ++i)
        t->fifo[(tail + i) % CB_TERM_BYTES] = t->edit[i];
    t->records[(t->record_head + t->record_count) % CB_TERM_RECORDS] = t->edit_used;
    ++t->record_count;
    t->fifo_used += t->edit_used;
    t->edit_used = 0;
    t->pending = 0;
}

void cb_terminal_init(struct cb_terminal_engine *t)
{
    memset(t, 0, sizeof(*t));
}

int cb_terminal_can_accept(const struct cb_terminal_engine *t)
{
    return !t->pending && !t->source_eof && !t->echo_used;
}

int cb_terminal_feed(struct cb_terminal_engine *t, unsigned char byte)
{
    if (!cb_terminal_can_accept(t)) return 0;
    if (byte == '\r') byte = '\n';
    if (byte == 8) {
        if (t->edit_used) {
            --t->edit_used;
            t->echo[0] = '\b'; t->echo[1] = ' '; t->echo[2] = '\b';
            t->echo_used = 3;
        }
    } else if (byte == 4) {
        t->pending = 1;
        commit_pending(t);
    } else if (byte == '\n' || t->edit_used < CB_TERM_BYTES - 1) {
        t->edit[t->edit_used++] = byte;
        t->echo[0] = byte;
        t->echo_used = 1;
        if (byte == '\n') {
            t->pending = 1;
            commit_pending(t);
        }
    }
    return 1;
}

void cb_terminal_source_eof(struct cb_terminal_engine *t)
{
    if (t->source_eof) return;
    t->source_eof = 1;
    if (t->edit_used) t->pending = 1;
    commit_pending(t);
}

int cb_terminal_ready(const struct cb_terminal_engine *t)
{
    return t->record_count != 0 || t->source_eof;
}

int cb_terminal_read(struct cb_terminal_engine *t, void *buffer, size_t capacity,
                     size_t *count)
{
    unsigned char *out = buffer;
    size_t n, i;
    if (!count) return -1;
    *count = 0;
    if (!capacity) return 1;
    if (!buffer) return -1;
    if (!t->record_count) return t->source_eof ? 1 : 0;
    n = t->records[t->record_head];
    if (n > capacity) n = capacity;
    for (i = 0; i < n; ++i)
        out[i] = t->fifo[(t->fifo_head + i) % CB_TERM_BYTES];
    t->fifo_head = (t->fifo_head + n) % CB_TERM_BYTES;
    t->fifo_used -= n;
    t->records[t->record_head] -= n;
    if (!t->records[t->record_head]) {
        t->record_head = (t->record_head + 1) % CB_TERM_RECORDS;
        --t->record_count;
    }
    *count = n;
    commit_pending(t);
    return 1;
}

const unsigned char *cb_terminal_echo(const struct cb_terminal_engine *t,
                                      size_t *count)
{
    *count = t->echo_used;
    return t->echo + t->echo_offset;
}

int cb_terminal_echo_ack(struct cb_terminal_engine *t, size_t count)
{
    if (count > t->echo_used) return -1;
    t->echo_offset += count;
    t->echo_used -= count;
    if (!t->echo_used) t->echo_offset = 0;
    return 0;
}
