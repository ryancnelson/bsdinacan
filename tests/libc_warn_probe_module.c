#include "cannedbsd/libc.h"

extern int cb_warn_probe_main(int argc, char *argv[]);
CB_LIBC_PROGRAM(cb_warn_probe_program, "warnprobe", cb_warn_probe_main);
