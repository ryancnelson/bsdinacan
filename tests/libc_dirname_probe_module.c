#include "cannedbsd/libc.h"

extern int cb_dirname_probe_main(int argc, char **argv);
CB_LIBC_PROGRAM(cb_dirname_probe_program, "libcdirnameprobe", cb_dirname_probe_main);
