#include "cannedbsd/libc.h"

int cb_rm_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_rm_program, "rm", cb_rm_main);
