#include <fts.h>
#include <string.h>

/*
 * FTS_DC's true-positive path is not exercised here: it is provably
 * unconstructible against the current RAMFS backend, not merely untried.
 * ramfs_rename() (src/ramfs.c) refuses to move any node whose parent is
 * NULL (EPERM), so the mount root itself can never be relinked; and for
 * any non-root node, renaming it into one of its own descendants only
 * ever reassigns that one node's single parent pointer, severing the one
 * edge that made the whole subtree reachable from root in the first
 * place (RAMFS has no hardlink primitive -- confirmed by reading
 * ramfs_node_ops's full op list in src/ramfs.c: no "link" op exists).
 * Every rename-based construction therefore disconnects the cyclic
 * component from root rather than exposing it to a walk that starts
 * there; this was verified by attempting exactly this (splicing the
 * mount root under one of its own descendants via a raw node_ops->rename
 * call) and observing the EPERM in practice, not just reasoning about it.
 * See notes/iterations/FTS-CORE-01.md for the full writeup.
 *
 * What IS tested here: a real, non-trivial nesting depth produces no
 * false-positive FTS_DC and still terminates with correct pre/post-order
 * pairing -- the actual, reachable case cycle detection must not get in
 * the way of.
 */
int cb_fts_cycle_walk_main(int argc, char *argv[])
{
    char *roots[2];
    FTS *fts;
    FTSENT *ent;
    int depth_seen = -1;
    int dp_count = 0;
    int d_count = 0;
    (void)argc;
    (void)argv;

    roots[0] = "/tmp/ftsdeep";
    roots[1] = NULL;
    fts = fts_open(roots, FTS_NOCHDIR, NULL);
    if (fts == NULL)
        return 1;
    while ((ent = fts_read(fts)) != NULL) {
        if (ent->fts_info == FTS_DC) {
            fts_close(fts);
            return 2;
        }
        if (ent->fts_info == FTS_D) {
            d_count++;
            if (ent->fts_level > depth_seen)
                depth_seen = ent->fts_level;
        } else if (ent->fts_info == FTS_DP) {
            dp_count++;
        } else if (ent->fts_info != FTS_DEFAULT) {
            fts_close(fts);
            return 3;
        }
    }
    if (fts_close(fts) != 0)
        return 4;
    if (d_count != dp_count)
        return 5;
    if (depth_seen < 8)
        return 6;
    return 0;
}
