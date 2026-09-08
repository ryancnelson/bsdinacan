#include "cannedbsd/libc.h"

int cb_getoptprobe_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_getoptprobe_program, "libcgetoptprobe", cb_getoptprobe_main);
