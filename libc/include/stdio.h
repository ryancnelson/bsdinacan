#ifndef CANNEDBSD_STDIO_H
#define CANNEDBSD_STDIO_H

#include "cannedbsd/libc.h"

#define EOF (-1)

typedef struct cb_libc_file FILE;

FILE *cb_libc_fopen(const char *path, const char *mode);
int cb_libc_fclose(FILE *stream);
int cb_libc_getc(FILE *stream);
int cb_libc_feof(FILE *stream);
int cb_libc_puts(const char *text);
int cb_libc_putchar(int character);
int cb_libc_fflush(FILE *stream);
int cb_libc_ferror(FILE *stream);
int cb_libc_printf(const char *format, ...);
int cb_libc_fprintf(FILE *stream, const char *format, ...);

#define fopen cb_libc_fopen
#define fclose cb_libc_fclose
#define getc cb_libc_getc
#define feof cb_libc_feof
#define stdin cb_libc_stdin_stream
#define puts cb_libc_puts
#define putchar cb_libc_putchar
#define fflush cb_libc_fflush
#define ferror cb_libc_ferror
#define printf cb_libc_printf
#define fprintf cb_libc_fprintf
#define stdout cb_libc_stdout_stream
#define stderr cb_libc_stderr_stream

#endif
