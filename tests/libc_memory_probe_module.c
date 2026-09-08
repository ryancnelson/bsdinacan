#include "cannedbsd/libc.h"

extern int cb_memory_probe_main(int argc, char **argv);
CB_LIBC_PROGRAM(cb_memory_probe_program, "libcmemoryprobe", cb_memory_probe_main);
