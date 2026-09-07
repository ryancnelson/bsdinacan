#include "internal.h"

#include <stdio.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s [-c command]\n", program);
}

int main(int argc, char **argv)
{
    struct cb_kernel *kernel;
    const char *command = NULL;
    int status;
    if (argc == 3 && strcmp(argv[1], "-c") == 0)
        command = argv[2];
    else if (argc != 1) {
        usage(argv[0]);
        return 2;
    }
    kernel = cb_kernel_create(cb_posix_host_ops());
    if (kernel == NULL) {
        fprintf(stderr, "cannedBSD: cannot create kernel\n");
        return 1;
    }
    cb_register_base_programs(kernel);
    if (cb_kernel_boot(kernel, command) < 0) {
        fprintf(stderr, "cannedBSD: cannot boot shell\n");
        cb_kernel_destroy(kernel);
        return 1;
    }
    status = cb_kernel_run(kernel);
    cb_kernel_destroy(kernel);
    return status;
}
