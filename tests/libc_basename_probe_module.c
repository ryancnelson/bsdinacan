#include "cannedbsd/libc.h"

extern int cb_basename_probe_main(int argc, char **argv);
CB_LIBC_PROGRAM(cb_basename_probe_program, "libcbasenameprobe", cb_basename_probe_main);
