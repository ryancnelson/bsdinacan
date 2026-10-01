#ifndef CANNEDBSD_LS_STATE_H
#define CANNEDBSD_LS_STATE_H

#include <stddef.h>

/* Command-side bridge for the pinned anonymous DISPLAY callback type. */
void cb_ls_printfcn_slot(void **address, size_t *size);

#endif
