#include "cannedbsd/libc.h"

extern int cb_strtoimax_probe_main(int argc, char **argv);
CB_LIBC_PROGRAM(cb_strtoimax_probe_program, "libcstrtoimaxprobe", cb_strtoimax_probe_main);
