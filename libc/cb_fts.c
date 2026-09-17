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

/*
 * Depth bound for cycle detection and stack allocation. Any tree deeper than
 * CB_PATH_MAX / 2 would have a full path exceeding CB_PATH_MAX (assuming >= 1
 * char name plus '/'). This bound couples to utility-visible PATH_MAX being
 * strictly equal to VFS CB_PATH_MAX (enforced in limits.h and sys/param.h).
 */
#define CB_FTS_MAX_DEPTH (CB_PATH_MAX / 2)

struct cb_fts_frame {
    struct cb_libc_dir *dir;
    uint64_t inode;
    FTSENT *entry;   /* the FTS_D entry that opened this frame; reused,
                        info flipped to FTS_DP, when the frame pops */
    int skip;        /* fts_set(FTS_SKIP) called on `entry` */
    /* FTS-CHILDREN-01: set only if fts_children() was called while this
       frame was on top. children_snapshot owns the whole list (freed by
       fts_close() from next_child onward -- entries before next_child
       have already been handed to the caller via fts_read() and are
       owned by pending_free/a child frame's own entry by then).
       next_child is fts_read()'s own consumption cursor into that same
       list, advanced one fts_link at a time instead of calling
       cb_libc_readdir() again -- the directory handle was already fully
       drained building the snapshot, so reading it again would only
       ever see EOF. */
    FTSENT *children_snapshot;
    FTSENT *next_child;
};

struct cb_fts {
    char *const *path_argv;
    int root_index;
    int options;
    int (*compar)(const FTSENT **, const FTSENT **);  /* NULL for every
        pinned rm/cp call site (design note); ls.c passes mastercmp, applied
        by cb_libc_fts_children() below (FTS-CHILDREN-01/LS-02) to the list
        it returns, matching real fts(3)'s own contract. */
    struct cb_fts_frame stack[CB_FTS_MAX_DEPTH];
    int depth;
    FTSENT *pending_free;  /* previous entry not owned by a frame; real
                               fts's contract is that only one FTSENT is
                               valid across a fts_read() call, so this is
                               freed at the top of the next call rather
                               than immediately -- callers may still be
                               holding/reading the pointer they were just
                               handed until they ask for the next one. */
    /* FTS-CHILDREN-01: fts_children(ftsp, 0) called before any fts_read()
       (ls.c's own first call, per real fts(3)) previews path_argv itself
       without consuming root_index -- the main fts_read() loop below
       still creates and yields its own, separately owned entries for
       each root afterward. Freed and rebuilt on each such call. */
    FTSENT *root_children;
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

/* Shared stat/cycle/depth checks for both classify() (opens and
   descends) and classify_peek() (fts_children()'s non-descending
   preview). Returns 1 if entry is a directory candidate still needing
   an opendir() attempt to resolve FTS_D vs FTS_DNR; 0 if fts_info is
   already final (NS/DC/DEFAULT/the depth-bound DNR case). */
static int classify_kind(struct cb_fts *fts, FTSENT *entry)
{
    if (cb_libc_stat(entry->fts_path, entry->fts_statp) < 0) {
        entry->fts_info = FTS_NS;
        entry->fts_errno = errno;
        return 0;
    }
    if (!S_ISDIR(entry->fts_statp->st_mode)) {
        entry->fts_info = FTS_DEFAULT;
        return 0;
    }
    if (cycle_detected(fts, entry->fts_statp->st_ino)) {
        entry->fts_info = FTS_DC;
        return 0;
    }
    if (fts->depth >= CB_FTS_MAX_DEPTH) {
        /* A path this deep already exceeds CB_PATH_MAX and could not have
           been resolved by the VFS in the first place (design note /
           review S3.A); reported the same way an unreadable directory is,
           since further descent is equally impossible. */
        entry->fts_info = FTS_DNR;
        entry->fts_errno = ENAMETOOLONG;
        return 0;
    }
    return 1;
}

/* Classifies a freshly-created entry, pushing a new frame onto the stack
   when it becomes an FTS_D. Never returns NULL -- a stat or opendir
   failure is reported as an FTSENT (NS/DNR), matching real fts's own
   error-continuation contract (per-entry errors never abort the walk;
   see the design note and rm.c's own handling). */
static void classify(struct cb_fts *fts, FTSENT *entry)
{
    struct cb_libc_dir *dir;

    if (!classify_kind(fts, entry))
        return;
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
    fts->stack[fts->depth].children_snapshot = NULL;
    fts->stack[fts->depth].next_child = NULL;
    fts->depth++;
}

/* FTS-CHILDREN-01: fts_children()'s own classification. Distinguishes
   FTS_D from FTS_DNR the same way classify() does, but never keeps a
   directory open or pushes a frame -- this previews, it does not
   descend. The real descent happens later, at actual consumption via
   classify() in cb_libc_fts_read()'s main loop, when this same entry is
   handed out as the current one. */
static void classify_peek(struct cb_fts *fts, FTSENT *entry)
{
    struct cb_libc_dir *dir;

    if (!classify_kind(fts, entry))
        return;
    dir = cb_libc_opendir(entry->fts_accpath);
    if (dir == NULL) {
        entry->fts_info = FTS_DNR;
        entry->fts_errno = errno;
        return;
    }
    cb_libc_closedir(dir);
    entry->fts_info = FTS_D;
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

/* Frees a fts_children() list from `from` onward (inclusive), following
   fts_link. Used both to discard a stale/superseded list and, in
   fts_close(), to free whatever a still-open frame's snapshot never got
   consumed. */
static void free_child_list(FTSENT *from)
{
    while (from != NULL) {
        FTSENT *next = from->fts_link;
        entry_destroy(from);
        from = next;
    }
}

/* LS-02: real fts_children() sorts the list it returns using the
   comparator fts_open() was given, the same one applied to each
   directory's children during an ordinary fts_read() descent -- ls.c's
   own default (non -f/-U) listing order depends on this; rm.c/cp.c
   never pass a non-NULL comparator, so this is a no-op for them. A
   simple linked-list merge sort, not qsort(3) over a temporary array:
   this project's libc veneer has no qsort, and RAMFS directories are
   small enough that introducing one just for this one internal use
   would be pure overhead. */
static FTSENT *merge_sorted_children(
        int (*compar)(const FTSENT **, const FTSENT **),
        FTSENT *left, FTSENT *right)
{
    FTSENT head_stub;
    FTSENT *tail = &head_stub;

    while (left != NULL && right != NULL) {
        const FTSENT *left_entry = left;
        const FTSENT *right_entry = right;
        if (compar(&left_entry, &right_entry) <= 0) {
            tail->fts_link = left;
            tail = left;
            left = left->fts_link;
        } else {
            tail->fts_link = right;
            tail = right;
            right = right->fts_link;
        }
    }
    tail->fts_link = (left != NULL) ? left : right;
    return head_stub.fts_link;
}

static FTSENT *sort_children(struct cb_fts *fts, FTSENT *head)
{
    FTSENT *slow, *fast, *second_half;

    if (fts->compar == NULL || head == NULL || head->fts_link == NULL)
        return head;

    slow = head;
    fast = head->fts_link;
    while (fast != NULL && fast->fts_link != NULL) {
        slow = slow->fts_link;
        fast = fast->fts_link->fts_link;
    }
    second_half = slow->fts_link;
    slow->fts_link = NULL;

    return merge_sorted_children(fts->compar,
                                  sort_children(fts, head),
                                  sort_children(fts, second_half));
}

/* FTS-CHILDREN-01. See the FTSENT fts_link/fts_parent doc comment and
   struct cb_fts_frame's children_snapshot/next_child doc comment in
   fts.h and above. */
FTSENT *cb_libc_fts_children(FTS *ftsp, int options)
{
    FTSENT *head = NULL, *tail = NULL;
    (void)options; /* FTS_NAMEONLY accepted, not distinguished -- see fts.h */

    if (ftsp == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (ftsp->depth == 0) {
        int i;
        free_child_list(ftsp->root_children);
        ftsp->root_children = NULL;
        for (i = ftsp->root_index; ftsp->path_argv[i] != NULL; ++i) {
            FTSENT *entry = entry_create(NULL, ftsp->path_argv[i], 0);
            if (entry == NULL) {
                free_child_list(head);
                return NULL;
            }
            classify_peek(ftsp, entry);
            if (tail == NULL)
                head = entry;
            else
                tail->fts_link = entry;
            tail = entry;
        }
        head = sort_children(ftsp, head);
        ftsp->root_children = head;
        errno = 0;
        return head;
    } else {
        struct cb_fts_frame *top = &ftsp->stack[ftsp->depth - 1];
        struct dirent *de;

        if (top->children_snapshot != NULL)
            return top->children_snapshot;
        if (ftsp->options & FTS_SEEDOT) {
            const char *dotnames[2] = { ".", ".." };
            int i;
            for (i = 0; i < 2; ++i) {
                FTSENT *entry = entry_create(top->entry->fts_path,
                                             dotnames[i],
                                             top->entry->fts_level + 1);
                if (entry == NULL) {
                    free_child_list(head);
                    return NULL;
                }
                classify_peek(ftsp, entry);
                entry->fts_parent = top->entry;
                if (tail == NULL)
                    head = entry;
                else
                    tail->fts_link = entry;
                tail = entry;
            }
        }
        while ((de = cb_libc_readdir(top->dir)) != NULL) {
            FTSENT *entry = entry_create(top->entry->fts_path, de->d_name,
                                         top->entry->fts_level + 1);
            if (entry == NULL) {
                free_child_list(head);
                return NULL;
            }
            classify_peek(ftsp, entry);
            entry->fts_parent = top->entry;
            if (tail == NULL)
                head = entry;
            else
                tail->fts_link = entry;
            tail = entry;
        }
        head = sort_children(ftsp, head);
        top->children_snapshot = head;
        top->next_child = head;
        errno = 0;
        return head;
    }
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
                free_child_list(top->next_child);
                ftsp->depth--;
                continue;
            }
            if (top->next_child != NULL) {
                /* fts_children() already drained this frame's directory
                   into a snapshot; consume that instead of calling
                   cb_libc_readdir() again (which would only see EOF).
                   The snapshot's own classify_peek() only distinguished
                   FTS_D from FTS_DNR without opening/descending -- do
                   the real classify() now so descending into a
                   previewed directory actually opens and pushes a
                   frame for it. Non-directory previews are already
                   final; classify_kind() is cheap and idempotent to
                   redo rather than special-case skipping it. */
                FTSENT *child = top->next_child;
                top->next_child = child->fts_link;
                if (child->fts_info == FTS_D)
                    classify(ftsp, child);
                if (child->fts_info != FTS_D)
                    ftsp->pending_free = child;
                return child;
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
        if (ftsp->path_argv[ftsp->root_index] == NULL) {
            /* Real fts_read()'s documented contract: a clean end of the
               whole walk must leave errno reflecting only a genuine
               fts-level failure, never leftover state from a per-entry
               condition (FTS_NS/FTS_DNR/etc.) this function already
               reported through fts_info/fts_errno. Callers rely on this
               directly -- rm.c's own rm_tree() checks `if (errno)
               err(1, "fts_read")` immediately after this loop ends, with
               no errno reset of its own; found via rm -rf on a missing
               path falsely exiting 1 instead of 0, not by inspection. */
            errno = 0;
            return NULL;
        }
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
        /* Only from next_child onward: entries before it were already
           handed to the caller via fts_read() and are owned by
           pending_free or a child frame's own entry by now. */
        free_child_list(ftsp->stack[i].next_child);
    }
    if (ftsp->pending_free != NULL)
        entry_destroy(ftsp->pending_free);
    free_child_list(ftsp->root_children);
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
