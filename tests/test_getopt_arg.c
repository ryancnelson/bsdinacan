#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const struct cb_program_v1 cb_getopt_arg_probe_program;
static char transcript[2][256];
static size_t used[2];
static void fail(const char *message)
{
    fprintf(stderr, "FAIL: required getopt %s\n", message);
    exit(1);
}
static cb_ssize_t capture(int stream, const void *data, size_t length)
{
    if (stream < 1 || stream > 2 || used[stream-1] + length >= sizeof(transcript[0]))
        fail("capture bounds");
    memcpy(transcript[stream-1] + used[stream-1], data, length);
    used[stream-1] += length;
    transcript[stream-1][used[stream-1]] = '\0';
    return (cb_ssize_t)length;
}
void cb_test_getopt_arg(void)
{
    static const struct { const char *mode, *error; } cases[] = {
        {"attached", ""}, {"separate", ""}, {"cluster", ""},
        {"cluster-separate", ""}, {"dash-value", ""}, {"double-value", ""},
        {"empty-value", ""},
        {"missing", "argprobe: option requires an argument -- n\n"},
        {"missing-cluster", "argprobe: option requires an argument -- n\n"},
        {"missing-after", "argprobe: option requires an argument -- n\n"},
        {"attached-dash", ""}, {"missing-colon", ""}, {"missing-quiet", ""},
        {"unknown", "argprobe: illegal option -- z\n"},
        {"unknown-colon", ""}, {"unknown-quiet", ""}, {"colon-is-syntax", ""},
        {"operand", ""}, {"dash", ""}, {"double", ""}, {"attached-rest", ""},
        {"flag-after", ""}, {"unknown-cluster", "argprobe: illegal option -- z\n"},
        {"no-permutation", ""}, {"empty", ""}, {"lifecycle", ""}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        struct cb_host_ops_v1 host = *cb_linux_host_ops();
        struct cb_kernel *kernel;
        int status;
        char command[100];
        host.console_write = capture;
        memset(transcript, 0, sizeof(transcript));
        memset(used, 0, sizeof(used));
        kernel = cb_kernel_create(&host);
        if (kernel == NULL) fail("create");
        cb_register_base_programs(kernel);
        if (cb_kernel_register(kernel, &cb_getopt_arg_probe_program) != 0)
            fail("registration");
        snprintf(command, sizeof(command), "getoptargs %s", cases[i].mode);
        if (cb_kernel_boot(kernel, command) != 0) fail("boot");
        status = cb_kernel_run(kernel);
        cb_kernel_destroy(kernel);
        if (status != 0 || transcript[0][0] != '\0' ||
            strcmp(transcript[1], cases[i].error) != 0) {
            fprintf(stderr, "mode %s status %d stdout [%s] stderr [%s]\n",
                    cases[i].mode, status, transcript[0], transcript[1]);
            fail("exact parse/stream result");
        }
    }
    puts("required getopt tests passed");
}
