#include "cannedbsd/libc.h"

extern int cb_err_probe_main(int argc, char **argv);
CB_LIBC_PROGRAM(cb_err_probe_program, "libcerrprobe", cb_err_probe_main);
