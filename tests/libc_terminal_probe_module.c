#include "cannedbsd/libc.h"
extern int cb_terminal_probe_main(int argc, char **argv);
CB_LIBC_PROGRAM(cb_terminal_probe_program, "libcterminalprobe",
                cb_terminal_probe_main);
