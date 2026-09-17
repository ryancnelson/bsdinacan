#ifndef CANNEDBSD_NETBSD_SYS_FILE_H
#define CANNEDBSD_NETBSD_SYS_FILE_H

#include <fcntl.h>

#define setlocale(cat, loc) \
    ((void)(tlinect = twordct = tcharct = tlongest = 0, \
            doline = doword = dobyte = dochar = dolongest = false, \
            rval = 0), \
     cb_libc_setlocale(cat, loc))

#endif
