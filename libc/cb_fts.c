/*
 * FTS-CORE-01: fts_open/fts_read/fts_close/fts_set(FTS_SKIP).
 *
 * Built entirely on the already-public cb_libc_opendir/readdir/closedir,
 * cb_libc_stat, and cb_libc_malloc/calloc/free -- no new kernel ABI, no
 * cb_stat_v1 change (see notes/iterations/FTS-01-design.md and its
 * independent review). This file lives outside src/ and therefore has no
 * access to struct cb_vfs_node or bound_api (private to cb_libc.c's own
 * translation unit); every operation here goes through the public surface.
 *
 * Cycle detection deviates from the design note's literal phrasing
 * ("live struct cb_vfs_node* pointer identity... cb_vfs_node_retain/
 * release") for exactly that reason: this file cannot see a
 * cb_vfs_node* at all. It compares directory inode numbers instead,
 * obtained via the same public stat() surface. The design note itself
 * (and the independent review, S3.A) already establish that RAMFS inode
 * numbers are drawn from a single per-kernel counter and are therefore
 * globally unique for the kernel's lifetime -- the same stability
 * guarantee pointer identity would have provided, reached through the
 * public API instead of privileged internal access. Because the values
 * held here are copied integers, not pointers into the VFS, there is
 * also nothing to retain/release: an inode number cannot dangle the way
 * a stale node pointer could.
 */

#include "cannedbsd/libc.h"
#include "sys/stat.h"
#include "fts.h"

#include <errno.h>

#define CB_FTS_MAX_DEPTH (CB_PATH_MAX / 2)

struct cb_fts_frame {
    struct cb_libc_dir *dir;
    uint64_t inode;
    FTSENT *entry;   /* the FTS_D entry that opened this frame; reused,
                        info flipped to FTS_DP, when the frame pops */
    int skip;        /* fts_set(FTS_SKIP) called on `entry` */
};

struct cb_fts {
    char *const *path_argv;
    int root_index;
    int options;
    int (*compar)(const FTSENT **, const FTSENT **);  /* accepted, unused:
        no pinned rm/cp call site ever passes a non-NULL comparator (design
        note); sorting is ls's concern, which is FTS-CHILDREN-01's scope. */
    struct cb_fts_frame stack[CB_FTS_MAX_DEPTH];
    int depth;
    FTSENT *pending_free;  /* previous entry not owned by a frame; real
                               fts's contract is that only one FTSENT is
                               valid across a fts_read() call, so this is
                               freed at the top of the next call rather
                               than immediately -- callers may still be
                               holding/reading the pointer they were just
                               handed until they ask for the next one. */
};

static FTSENT *entry_create(const char *parent_path, const char *name,
                            int level)
{
    FTSENT *entry;
    size_t parent_len;
    size_t name_len;
    size_t i;
    const char *slash;

    entry = cb_libc_calloc(1, sizeof(*entry));
    if (entry == NULL)
        return NULL;
    name_len = cb_libc_strlen(name);
    if (parent_path == NULL) {
        entry->fts_pathlen = name_len;
        entry->fts_path = cb_libc_malloc(name_len + 1);
        if (entry->fts_path == NULL) {
            cb_libc_free(entry);
            return NULL;
        }
        cb_libc_memcpy(entry->fts_path, name, name_len + 1);
    } else {
        parent_len = cb_libc_strlen(parent_path);
        entry->fts_pathlen = parent_len + 1 + name_len;
        entry->fts_path = cb_libc_malloc(entry->fts_pathlen + 1);
        if (entry->fts_path == NULL) {
            cb_libc_free(entry);
            return NULL;
        }
        cb_libc_memcpy(entry->fts_path, parent_path, parent_len);
        entry->fts_path[parent_len] = '/';
        cb_libc_memcpy(entry->fts_path + parent_len + 1, name, name_len + 1);
    }
    /* FTS_NOCHDIR is the only mode this fts implements: fts_accpath is
       always identical to fts_path (invariant 2), by construction, not by
       a runtime branch that could diverge. */
    entry->fts_accpath = entry->fts_path;
    slash = NULL;
    for (i = 0; i < entry->fts_pathlen; ++i)
        if (entry->fts_path[i] == '/')
            slash = &entry->fts_path[i];
    entry->fts_name = slash != NULL ? (char *)slash + 1 : entry->fts_path;
    entry->fts_namelen = cb_libc_strlen(entry->fts_name);
    entry->fts_level = level;
    entry->fts_statp = cb_libc_calloc(1, sizeof(*entry->fts_statp));
    if (entry->fts_statp == NULL) {
        cb_libc_free(entry->fts_path);
        cb_libc_free(entry);
        return NULL;
    }
    return entry;
}

static void entry_destroy(FTSENT *entry)
{
    if (entry == NULL)
        return;
    cb_libc_free(entry->fts_statp);
    cb_libc_free(entry->fts_path);
    cb_libc_free(entry);
}

static int cycle_detected(struct cb_fts *fts, uint64_t inode)
{
    int i;
    for (i = 0; i < fts->depth; ++i)
        if (fts->stack[i].inode == inode)
            return 1;
    return 0;
}

/* Classifies a freshly-created entry (stats it, and for directories,
   attempts to open/descend or detects DNR/DC), pushing a new frame onto
   the stack when it becomes an FTS_D. Never returns NULL -- a stat or
   opendir failure is reported as an FTSENT (NS/DNR), matching real fts's
   own error-continuation contract (per-entry errors never abort the
   walk; see the design note and rm.c's own handling). */
static void classify(struct cb_fts *fts, FTSENT *entry)
{
    struct cb_libc_dir *dir;

    if (cb_libc_stat(entry->fts_path, entry->fts_statp) < 0) {
        entry->fts_info = FTS_NS;
        entry->fts_errno = errno;
        return;
    }
    if (!S_ISDIR(entry->fts_statp->st_mode)) {
        entry->fts_info = FTS_DEFAULT;
        return;
    }
    if (cycle_detected(fts, entry->fts_statp->st_ino)) {
        entry->fts_info = FTS_DC;
        return;
    }
    if (fts->depth >= CB_FTS_MAX_DEPTH) {
        /* A path this deep already exceeds CB_PATH_MAX and could not have
           been resolved by the VFS in the first place (design note /
           review S3.A); reported the same way an unreadable directory is,
           since further descent is equally impossible. */
        entry->fts_info = FTS_DNR;
        entry->fts_errno = ENAMETOOLONG;
        return;
    }
    dir = cb_libc_opendir(entry->fts_accpath);
    if (dir == NULL) {
        entry->fts_info = FTS_DNR;
        entry->fts_errno = errno;
        return;
    }
    entry->fts_info = FTS_D;
    fts->stack[fts->depth].dir = dir;
    fts->stack[fts->depth].inode = entry->fts_statp->st_ino;
    fts->stack[fts->depth].entry = entry;
    fts->stack[fts->depth].skip = 0;
    fts->depth++;
}

FTS *cb_libc_fts_open(char *const *path_argv, int options,
                      int (*compar)(const FTSENT **, const FTSENT **))
{
    struct cb_fts *fts;

    if (path_argv == NULL || path_argv[0] == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (options & FTS_XDEV) {
        /* Rejected at runtime, not silently ignored: mount-crossing
           detection has no public surface yet (FTS-XDEV-01, still open
           per the design note and the review's FS-STAT-01 decision). */
        errno = ENOSYS;
        return NULL;
    }
    fts = cb_libc_calloc(1, sizeof(*fts));
    if (fts == NULL)
        return NULL;
    fts->path_argv = path_argv;
    fts->root_index = 0;
    fts->options = options;
    fts->compar = compar;
    fts->depth = 0;
    fts->pending_free = NULL;
    return fts;
}

FTSENT *cb_libc_fts_read(FTS *ftsp)
{
    if (ftsp == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (ftsp->pending_free != NULL) {
        entry_destroy(ftsp->pending_free);
        ftsp->pending_free = NULL;
    }
    for (;;) {
        if (ftsp->depth > 0) {
            struct cb_fts_frame *top = &ftsp->stack[ftsp->depth - 1];
            struct dirent *de;

            if (top->skip) {
                /* FTS_SKIP suppresses both descent and the post-order
                   FTS_DP visit (invariant 3): pop and free without ever
                   returning anything for this frame. */
                cb_libc_closedir(top->dir);
                entry_destroy(top->entry);
                ftsp->depth--;
                continue;
            }
            de = cb_libc_readdir(top->dir);
            if (de != NULL) {
                FTSENT *child = entry_create(top->entry->fts_path,
                                             de->d_name,
                                             top->entry->fts_level + 1);
                if (child == NULL)
                    return NULL;
                classify(ftsp, child);
                if (child->fts_info != FTS_D)
                    ftsp->pending_free = child;
                return child;
            }
            /* Directory exhausted: pop and re-use the same FTSENT for the
               postorder visit, exactly as real fts does. */
            cb_libc_closedir(top->dir);
            ftsp->depth--;
            top->entry->fts_info = FTS_DP;
            ftsp->pending_free = top->entry;
            return top->entry;
        }
        if (ftsp->path_argv[ftsp->root_index] == NULL)
            return NULL;
        {
            FTSENT *root = entry_create(NULL,
                                        ftsp->path_argv[ftsp->root_index],
                                        0);
            ftsp->root_index++;
            if (root == NULL)
                return NULL;
            classify(ftsp, root);
            if (root->fts_info != FTS_D)
                ftsp->pending_free = root;
            return root;
        }
    }
}

int cb_libc_fts_close(FTS *ftsp)
{
    int i;

    if (ftsp == NULL) {
        errno = EINVAL;
        return -1;
    }
    /* Symmetric cleanup even mid-walk (invariant 5): every still-open
       frame's directory handle is closed and its owning entry freed. */
    for (i = 0; i < ftsp->depth; ++i) {
        cb_libc_closedir(ftsp->stack[i].dir);
        entry_destroy(ftsp->stack[i].entry);
    }
    if (ftsp->pending_free != NULL)
        entry_destroy(ftsp->pending_free);
    cb_libc_free(ftsp);
    return 0;
}

int cb_libc_fts_set(FTS *ftsp, FTSENT *entry, int instr)
{
    int i;

    if (ftsp == NULL || entry == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (instr != FTS_SKIP) {
        errno = EINVAL;
        return -1;
    }
    if (entry->fts_info != FTS_D)
        return 0;
    for (i = 0; i < ftsp->depth; ++i) {
        if (ftsp->stack[i].entry == entry) {
            ftsp->stack[i].skip = 1;
            return 0;
        }
    }
    return 0;
}
