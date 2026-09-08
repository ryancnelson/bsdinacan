/* SPDX-License-Identifier: GPL-2.0-or-later
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
int main(int argc, char **argv)
{
    static const char *names[] = {"Empty", "Subdir", "Subdir:Sentinel", "Zero",
        "Eight", "Forked", "1234567890123456789012345678901"};
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
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char path[96];
        snprintf(path, sizeof(path), "CatalogFixture:%s", names[i]);
        check(hfs_stat(v, path, &e));
        printf("%s\t%lu\t%lu\t%d\t%lu\t%lu\n", names[i], e.cnid, e.parid,
               !!(e.flags & HFS_ISDIR), (e.flags & HFS_ISDIR) ? 0 : e.u.file.dsize,
               (e.flags & HFS_ISDIR) ? 0 : e.u.file.rsize);
    }
    check(hfs_umount(v));
    return 0;
}
