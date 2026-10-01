#include "internal.h"
#include <string.h>

/*
 * STATICS-CACHE-02: lifecycle of the pinned commands' retained buffers
 * (cat raw_cat()'s buf/fb_buf/bsize, ls printcol()'s array/lastentries).
 * Runs the real registered cat and ls through ordinary shell sessions.
 *
 * The host allocator below guards every block with a trailing canary and
 * quarantines released blocks with a poison fill until the scenario ends,
 * so an overflow or a write through freed memory fails deterministically
 * in the normal build too, not only under the sanitizer. Each nonzero
 * result names the failing observation.
 */

#define GUARD_SIZE 16
#define GUARD_BYTE 0xa5
#define POISON_BYTE 0xdd
#define HEADER_MAGIC 0x43414348u
#define FIXTURE_LINE 1500
#define CACHE_SIZE 4099

struct block {
    struct block *next;
    size_t size;
    unsigned magic;
    unsigned released;
    unsigned char padding[8];
};

static const struct cb_host_ops_v1 *base;
static struct cb_host_ops_v1 host;
static struct cb_kernel *current_kernel;
static struct block *quarantine;
static long live_blocks;
static unsigned corrupt_blocks;
static size_t fail_size;
static unsigned fail_countdown;
static unsigned fail_hits;
static char output[32768];
static size_t output_size;
static int output_overflow;
static char fixture[2 * (FIXTURE_LINE + 1)];
static char big[4 * sizeof(fixture)];
/* Live blocks of exactly CACHE_SIZE bytes: only cat -B CACHE_SIZE asks for
   that size, so this counts cat's own retained buffer and nothing else. */
static long tracked_cache_blocks;
static unsigned cache_allocations;

static int guard_intact(const struct block *block)
{
    const unsigned char *guard = (const unsigned char *)(block + 1) + block->size;
    size_t i;
    for (i = 0; i < GUARD_SIZE; ++i)
        if (guard[i] != GUARD_BYTE)
            return 0;
    return block->magic == HEADER_MAGIC;
}

/* The shell spawning cat runs the executor's instance_create, and cat
   itself runs the command: both sides of the lifecycle are faultable. */
static int faultable_task(void)
{
    const struct cb_task *task =
        current_kernel != NULL ? current_kernel->current : NULL;
    return task != NULL && task->program != NULL &&
           task->program->name != NULL &&
           (strcmp(task->program->name, "cat") == 0 ||
            strcmp(task->program->name, "sh") == 0);
}

static void *probe_allocate(size_t size)
{
    struct block *block;
    if (fail_size != 0 && size == fail_size) {
        fail_size = 0;
        ++fail_hits;
        return NULL;
    }
    if (fail_countdown != 0 && faultable_task() && --fail_countdown == 0) {
        ++fail_hits;
        return NULL;
    }
    block = base->allocate(sizeof(*block) + size + GUARD_SIZE);
    if (block == NULL)
        return NULL;
    block->next = NULL;
    block->size = size;
    block->magic = HEADER_MAGIC;
    block->released = 0;
    memset((unsigned char *)(block + 1) + size, GUARD_BYTE, GUARD_SIZE);
    ++live_blocks;
    if (size == CACHE_SIZE) {
        ++tracked_cache_blocks;
        ++cache_allocations;
    }
    return block + 1;
}

static void probe_release(void *pointer)
{
    struct block *block;
    if (pointer == NULL)
        return;
    block = (struct block *)pointer - 1;
    if (block->released || !guard_intact(block))
        ++corrupt_blocks;
    block->released = 1;
    memset(pointer, POISON_BYTE, block->size);
    block->next = quarantine;
    quarantine = block;
    --live_blocks;
    if (block->size == CACHE_SIZE)
        --tracked_cache_blocks;
}

static void *probe_resize(void *pointer, size_t size)
{
    void *resized;
    struct block *block;
    if (pointer == NULL)
        return probe_allocate(size);
    resized = probe_allocate(size != 0 ? size : 1);
    if (resized == NULL)
        return NULL;
    block = (struct block *)pointer - 1;
    memcpy(resized, pointer, block->size < size ? block->size : size);
    probe_release(pointer);
    return resized;
}

/* Every released block must still hold its poison: anything else is a
   write through memory a task or kernel had already given back. */
static void drain_quarantine(void)
{
    while (quarantine != NULL) {
        struct block *block = quarantine;
        const unsigned char *bytes = (const unsigned char *)(block + 1);
        size_t i;
        quarantine = block->next;
        for (i = 0; i < block->size; ++i) {
            if (bytes[i] != POISON_BYTE) {
                ++corrupt_blocks;
                break;
            }
        }
        if (!guard_intact(block))
            ++corrupt_blocks;
        base->release(block);
    }
}

static cb_ssize_t probe_console_write(int stream, const void *buffer, size_t count)
{
    (void)stream;
    if (count > sizeof(output) - output_size - 1) {
        output_overflow = 1;
        return -CB_ENOSPC;
    }
    memcpy(output + output_size, buffer, count);
    output_size += count;
    output[output_size] = '\0';
    return (cb_ssize_t)count;
}

static int probe_console_poll(int timeout_ms)
{
    (void)timeout_ms;
    return 0;
}

static cb_ssize_t probe_console_read(void *buffer, size_t count)
{
    (void)buffer;
    (void)count;
    return 0;
}

static int write_file(const struct cb_api_v1 *api, const char *path,
                      const char *data, size_t size)
{
    int fd = api->open(path, CB_O_WRONLY | CB_O_CREAT | CB_O_TRUNC, 0644);
    size_t done = 0;
    if (fd < 0)
        return -1;
    while (done < size) {
        cb_ssize_t wrote = api->write(fd, data + done, size - done);
        if (wrote <= 0) {
            api->close(fd);
            return -1;
        }
        done += (size_t)wrote;
    }
    return api->close(fd);
}

/* /tmp/f: two 1500-letter lines (3002 bytes); /tmp/big: four copies. */
static int fixture_main(const struct cb_api_v1 *api, int argc,
                        char *const argv[], char *const envp[])
{
    (void)argc;
    (void)argv;
    (void)envp;
    return write_file(api, "/tmp/f", fixture, sizeof(fixture)) != 0 ||
                   write_file(api, "/tmp/big", big, sizeof(big)) != 0
               ? 1
               : 0;
}

static const struct cb_program_v1 fixture_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "cachefixture", 0,
    64 * 1024, fixture_main
};

/* Reads its stdin to EOF, i.e. until every writer -- the cat task ahead of
   it in the pipeline -- has exited, then reports whether that task's
   CACHE_SIZE buffer is still allocated. Runs before the shell's own
   pipeline wait returns and long before kernel teardown. */
static int cachecheck_main(const struct cb_api_v1 *api, int argc,
                           char *const argv[], char *const envp[])
{
    char buffer[512];
    cb_ssize_t got;
    (void)argc;
    (void)argv;
    (void)envp;
    while ((got = api->read(0, buffer, sizeof(buffer))) > 0)
        continue;
    if (got < 0)
        return 2;
    return tracked_cache_blocks != 0 ? 3 : 0;
}

static const struct cb_program_v1 cachecheck_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "cachecheck", 0,
    64 * 1024, cachecheck_main
};

struct session_result {
    int boot;
    int status;
};

static struct session_result run_session(const char *command)
{
    struct session_result result = { -1, -1 };
    output_size = 0;
    output[0] = '\0';
    output_overflow = 0;
    current_kernel = cb_kernel_create(&host);
    if (current_kernel == NULL)
        return result;
    cb_register_base_programs(current_kernel);
    if (cb_kernel_register(current_kernel, &fixture_program) < 0 ||
        cb_kernel_register(current_kernel, &cachecheck_program) < 0) {
        cb_kernel_destroy(current_kernel);
        current_kernel = NULL;
        return result;
    }
    result.boot = cb_kernel_boot(current_kernel, command);
    if (result.boot == 0)
        result.status = cb_kernel_run(current_kernel);
    cb_kernel_destroy(current_kernel);
    current_kernel = NULL;
    return result;
}

static int expect_output(const char *expected, size_t size)
{
    return !output_overflow && output_size == size &&
           memcmp(output, expected, size) == 0;
}

/* Ends a scenario: everything a session allocated must be back and every
   released block untouched since. */
static int settled(void)
{
    drain_quarantine();
    return live_blocks == 0 && corrupt_blocks == 0 && tracked_cache_blocks == 0;
}

static int check_resize(void)
{
    char expected[2 * sizeof(fixture)];
    struct session_result result = run_session(
        "cachefixture; cat -B 2048 /tmp/f > /tmp/o1; "
        "cat -B 4096 /tmp/f > /tmp/o2; cat /tmp/o1 /tmp/o2");
    memcpy(expected, fixture, sizeof(fixture));
    memcpy(expected + sizeof(fixture), fixture, sizeof(fixture));
    if (result.boot != 0 || result.status != 0)
        return 10;
    if (!expect_output(expected, sizeof(expected)))
        return 11;
    return settled() ? 0 : 12;
}

static int check_recreate(void)
{
    int pass;
    for (pass = 0; pass < 2; ++pass) {
        struct session_result result =
            run_session("cachefixture; cat -B 2048 /tmp/f");
        if (result.boot != 0 || result.status != 0)
            return 20 + pass;
        if (!expect_output(fixture, sizeof(fixture)))
            return 22 + pass;
    }
    /* Drained only now: a second kernel writing through the first
       kernel's released buffer must still find it poisoned. */
    return settled() ? 0 : 24;
}

static int check_defaults(void)
{
    static const char listing[] = "aa bb cc\n";
    int pass;
    for (pass = 0; pass < 2; ++pass) {
        struct session_result result = run_session(
            "echo x > /tmp/aa; echo x > /tmp/bb; echo x > /tmp/cc; "
            "ls /tmp; ls /tmp");
        if (result.boot != 0 || result.status != 0)
            return 30 + pass;
        /* termwidth's compiled 80 makes both listings columnar; a zeroed
           default falls back to one name per line. */
        if (output_size != 2 * (sizeof(listing) - 1) ||
            memcmp(output, listing, sizeof(listing) - 1) != 0 ||
            memcmp(output + sizeof(listing) - 1, listing,
                   sizeof(listing) - 1) != 0)
            return 32 + pass;
    }
    return settled() ? 0 : 34;
}

static int check_interleaved(void)
{
    static const char *const commands[] = {
        "cachefixture; cat /tmp/big | cat",
        "cachefixture; cat -B 2048 /tmp/big | cat -B 4096",
        "cachefixture; cat -B 4096 /tmp/big | cat | cat -B 2048",
    };
    size_t i;
    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        struct session_result result = run_session(commands[i]);
        if (result.boot != 0 || result.status != 0)
            return 40 + (int)i;
        if (!expect_output(big, sizeof(big)))
            return 43 + (int)i;
    }
    return settled() ? 0 : 46;
}

static int check_immediate_cleanup(void)
{
    struct session_result result = run_session(
        "cachefixture; cat -B 4099 /tmp/big | cachecheck; echo $?");
    if (result.boot != 0 || result.status != 0)
        return 50;
    if (!expect_output("0\n", 2))
        return 51;
    return settled() ? 0 : 52;
}

static int check_cat_malloc_failure(void)
{
    /* Pinned cat reports the failed request before selecting its fallback. */
    static const char warning[] = "cat: malloc, using 4099 buffer\n";
    const char *contents;
    struct session_result result;
    fail_size = CACHE_SIZE;
    fail_hits = 0;
    cache_allocations = 0;
    /* The first cat falls back to its built-in buffer; the second must
       start from its own -B request again, not the fallback's size. */
    result = run_session("cachefixture; cat -B 4099 /tmp/f > /tmp/o1; "
                         "cat -B 4099 /tmp/f > /tmp/o2; cat /tmp/o1 /tmp/o2");
    fail_size = 0;
    if (fail_hits != 1)
        return 60;
    if (result.boot != 0 || result.status != 0)
        return 61;
    /* The second cat's -B 4099 is its own request: reusing the first
       cat's 1024-byte fallback would read 4099 bytes into it. */
    if (cache_allocations != 1)
        return 64;
    contents = output_overflow ? NULL : strchr(output, '\n');
    if (contents == NULL ||
        (size_t)(contents + 1 - output) != sizeof(warning) - 1 ||
        memcmp(output, warning, sizeof(warning) - 1) != 0 ||
        (size_t)(output + output_size - (contents + 1)) != 2 * sizeof(fixture) ||
        memcmp(contents + 1, fixture, sizeof(fixture)) != 0 ||
        memcmp(contents + 1 + sizeof(fixture), fixture, sizeof(fixture)) != 0)
        return 62;
    return settled() ? 0 : 63;
}

static int check_allocation_sweep(void)
{
    unsigned position;
    struct session_result result;
    for (position = 1; position < 512; ++position) {
        fail_countdown = position;
        fail_hits = 0;
        result = run_session("cachefixture; cat -B 4099 /tmp/f > /tmp/o; "
                             "cat /tmp/o");
        fail_countdown = 0;
        if (result.boot != 0)
            return 70;
        if (!settled())
            return 71;
        if (fail_hits == 0)
            break;
    }
    if (position == 512)
        return 72;
    /* The last position injected nothing: an ordinary complete session. */
    if (result.status != 0 || !expect_output(fixture, sizeof(fixture)))
        return 73;
    return 0;
}

static int (*const cases[])(void) = {
    check_resize, check_recreate, check_defaults, check_interleaved,
    check_immediate_cleanup, check_cat_malloc_failure, check_allocation_sweep,
};

size_t cb_statics_cache_probe_count(void)
{
    return sizeof(cases) / sizeof(cases[0]);
}

/* Runs one case with fresh accounting; zero is a pass. */
int cb_statics_cache_probe(const struct cb_host_ops_v1 *host_ops, size_t index)
{
    size_t i;
    if (index >= cb_statics_cache_probe_count())
        return 1;
    base = host_ops;
    host = *host_ops;
    host.allocate = probe_allocate;
    host.resize = probe_resize;
    host.release = probe_release;
    host.console_write = probe_console_write;
    host.console_poll = probe_console_poll;
    host.console_read = probe_console_read;
    for (i = 0; i < FIXTURE_LINE; ++i) {
        fixture[i] = 'a';
        fixture[FIXTURE_LINE + 1 + i] = 'b';
    }
    fixture[FIXTURE_LINE] = '\n';
    fixture[2 * FIXTURE_LINE + 1] = '\n';
    for (i = 0; i < 4; ++i)
        memcpy(big + i * sizeof(fixture), fixture, sizeof(fixture));
    drain_quarantine();
    live_blocks = 0;
    corrupt_blocks = 0;
    tracked_cache_blocks = 0;
    fail_size = 0;
    fail_countdown = 0;
    return cases[index]();
}

const char *cb_statics_cache_probe_output(void)
{
    return output;
}
