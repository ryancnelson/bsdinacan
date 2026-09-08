/*
 * Altered derivative, not original DSC Sample Code.
 * Extracted/adapted from Jim Luther's IterateDirectory.c and
 * MoreFilesExtras.c (GetCatInfoNoName), CVS 1.1; see PROVENANCE.md.
 * Copyright 1995-1999 Jim Luther and Apple Computer, Inc. All rights reserved.
 * Copyright 1992-1999 Apple Computer, Inc. All rights reserved.
 *
 * You may incorporate this sample code into your applications without
 * restriction, though the sample code has been provided "AS IS" and the
 * responsibility for its operation is 100% yours. However, what you are
 * not permitted to do is to redistribute the source as "DSC Sample Code"
 * after having made changes. If you're going to re-distribute the source,
 * we require that you make it clear in the source that the code was
 * descended from Apple Sample Code, but that you've made changes.
 *
 * cannedBSD changes: explicit resolved identity; no recursive/path traversal;
 * copied records; bounded indices; callback cancellation; propagate every
 * native error except fnfErr at enumeration end, including access denial.
 */
#include "catalog.h"
#include <Errors.h>
#include <limits.h>
#include <string.h>

/* Pinned MacPerl Mac/Files/Files.pm kioFlAttribDir: 0x10; see PROVENANCE.md. */
enum { directory_attribute = 0x10 };

static OSErr query_catalog(CInfoPBPtr pb) { return PBGetCatInfoSync(pb); }

static OSErr copy_entry(const CInfoPBRec *pb, struct cb_catalog_entry *entry)
{
    const unsigned char *name = pb->hFileInfo.ioNamePtr;
    unsigned short length = name[0];
    if (length > 31) return paramErr;
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, name + 1, length);
    entry->directory = (pb->hFileInfo.ioFlAttrib & directory_attribute) != 0;
    if (entry->directory) {
        entry->id = pb->dirInfo.ioDrDirID;
        entry->parent_id = pb->dirInfo.ioDrParID;
    } else {
        entry->id = pb->hFileInfo.ioDirID;
        entry->parent_id = pb->hFileInfo.ioFlParID;
        entry->data_length = pb->hFileInfo.ioFlLgLen;
        entry->resource_length = pb->hFileInfo.ioFlRLgLen;
    }
    return noErr;
}

OSErr cb_catalog_scan_using(short volume, long directory, unsigned short limit,
                            cb_catalog_callback callback, void *data,
                            cb_catalog_query query)
{
    CInfoPBRec pb;
    Str63 name;
    struct cb_catalog_entry entry;
    unsigned short index;
    OSErr error;
    /* One additional query proves EOF without overflowing signed SDK index. */
    if (!volume || !callback || !query || !limit || limit >= SHRT_MAX || directory <= 0)
        return paramErr;
    for (index = 1; index <= limit + 1; ++index) {
        memset(&pb, 0, sizeof(pb));
        name[0] = 0;
        pb.dirInfo.ioNamePtr = name;
        pb.dirInfo.ioVRefNum = volume;
        pb.dirInfo.ioDrDirID = directory;
        pb.dirInfo.ioFDirIndex = (short)index;
        error = query(&pb);
        if (error == fnfErr) return noErr;
        if (error != noErr) return error;
        if (index > limit) return paramErr; /* explicit capacity exceeded */
        error = copy_entry(&pb, &entry);
        if (error != noErr) return error;
        if (callback(&entry, data)) return noErr;
    }
    return paramErr;
}

OSErr cb_catalog_scan(short volume, long directory, unsigned short limit,
                      cb_catalog_callback callback, void *data)
{
    struct cb_catalog_entry entry;
    OSErr error = cb_catalog_lookup(volume, directory, NULL, &entry);
    if (error != noErr) return error;
    if (!entry.directory) return dirNFErr;
    return cb_catalog_scan_using(volume, directory, limit, callback, data, query_catalog);
}

OSErr cb_catalog_lookup(short volume, long parent, ConstStr255Param name,
                        struct cb_catalog_entry *entry)
{
    CInfoPBRec pb;
    Str63 copied_name;
    OSErr error;
    if (!volume || !entry || parent <= 0 || (name && name[0] > 31)) return paramErr;
    memset(&pb, 0, sizeof(pb));
    copied_name[0] = name ? name[0] : 0;
    if (copied_name[0]) memcpy(copied_name + 1, name + 1, copied_name[0]);
    pb.dirInfo.ioNamePtr = copied_name;
    pb.dirInfo.ioVRefNum = volume;
    pb.dirInfo.ioDrDirID = parent;
    pb.dirInfo.ioFDirIndex = copied_name[0] ? 0 : -1;
    error = query_catalog(&pb);
    return error == noErr ? copy_entry(&pb, entry) : error;
}
