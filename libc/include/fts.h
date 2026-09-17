#ifndef CANNEDBSD_FTS_H
#define CANNEDBSD_FTS_H

/*
 * cannedBSD's fts(3): FTS-CORE-01, extended by FTS-CHILDREN-01.
 *
 * This is not a port of BSD fts(3). It is the minimal subset derived in
 * notes/iterations/FTS-CORE-01.md by grepping the pinned NetBSD b890038f
 * consumer sources (bin/rm/rm.c, bin/cp/cp.c, bin/cp/utils.c, and for
 * FTS-CHILDREN-01, bin/ls/ls.c/print.c/cmp.c/util.c) for every
 * fts_/FTS_/FTSENT reference they actually make. Values/fields no pinned
 * consumer references (FTS_F, FTS_SL, FTS_SLNONE, FTS_DEFAULT's real-BSD
 * split, FTS_DOT, FTS_INIT, FTS_NSOK, fts_cycle) are deliberately absent.
 *
 * fts_children/fts_link/fts_parent and FTS_SEEDOT's "."/".." synthesis
 * are FTS-CHILDREN-01's addition, ls-only per the same grep.
 */

#include "cannedbsd/libc.h"
#include <stddef.h>

/* fts_info values. Ordinary/whiteout/directory-cycle entries all reuse the
   same struct; entries never carry a fts_info value not listed here. */
#define FTS_D       1  /* preorder directory */
#define FTS_DP      2  /* postorder directory */
#define FTS_DNR     3  /* directory that could not be read (opendir failed) */
#define FTS_ERR     4  /* error, other than DNR/NS; fts_errno set */
#define FTS_NS      5  /* stat(2) failed; fts_errno set, fts_statp unusable */
#define FTS_DC      6  /* directory causes a cycle in the tree */
#define FTS_W       7  /* whiteout entry; never produced by RAMFS, exists
                           only so rm.c's unguarded FTS_WHITEOUT reference
                           compiles */
#define FTS_DEFAULT 8  /* any other visited entry (regular file, etc.) --
                           real BSD splits this into FTS_F/FTS_SL/etc; no
                           pinned consumer switches on those, so cannedBSD
                           does not distinguish them (design note S3) */

/* fts_open() options. */
#define FTS_PHYSICAL  0x0001
#define FTS_LOGICAL   0x0002
#define FTS_NOCHDIR   0x0004  /* accepted; the only mode this fts ever
                                  implements -- there is no host chdir
                                  concept for this to be a compromise
                                  against. fts_open() ignores its absence
                                  since there is no other mode to fall
                                  back to. */
#define FTS_COMFOLLOW 0x0008
#define FTS_XDEV      0x0010  /* accepted at compile time; fts_open()
                                  rejects it at runtime with ENOSYS until
                                  FTS-XDEV-01 adds mount-crossing
                                  detection */
#define FTS_NOSTAT    0x0020
#define FTS_SEEDOT    0x0040  /* fts_children() also synthesizes "." and
                                  ".." entries for a directory's children
                                  (never for the top-level path_argv
                                  preview, matching real fts(3): those
                                  are never directory entries themselves) */
#define FTS_WHITEOUT  0x0080  /* accepted; RAMFS has no whiteout concept,
                                  so no entry is ever tagged FTS_W */

/* fts_set() instructions. */
#define FTS_SKIP 1

/* fts_level constants. */
#define FTS_ROOTLEVEL 0

typedef struct cb_fts FTS;

typedef struct cb_ftsent {
    unsigned short fts_info;
    int fts_level;
    long fts_number;           /* caller-owned scratch, never touched */
    void *fts_pointer;         /* caller-owned scratch, never touched */
    char *fts_path;            /* NUL-terminated, from one of path_argv */
    size_t fts_pathlen;
    char *fts_accpath;         /* == fts_path; FTS_NOCHDIR is the only mode */
    char *fts_name;            /* last component of fts_path */
    size_t fts_namelen;
    int fts_errno;             /* set when fts_info is DNR/ERR/NS */
    struct stat *fts_statp;
    /* FTS-CHILDREN-01. fts_link: next sibling in a fts_children() list
       (NULL for entries reached via ordinary fts_read(), which are not
       part of any such list). fts_parent: the FTS_D entry whose children
       list this entry came from (NULL for path_argv-level entries, which
       have no FTSENT parent). */
    struct cb_ftsent *fts_link;
    struct cb_ftsent *fts_parent;
} FTSENT;

FTS *cb_libc_fts_open(char *const *path_argv, int options,
                      int (*compar)(const FTSENT **, const FTSENT **));
FTSENT *cb_libc_fts_read(FTS *ftsp);
int cb_libc_fts_close(FTS *ftsp);
int cb_libc_fts_set(FTS *ftsp, FTSENT *entry, int instr);
FTSENT *cb_libc_fts_children(FTS *ftsp, int options);

#define fts_open cb_libc_fts_open
#define fts_read cb_libc_fts_read
#define fts_close cb_libc_fts_close
#define fts_set cb_libc_fts_set
#define fts_children cb_libc_fts_children

/* FTS-CHILDREN-01: accepted at compile time (ls.c:439's ch_options
   computation references it); fts_children() itself always stats every
   entry regardless, so this does not change behavior -- see the design
   note on why: separating "just names" from "names+stat" would be a
   second, unmeasured code path for a cost (skipping a stat RAMFS already
   does cheaply in memory) real fts_children() exists to avoid on
   disk-backed filesystems, not this one. */
#define FTS_NAMEONLY 0x0100

#endif
