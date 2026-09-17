#include <fts.h>

/* Early fts_close mid-walk, with several frames still open, must clean up
   symmetrically (invariant 5). The wrapper (ftscloseprobe_main in
   tests/test_core.c) verifies no directory descriptor leaked by exhausting
   CB_MAX_DIRS afterward -- this side just proves fts_close itself
   succeeds without ever completing the walk. The tree it walks is at
   least three directories deep so three reads leave open frames. */
int cb_fts_close_walk_main(int argc, char *argv[])
{
    char *roots[2];
    FTS *fts;
    int i;
    (void)argc;
    (void)argv;

    roots[0] = "/tmp/ftsclose";
    roots[1] = NULL;
    fts = fts_open(roots, FTS_NOCHDIR, NULL);
    if (fts == NULL)
        return 1;
    for (i = 0; i < 3; ++i) {
        if (fts_read(fts) == NULL) {
            fts_close(fts);
            return 2;
        }
    }
    if (fts_close(fts) != 0)
        return 3;
    return 0;
}
