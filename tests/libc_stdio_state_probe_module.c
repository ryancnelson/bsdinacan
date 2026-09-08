#include "cannedbsd/libc.h"
extern int cb_stdio_state_probe_main(int argc, char **argv);
CB_LIBC_PROGRAM(cb_stdio_state_probe_program, "libcstdiostateprobe", cb_stdio_state_probe_main);
