#include "cannedbsd/libc.h"

int cb_mkdir_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_mkdir_program, "mkdir", cb_mkdir_main);
