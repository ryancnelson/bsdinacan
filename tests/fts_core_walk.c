#include <fts.h>
#include <string.h>

/* FTS-CORE-01 core mechanics: multi-root support (invariant 1), fts_accpath
   == fts_path under FTS_NOCHDIR (invariant 2), correct pre/post-order and
   level numbering, plus fts_open's argument validation (empty path_argv,
   FTS_XDEV rejected at runtime). Tree built by the caller before invoking
   this via cb_libc_start; see ftscoreprobe_main in tests/test_core.c. */
int cb_fts_core_walk_main(int argc, char *argv[])
{
    char *bad_argv[1];
    char *roots[3];
    FTS *fts;
    FTSENT *ent;
    int seen_a_pre = 0, seen_a_post = 0;
    int seen_d1_pre = 0, seen_d1_post = 0;
    int seen_d2_pre = 0, seen_d2_post = 0;
    int seen_f1 = 0, seen_f2 = 0, seen_f3 = 0;
    int seen_b_pre = 0, seen_b_post = 0, seen_f4 = 0;
    int order = 0;
    int a_pre_order = -1, d1_pre_order = -1, d2_pre_order = -1, f3_order = -1;
    int a_post_order = -1, d1_post_order = -1, d2_post_order = -1;
    (void)argc;
    (void)argv;

    bad_argv[0] = NULL;
    if (fts_open(bad_argv, FTS_NOCHDIR, NULL) != NULL)
        return 1;

    roots[0] = "/tmp/ftsA";
    roots[1] = NULL;
    if (fts_open(roots, FTS_NOCHDIR | FTS_XDEV, NULL) != NULL)
        return 2;

    roots[0] = "/tmp/ftsA";
    roots[1] = "/tmp/ftsB";
    roots[2] = NULL;
    fts = fts_open(roots, FTS_NOCHDIR, NULL);
    if (fts == NULL)
        return 3;

    while ((ent = fts_read(fts)) != NULL) {
        if (ent->fts_accpath != ent->fts_path)
            return 4;
        if (strcmp(ent->fts_path, "/tmp/ftsA") == 0) {
            if (ent->fts_info == FTS_D) {
                seen_a_pre = 1;
                a_pre_order = order;
                if (ent->fts_level != 0)
                    return 5;
            } else if (ent->fts_info == FTS_DP) {
                seen_a_post = 1;
                a_post_order = order;
            } else {
                return 6;
            }
        } else if (strcmp(ent->fts_path, "/tmp/ftsA/f1") == 0) {
            if (ent->fts_info != FTS_DEFAULT || ent->fts_level != 1)
                return 7;
            seen_f1 = 1;
        } else if (strcmp(ent->fts_path, "/tmp/ftsA/d1") == 0) {
            if (ent->fts_info == FTS_D) {
                seen_d1_pre = 1;
                d1_pre_order = order;
                if (ent->fts_level != 1)
                    return 8;
            } else if (ent->fts_info == FTS_DP) {
                seen_d1_post = 1;
                d1_post_order = order;
            } else {
                return 9;
            }
        } else if (strcmp(ent->fts_path, "/tmp/ftsA/d1/f2") == 0) {
            if (ent->fts_info != FTS_DEFAULT || ent->fts_level != 2)
                return 10;
            seen_f2 = 1;
        } else if (strcmp(ent->fts_path, "/tmp/ftsA/d1/d2") == 0) {
            if (ent->fts_info == FTS_D) {
                seen_d2_pre = 1;
                d2_pre_order = order;
                if (ent->fts_level != 2)
                    return 11;
            } else if (ent->fts_info == FTS_DP) {
                seen_d2_post = 1;
                d2_post_order = order;
            } else {
                return 12;
            }
        } else if (strcmp(ent->fts_path, "/tmp/ftsA/d1/d2/f3") == 0) {
            if (ent->fts_info != FTS_DEFAULT || ent->fts_level != 3)
                return 13;
            seen_f3 = 1;
            f3_order = order;
        } else if (strcmp(ent->fts_path, "/tmp/ftsB") == 0) {
            if (ent->fts_info == FTS_D) {
                seen_b_pre = 1;
                if (ent->fts_level != 0)
                    return 14;
            } else if (ent->fts_info == FTS_DP) {
                seen_b_post = 1;
            } else {
                return 15;
            }
        } else if (strcmp(ent->fts_path, "/tmp/ftsB/f4") == 0) {
            if (ent->fts_info != FTS_DEFAULT || ent->fts_level != 1)
                return 16;
            seen_f4 = 1;
        } else {
            return 17;
        }
        order++;
    }
    if (fts_close(fts) != 0)
        return 18;

    if (!seen_a_pre || !seen_a_post || !seen_d1_pre || !seen_d1_post ||
        !seen_d2_pre || !seen_d2_post || !seen_f1 || !seen_f2 || !seen_f3 ||
        !seen_b_pre || !seen_b_post || !seen_f4)
        return 19;

    if (!(a_pre_order < d1_pre_order && d1_pre_order < d2_pre_order &&
          d2_pre_order < f3_order && f3_order < d2_post_order &&
          d2_post_order < d1_post_order && d1_post_order < a_post_order))
        return 20;

    return 0;
}
