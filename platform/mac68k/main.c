#include "host_mac.h"
#include "autorun.h"

#include <string.h>

int cb_console_write_probe(const struct cb_host_ops_v1 *host);

struct acceptance_case { const char *command, *expected; int status; };
static const struct cb_mac_autorun_ops autorun_ops = {
    cb_mac_write_result, cb_mac_capture_screen, cb_mac_write_done
};
extern const struct cb_program_v1 cb_progname_probe_program;
extern const struct cb_program_v1 cb_vfs_executable_probe_program;
extern const struct cb_program_v1 cb_direntprobe_program;
extern const struct cb_program_v1 cb_dirname_probe_program;
extern const struct cb_program_v1 cb_basename_probe_program;
extern const struct cb_program_v1 cb_locale_probe_program;
extern const struct cb_program_v1 cb_locale_env_probe_program;
extern const struct cb_program_v1 cb_terminal_probe_program;
extern const struct cb_program_v1 cb_poll_probe_program;
extern const struct cb_program_v1 cb_err_probe_program;
extern const struct cb_program_v1 cb_warn_probe_program;
extern const struct cb_program_v1 cb_strcpy_probe_program;
extern const struct cb_program_v1 cb_head_probe_program, cb_head_pipe_program;
extern const struct cb_program_v1 cb_memory_probe_program;
extern const struct cb_program_v1 cb_fread_probe_program, cb_fread_compat_program;
extern const struct cb_program_v1 cb_file_probe_program, cb_file_compat_program;
extern const struct cb_program_v1 cb_stdin_probe_program, cb_stdin_compat_program;
extern const struct cb_program_v1 cb_stdio_state_probe_program;
extern const struct cb_program_v1 cb_argv_probe_program;
extern const struct cb_program_v1 cb_stdio_oldtable_program;
extern const struct cb_program_v1 cb_fwrite_probe_program;
extern const struct cb_program_v1 cb_fwrite_compat_program;
extern const struct cb_program_v1 cb_fwrite_wrapper_program;
extern const struct cb_program_v1 cb_getoptprobe_program;
extern const struct cb_program_v1 cb_getopt_arg_probe_program;
extern const struct cb_program_v1 cb_truncate_probe_program;
extern const struct cb_program_v1 cb_strtoimax_probe_program;
static const struct acceptance_case cases[] = {
#define CB_MAC_CASE(command, expected, status) {command, expected, status},
#include "acceptance_cases.def"
#undef CB_MAC_CASE
};

int main(void)
{
    size_t index;
    int passed, autorun;
    char result[2048] = "cannedBSD System 7 / Retro68\n";
    struct cb_kernel *kernel;
    if (cb_mac_initialize() < 0) return 1;
    autorun = cb_mac_autorun_requested();
    if (autorun < 0 || (autorun && cb_mac_write_done("") < 0)) {
        cb_mac_text("Autorun setup failed. Shared evidence files must be writable.\n");
        while (!cb_mac_quitting()) cb_mac_pump(-1);
        cb_mac_shutdown();
        return 1;
    }
    cb_mac_text("cannedBSD: native 68K System 7 backend\n");
    passed = cb_mac_context_check() == 0;
    cb_mac_text(passed ? "PASS: independent stacks, 512 yields\n" : "FAIL: contexts\n");
    strcat(result, passed ? "PASS contexts\n" : "FAIL contexts\n");
    if (passed) {
        /* This helper owns and destroys its temporary kernel before any of the
           ordinary case kernels exist; it never replaces a live task's binding. */
        passed = cb_console_write_probe(cb_mac_host_ops()) == 0;
        cb_mac_text(passed ? "PASS: portable console write\n" : "FAIL: console write\n");
        strcat(result, passed ? "PASS consolewrite\n" : "FAIL consolewrite\n");
    }
    for (index = 0; passed && index < sizeof(cases) / sizeof(cases[0]); ++index) {
        int status = -1;
        cb_mac_text("$ "); cb_mac_text(cases[index].command); cb_mac_text("\n");
        kernel = cb_kernel_create(cb_mac_host_ops());
        cb_mac_capture_begin();
        if (kernel != NULL) {
            cb_register_base_programs(kernel);
            if (cb_kernel_register(kernel, &cb_progname_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_vfs_executable_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_direntprobe_program) == 0 &&
                cb_kernel_register(kernel, &cb_dirname_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_basename_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_strtoimax_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_locale_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_locale_env_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_terminal_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_poll_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_err_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_warn_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_strcpy_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_head_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_head_pipe_program) == 0 &&
                cb_kernel_register(kernel, &cb_memory_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_getopt_arg_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_argv_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_fread_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_fread_compat_program) == 0 &&
                cb_kernel_register(kernel, &cb_file_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_file_compat_program) == 0 &&
                cb_kernel_register(kernel, &cb_stdin_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_stdin_compat_program) == 0 &&
                cb_kernel_register(kernel, &cb_stdio_state_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_stdio_oldtable_program) == 0 &&
                cb_kernel_register(kernel, &cb_fwrite_probe_program) == 0 &&
                cb_kernel_register(kernel, &cb_fwrite_compat_program) == 0 &&
                cb_kernel_register(kernel, &cb_fwrite_wrapper_program) == 0 &&
                cb_kernel_register(kernel, &cb_getoptprobe_program) == 0 &&
                cb_kernel_register(kernel, &cb_truncate_probe_program) == 0 &&
                cb_kernel_boot(kernel, cases[index].command) == 0)
                status = cb_kernel_run(kernel);
            cb_kernel_destroy(kernel);
        }
        passed = cb_mac_capture_matches(cases[index].expected) &&
                 status == cases[index].status;
        strcat(result, passed ? "PASS " : "FAIL ");
        strcat(result, cases[index].command); strcat(result, "\n");
        cb_mac_text(passed ? "PASS\n" : "FAIL\n");
    }
    strcat(result, passed ? "ALL PASS\n" : "FAILED\n");
    cb_mac_text(passed ? "\nALL PASS\n" : "\nFAILED\n");
    if (autorun) {
        if (cb_mac_finish_autorun(passed, result, &autorun_ops) < 0) {
            cb_mac_text("Autorun evidence failed; completion withheld.\n");
            while (!cb_mac_quitting()) cb_mac_pump(-1);
            cb_mac_shutdown();
            return 1;
        }
        cb_mac_shutdown();
        return passed ? 0 : 1;
    }
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
