#include "cannedbsd/libc.h"

int cb_echo_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_echo_program, "echo", cb_echo_main);
