#include "terminal.h"
#include <string.h>

#define CHECK(expression, code) do { if (!(expression)) return (code); } while (0)

/* Shared native/Mac pure-state test: no live kernel, host, or owned resources. */
static int basic_line(void)
{
    struct cb_terminal_engine t;
    unsigned char data[CB_TERM_BYTES];
    size_t count;
    cb_terminal_init(&t);
    CHECK(cb_terminal_can_accept(&t) && !cb_terminal_ready(&t), 10);
    CHECK(cb_terminal_feed(&t, 'a') == 1, 11);
    CHECK(!cb_terminal_ready(&t) && !cb_terminal_can_accept(&t), 12);
    CHECK(*cb_terminal_echo(&t, &count) == 'a' && count == 1, 13);
    CHECK(cb_terminal_echo_ack(&t, 1) == 0, 14);
    CHECK(cb_terminal_feed(&t, '\n') == 1 && cb_terminal_ready(&t), 15);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &count) == 1 && count == 2 &&
          memcmp(data, "a\n", 2) == 0, 16);
    CHECK(!cb_terminal_ready(&t), 17);
    return 0;
}

static int accept(struct cb_terminal_engine *t, unsigned char byte)
{
    size_t n;
    if (cb_terminal_feed(t, byte) != 1) return 0;
    (void)cb_terminal_echo(t, &n);
    return cb_terminal_echo_ack(t, n) == 0;
}

static int bounds(const struct cb_terminal_engine *t)
{
    size_t i, sum = 0;
    if (t->edit_used > CB_TERM_BYTES || t->fifo_used > CB_TERM_BYTES ||
        t->record_count > CB_TERM_RECORDS || t->echo_used > 3 ||
        t->echo_offset + t->echo_used > 3) return 0;
    for (i = 0; i < t->record_count; ++i)
        sum += t->records[(t->record_head + i) % CB_TERM_RECORDS];
    return sum == t->fifo_used;
}

int cb_terminal_engine_probe(void)
{
    struct cb_terminal_engine t, saved, peer;
    unsigned char data[CB_TERM_BYTES];
    const unsigned char *echo;
    size_t n, i, j;
    int r = basic_line();
    if (r) return r;

    cb_terminal_init(&t);
    CHECK(accept(&t, 8) && !cb_terminal_ready(&t), 20);
    CHECK(accept(&t, 'a') && cb_terminal_feed(&t, 8), 21);
    echo = cb_terminal_echo(&t, &n);
    CHECK(n == 3 && memcmp(echo, "\b \b", 3) == 0, 22);
    memcpy(&saved, &t, sizeof(saved));
    CHECK(cb_terminal_echo_ack(&t, 4) == -1 &&
          memcmp(&saved, &t, sizeof(t)) == 0, 23);
    CHECK(cb_terminal_echo_ack(&t, 0) == 0 &&
          cb_terminal_feed(&t, 'x') == 0 &&
          memcmp(&saved, &t, sizeof(t)) == 0, 24);
    for (i = 0; i < 3; ++i) {
        echo = cb_terminal_echo(&t, &n);
        CHECK(n == 3 - i && memcmp(echo, &"\b \b"[i], n) == 0, 25);
        CHECK(!cb_terminal_can_accept(&t) && cb_terminal_echo_ack(&t, 1) == 0, 26);
    }
    CHECK(cb_terminal_can_accept(&t) && accept(&t, 'b') && accept(&t, '\r'), 27);
    CHECK(cb_terminal_read(&t, data, 1, &n) == 1 && n == 1 && data[0] == 'b', 28);
    CHECK(cb_terminal_ready(&t) && cb_terminal_read(&t, data, 9, &n) == 1 &&
          n == 1 && data[0] == '\n' && !cb_terminal_ready(&t), 29);

    /* Consecutive VEOF is a one-shot event; reads never concatenate records. */
    CHECK(accept(&t, 'd') && accept(&t, 4) && accept(&t, 4) &&
          accept(&t, 'm') && accept(&t, '\n'), 30);
    memcpy(&saved, &t, sizeof(saved));
    CHECK(cb_terminal_read(&t, NULL, 0, &n) == 1 && n == 0 &&
          memcmp(&saved, &t, sizeof(t)) == 0, 31);
    CHECK(cb_terminal_read(&t, NULL, 1, &n) == -1 &&
          memcmp(&saved, &t, sizeof(t)) == 0, 32);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == 1 && data[0] == 'd', 33);
    memcpy(&saved, &t, sizeof(saved));
    CHECK(cb_terminal_read(&t, data, 0, &n) == 1 && n == 0 &&
          memcmp(&saved, &t, sizeof(t)) == 0, 34);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == 0, 35);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == 2 &&
          memcmp(data, "m\n", 2) == 0, 36);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 0 && !cb_terminal_ready(&t), 37);

    /* Both boundary lengths preserve a final newline. Erase after overflow
       permits another byte; overflow itself generates no echo. */
    for (j = CB_TERM_BYTES - 2; j < CB_TERM_BYTES; ++j) {
        cb_terminal_init(&t);
        for (i = 0; i < j; ++i) CHECK(accept(&t, 'x'), 40);
        if (j == CB_TERM_BYTES - 1) CHECK(accept(&t, 'z'), 81);
        CHECK(accept(&t, j == CB_TERM_BYTES - 1 ? '\r' : '\n') && bounds(&t), 41);
        CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == j + 1, 42);
        for (i = 0; i < j; ++i) CHECK(data[i] == 'x', 43);
        CHECK(data[j] == '\n', 44);
    }
    cb_terminal_init(&t);
    for (i = 0; i < CB_TERM_BYTES - 1; ++i) CHECK(accept(&t, 'x'), 45);
    CHECK(cb_terminal_feed(&t, 'z') == 1, 46);
    (void)cb_terminal_echo(&t, &n);
    CHECK(n == 0 && t.edit_used == CB_TERM_BYTES - 1, 47);
    CHECK(accept(&t, 8) && accept(&t, 'y') && accept(&t, 4), 48);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 &&
          n == CB_TERM_BYTES - 1 && data[n - 1] == 'y', 49);
    for (i = 0; i + 1 < n; ++i) CHECK(data[i] == 'x', 50);
    CHECK(!cb_terminal_ready(&t), 51);

    /* FIFO plus a completed edit occupies exactly 2048 payload bytes.
       Repeated partial reads wrap the FIFO and preserve record order. */
    cb_terminal_init(&t);
    for (j = 0; j < 4; ++j) {
        for (i = 0; i < CB_TERM_BYTES - 1; ++i) CHECK(accept(&t, (unsigned char)('a' + j)), 52);
        CHECK(accept(&t, '\n') && bounds(&t), 53);
        if (j == 0) continue;
        CHECK(t.pending && t.fifo_used + t.edit_used == 2 * CB_TERM_BYTES, 54);
        memcpy(&saved, &t, sizeof(saved));
        CHECK(cb_terminal_feed(&t, 'z') == 0 && memcmp(&saved, &t, sizeof(t)) == 0, 55);
        for (i = 0; i < CB_TERM_BYTES; ++i) {
            CHECK(cb_terminal_read(&t, data, 1, &n) == 1 && n == 1 && bounds(&t), 56);
            CHECK(data[0] == (i == CB_TERM_BYTES - 1 ? '\n' : (unsigned char)('a' + j - 1)), 57);
        }
        CHECK(!t.pending && cb_terminal_can_accept(&t), 58);
    }
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == CB_TERM_BYTES &&
          data[0] == 'd' && data[n - 1] == '\n', 59);

    /* Descriptor exhaustion can retain one more completed edit/event. */
    cb_terminal_init(&t);
    for (i = 0; i < CB_TERM_RECORDS; ++i) CHECK(accept(&t, 4), 60);
    CHECK(accept(&t, 4) && t.pending && bounds(&t), 61);
    CHECK(!cb_terminal_can_accept(&t) && cb_terminal_feed(&t, 'x') == 0, 62);
    for (i = 0; i < CB_TERM_RECORDS + 1; ++i)
        CHECK(cb_terminal_read(&t, data, 1, &n) == 1 && n == 0 && bounds(&t), 63);
    CHECK(!cb_terminal_ready(&t) && cb_terminal_can_accept(&t), 64);

    /* Physical EOF flushes an incomplete edit exactly once, after old events.
       It persists even after another EOF notification and blocks ingestion. */
    CHECK(accept(&t, 4) && accept(&t, 0) && accept(&t, 255), 65);
    cb_terminal_source_eof(&t);
    CHECK(cb_terminal_ready(&t) && !cb_terminal_can_accept(&t), 66);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == 0, 67);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == 2 &&
          data[0] == 0 && data[1] == 255, 68);
    cb_terminal_source_eof(&t);
    for (i = 0; i < 3; ++i)
        CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == 0 &&
              cb_terminal_ready(&t) && cb_terminal_feed(&t, 'x') == 0, 69);

    /* EOF does not drop a completed record waiting behind a full FIFO or
       its pending echo; acknowledging a suffix is independent of new input. */
    cb_terminal_init(&t);
    for (i = 0; i < CB_TERM_BYTES - 1; ++i) CHECK(accept(&t, 'q'), 70);
    CHECK(accept(&t, '\n') && accept(&t, 'z') && cb_terminal_feed(&t, '\n'), 71);
    cb_terminal_source_eof(&t);
    echo = cb_terminal_echo(&t, &n);
    CHECK(n == 1 && *echo == '\n' && t.pending, 72);
    CHECK(cb_terminal_echo_ack(&t, 1) == 0 && !cb_terminal_can_accept(&t), 73);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == CB_TERM_BYTES, 74);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == 2 &&
          memcmp(data, "z\n", 2) == 0, 75);
    CHECK(cb_terminal_read(&t, data, 1, &n) == 1 && n == 0, 76);

    /* Commit/copy/read across nonaligned ring positions, preserving two
       records at a time while both byte and record heads repeatedly wrap. */
    cb_terminal_init(&t);
    CHECK(accept(&t, 'a') && accept(&t, 'b') && accept(&t, '\n'), 82);
    for (i = 0; i < CB_TERM_BYTES; ++i) {
        CHECK(accept(&t, 'a') && accept(&t, 'b') && accept(&t, '\n'), 83);
        CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == 3 &&
              memcmp(data, "ab\n", 3) == 0 && bounds(&t), 84);
    }
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == 3, 85);
    CHECK(!cb_terminal_ready(&t), 86);

    /* An unfinished edit at physical EOF waits behind a full committed FIFO. */
    cb_terminal_init(&t);
    for (i = 0; i < CB_TERM_BYTES - 1; ++i) CHECK(accept(&t, 'x'), 87);
    CHECK(accept(&t, '\n') && accept(&t, 'f'), 88);
    cb_terminal_source_eof(&t);
    CHECK(t.pending && bounds(&t), 89);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == CB_TERM_BYTES, 90);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == 1 && data[0] == 'f', 91);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == 0, 92);

    /* No global queue state: interleaved caller-owned engines remain distinct. */
    cb_terminal_init(&t); cb_terminal_init(&peer);
    CHECK(accept(&t, 'a') && accept(&peer, 'b') && accept(&t, '\n') &&
          accept(&peer, 4), 77);
    CHECK(cb_terminal_read(&t, data, sizeof(data), &n) == 1 && n == 2 &&
          memcmp(data, "a\n", 2) == 0, 78);
    CHECK(cb_terminal_read(&peer, data, sizeof(data), &n) == 1 && n == 1 && data[0] == 'b', 79);
    CHECK(bounds(&t) && bounds(&peer), 80);
    return 0;
}
