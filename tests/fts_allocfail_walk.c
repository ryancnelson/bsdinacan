#include <fts.h>

/* Tolerant of allocation failure at any point: proves fts_open/fts_read/
   fts_close never crash and always leave things in a closeable state
   when an injected allocation failure lands inside them. The wrapper
   (ftsallocfailprobe_main in tests/test_core.c) drives the injection
   countdown and separately confirms no directory descriptor leaked. */
int cb_fts_allocfail_walk_main(int argc, char *argv[])
{
    char *roots[2];
    FTS *fts;
    (void)argc;
    (void)argv;

    roots[0] = "/tmp/ftsallocroot";
    roots[1] = NULL;
    fts = fts_open(roots, FTS_NOCHDIR, NULL);
    if (fts == NULL)
        return 0;
    fts_read(fts);
    if (fts_close(fts) != 0)
        return 1;
    return 0;
}
