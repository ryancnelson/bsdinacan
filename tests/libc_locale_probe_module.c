#include "cannedbsd/libc.h"
extern int cb_locale_probe_main(int argc, char **argv);
extern int locale_environment_main(int argc, char **argv);
CB_LIBC_PROGRAM(cb_locale_probe_program, "libclocaleprobe", cb_locale_probe_main);
CB_LIBC_PROGRAM(cb_locale_env_probe_program, "libclocaleenv", locale_environment_main);
