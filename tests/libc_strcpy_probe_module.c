#include "cannedbsd/libc.h"

extern int cb_strcpy_probe_main(int argc, char **argv);
CB_LIBC_PROGRAM(cb_strcpy_probe_program, "strcpyprobe", cb_strcpy_probe_main);
