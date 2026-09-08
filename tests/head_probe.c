#include "cannedbsd/abi.h"
#include "cannedbsd/libc.h"
#include <string.h>

extern int cb_head_main(int argc, char *argv[]);

/* Fault-injection modes, dispatched through the existing "headpipeproducer"
 * registration below (no new registration: the shared FIXTURE_FULL table
 * is already at CB_MAX_PROGRAMS's 64-slot ceiling -- HEAD-01 itself had to
 * relocate cb_strcpy_probe_program's own registration to make room there).
 * Each mode wraps the exact unchanged cb_head_main with a task-local copy
 * of the API whose read()/write()/exit() is overridden -- the real
 * registered "head" program is never touched, and is spawned normally by
 * other cases in this same file to prove the fault state does not leak
 * into a sibling task. Mode 0 (no argv[1], or an argv[1] outside '1'..'5')
 * keeps "headpipeproducer"'s original, unrelated pipe-producer behavior. */
enum head_fault_mode {
    HEAD_FAULT_NONE = 0,
    HEAD_FAULT_READ = 1,           /* read() fails after a fixed byte budget */
    HEAD_FAULT_WRITE_NEGATIVE = 2, /* write() returns -1 (injected errno) */
    HEAD_FAULT_WRITE_ZERO = 3,     /* write() returns 0 (zero progress) */
    HEAD_FAULT_WRITE_PARTIAL = 4,  /* write() always short; must retry to complete */
    HEAD_FAULT_READ_FIRST = 5      /* read() fails on the very first call: zero-progress read failure */
};

/* Bytes HEAD_FAULT_READ delivers before permanently failing. Chosen so
 * both line mode (getc, one byte at a time: this lands exactly after two
 * "N\n" lines) and byte mode (fread, one larger request) produce a clean,
 * exact expected output. HEAD_FAULT_READ_FIRST uses a budget of 0: it
 * fails before delivering anything at all. */
#define HEAD_FAULT_READ_BUDGET 4

/* Hard, defensive cap on how many times fault_read()/fault_write() may be
 * called for one dispatch. Every scenario below needs at most a handful
 * of calls; this exists solely so that an unrelated regression eliciting
 * unexpected extra retries fails this fixture with a bounded, real exit
 * (still going through fault_exit, so the real API stays correctly
 * restored) rather than spinning the read/write retry loop forever and
 * hanging the whole test run. */
#define HEAD_FAULT_CALL_LIMIT 64

/* Sentinel for struct head_case's expect_read_calls/expect_read_delivered:
   "not checked", distinct from a real, assertable value of 0. */
#define HEAD_FAULT_UNCHECKED (-1)

/* Distinct exit status used only when HEAD_FAULT_CALL_LIMIT actually
   fires (never expected in any case below); kept apart from any real
   head exit status (0/1) so a regression that hits this path is never
   confused with a genuine pass or a genuine head failure. */
#define HEAD_FAULT_BUDGET_EXIT_STATUS 97

/* These globals are safe only because this whole fixture (headprobe's own
 * run_one loop) spawns one child, waits for it to fully exit, then moves
 * to the next -- never two of these fault-mode children concurrently, and
 * nothing here ever yields back to the scheduler while a copy is bound.
 * This is sequential-fixture-only safety, not a general task-isolation
 * mechanism; do not reuse this pattern from a context that might yield or
 * interleave without re-establishing the same guarantee. */
static const struct cb_api_v1 *fault_real_api;
static int fault_mode;
static size_t fault_read_budget;
static size_t fault_read_delivered;
static size_t fault_read_calls;
/* fd==1 (stdout) call count only -- what expect_write_calls asserts.
   Distinct from fault_write_total_calls below, which is the safety-cap
   counter and must see every invocation regardless of destination fd. */
static size_t fault_write_calls;
/* Every fault_write() invocation, any fd (including fd 2/stderr, which
   err()/warn() write through the same overridden API). A review found
   the previous cap only counted fd==1 calls, since fault_write returned
   early for any other fd before ever reaching the counter -- leaving
   stderr writes completely unbounded despite the documented "hard,
   defensive cap on how many times fault_read()/fault_write() may be
   called" claim above. This counter is what HEAD_FAULT_CALL_LIMIT is
   actually checked against now. */
static size_t fault_write_total_calls;
/* Set by fault_exit() (below) once it has actually run. Checked by
 * run_one() after waitpid for every fault-mode case, on both the success
 * and the failure path, since the pinned head.c's own main() always
 * calls exit() itself -- there is no other path back out of
 * cb_libc_start(&fault_api_copy, ...) to restore anything from. */
static int fault_restored;

/* The task-local API override, given fixture-owned (static) storage
   rather than a stack-local variable inside head_fault_dispatch. A
   review found that with stack-local storage, an omitted-rebind
   regression left cb_libc's internal binding pointing into the exited
   child's now-deallocated stack frame -- reading it back from run_one()
   after waitpid reaped that child is undefined behavior (whether it
   "looks like" the old fault_write pointer is happenstance, not a
   defined test outcome), even though it happened to reproduce the
   intended failure when this was tried. Static storage keeps the
   pointed-to memory legitimately alive for the lifetime of the whole
   fixture (which only ever runs one fault-mode child at a time, see
   the sequential-fixture-only note above), so a genuine omitted-rebind
   bug is now well-defined to detect via run_one's cb_libc_write()
   probe, not merely observed to happen to work. */
static struct cb_api_v1 fault_api_copy;

/* Forward-declared so fault_read/fault_write can route a call-count
   budget overrun through it: a bare -1 return only stops one call, and
   does not stop a caller that keeps retrying (a hypothetical
   regression in cb_libc's own retry loop). Only fault_exit's real
   exit() is a guaranteed, bounded way out -- and it restores the real
   API binding first, exactly as it does for a genuine head exit(). */
static void fault_exit(int status);

static cb_ssize_t fault_read(int fd, void *buffer, size_t count)
{
    cb_ssize_t result;
    int budgeted = fault_mode == HEAD_FAULT_READ || fault_mode == HEAD_FAULT_READ_FIRST;
    if (fd == 0 && budgeted) {
        if (++fault_read_calls > HEAD_FAULT_CALL_LIMIT)
            fault_exit(HEAD_FAULT_BUDGET_EXIT_STATUS); /* never returns */
        if (fault_read_delivered >= fault_read_budget) {
            fault_real_api->set_errno(CB_EIO);
            return -1;
        }
        if (count > fault_read_budget - fault_read_delivered)
            count = fault_read_budget - fault_read_delivered;
    }
    result = fault_real_api->read(fd, buffer, count);
    if (fd == 0 && budgeted && result > 0)
        fault_read_delivered += (size_t)result;
    return result;
}

static cb_ssize_t fault_write(int fd, const void *buffer, size_t count)
{
    if (++fault_write_total_calls > HEAD_FAULT_CALL_LIMIT)
        fault_exit(HEAD_FAULT_BUDGET_EXIT_STATUS); /* never returns */
    if (fd != 1)
        return fault_real_api->write(fd, buffer, count);
    ++fault_write_calls;
    switch (fault_mode) {
    case HEAD_FAULT_WRITE_NEGATIVE:
        fault_real_api->set_errno(CB_EPIPE);
        return -1;
    case HEAD_FAULT_WRITE_ZERO:
        return 0;
    case HEAD_FAULT_WRITE_PARTIAL:
        if (count > 2)
            count = 2;
        return fault_real_api->write(fd, buffer, count);
    default:
        return fault_real_api->write(fd, buffer, count);
    }
}

static int head_fault_noop(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return 0;
}

/* The pinned head.c's own main() (upstream/netbsd/usr.bin/head/head.c,
 * "exit(eval);" as its unconditional final statement) always calls
 * exit() itself, on every path, success included -- it never returns a
 * value for cb_libc_start to hand back. That makes this exit() call,
 * routed here via the task-local API copy's own .exit override, the
 * *only* point that reliably runs before this task terminates, on both
 * the success and the failure path; restoring cb_libc's internal binding
 * after cb_libc_start(&fault_api_copy, ...) returns (as an earlier version of this
 * file did) is dead code -- it never executes, on either path, and was
 * wrong to claim as a running restoration. fault_restored is set only
 * after cb_libc_start(real, ...) actually returns, so the flag reflects
 * the rebind call having run, not merely this function having been
 * entered; run_one's own cb_libc_write() probe (below) is the
 * authoritative, behavioral check that the rebind actually took
 * effect -- this flag is a secondary, cheaper sanity check alongside
 * it, not a replacement for it. */
static void fault_exit(int status)
{
    const struct cb_api_v1 *real = fault_real_api;
    cb_libc_start(real, 0, NULL, head_fault_noop);
    fault_restored = 1;
    real->exit(status);
}

/* Strips its own argv[0]/argv[1] (name, fault-mode digit) and calls the
 * exact unchanged cb_head_main with the remainder as head's own argv,
 * with a synthetic argv[0] of "head" for that inner call. This affects
 * only what cb_head_main itself sees as argv[0] (irrelevant here: the
 * pinned source never reads argv[0]); it does *not* change this task's
 * own identity -- getprogname()/err()'s diagnostic prefix reads the
 * task's real, spawn-time-fixed argv[0] ("headpipeproducer", the actual
 * name this task was spawned under), never the locally reconstructed
 * inner_argv. Expected diagnostic strings below say "headpipeproducer",
 * not "head", for exactly this reason -- confirmed by running this
 * fixture, not assumed. Validates argc/argv bounds before indexing
 * anything, and caps the copy to this file's own fixed 8-slot
 * inner_argv. cb_libc_start(&fault_api_copy, ...) below never returns (see
 * fault_exit's own comment); the trailing return exists only to satisfy
 * the compiler. */
static int head_fault_dispatch(const struct cb_api_v1 *api, int argc,
                               char *const argv[], char *const envp[])
{
    char *inner_argv[8];
    int i, inner_argc;
    (void)envp;
    if (argc < 2 || argc > 8 || argv[1] == NULL)
        return 1;
    fault_real_api = api;
    fault_mode = argv[1][0] - '0';
    fault_read_budget = fault_mode == HEAD_FAULT_READ ? HEAD_FAULT_READ_BUDGET : 0;
    fault_read_delivered = 0;
    fault_read_calls = 0;
    fault_write_calls = 0;
    fault_write_total_calls = 0;
    fault_restored = 0;
    fault_api_copy = *api;
    fault_api_copy.read = fault_read;
    fault_api_copy.write = fault_write;
    fault_api_copy.exit = fault_exit;
    inner_argv[0] = (char *)"head";
    for (i = 2; i < argc; ++i)
        inner_argv[i - 1] = argv[i];
    inner_argc = argc - 1;
    inner_argv[inner_argc] = NULL;
    cb_libc_start(&fault_api_copy, inner_argc, inner_argv, cb_head_main);
    return 1;
}

struct head_case {
    char *args[8];
    const char *input;
    const char *output;
    const char *error;
    int status;
    int large;
    int pipe_input;
    /* Exact expected fault_write() call count for a "headpipeproducer"
       fault-mode case; 0 means "not checked" (every existing, non-fault
       case, which C's own aggregate-initialization rules already
       zero-fill without needing to touch any of their lines). */
    int expect_write_calls;
    /* Exact expected fault_read_calls()/fault_read_delivered for a
       read-fault-mode case. HEAD_FAULT_UNCHECKED (-1), not 0, means
       "not checked" here: the HEAD_FAULT_READ_FIRST cases genuinely
       deliver zero bytes on exactly one call, and that must be
       asserted, not silently skipped by colliding with a zero
       sentinel. */
    int expect_read_calls;
    int expect_read_delivered;
};

static const struct head_case cases[] = {
    {{"head", NULL}, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n", "", 0, 0, 0, 0, -1, -1},
    {{"head", "-3", NULL}, "A\nB\nC\nD\n", "A\nB\nC\n", "", 0, 0, 0, 0, -1, -1},
    {{"head", "-q", "-3", NULL}, "", "", "head: illegal option -- 3\nusage: head [-n lines] [file ...]\n", 1, 0, 0, 0, -1, -1},
    {{"head", "-3", "-q", "A", NULL}, "", "1\n", "", 0, 0, 0, 0, -1, -1},
    {{"head", "-n", "1", "-c", "3", NULL}, "abcde", "abc", "", 0, 0, 0, 0, -1, -1},
    {{"head", "-c", "3", "-n", "1", NULL}, "abcde", "abc", "", 0, 0, 0, 0, -1, -1},
    {{"head", "-n", "1", "A", "B", NULL}, "", "==> A <==\n1\n\n==> B <==\n3\n", "", 0, 0, 0, 0, -1, -1},
    {{"head", "miss", "A", NULL}, "", "==> A <==\n1\n", "head: miss: no such file or directory\n", 1, 0, 0, 0, -1, -1},
    {{"head", "-v", "-q", "A", NULL}, "", "1\n", "", 0, 0, 0, 0, -1, -1},
    {{"head", "-q", "-v", "A", NULL}, "", "==> A <==\n1\n", "", 0, 0, 0, 0, -1, -1},
    {{"head", "-", NULL}, "", "", "head: -: no such file or directory\n", 1, 0, 0, 0, -1, -1},
    {{"head", "-c", "0", NULL}, "", "", "head: illegal byte count -- 0\n", 1, 0, 0, 0, -1, -1},
    {{"head", "-z", NULL}, "", "", "head: illegal option -- z\nusage: head [-n lines] [file ...]\n", 1, 0, 0, 0, -1, -1},
    {{"head", "-c", "65538", NULL}, "", "", "", 0, 1, 0, 0, -1, -1},
    {{"head", "-n", "2", NULL}, "\xff\n\xff", "\xff\n\xff", "", 0, 0, 0, 0, -1, -1},
    {{"head", "-c", "10", NULL}, "ab", "ab", "", 0, 0, 0, 0, -1, -1},
    {{"head", NULL}, "", "", "", 0, 0, 0, 0, -1, -1},
    {{"head", "-n", "1", NULL}, "", "pipe\n", "", 0, 0, 1, 0, -1, -1},
    {{"head", "-n", "bad", NULL}, "", "", "head: illegal line count -- bad\n", 1, 0, 0, 0, -1, -1},
    {{"head", "-c", "9223372036854775808", NULL}, "", "", "head: illegal byte count -- 9223372036854775808\n", 1, 0, 0, 0, -1, -1},
    {{"head", "-n", "-1", NULL}, "", "", "head: illegal line count -- -1\n", 1, 0, 0, 0, -1, -1},
    /* Deterministic input read failure, line mode (getc): the pinned
       source's own `while ((ch = getc(fp)) != EOF)` loop cannot tell a
       real read() failure apart from a clean EOF (getc returns EOF
       either way; head.c never calls ferror()). Observed, not desired,
       behavior: head stops with the partial output already produced
       and exit status 0 -- a real read failure is silently equivalent
       to a short file, not an error, for this pinned source. */
    {{"headpipeproducer", "1", NULL}, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n", "1\n2\n", "", 0, 0, 0, 0, 5, 4},
    /* Same conflation, byte mode (fread): `if (rv == 0) break;` treats a
       failed read exactly like EOF here too. */
    {{"headpipeproducer", "1", "-c", "20", NULL}, "ABCDEFGHIJKLMNOPQRST", "ABCD", "", 0, 0, 0, 0, 3, 4},
    /* Same conflation again, but failing on the very first read (zero
       bytes ever delivered), line mode: the boundary case distinct from
       the prefix-then-error cases above. Output is completely empty;
       the observed exit-0 limitation still holds. */
    {{"headpipeproducer", "5", NULL}, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n", "", "", 0, 0, 0, 0, 1, 0},
    /* Same first-read boundary, byte mode. */
    {{"headpipeproducer", "5", "-c", "10", NULL}, "1234567890", "", "", 0, 0, 0, 0, 1, 0},
    /* Output write failure (write() returns -1), line mode (putchar):
       `if (putchar(ch) == EOF) err(1, "stdout");` -- exact observed
       message and exit status 1, zero bytes actually written since the
       very first putchar fails. */
    {{"headpipeproducer", "2", NULL}, "X\n", "", "headpipeproducer: stdout: broken pipe\n", 1, 0, 0, 0, -1, -1},
    /* Same fault, byte mode (fwrite): cb_libc_feof(stdout) is always 0
       for an output stream in this project's implementation, so
       head.c's `if (feof(stdout)) errx(1, "EOF on stdout");` branch is
       never reachable here -- it always falls to
       `err(1, "failure writing to stdout")`. */
    {{"headpipeproducer", "2", "-c", "10", NULL}, "0123456789", "", "headpipeproducer: failure writing to stdout: broken pipe\n", 1, 0, 0, 0, -1, -1},
    /* Repeated invocation immediately after a failure, using the real,
       unmodified "head" program (not "headpipeproducer" in fault mode):
       proves the fault state above does not leak into a sibling task. */
    {{"head", "-c", "5", NULL}, "hello world", "hello", "", 0, 0, 0, 0, -1, -1},
    /* Output write failure (write() returns 0, zero progress, no error
       of its own): cb_libc_fwrite forces its own EIO regardless of
       what the underlying write() did to errno, so the observed
       message differs only in its error text from the negative-failure
       case above. */
    {{"headpipeproducer", "3", "-c", "10", NULL}, "0123456789", "", "headpipeproducer: failure writing to stdout: input/output error\n", 1, 0, 0, 0, -1, -1},
    /* Repeated invocation after this second, differently-shaped
       failure -- same isolation proof, a second time. */
    {{"head", "-c", "5", NULL}, "hello world", "hello", "", 0, 0, 0, 0, -1, -1},
    /* Output write positive partial retry: write() always returns a
       short count (2 bytes) but never zero and never negative.
       cb_libc_fwrite's own retry loop reassembles the full request
       across several short writes -- this is not a failure at all;
       head produces the complete, correct output and exits 0, proving
       the retry path is correct rather than merely present. Also
       asserts the exact call count: 10 bytes at 2 bytes per write() is
       exactly 5 calls, not merely "eventually reaches 10 bytes". */
    {{"headpipeproducer", "4", "-c", "10", NULL}, "0123456789", "0123456789", "", 0, 0, 0, 5, -1, -1}
};

static int write_bytes(const struct cb_api_v1 *api, int fd,
                       const char *bytes, size_t count)
{
    while (count != 0) {
        cb_ssize_t n = api->write(fd, bytes, count);
        if (n <= 0 || (uint64_t)n > count) return -1;
        bytes += (size_t)n;
        count -= (size_t)n;
    }
    return 0;
}

static int populate(const struct cb_api_v1 *api, const char *path,
                    const char *bytes, int large)
{
    char chunk[256];
    size_t remaining = large ? 65538 : strlen(bytes);
    int fd = api->open(path, CB_O_WRONLY | CB_O_CREAT | CB_O_TRUNC, 0644);
    int result = 0;
    if (fd < 0) return -1;
    memset(chunk, 'A', sizeof(chunk));
    while (remaining != 0) {
        size_t n = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        if (write_bytes(api, fd, large ? chunk : bytes, n) < 0) {
            result = -1;
            break;
        }
        if (!large) bytes += n;
        remaining -= n;
    }
    if (api->close(fd) < 0) result = -1;
    return result;
}

static int verify_file(const struct cb_api_v1 *api, const char *path,
                       const char *expected, int large)
{
    struct cb_stat_v1 st;
    char chunk[256];
    size_t remaining = large ? 65538 : strlen(expected);
    int fd, result = 0;
    if (api->stat(path, &st) < 0 || st.size != remaining) return -1;
    fd = api->open(path, CB_O_RDONLY, 0);
    if (fd < 0) return -1;
    while (remaining != 0) {
        size_t i, request = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        cb_ssize_t n = api->read(fd, chunk, request);
        if (n <= 0 || (uint64_t)n > request) { result = -1; break; }
        for (i = 0; i < (size_t)n; ++i) {
            if (chunk[i] != (large ? 'A' : expected[i])) result = -1;
        }
        if (result < 0) break;
        if (!large) expected += (size_t)n;
        remaining -= (size_t)n;
    }
    if (result == 0 && api->read(fd, chunk, sizeof(chunk)) != 0) result = -1;
    if (api->close(fd) < 0) result = -1;
    return result;
}

static void action(struct cb_spawn_action_v1 *a, int type, int from, int to)
{
    memset(a, 0, sizeof(*a));
    a->abi_version = CB_ABI_VERSION_V1;
    a->struct_size = sizeof(*a);
    a->type = type;
    a->from_fd = from;
    a->to_fd = to;
}

/* "headpipeproducer" keeps its original, unrelated pipe-producer role
 * (invoked with no extra argv, argc == 1) and doubles as the fault-mode
 * dispatcher above (invoked with a mode digit as argv[1]) -- reusing this
 * one existing registration rather than adding another, since the shared
 * FIXTURE_FULL table is already at its 64-slot ceiling. Its stack budget
 * is raised to head's own 128 KiB here (from the plain producer's
 * original 64 KiB) since fault mode runs the exact same cb_head_main body
 * as "head" itself, including its 65536-byte automatic buffer -- this is
 * a per-task stack allocation, not a change to the 64-program registration
 * table capacity the comment above is about. */
static int producer_main(const struct cb_api_v1 *api, int argc,
                          char *const argv[], char *const envp[])
{
    if (argc >= 2 && argv[1] != NULL)
        return head_fault_dispatch(api, argc, argv, envp);
    (void)envp;
    return write_bytes(api, 1, "pipe\n", 5) == 0 ? 0 : 1;
}

const struct cb_program_v1 cb_head_pipe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "headpipeproducer", 0,
    128 * 1024, producer_main
};

static int run_one(const struct cb_api_v1 *api, char *const envp[],
                   const struct head_case *test)
{
    struct cb_spawn_action_v1 a[7];
    int in = -1, out = -1, err = -1, writer = -1, result = -1;
    cb_pid_t child = 0, producer = 0;
    int status, producer_status;
    size_t count = 6;
    if (populate(api, "head-input", test->input, test->large) < 0) goto done;
    if (test->pipe_input) {
        int p[2];
        if (api->pipe(p) < 0) goto done;
        in = p[0]; writer = p[1];
    } else {
        in = api->open("head-input", CB_O_RDONLY, 0);
    }
    out = api->open("head-output", CB_O_WRONLY | CB_O_CREAT | CB_O_TRUNC, 0644);
    err = api->open("head-error", CB_O_WRONLY | CB_O_CREAT | CB_O_TRUNC, 0644);
    if (in < 0 || out < 0 || err < 0) goto done;
    action(&a[0], CB_SPAWN_DUP2, in, 0);
    action(&a[1], CB_SPAWN_DUP2, out, 1);
    action(&a[2], CB_SPAWN_DUP2, err, 2);
    action(&a[3], CB_SPAWN_CLOSE, in, 0);
    action(&a[4], CB_SPAWN_CLOSE, out, 0);
    action(&a[5], CB_SPAWN_CLOSE, err, 0);
    if (writer >= 0) action(&a[count++], CB_SPAWN_CLOSE, writer, 0);
    if (api->spawn(test->args[0], test->args, envp, a, count, &child) < 0) goto done;
    if (writer >= 0) {
        char *args[] = {"headpipeproducer", NULL};
        action(&a[0], CB_SPAWN_DUP2, writer, 1);
        action(&a[1], CB_SPAWN_CLOSE, writer, 0);
        action(&a[2], CB_SPAWN_CLOSE, in, 0);
        action(&a[3], CB_SPAWN_CLOSE, out, 0);
        action(&a[4], CB_SPAWN_CLOSE, err, 0);
        if (api->spawn("headpipeproducer", args, envp, a, 5, &producer) < 0) goto done;
    }
    result = 0;
done:
    if (in >= 0 && api->close(in) < 0) result = -1;
    if (out >= 0 && api->close(out) < 0) result = -1;
    if (err >= 0 && api->close(err) < 0) result = -1;
    if (writer >= 0 && api->close(writer) < 0) result = -1;
    if (child > 0 && (api->waitpid(child, &status) != child || status != test->status)) result = -1;
    if (producer > 0 && (api->waitpid(producer, &producer_status) != producer || producer_status != 0)) result = -1;
    /* Fault-mode cases only (args[0] == "headpipeproducer", a mode digit
       as args[1]): the child's own fault_exit() must actually have run,
       on both the success and the failure path -- pinned head.c's own
       main() always calls exit() itself, so this is the only point that
       reliably restores cb_libc's internal binding. A zero-check limit
       means "don't check", so every existing, non-fault case (whose
       expect_write_calls defaults to 0 via aggregate initialization) is
       unaffected. expect_read_calls/expect_read_delivered use
       HEAD_FAULT_UNCHECKED (-1), not 0, as their "don't check" sentinel,
       since a real, assertable value of exactly 0 is the whole point of
       the HEAD_FAULT_READ_FIRST cases. */
    if (result == 0 && child > 0 && strcmp(test->args[0], "headpipeproducer") == 0 &&
        test->args[1] != NULL) {
        size_t write_calls_before_probe;
        if (!fault_restored) result = -1;
        if (test->expect_write_calls != 0 &&
            fault_write_calls != (size_t)test->expect_write_calls) result = -1;
        if (test->expect_read_calls != HEAD_FAULT_UNCHECKED &&
            fault_read_calls != (size_t)test->expect_read_calls) result = -1;
        if (test->expect_read_delivered != HEAD_FAULT_UNCHECKED &&
            fault_read_delivered != (size_t)test->expect_read_delivered) result = -1;
        /* Behavioral proof the real API binding is actually restored,
           not merely a flag: cb_libc_write() dispatches through
           cb_libc's own internal binding, opaque to this file. If
           fault_exit's rebind call had not actually run, that binding
           would still hold fault_api_copy -- static, fixture-owned
           storage (not the exited child's now-deallocated stack), so
           reading it back here is well-defined, not merely
           happenstance: its function pointers are still exactly the
           values head_fault_dispatch installed, and the call would
           reliably route through fault_write and bump this counter,
           rather than erroring, crashing, or reading stack garbage.
           A zero-length write to fd 1 is side-effect-free either way
           it resolves, so this probe is safe to run unconditionally
           here. */
        write_calls_before_probe = fault_write_calls;
        cb_libc_write(1, NULL, 0);
        if (fault_write_calls != write_calls_before_probe) result = -1;
    }
    if (result == 0 && (verify_file(api, "head-output", test->output, test->large) < 0 ||
                        verify_file(api, "head-error", test->error, 0) < 0)) result = -1;
    return result;
}

static int probe_main(const struct cb_api_v1 *api, int argc,
                       char *const argv[], char *const envp[])
{
    size_t i;
    (void)argc; (void)argv;
    if (api->chdir("/tmp") < 0 || populate(api, "A", "1\n", 0) < 0 ||
        populate(api, "B", "3\n", 0) < 0) return 1;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (run_one(api, envp, &cases[i]) < 0) return (int)i + 20;
    }
    return 0;
}

const struct cb_program_v1 cb_head_probe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "headprobe", 0,
    64 * 1024, probe_main
};
