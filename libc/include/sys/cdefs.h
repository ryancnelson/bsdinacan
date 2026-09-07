#ifndef CANNEDBSD_SYS_CDEFS_H
#define CANNEDBSD_SYS_CDEFS_H

#define __COPYRIGHT(message) \
    typedef char cb_copyright_notice[(sizeof(message) > 0) ? 1 : -1]
#define __RCSID(message) \
    typedef char cb_rcsid_notice[(sizeof(message) > 0) ? 1 : -1]

#endif
