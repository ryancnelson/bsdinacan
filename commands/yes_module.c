#include "cannedbsd/libc.h"

int cb_yes_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_yes_program, "yes", cb_yes_main);
