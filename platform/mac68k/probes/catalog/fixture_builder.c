/* SPDX-License-Identifier: GPL-2.0-or-later
 * Desktop initialization adapted from mkisofs/desktop.c 1.10:
 * Copyright (c) 1997, 1998, 1999, 2000 James Pearson
 * Copyright (c) 2000-2009 J. Schilling
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version. This program is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details. You should have received a copy
 * of the GNU General Public License along with this program; if not, see
 * https://www.gnu.org/licenses/old-licenses/gpl-2.0.html.
 * Explicit alterations and exact source pin are documented in PROVENANCE.md.
 *
 * Host-only fixture generator linked to the pinned image's GPL libhfs.
 * No part of this generator is linked into the Mac application.
 */
#include <hfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Freeze libhfs creation/modification times, not the compiler or guest clock. */
time_t time(time_t *out)
{
    time_t fixed = 1700000000;
    if (out) *out = fixed;
    return fixed;
}
static void check_at(int bad, int line)
{
    if (bad) { fprintf(stderr, "fixture line %d: %s\n", line, hfs_error); exit(1); }
}
#define check(bad) check_at((bad), __LINE__)
static void file(hfsvol *v, const char *name, const char *data, const char *resource)
{
    char path[96];
    hfsfile *f;
    snprintf(path, sizeof(path), "CatalogFixture:%s", name);
    f = hfs_create(v, path, "TEXT", "RUSE");
    check(!f);
    check(hfs_write(f, data, strlen(data)) != strlen(data));
    check(hfs_setfork(f, 1));
    check(hfs_write(f, resource, strlen(resource)) != strlen(resource));
    check(hfs_close(f));
}
/* Initial empty Desktop Manager files for protected HFS media. All multibyte
 * values are explicit on-disk big endian bytes, not fabricated Toolbox types. */
static void put16(unsigned char *bytes, unsigned int offset, unsigned int value)
{
    bytes[offset] = (unsigned char)(value >> 8);
    bytes[offset + 1] = (unsigned char)value;
}
static void put32(unsigned char *bytes, unsigned int offset, unsigned long value)
{
    put16(bytes, offset, (unsigned int)(value >> 16));
    put16(bytes, offset + 2, (unsigned int)value);
}
static void desktop_file(hfsvol *v, const char *name, const char *type,
                         const unsigned char *bytes, unsigned long size)
{
    hfsdirent entry;
    hfsfile *f = hfs_create(v, name, type, "DMGR");
    check(!f);
    if (size) check(hfs_write(f, bytes, size) != size);
    check(hfs_fstat(f, &entry));
    entry.fdflags |= HFS_FNDR_ISINVISIBLE;
    check(hfs_fsetattr(f, &entry));
    check(hfs_close(f));
}
static void desktop(hfsvol *v)
{
    hfsvolent volume;
    unsigned long blocks, size;
    unsigned char *bytes;
    check(hfs_vstat(v, &volume));
    size = volume.clumpsz;
    check(size < 512 || size > 1024 * 1024 || size % 512);
    blocks = size / 512;
    bytes = calloc(1, size);
    check(!bytes);
    put16(bytes, 8, 0x100); put16(bytes, 10, 3);
    put16(bytes, 32, 0x200); put16(bytes, 34, 0x25);
    put32(bytes, 36, blocks); put32(bytes, 40, blocks - 1);
    put32(bytes, 46, size); put16(bytes, 50, 0xff);
    put16(bytes, 120, 0x20a); put16(bytes, 122, 0x100);
    put16(bytes, 248, 0x8000);
    put32(bytes, 504, 0x1f800f8); put32(bytes, 508, 0x78000e);
    desktop_file(v, "CatalogFixture:Desktop DB", "BTFL", bytes, size);
    desktop_file(v, "CatalogFixture:Desktop DF", "DTFL", NULL, 0);
    free(bytes);
}
int main(int argc, char **argv)
{
    static const char *names[] = {"Empty", "Subdir", "Subdir:Sentinel", "Zero",
        "Eight", "Forked", "1234567890123456789012345678901", "Desktop DB", "Desktop DF"};
    hfsvol *v;
    hfsdirent e;
    FILE *image;
    unsigned int i;
    if (argc != 2) return 2;
    image = fopen(argv[1], "wbx");
    if (!image) return 3;
    if (fseek(image, 2 * 1024 * 1024 - 1, SEEK_SET) || fputc(0, image) == EOF || fclose(image))
        return 4;
    check(hfs_format(argv[1], 0, HFS_OPT_ZERO, "CatalogFixture", 0, NULL));
    v = hfs_mount(argv[1], 0, HFS_MODE_RDWR);
    check(!v);
    check(hfs_mkdir(v, "Empty"));
    check(hfs_mkdir(v, "Subdir"));
    file(v, "Subdir:Sentinel", "nested\n", "");
    file(v, "Zero", "", "");
    file(v, "Eight", "fixture\n", "");
    file(v, "Forked", "fork-data\n", "resource-fixture\n");
    file(v, "1234567890123456789012345678901", "long\n", "");
    desktop(v);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char path[96];
        snprintf(path, sizeof(path), "CatalogFixture:%s", names[i]);
        check(hfs_stat(v, path, &e));
        printf("%s\t%lu\t%lu\t%d\t%lu\t%lu\t%u\t%s\t%s\n", names[i], e.cnid, e.parid,
               !!(e.flags & HFS_ISDIR), (e.flags & HFS_ISDIR) ? 0 : e.u.file.dsize,
               (e.flags & HFS_ISDIR) ? 0 : e.u.file.rsize, (unsigned short)e.fdflags,
               (e.flags & HFS_ISDIR) ? "-" : e.u.file.type,
               (e.flags & HFS_ISDIR) ? "-" : e.u.file.creator);
    }
    check(hfs_umount(v));
    return 0;
}
