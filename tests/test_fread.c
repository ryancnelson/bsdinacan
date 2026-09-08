#include "internal.h"
#include <stdio.h>
#include <stdlib.h>

extern const struct cb_program_v1 cb_fread_probe_program, cb_fread_compat_program;
void cb_test_fread(void)
{
    const struct cb_program_v1 *programs[] = {&cb_fread_probe_program, &cb_fread_compat_program};
    const char *commands[] = {"freadprobe", "freadcompat"};
    size_t i;
    for (i = 0; i < sizeof(programs)/sizeof(programs[0]); ++i) {
        struct cb_kernel *kernel = cb_kernel_create(cb_linux_host_ops());
        int status;
        if (kernel == NULL || cb_kernel_register(kernel, programs[i]) != 0) {
            fputs("FAIL: fread setup\n", stderr); exit(1);
        }
        cb_register_base_programs(kernel);
        if (cb_kernel_boot(kernel, commands[i]) != 0) {
            fputs("FAIL: fread boot\n", stderr); exit(1);
        }
        status = cb_kernel_run(kernel);
        cb_kernel_destroy(kernel);
        if (status != 0) {
            fprintf(stderr, "FAIL: %s status %d\n", commands[i], status); exit(1);
        }
    }
    puts("fread tests passed");
}
