#include "host_mac.h"

#include <string.h>

struct acceptance_case { const char *command, *expected; int status; };
static const struct acceptance_case cases[] = {
    {"echo hello | tr a-z A-Z > /tmp/result; cat /tmp/result", "HELLO\n", 0},
    {"echo -n hello | wc -c", "5\n", 0},
    {"echo abc | cat | tr a-z A-Z", "ABC\n", 0},
    {"echo one > /tmp/x; echo two >> /tmp/x; cat /tmp/x", "one\ntwo\n", 0},
    {"false; echo $?", "1\n", 0},
    {"cd /tmp; pwd", "/tmp\n", 0},
    {"exit 7", "", 7}
};

int main(void)
{
    size_t index;
    int passed;
    char result[2048] = "cannedBSD System 7 / Retro68\n";
    struct cb_kernel *kernel;
    if (cb_mac_initialize() < 0) return 1;
    cb_mac_text("cannedBSD: native 68K System 7 backend\n");
    passed = cb_mac_context_check() == 0;
    cb_mac_text(passed ? "PASS: independent stacks, 512 yields\n" : "FAIL: contexts\n");
    strcat(result, passed ? "PASS contexts\n" : "FAIL contexts\n");
    for (index = 0; passed && index < sizeof(cases) / sizeof(cases[0]); ++index) {
        int status = -1;
        cb_mac_text("$ "); cb_mac_text(cases[index].command); cb_mac_text("\n");
        kernel = cb_kernel_create(cb_mac_host_ops());
        cb_mac_capture_begin();
        if (kernel != NULL) {
            cb_register_base_programs(kernel);
            if (cb_kernel_boot(kernel, cases[index].command) == 0)
                status = cb_kernel_run(kernel);
            cb_kernel_destroy(kernel);
        }
        passed = status == cases[index].status &&
                 strcmp(cb_mac_capture(), cases[index].expected) == 0;
        strcat(result, passed ? "PASS " : "FAIL ");
        strcat(result, cases[index].command); strcat(result, "\n");
        cb_mac_text(passed ? "PASS\n" : "FAIL\n");
    }
    strcat(result, passed ? "ALL PASS\n" : "FAILED\n");
    cb_mac_text(passed ? "\nALL PASS\n" : "\nFAILED\n");
    cb_mac_text(cb_mac_write_result(result) == 0
        ? "Evidence: Unix:cannedbsd-result.txt\n"
        : "Could not write evidence to Unix volume.\n");
    if (passed) {
        cb_mac_text("\nInteractive shell. Type exit or Command-Q to quit.\n");
        kernel = cb_kernel_create(cb_mac_host_ops());
        if (kernel != NULL) {
            cb_register_base_programs(kernel);
            if (cb_kernel_boot(kernel, NULL) == 0) cb_kernel_run(kernel);
            cb_kernel_destroy(kernel);
        }
    } else {
        while (!cb_mac_quitting()) cb_mac_pump(-1);
    }
    cb_mac_shutdown();
    return passed ? 0 : 1;
}
