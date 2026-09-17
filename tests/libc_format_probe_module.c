#include "cannedbsd/libc.h"

extern int cb_format_probe_main(int argc, char *argv[]);
CB_LIBC_PROGRAM(cb_format_probe_program, "formatprobe", cb_format_probe_main);
