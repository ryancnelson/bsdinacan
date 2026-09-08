#ifndef CANNEDBSD_TERMIOS_H
#define CANNEDBSD_TERMIOS_H

#include "cannedbsd/libc.h"

/* Attribute control is explicitly unsupported until a raw adapter exists.
 * These request names permit ordinary callers to detect that limitation. */
#define termios cb_termios_v1
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define tcgetattr cb_libc_tcgetattr
#define tcsetattr cb_libc_tcsetattr

#endif
