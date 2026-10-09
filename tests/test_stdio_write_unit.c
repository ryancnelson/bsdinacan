/* Deterministic callback faults, identity and genuinely short capability storage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../libc/cb_libc.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "stdio write line %d\n", __LINE__); exit(1); } } while (0)
static int err, next_fd, opens, closes, allocations, releases, writes, plan, fail_alloc, fail_open;
static char bytes[64];
static size_t used;
static struct cb_input_state_v1 input = {CB_ABI_VERSION_V1, sizeof(input), 0, 0, 0, NULL, NULL};
static struct cb_stdio_state_v1 output;
static struct cb_input_state_v1 *supplied = &input;
static int get_error(void) { return err; }
static void set_error(int e) { err = e; }
static struct cb_input_state_v1 *get_input(void) { return supplied; }
static struct cb_stdio_state_v1 *get_output(void) { return &output; }
static void *alloc(size_t n) { ++allocations; return fail_alloc ? NULL : malloc(n); }
static void release(void *p) { ++releases; free(p); }
static int open_file(const char *p, int f, uint32_t m)
{ (void)p; ++opens; CHECK(f == (CB_O_WRONLY | CB_O_CREAT | CB_O_TRUNC) && m == 0666); if (fail_open) { err = CB_EISDIR; return -1; } return next_fd; }
static int close_file(int fd) { CHECK(fd == next_fd); ++closes; err = CB_EIO; return -1; }
static cb_ssize_t transfer(int fd, const void *p, size_t n)
{
    int call = writes++;
    CHECK(fd == next_fd);
    if (plan == 1 && call == 1) { err = CB_EPIPE; return -1; }
    if (plan == 2) return 0;
    if (plan == 3) return INT64_C(4294967360);
    if (n > 3) n = 3;
    CHECK(used + n <= sizeof(bytes)); memcpy(bytes + used, p, n); used += n;
    err = CB_EIO; return (cb_ssize_t)n;
}
int main(void)
{
    struct cb_api_v1 api = {0};
    struct cb_libc_file *stream, *other;
    unsigned char *short_storage;
    size_t size;
    int fd, before;
    api.abi_version = CB_ABI_VERSION_V1; api.struct_size = sizeof(api);
    api.get_errno = get_error; api.set_errno = set_error;
    api.input_state_location = get_input; api.stdio_state_location = get_output;
    api.allocate = alloc; api.release = release; api.open = open_file;
    api.close = close_file; api.write = transfer; bound_api = &api;
    output.abi_version = CB_ABI_VERSION_V1; output.struct_size = sizeof(output);
    fail_alloc = 1;
    CHECK(cb_libc_fopen("exists", "w") == NULL && err == CB_ENOMEM && opens == 0);
    fail_alloc = 0; fail_open = 1;
    CHECK(cb_libc_fopen("directory", "wb") == NULL && err == CB_EISDIR && releases == 1 && input.input_streams == NULL);
    fail_open = 0;
    for (fd = 1; fd <= 2; ++fd) {
        next_fd = fd; err = CB_ERANGE;
        stream = cb_libc_fopen("file", "w");
        CHECK(stream && err == CB_ERANGE);
        /* Logical standard closure and error flags do not belong to this FILE. */
        output.stdout_closed = output.stderr_closed = 1;
        output.stdout_error = output.stderr_error = 0;
        plan = 0; writes = 0; used = 0;
        CHECK(cb_libc_fprintf(stream, "%4d %s", 12, "abc") == 8 && err == CB_ERANGE);
        CHECK(used == 8 && memcmp(bytes, "  12 abc", 8) == 0);
        CHECK(cb_libc_fileno(stream) == fd && cb_libc_fflush(stream) == 0);
        before = writes;
        CHECK(cb_libc_fwrite("x", SIZE_MAX, 2, cb_libc_stdout_stream) == 0 &&
              err == CB_EOVERFLOW && writes == before);
        CHECK(cb_libc_fwrite(NULL, 1, 1, cb_libc_stdout_stream) == 0 &&
              err == CB_EINVAL && writes == before);
        CHECK(cb_libc_fwrite("x", 1, 1, cb_libc_stdout_stream) == 0 &&
              err == CB_EBADF && writes == before);
        CHECK(cb_libc_getc(stream) == EOF && err == CB_EINVAL && writes == before);
        CHECK(cb_libc_fwrite(NULL, 0, SIZE_MAX, (void *)1) == 0 && writes == before);
        CHECK(cb_libc_fwrite("x", SIZE_MAX, 2, stream) == 0 && err == CB_EOVERFLOW && writes == before);
        CHECK(cb_libc_fwrite(NULL, 1, 1, stream) == 0 && err == CB_EINVAL);
        for (plan = 1; plan <= 3; ++plan) {
            cb_libc_clearerr(stream); used = 0; writes = 0;
            CHECK(cb_libc_fwrite("abcdefgh", 2, 4, stream) == (plan == 1 ? 1u : 0u));
            CHECK(err == (plan == 1 ? CB_EPIPE : CB_EIO) && cb_libc_ferror(stream));
            CHECK(output.stdout_error == 0 && output.stderr_error == 0);
            CHECK(used == (plan == 1 ? 3u : 0u));
        }
        plan = 0; used = 0; writes = 0; err = CB_ENOENT;
        CHECK(cb_libc_fwrite("\0Z", 1, 2, stream) == 2 && err == CB_ENOENT && cb_libc_ferror(stream));
        CHECK(used == 2 && bytes[0] == 0 && bytes[1] == 'Z');
        cb_libc_clearerr(stream); CHECK(!cb_libc_ferror(stream));
        used = 0; CHECK(cb_libc_fprintf(stream, "a%q") == -1 && err == CB_EINVAL && !cb_libc_ferror(stream));
        plan = 3; CHECK(cb_libc_fprintf(stream, "x") == -1 && err == CB_EIO && cb_libc_ferror(stream));
        before = writes;
        CHECK(cb_libc_fprintf((void *)1, "x") == -1 && err == CB_EINVAL && writes == before);
        other = cb_libc_fopen("other", "w"); CHECK(other && !cb_libc_ferror(other));
        CHECK(cb_libc_fclose(other) == EOF && err == CB_EIO);
        CHECK(cb_libc_fclose(stream) == EOF && err == CB_EIO && input.input_streams == NULL);
        before = writes; CHECK(cb_libc_fprintf(stream, "x") == -1 && writes == before);
    }
    before = opens;
    size = offsetof(struct cb_api_v1, input_state_location);
    short_storage = malloc(size); CHECK(short_storage);
    api.struct_size = size; memcpy(short_storage, &api, size); bound_api = (void *)short_storage;
    CHECK(cb_libc_fopen("x", "w") == NULL && err == CB_ENOSYS && opens == before);
    free(short_storage); bound_api = &api; api.struct_size = sizeof(api);
    size = CB_INPUT_STATE_V1_MIN_SIZE;
    short_storage = malloc(size); CHECK(short_storage);
    input.struct_size = size; memcpy(short_storage, &input, size); supplied = (void *)short_storage;
    CHECK(cb_libc_fopen("x", "w") == NULL && err == CB_ENOSYS && opens == before);
    free(short_storage); supplied = &input; input.struct_size = sizeof(input);
    input.abi_version = 0;
    CHECK(cb_libc_fopen("x", "w") == NULL && err == CB_ENOSYS);
    supplied = NULL; CHECK(cb_libc_fopen("x", "w") == NULL && err == CB_ENOSYS);
    api.input_state_location = NULL; CHECK(cb_libc_fopen("x", "w") == NULL && err == CB_ENOSYS);
    CHECK(allocations == releases + 1 && closes == 4); /* One failed allocation. */
    puts("writable FILE callback tests passed"); return 0;
}
