#include "cannedbsd/libc.h"

extern int cb_mv_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_mv_program, "mv", cb_mv_main);
