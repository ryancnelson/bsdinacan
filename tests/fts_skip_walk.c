#include <fts.h>
#include <string.h>

/* FTS_SKIP must suppress both subtree descent AND the post-order FTS_DP
   visit for the skipped directory (invariant 3). Tree built by the
   caller; see ftsskipprobe_main in tests/test_core.c. */
int cb_fts_skip_walk_main(int argc, char *argv[])
{
    char *roots[2];
    FTS *fts;
    FTSENT *ent;
    int seen_keep_pre = 0, seen_keep_post = 0, seen_k1 = 0;
    int seen_skip_pre = 0, seen_skip_post = 0, seen_s1 = 0;
    (void)argc;
    (void)argv;

    roots[0] = "/tmp/ftsskip";
    roots[1] = NULL;
    fts = fts_open(roots, FTS_NOCHDIR, NULL);
    if (fts == NULL)
        return 1;
    while ((ent = fts_read(fts)) != NULL) {
        if (strcmp(ent->fts_path, "/tmp/ftsskip/skip") == 0 &&
            ent->fts_info == FTS_D) {
            seen_skip_pre = 1;
            if (fts_set(fts, ent, FTS_SKIP) != 0) {
                fts_close(fts);
                return 2;
            }
            continue;
        }
        if (strcmp(ent->fts_path, "/tmp/ftsskip/skip") == 0 &&
            ent->fts_info == FTS_DP)
            seen_skip_post = 1;
        else if (strcmp(ent->fts_path, "/tmp/ftsskip/skip/s1") == 0)
            seen_s1 = 1;
        else if (strcmp(ent->fts_path, "/tmp/ftsskip/keep") == 0) {
            if (ent->fts_info == FTS_D)
                seen_keep_pre = 1;
            else if (ent->fts_info == FTS_DP)
                seen_keep_post = 1;
        } else if (strcmp(ent->fts_path, "/tmp/ftsskip/keep/k1") == 0) {
            seen_k1 = 1;
        } else if (strcmp(ent->fts_path, "/tmp/ftsskip") == 0) {
            /* root entry itself */
        } else {
            fts_close(fts);
            return 3;
        }
    }
    if (fts_close(fts) != 0)
        return 4;
    if (!seen_skip_pre || seen_skip_post || seen_s1)
        return 5;
    if (!seen_keep_pre || !seen_keep_post || !seen_k1)
        return 6;
    return 0;
}
