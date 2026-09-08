#include "cannedbsd/libc.h"

extern int cb_dirname_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_dirname_program, "dirname", cb_dirname_main);
