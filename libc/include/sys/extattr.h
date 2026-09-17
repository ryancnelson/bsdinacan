#ifndef CANNEDBSD_SYS_EXTATTR_H
#define CANNEDBSD_SYS_EXTATTR_H

#include "cannedbsd/libc.h"

int cb_libc_fcpxattr(int from_descriptor, int to_descriptor);

#define fcpxattr cb_libc_fcpxattr

#endif
