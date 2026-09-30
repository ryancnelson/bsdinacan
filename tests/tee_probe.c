#include "internal.h"
#include <string.h>

/* Host/root entry only. All fault callbacks dispatch by the current program;
   every command uses the real kernel API table, never a dangling libc rebind.
   Cases run serially, except the explicit two-tee pipe scenario. */
enum { NORMAL, SHORT_READ, READ_FIRST, READ_AFTER, WRITE_SHORT, WRITE_EIO,
       WRITE_EPIPE, WRITE_PART_ERROR, WRITE_ZERO, CLOSE_ERROR, ALLOC_ERROR };
static const struct cb_host_ops_v1 *host;
static struct cb_kernel *kernel;
static struct cb_api_v1 real;
static unsigned mode, calls, reads, out_writes, stdout_calls, closes, alloc_calls;
static unsigned fault_hits, record_count;
static int out_fd;
static uint64_t opened_outputs, explicitly_closed;
static unsigned char output[20000], errors[1024], large_input[8193];
static size_t output_size, errors_size;
static struct { void *payload, *bookkeeping; cb_pid_t owner; } records[64];

static int is_tee(void)
{
    return kernel != NULL && kernel->current != NULL &&
           strcmp(kernel->current->program->name, "tee") == 0;
}
static void tick(void)
{
    if (++calls > 4096) real.exit(97);
}
static void release(void *p)
{
    unsigned i;
    for (i = 0; i < record_count; ++i) {
        if (p == records[i].payload) records[i].payload = NULL;
        if (p == records[i].bookkeeping) records[i].bookkeeping = NULL;
    }
    host->release(p);
}
static void *allocate(size_t n)
{
    void *p;
    if (!is_tee()) return real.allocate(n);
    tick();
    if (mode == ALLOC_ERROR && ++alloc_calls == 3) {
        ++fault_hits;
        real.set_errno(CB_ENOMEM);
        return NULL;
    }
    p = real.allocate(n);
    if (p != NULL) {
        if (record_count == 64) real.exit(97);
        records[record_count].payload = p;
        records[record_count].bookkeeping = kernel->current->allocations;
        records[record_count++].owner = kernel->current->pid;
    }
    return p;
}
static int open_file(const char *path, int flags, uint32_t permissions)
{
    int fd = real.open(path, flags, permissions);
    if (is_tee()) {
        tick();
        if (fd >= 0) opened_outputs |= UINT64_C(1) << fd;
        if (strcmp(path, "/out1") == 0) out_fd = fd;
    }
    return fd;
}
static cb_ssize_t read_file(int fd, void *p, size_t n)
{
    if (is_tee()) {
        tick();
        if (fd == 0) {
            ++reads;
            if (mode == READ_FIRST || (mode == READ_AFTER && reads == 2)) {
                ++fault_hits; real.set_errno(CB_EIO); return -1;
            }
            if (mode == SHORT_READ && n > 113) n = 113;
            if (mode == READ_AFTER || mode == WRITE_EIO ||
                mode == WRITE_EPIPE || mode == WRITE_PART_ERROR) {
                if (n > 2) n = 2;
            }
        }
    }
    return real.read(fd, p, n);
}
static cb_ssize_t write_file(int fd, const void *p, size_t n)
{
    if (is_tee()) {
        tick();
        if (mode == WRITE_SHORT && n > 2 && fd != 2) {
            n = 2; ++fault_hits;
        }
        if (fd == out_fd) {
            ++out_writes;
            if (((mode == WRITE_EIO || mode == WRITE_EPIPE) && out_writes == 1) ||
                (mode == WRITE_PART_ERROR && out_writes == 2)) {
                ++fault_hits;
                real.set_errno(mode == WRITE_EPIPE ? CB_EPIPE : CB_EIO);
                return -1;
            }
            if (mode == WRITE_PART_ERROR && out_writes == 1 && n > 1) n = 1;
        }
    }
    return real.write(fd, p, n);
}
static int close_file(int fd)
{
    int result = real.close(fd);
    if (is_tee()) {
        tick(); ++closes;
        if (result == 0) explicitly_closed |= UINT64_C(1) << fd;
        if (mode == CLOSE_ERROR && fd == 1 && result == 0) {
            ++fault_hits; real.set_errno(CB_EIO); return -1;
        }
    }
    return result;
}
static cb_ssize_t console_write(int stream, const void *p, size_t n)
{
    if (stream == 1) {
        ++stdout_calls;
        /* Zero is injected at the real host callback, so WRITE-02 must turn
           it into -1/EIO before upstream's raw write loop can spin. */
        if (mode == WRITE_ZERO && is_tee()) { ++fault_hits; return 0; }
        if (n > sizeof(output) - output_size) return -CB_EIO;
        memcpy(output + output_size, p, n); output_size += n;
    } else if (stream == 2) {
        if (n > sizeof(errors) - errors_size) return -CB_EIO;
        memcpy(errors + errors_size, p, n); errors_size += n;
    } else return -CB_EIO;
    return (cb_ssize_t)n;
}
static struct cb_task *find_task(cb_pid_t pid)
{
    struct cb_task *t;
    for (t = kernel->tasks; t != NULL; t = t->next) if (t->pid == pid) return t;
    return NULL;
}
static int wait_state(const struct cb_api_v1 *api, cb_pid_t pid, enum cb_task_state state)
{
    unsigned i;
    for (i = 0; i < 100; ++i) {
        struct cb_task *t = find_task(pid);
        if (t == NULL) return -1;
        if (t->state == state) return 0;
        api->yield();
    }
    return -1;
}
static int collect(const struct cb_api_v1 *api, cb_pid_t pid, int expected)
{
    struct cb_task *t;
    unsigned i, seen = 0;
    int status;
    if (wait_state(api, pid, CB_TASK_ZOMBIE) != 0) return 1;
    t = find_task(pid);
    /* Observe real tee payload and bookkeeping releases, and all descriptor
       closure, BEFORE waitpid can reap or kernel destruction can hide a leak. */
    if (t->allocations != NULL) return 2;
    for (i = 0; i < CB_MAX_FDS; ++i) if (t->descriptors[i].file != NULL) return 3;
    for (i = 0; i < record_count; ++i) if (records[i].owner == pid) {
        ++seen;
        if (records[i].payload != NULL || records[i].bookkeeping != NULL) return 4;
    }
    if (!seen) return 5;
    if (api->waitpid(pid, &status) != pid || status != expected) return 6;
    return 0;
}
static int put_file(const struct cb_api_v1 *api, const char *path, const void *p, size_t n)
{
    int fd = api->open(path, CB_O_WRONLY | CB_O_CREAT | CB_O_TRUNC, 0666);
    if (fd < 0 || api->write(fd, p, n) != (cb_ssize_t)n || api->close(fd) != 0)
        return -1;
    return 0;
}
static int check_file(const struct cb_api_v1 *api, const char *path, const void *p, size_t n)
{
    unsigned char buffer[128];
    size_t done = 0;
    struct cb_stat_v1 st;
    int fd = api->open(path, CB_O_RDONLY, 0);
    if (fd < 0 || api->fstat(fd, &st) != 0 || st.size != (uint64_t)n ||
        (st.mode & 0777) != 0666) return -1;
    while (done < n) {
        size_t part = n - done > sizeof(buffer) ? sizeof(buffer) : n - done;
        if (api->read(fd, buffer, part) != (cb_ssize_t)part ||
            memcmp(buffer, (const unsigned char *)p + done, part) != 0) return -1;
        done += part;
    }
    return api->read(fd, buffer, 1) == 0 && api->close(fd) == 0 ? 0 : -1;
}
static void reset_case(unsigned next_mode)
{
    mode = next_mode;
    calls = reads = out_writes = stdout_calls = closes = alloc_calls = fault_hits = 0;
    output_size = errors_size = record_count = 0;
    out_fd = -1; opened_outputs = explicitly_closed = 0;
    memset(records, 0, sizeof(records));
}
static int run_case(const struct cb_api_v1 *api, char *const envp[],
                    unsigned fault, char *const args[], const void *input, size_t n,
                    const void *expected, size_t count, const char *diagnostic,
                    int status, const void *file1, size_t n1, unsigned close_count)
{
    struct cb_spawn_action_v1 actions[] = {
        {CB_ABI_VERSION_V1, sizeof(struct cb_spawn_action_v1), CB_SPAWN_DUP2, 0, 0},
        {CB_ABI_VERSION_V1, sizeof(struct cb_spawn_action_v1), CB_SPAWN_CLOSE, 0, 0}
    };
    cb_pid_t pid;
    int fd;
    reset_case(fault);
    if (put_file(api, "/input", input, n) != 0) return 10;
    fd = api->open("/input", CB_O_RDONLY, 0);
    if (fd < 0) return 11;
    actions[0].from_fd = actions[1].from_fd = fd;
    if (api->spawn("tee", args, envp, actions, 2, &pid) != 0 || api->close(fd) != 0)
        return 12;
    if (collect(api, pid, status) != 0) return 13;
    if (output_size != count || memcmp(output, expected, count) != 0) return 14;
    if (errors_size != strlen(diagnostic) || memcmp(errors, diagnostic, errors_size) != 0)
        return 15;
    if (file1 != NULL && check_file(api, "/out1", file1, n1) != 0) return 16;
    if (closes != close_count || (fault != NORMAL && fault != SHORT_READ && !fault_hits))
        return 17;
    if (fault != ALLOC_ERROR &&
        explicitly_closed != (opened_outputs | UINT64_C(2))) return 21;
    if (fault == WRITE_ZERO && stdout_calls != 1) return 18;
    if ((fault == WRITE_EIO || fault == WRITE_EPIPE) && out_writes != 2) return 19;
    if (fault == WRITE_PART_ERROR && out_writes != 3) return 20;
    return 0;
}

static int pipe_child(const struct cb_api_v1 *api, char *const envp[],
                      char *const args[], cb_pid_t *pid, int *writer)
{
    int fds[2];
    struct cb_spawn_action_v1 actions[CB_MAX_FDS + 1];
    size_t count = 1;
    int fd;
    if (api->pipe(fds) != 0) return -1;
    actions[0].abi_version = CB_ABI_VERSION_V1;
    actions[0].struct_size = sizeof(actions[0]);
    actions[0].type = CB_SPAWN_DUP2;
    actions[0].from_fd = fds[0]; actions[0].to_fd = 0;
    /* Close every inherited nonstandard descriptor, including any sibling's
       writer. Spawn does not imply exec's close-on-exec semantics. */
    for (fd = 3; fd < CB_MAX_FDS; ++fd) {
        actions[count].abi_version = CB_ABI_VERSION_V1;
        actions[count].struct_size = sizeof(actions[count]);
        actions[count].type = CB_SPAWN_CLOSE;
        actions[count].from_fd = fd; actions[count].to_fd = 0;
        ++count;
    }
    if (api->spawn("tee", args, envp, actions, count, pid) != 0 || api->close(fds[0]) != 0)
        return -1;
    *writer = fds[1];
    return 0;
}
static int signal_cases(const struct cb_api_v1 *api, char *const envp[])
{
    char *ignore[] = {(char *)"tee", (char *)"-i", (char *)"/out1", NULL};
    char *ordinary[] = {(char *)"tee", (char *)"/out2", NULL};
    cb_pid_t a, b;
    int wa, wb, previous;
    reset_case(NORMAL);
    if (pipe_child(api, envp, ignore, &a, &wa) != 0 ||
        pipe_child(api, envp, ordinary, &b, &wb) != 0 ||
        wait_state(api, a, CB_TASK_BLOCKED_PIPE) != 0 ||
        wait_state(api, b, CB_TASK_BLOCKED_PIPE) != 0) return 50;
    /* Both lists are live and both commands have executed their getopt loop.
       Real requests must ignore one and terminate the independently default peer. */
    if (cb_kernel_request_interrupt(kernel, a) != 0 ||
        cb_kernel_request_interrupt(kernel, b) != 0 || collect(api, b, 130) != 0)
        return 51;
    if (api->write(wa, "I", 1) != 1 || api->close(wa) != 0 || api->close(wb) != 0 ||
        collect(api, a, 0) != 0 || output_size != 1 || output[0] != 'I' || errors_size ||
        check_file(api, "/out1", "I", 1) != 0 || check_file(api, "/out2", "", 0) != 0)
        return 52;
    /* Ordinary tee preserves inherited ignore; it must not install default. */
    reset_case(NORMAL);
    if (api->set_interrupt(CB_INTERRUPT_IGNORE, &previous) != 0 ||
        pipe_child(api, envp, ordinary, &a, &wa) != 0 ||
        api->set_interrupt(CB_INTERRUPT_DEFAULT, &previous) != 0 ||
        wait_state(api, a, CB_TASK_BLOCKED_PIPE) != 0 ||
        cb_kernel_request_interrupt(kernel, a) != 0 ||
        api->write(wa, "H", 1) != 1 || api->close(wa) != 0 || collect(api, a, 0) != 0 ||
        output_size != 1 || output[0] != 'H' || errors_size ||
        check_file(api, "/out2", "H", 1) != 0) return 53;
    /* Two ordinary commands retain distinct lists while both continue.
       Queue both payloads, close both writers, and observe exact per-file data. */
    reset_case(NORMAL);
    ignore[1] = (char *)"/out1"; ignore[2] = NULL;
    if (pipe_child(api, envp, ignore, &a, &wa) != 0 ||
        pipe_child(api, envp, ordinary, &b, &wb) != 0 ||
        wait_state(api, a, CB_TASK_BLOCKED_PIPE) != 0 ||
        wait_state(api, b, CB_TASK_BLOCKED_PIPE) != 0 ||
        api->write(wa, "AAA", 3) != 3 || api->write(wb, "BB", 2) != 2 ||
        api->close(wa) != 0 || api->close(wb) != 0 ||
        collect(api, a, 0) != 0 || collect(api, b, 0) != 0 ||
        errors_size || output_size != 5 ||
        (memcmp(output, "AAABB", 5) != 0 && memcmp(output, "BBAAA", 5) != 0) ||
        check_file(api, "/out1", "AAA", 3) != 0 || check_file(api, "/out2", "BB", 2) != 0)
        return 54;
    return 0;
}
static int controller(const struct cb_api_v1 *api, int argc,
                      char *const argv[], char *const envp[])
{
    static const unsigned char binary[] = {'A', 0, 'B'};
    char *stdout_only[] = {(char *)"tee", NULL};
    char *one[] = {(char *)"tee", (char *)"/out1", NULL};
    char *two[] = {(char *)"tee", (char *)"/out1", (char *)"/out2", NULL};
    char *append[] = {(char *)"tee", (char *)"-a", (char *)"/out1", NULL};
    char *bad[] = {(char *)"tee", (char *)"/missing/out", (char *)"/tmp", (char *)"/out1", NULL};
    int result;
    unsigned i;
    (void)argc; (void)argv;
#define RUN(id, ...) do { result = run_case(api, envp, __VA_ARGS__); if (result) return (id)*100+result; } while (0)
    RUN(1, NORMAL, one, "", 0, "", 0, "", 0, "", 0, 2);
    RUN(2, NORMAL, two, binary, sizeof(binary), binary, sizeof(binary), "", 0, binary, sizeof(binary), 3);
    if (check_file(api, "/out2", binary, sizeof(binary)) != 0) return 221;
    for (i = 0; i < sizeof(large_input)-1; ++i) large_input[i] = 'A';
    large_input[sizeof(large_input)-1] = 'B';
    RUN(3, NORMAL, one, large_input, sizeof(large_input), large_input, sizeof(large_input), "", 0, large_input, sizeof(large_input), 2);
    if (reads != 3) return 321; /* controlled RAMFS: 8192, 1, EOF */
    RUN(4, SHORT_READ, one, large_input, sizeof(large_input), large_input, sizeof(large_input), "", 0, large_input, sizeof(large_input), 2);
    if (reads != 74) return 421;
    if (put_file(api, "/out1", "OLD", 3) != 0) return 500;
    RUN(5, NORMAL, one, "NEW", 3, "NEW", 3, "", 0, "NEW", 3, 2);
    RUN(6, NORMAL, append, "!", 1, "!", 1, "", 0, "NEW!", 4, 2);
    RUN(7, NORMAL, bad, "OK", 2, "OK", 2, "tee: /missing/out: no such file or directory\ntee: /tmp: is a directory\n", 1, "OK", 2, 2);
    RUN(8, READ_FIRST, one, "ABCD", 4, "", 0, "tee: read: input/output error\n", 1, "", 0, 2);
    RUN(9, READ_AFTER, one, "ABCD", 4, "AB", 2, "tee: read: input/output error\n", 1, "AB", 2, 2);
    RUN(10, WRITE_SHORT, one, "ABCD", 4, "ABCD", 4, "", 0, "ABCD", 4, 2);
    RUN(11, WRITE_EIO, two, "ABCD", 4, "ABCD", 4, "tee: /out1: input/output error\n", 1, "CD", 2, 3);
    if (check_file(api, "/out2", "ABCD", 4) != 0) return 1121;
    RUN(12, WRITE_EPIPE, two, "ABCD", 4, "ABCD", 4, "tee: /out1: broken pipe\n", 1, "CD", 2, 3);
    if (check_file(api, "/out2", "ABCD", 4) != 0) return 1221;
    RUN(13, WRITE_PART_ERROR, one, "ABCD", 4, "ABCD", 4, "tee: /out1: input/output error\n", 1, "ACD", 3, 2);
    RUN(14, WRITE_ZERO, one, "OK", 2, "", 0, "tee: stdout: input/output error\n", 1, "OK", 2, 2);
    RUN(15, CLOSE_ERROR, one, "OK", 2, "OK", 2, "tee: stdout: input/output error\n", 1, "OK", 2, 2);
    RUN(16, ALLOC_ERROR, one, "OK", 2, "", 0, "tee: malloc: cannot allocate memory\n", 1, "", 0, 0);
#undef RUN
    result = run_case(api, envp, NORMAL, stdout_only, "S", 1, "S", 1, "", 0, NULL, 0, 1);
    if (result != 0) return 1700 + result;
    return signal_cases(api, envp);
}
static const struct cb_program_v1 program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "teeprobe", 0, 64*1024, controller
};
int cb_tee_probe(const struct cb_host_ops_v1 *base)
{
    struct cb_host_ops_v1 copy = *base;
    int result;
    host = base; kernel = NULL; reset_case(NORMAL);
    copy.console_write = console_write;
    copy.release = release;
    kernel = cb_kernel_create(&copy);
    if (kernel == NULL) return 90;
    cb_register_base_programs(kernel);
    real = kernel->api;
    kernel->api.open = open_file; kernel->api.read = read_file;
    kernel->api.write = write_file; kernel->api.close = close_file;
    kernel->api.allocate = allocate;
    if (cb_kernel_register(kernel, &program) != 0 || cb_kernel_boot(kernel, "teeprobe") != 0) {
        cb_kernel_destroy(kernel); kernel = NULL; return 91;
    }
    result = cb_kernel_run(kernel);
    cb_kernel_destroy(kernel); kernel = NULL;
    return result;
}
