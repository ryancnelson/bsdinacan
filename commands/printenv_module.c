#include "cannedbsd/libc.h"

int cb_printenv_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_printenv_program, "printenv", cb_printenv_main);
