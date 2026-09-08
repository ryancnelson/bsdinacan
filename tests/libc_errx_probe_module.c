#include "cannedbsd/libc.h"

int cb_errxprobe_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_errxprobe_program, "libcerrxprobe", cb_errxprobe_main);
