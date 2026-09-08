#include "cannedbsd/libc.h"
extern int cb_fwrite_probe_main(int argc, char **argv);
CB_LIBC_PROGRAM(cb_fwrite_probe_program, "fwrite_probe", cb_fwrite_probe_main);
