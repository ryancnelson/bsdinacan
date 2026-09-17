#include "cannedbsd/libc.h"

extern int cb_fclose_stdout_probe_main(int argc, char *argv[]);
CB_LIBC_PROGRAM(cb_fclose_stdout_probe_program, "fclosestdoutprobe",
                cb_fclose_stdout_probe_main);
