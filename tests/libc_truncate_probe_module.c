#include "cannedbsd/libc.h"

extern int cb_truncate_probe_main(int argc, char **argv);
CB_LIBC_PROGRAM(cb_truncate_probe_program, "libctruncateprobe",
                cb_truncate_probe_main);
