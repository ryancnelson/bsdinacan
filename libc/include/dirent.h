#ifndef CANNEDBSD_DIRENT_H
#define CANNEDBSD_DIRENT_H

#include "cannedbsd/libc.h"
#include "sys/types.h"

/* struct dirent and DT_* are defined in cannedbsd/libc.h, not here -- see
   the comment there for why (cb_libc.c needs to write struct dirent's
   fields and compare against DT_*, and this header is not on that
   translation unit's include path). d_ino is uint64_t there; ino_t (this
   header, via sys/types.h) is the same type, so `ino_t x =
   entry->d_ino;` is unproblematic without redeclaring the struct. */

typedef struct cb_libc_dir DIR;

DIR *cb_libc_opendir(const char *path);
struct dirent *cb_libc_readdir(DIR *dirp);
int cb_libc_closedir(DIR *dirp);

#define opendir cb_libc_opendir
#define readdir cb_libc_readdir
#define closedir cb_libc_closedir

#endif
