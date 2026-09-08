#include "cannedbsd/libc.h"

int cb_exitprobe_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_exitprobe_program, "libcexitprobe", cb_exitprobe_main);
