#ifndef CANNEDBSD_STDIO_H
#define CANNEDBSD_STDIO_H

#include "cannedbsd/libc.h"

#define EOF (-1)

typedef struct cb_libc_file FILE;

int cb_libc_puts(const char *text);
int cb_libc_printf(const char *format, ...);
int cb_libc_fprintf(FILE *stream, const char *format, ...);

#define puts cb_libc_puts
#define printf cb_libc_printf
#define fprintf cb_libc_fprintf
#define stdout cb_libc_stdout_stream
#define stderr cb_libc_stderr_stream

#endif
