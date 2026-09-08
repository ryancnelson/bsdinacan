#include "cannedbsd/libc.h"

extern int cb_basename_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_basename_program, "basename", cb_basename_main);
