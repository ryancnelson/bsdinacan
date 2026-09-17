#include "cannedbsd/libc.h"

extern int cb_cp_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_cp_program, "cp", cb_cp_main);
