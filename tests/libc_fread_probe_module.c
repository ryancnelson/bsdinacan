#include "cannedbsd/libc.h"
#include <string.h>

extern int cb_fread_probe_main(int, char **);
int cb_fread_call(const struct cb_api_v1 *api, const char *mode)
{
    char *args[] = {(char *)"freadprobe", (char *)mode, NULL};
    return cb_libc_start(api, 2, args, cb_fread_probe_main);
}
static int noop(int argc, char **argv) { (void)argc; (void)argv; return 0; }
static const struct cb_api_v1 *real_api;
static const unsigned char data[] = {0,255,'A','B','C','D','E','F','G','H','I','J'};
struct transfer { size_t request, offset; cb_ssize_t result; };
struct sequence { const char *argument, *ordinary; unsigned count; struct transfer steps[3]; };
static const struct sequence sequences[] = {
    {"full", "full", 3, {{8,0,3}, {5,3,5}, {1,0,0}}},
    {"bytes", "bytes", 3, {{12,0,3}, {9,3,5}, {4,8,4}}},
    {"partial-eof", "partial-eof", 2, {{8,0,6}, {2,6,0}}},
    {"small-eof", "small-eof", 2, {{4,0,2}, {2,2,0}}},
    {"partial-error", "partial-error", 3, {{8,0,6}, {2,6,-1}, {4,0,4}}},
    {"error-first", "error-first", 1, {{8,0,-1}}},
    {"eof-first", "eof-first", 1, {{8,0,0}}},
    {"overreturn", "overreturn", 1, {{8,0,9}}},
    {"wide-overreturn", "overreturn", 1, {{8,0,INT64_C(4294967304)}}},
    {"clip", "clip", 1, {{(uint64_t)SIZE_MAX > (uint64_t)INT64_MAX ?
                            (size_t)INT64_MAX : SIZE_MAX,0,0}}},
    {"arguments", "arguments", 0, {{0,0,0}}},
    {"zero", "zero", 0, {{0,0,0}}}
};
static const struct sequence *sequence;
static unsigned step, mismatch, callbacks;
static size_t consumed;
static unsigned char *first_buffer;
static struct cb_input_state_v1 *disturb_state(void)
{
    struct cb_input_state_v1 *state = real_api->input_state_location();
    real_api->set_errno(CB_EINVAL); return state;
}
static cb_ssize_t read_sequence(int fd, void *buffer, size_t count)
{
    const struct transfer *transfer;
    if (step >= sequence->count) { mismatch = 1; real_api->set_errno(CB_EIO); return -1; }
    transfer = &sequence->steps[step++];
    if (step == 1) first_buffer = buffer;
    if (fd != 0 || count != transfer->request ||
        (unsigned char *)buffer != first_buffer + transfer->offset) mismatch = 1;
    if (transfer->result < 0) { real_api->set_errno(CB_EIO); return -1; }
    if ((uint64_t)transfer->result <= (uint64_t)count && transfer->result > 0) {
        size_t bytes = (size_t)transfer->result;
        if (bytes > sizeof(data) - consumed) { mismatch = 1; return -1; }
        memcpy(buffer, data + consumed, bytes); consumed += bytes;
    }
    real_api->set_errno(CB_EINVAL); /* Successful callbacks must not leak errno. */
    return transfer->result;
}
static cb_ssize_t count_read(int fd, void *buffer, size_t count)
{ (void)fd; (void)buffer; (void)count; ++callbacks; return 0; }
static int count_get(void) { ++callbacks; return CB_EPIPE; }
static void count_set(int error) { (void)error; ++callbacks; }
static struct cb_input_state_v1 *count_state(void) { ++callbacks; return NULL; }
static int *count_errno(void) { static int error; ++callbacks; return &error; }
static int entry(const struct cb_api_v1 *api, int argc,
                  char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy = *api;
    size_t i;
    int status, fd;
    cb_pid_t child;
    if (argc == 2) {
        if (strcmp(argv[1], "zero-quiet") == 0) {
            copy.read = count_read; copy.get_errno = count_get; copy.set_errno = count_set;
            copy.errno_location = count_errno; copy.input_state_location = count_state;
            callbacks = 0;
            status = cb_fread_call(&copy, "zero-quiet");
            (void)cb_libc_start(api, 0, NULL, noop);
            return status == 0 && callbacks == 0 ? 0 : 30;
        }
        if (strcmp(argv[1], "dynamic") == 0 || strcmp(argv[1], "closed") == 0)
            return cb_fread_call(api, argv[1]);
        for (i = 0; i < sizeof(sequences)/sizeof(sequences[0]); ++i)
            if (strcmp(argv[1], sequences[i].argument) == 0) break;
        if (i == sizeof(sequences)/sizeof(sequences[0])) return 31;
        sequence = &sequences[i]; real_api = api;
        step = mismatch = 0; consumed = 0; first_buffer = NULL;
        copy.read = read_sequence; copy.input_state_location = disturb_state;
        status = cb_fread_call(&copy, sequence->ordinary);
        /* Argument checks must precede an existing EOF indicator too. */
        if (status == 0 && strcmp(argv[1], "eof-first") == 0)
            status = cb_fread_call(&copy, "arguments");
        (void)cb_libc_start(api, 0, NULL, noop);
        return status != 0 ? status : mismatch || step != sequence->count ? 32 : 0;
    }
    fd = api->open("/tmp/fread-input", CB_O_CREAT | CB_O_TRUNC | CB_O_WRONLY, 0600);
    if (fd < 0 || api->write(fd, data, sizeof(data)) != (cb_ssize_t)sizeof(data) ||
        api->close(fd) != 0) return 33;
    for (i = 0; i < sizeof(sequences)/sizeof(sequences[0]) + 3; ++i) {
        const char *mode = i < sizeof(sequences)/sizeof(sequences[0]) ? sequences[i].argument :
            i == sizeof(sequences)/sizeof(sequences[0]) ? "zero-quiet" :
            i == sizeof(sequences)/sizeof(sequences[0]) + 1 ? "dynamic" : "closed";
        char *args[] = {(char *)"freadprobe", (char *)mode, NULL};
        if (api->spawn("freadprobe", args, envp, NULL, 0, &child) != 0 ||
            api->waitpid(child, &status) != child || status != 0)
            return 40 + (int)i;
    }
    return 0;
}
const struct cb_program_v1 cb_fread_probe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "freadprobe", 0, 64 * 1024, entry
};
