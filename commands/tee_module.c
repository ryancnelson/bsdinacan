#include "cannedbsd/libc.h"

int cb_tee_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_tee_program, "tee", cb_tee_main);
