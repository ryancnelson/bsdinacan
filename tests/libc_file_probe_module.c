#include "cannedbsd/libc.h"
#include <string.h>

extern int cb_file_probe_main(int, char **);
extern void *stream_probe_handle(unsigned);
int cb_file_call(const struct cb_api_v1 *api, const char *mode)
{
    char *args[] = {(char *)"fileprobe", (char *)mode, NULL};
    return cb_libc_start(api, 2, args, cb_file_probe_main);
}
int cb_file_prepare(const struct cb_api_v1 *api)
{
    static const unsigned char bytes[] = {0,255,'A'};
    int fd = api->open("/tmp/stream-input", CB_O_CREAT | CB_O_TRUNC | CB_O_RDWR, 0600);
    if (fd < 0) return -1;
    if (api->write(fd, bytes, sizeof(bytes)) != (cb_ssize_t)sizeof(bytes)) {
        api->close(fd); return -1;
    }
    return api->close(fd);
}
static struct cb_libc_file *foreign_output;
static int writable_entry(const struct cb_api_v1 *api, const char *mode,
                          char *const envp[])
{
    struct cb_libc_file *out, *clo;
    struct cb_input_state_v1 *state = api->input_state_location();
    char *peer[] = {(char *)"fileprobe", (char *)"write-peer", NULL};
    char *after[] = {(char *)"fileprobe", (char *)"write-after", NULL};
    cb_pid_t child;
    int status, fd, clo_fd;
    if (strcmp(mode, "write-after") == 0) {
        if (state->input_streams != NULL || api->write(3, "z", 1) != 1 ||
            api->write(4, "z", 1) != -1 || api->get_errno() != CB_EBADF ||
            api->close(3) != 0) return 190;
        return 0;
    }
    /* Bind the ordinary libc entry before using its private veneers. */
    if (cb_file_call(api, "write-probe") != 0) return 191;
    if (strcmp(mode, "write-peer") == 0) {
        if (cb_libc_fprintf(foreign_output, "bad") != -1 ||
            api->get_errno() != CB_EINVAL) return 192;
        out = cb_libc_fopen("/tmp/peer-output", "w");
        if (out == NULL || cb_libc_fprintf(out, "peer") != 4 ||
            cb_libc_ferror(out) || cb_libc_fclose(out) != 0) return 193;
        return 0;
    }
    out = cb_libc_fopen("/tmp/lifecycle-output", "w");
    clo = cb_libc_fopen("/tmp/lifecycle-cloexec", "wb");
    if (out == NULL || clo == NULL) return 194;
    fd = cb_libc_fileno(out); clo_fd = cb_libc_fileno(clo);
    if (fd != 3 || clo_fd != 4 || api->set_cloexec(clo_fd, 1) != 0) return 195;
    foreign_output = out;
    api->set_errno(CB_ERANGE);
    if (api->spawn("fileprobe", peer, envp, NULL, 0, &child) != 0 ||
        api->waitpid(child, &status) != child || status != 0) return 196;
    api->set_errno(CB_ERANGE);
    if (cb_libc_fprintf(out, "parent") != 6 || api->get_errno() != CB_ERANGE ||
        cb_libc_ferror(out) || cb_libc_ferror(clo)) return 197;
    if (api->exec("missing-writable-image", after, envp) != -1 ||
        api->get_errno() != CB_ENOENT ||
        cb_libc_fwrite("x", 1, 1, out) != 1 || cb_libc_fwrite("y", 1, 1, clo) != 1)
        return 198;
    api->exec("fileprobe", after, envp);
    return 199;
}
static int entry(const struct cb_api_v1 *api, int argc,
                  char *const argv[], char *const envp[])
{
    char *peer[] = {(char *)"fileprobe", (char *)"peer", NULL};
    char *after[] = {(char *)"fileprobe", (char *)"after", NULL};
    struct cb_stat_v1 metadata;
    struct cb_input_state_v1 *state = api->input_state_location();
    cb_pid_t child;
    int status, fd;
    void *head;
    unsigned char byte;
    if (argc == 2 && (strcmp(argv[1], "write-probe") == 0 ||
                      strcmp(argv[1], "write-peer") == 0 ||
                      strcmp(argv[1], "write-after") == 0))
        return writable_entry(api, argv[1], envp);
    if (argc == 2 && (strcmp(argv[1], "timestamp-zero") == 0 ||
                      strcmp(argv[1], "timestamp-known") == 0))
        return cb_file_call(api, argv[1]);
    if (argc == 2 && strcmp(argv[1], "after") == 0) {
        if (state->input_streams != NULL || state->stdin_closed ||
            cb_file_call(api, "clean-stdin") != 0) return 41;
        /* The previous image's fopen descriptor remains a raw non-CLOEXEC fd. */
        if (api->read(3, &byte, 1) != 1 || byte != 0 || api->close(3) != 0)
            return 42;
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "peer") == 0) {
        if (state->input_streams != NULL) return 43;
        return cb_file_call(api, "foreign");
    }
    if (cb_file_call(api, "mman-probe") != 0) return 50;
    if (cb_file_call(api, "mkdir-probe") != 0) return 51;
    if (cb_file_call(api, "cp-stub-probe") != 0) return 57;
    /* No descriptors beyond stdin/stdout/stderr exist yet. The ordinary
     * source creates with DEFFILEMODE and closes fd3; verify before later
     * task exit or kernel cleanup could hide a leaked descriptor/file. */
    if (cb_file_call(api, "default-mode") != 0) return 51;
    if (api->fstat(3, &metadata) != -1 || api->get_errno() != CB_EBADF)
        return 52;
    if (api->stat("/tmp/default-mode", &metadata) != 0 ||
        metadata.mode != 0666 || metadata.type != CB_NODE_REGULAR ||
        metadata.size != 0) return 53;
    fd = api->open("/tmp/default-mode", CB_O_RDONLY, 0);
    if (fd != 3) { if (fd >= 0) api->close(fd); return 54; }
    status = api->fstat(fd, &metadata);
    if (api->close(fd) != 0 || status != 0 || metadata.mode != 0666 ||
        metadata.type != CB_NODE_REGULAR || metadata.size != 0) return 55;
    if (api->fstat(fd, &metadata) != -1 || api->get_errno() != CB_EBADF ||
        api->unlink("/tmp/default-mode") != 0 ||
        api->stat("/tmp/default-mode", &metadata) != -1 ||
        api->get_errno() != CB_ENOENT) return 56;
    if (cb_file_call(api, "write-probe") != 0) return 58;
    if (cb_file_prepare(api) != 0 || cb_file_call(api, "stat-probe") != 0 ||
        cb_file_call(api, "stdio-probe") != 0 ||
        cb_file_call(api, "invalid") != 0 ||
        cb_file_call(api, "missing-directory") != 0 ||
        cb_file_call(api, "open-two") != 0) return 44;
    head = state->input_streams;
    if (api->spawn("fileprobe", peer, envp, NULL, 0, &child) != 0 ||
        api->waitpid(child, &status) != child || status != 0 ||
        state->input_streams != head) return 45;
    if (api->exec("missing-file-command", after, envp) != -1 ||
        api->get_errno() != CB_ENOENT || state->input_streams != head ||
        cb_file_call(api, "read-two") != 0 || cb_file_call(api, "close-two") != 0 ||
        state->input_streams != NULL) return 46;
    if (cb_file_call(api, "close-stdin") != 0 ||
        cb_file_call(api, "closed-stdin") != 0) return 47;
    fd = api->open("/tmp/stream-input", CB_O_RDONLY, 0);
    if (fd != 0 || cb_file_call(api, "closed-stdin") != 0) return 48;
    /* Leave fd0 rebound and logically closed, plus one raw-on-exec stream. */
    if (cb_file_call(api, "open-one") != 0 || stream_probe_handle(0) == NULL)
        return 49;
    api->exec("fileprobe", after, envp);
    return 50;
}
const struct cb_program_v1 cb_file_probe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "fileprobe", 0,
    64 * 1024, entry
};
