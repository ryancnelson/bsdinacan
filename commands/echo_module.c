#include "cannedbsd/libc.h"

int cb_netbsdecho_main(int argc, char *argv[]);

CB_LIBC_PROGRAM(cb_netbsdecho_program, "netbsdecho", cb_netbsdecho_main);
