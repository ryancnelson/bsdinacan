#include <sys/types.h>
#include <fts.h>
#include "ls.h"
#include "cannedbsd/ls_state.h"

/* Renamed and globalized from pinned ls.c by the build. */
extern void (*cb_ls_printfcn)(DISPLAY *);

void cb_ls_printfcn_slot(void **address, size_t *size)
{
    *address = &cb_ls_printfcn;
    *size = sizeof(cb_ls_printfcn);
}
