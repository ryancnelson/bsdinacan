#include "cannedbsd/libc.h"

extern int cb_warnx_probe_main(int argc, char *argv[]);
CB_LIBC_PROGRAM(cb_warnx_probe_program, "warnxprobe", cb_warnx_probe_main);
