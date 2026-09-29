#ifndef CANNEDBSD_STDIO_H
#define CANNEDBSD_STDIO_H

#include "cannedbsd/libc.h"
#include "unistd.h"

#define EOF (-1)
#define BUFSIZ 1024

typedef struct cb_libc_file FILE;

FILE *cb_libc_fopen(const char *path, const char *mode);
int cb_libc_fclose(FILE *stream);
size_t cb_libc_fread(void *buffer, size_t size, size_t count, FILE *stream);
int cb_libc_getc(FILE *stream);
int cb_libc_feof(FILE *stream);
int cb_libc_puts(const char *text);
int cb_libc_putchar(int character);
int cb_libc_fflush(FILE *stream);
int cb_libc_ferror(FILE *stream);
void cb_libc_clearerr(FILE *stream);
int cb_libc_fileno(FILE *stream);
void cb_libc_setbuf(FILE *stream, char *buf);
int cb_libc_printf(const char *format, ...);
int cb_libc_fprintf(FILE *stream, const char *format, ...);
int cb_libc_snprintf(char *buffer, size_t size, const char *format, ...);
size_t cb_libc_fwrite(const void *buffer, size_t size, size_t count, FILE *stream);

int cb_libc_getchar(void);
int cb_libc_rename(const char *oldpath, const char *newpath);

#define fopen cb_libc_fopen
#define fclose cb_libc_fclose
#define fread cb_libc_fread
#define getc cb_libc_getc
#define getchar cb_libc_getchar
#define feof cb_libc_feof
#define clearerr cb_libc_clearerr
#define fileno cb_libc_fileno
#define setbuf cb_libc_setbuf
#define stdin cb_libc_stdin_stream
#define puts cb_libc_puts
#define putchar cb_libc_putchar
#define fflush cb_libc_fflush
#define ferror cb_libc_ferror
#define printf cb_libc_printf
#define fprintf cb_libc_fprintf
#define snprintf cb_libc_snprintf
#define fwrite cb_libc_fwrite
#define rename cb_libc_rename
#define stdout cb_libc_stdout_stream
#define stderr cb_libc_stderr_stream

#endif
