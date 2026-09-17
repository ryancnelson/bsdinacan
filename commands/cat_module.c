#include "cannedbsd/libc.h"

int cb_cat_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_cat_program, "cat", cb_cat_main);
