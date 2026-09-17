#include "cannedbsd/libc.h"

int cb_ls_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_ls_program, "ls", cb_ls_main);
