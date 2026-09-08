#ifndef CB_MAC_CATALOG_H
#define CB_MAC_CATALOG_H
#include <Files.h>

/* Owned C interface; no callback is handed to the Toolbox. */
struct cb_catalog_entry {
    char name[32];
    long id, parent_id, data_length, resource_length;
    Boolean directory;
};
typedef Boolean (*cb_catalog_callback)(const struct cb_catalog_entry *, void *);
typedef OSErr (*cb_catalog_query)(CInfoPBPtr);
/* Maximum one-directory entries, no recursion. Caller copies callback records. */
OSErr cb_catalog_scan(short volume, long directory, unsigned short limit,
                      cb_catalog_callback callback, void *data);
/* Explicit query seam for native-error/index-boundary tests using real SDK PBs. */
OSErr cb_catalog_scan_using(short volume, long directory, unsigned short limit,
                            cb_catalog_callback callback, void *data,
                            cb_catalog_query query);
OSErr cb_catalog_lookup(short volume, long parent, ConstStr255Param name,
                        struct cb_catalog_entry *entry);
#endif
