#include "cannedbsd/libc.h"

extern int normalpollprobe_main(int argc, char **argv);
CB_LIBC_PROGRAM(cb_poll_probe_program, "normalpollprobe", normalpollprobe_main);
