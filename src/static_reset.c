/*
 * STATICS-RESET-01: a single, reusable executor-ops wrapper that gives a
 * pinned command's cross-invocation state per-invocation isolation,
 * generalizing TEE-STATE-01-design.md's one-off per-symbol swap (never
 * itself landed, since no real tee import exists yet) into something
 * ls/cat/mv/rm/cp can all share without each needing its own bespoke
 * wrapper.
 *
 * Background: cannedBSD tasks multiplex inside one host process (see
 * src/executor.c's native_entry -- a cooperative stack-switch, not a real
 * fork/exec address-space reset), so a pinned command's cross-invocation
 * state keeps whatever value a PREVIOUS invocation left it at. Real BSD
 * source assumes a fresh, zeroed process image every time; nothing in this
 * runtime gave that assumption back until now. tests/test_statics_repro.sh
 * (adapted from antigravity's STATICS-REPRO-01 red suite, which is the
 * authority for what is DEMONSTRATED here versus merely structurally
 * argued) turns five of these into executable, sanitizer-verified
 * evidence, not just source-reading:
 *
 *   - ls's print.c printcol() column-mode cache: DEMONSTRATED heap
 *     use-after-free -- a second `ls` whose entry count does not exceed
 *     an earlier one's bypasses realloc and writes through memory the
 *     first ls task's own exit already freed.
 *   - cat's raw_cat() `buf`/`bsize` pair: DEMONSTRATED heap
 *     use-after-free, but ONLY when `-B` requests a buffer larger than
 *     the built-in 1024-byte one (cb_libc.c hardcodes st_blksize=1024,
 *     equal to BUFSIZ, so the fstat-driven default path never mallocs
 *     and never reaches this; a plain `cat x; cat y` does not reproduce
 *     it). A separate, independently-found zero-length-read bug came
 *     from the two halves of this same pair resetting out of step with
 *     each other -- see cat_slots' own comment below.
 *   - cp's seven leaking option flags (fflag, iflag, lflag, pflag,
 *     rflag, vflag, Nflag -- cp.c's own main() already resets the other
 *     four): DEMONSTRATED state leakage, verified worse than the
 *     original -v-only framing since -f/-i/-l/-p change cp's semantics,
 *     not just its output.
 *   - rm's `eval`: DEMONSTRATED wrong exit status on a successful rm
 *     that follows a failed one in the same session -- caught and fixed
 *     before this file grew a name for the pattern.
 *   - mv's fastcopy() `bp`/`blen` buffer cache: the identical UAF shape
 *     as ls's and cat's, confirmed directly against the source, but
 *     LATENT, not demonstrated: every session directory (/, /tmp,
 *     /home, /bin) sits on the one unified root ramfs mount, rename()
 *     never returns EXDEV inside a single mount, and fastcopy() is only
 *     ever reached on that EXDEV fallback path -- so no shell command
 *     exercises it today. Fixed anyway, on the same mechanism as ls's
 *     case, for whenever a second mount makes it reachable.
 *
 * ls.c's own 32 non-static globals plus `output` (stale option flags and
 * a stale accumulated exit status carrying across a session's later,
 * unrelated ls calls) and cp's `dnesp` recursion-depth index are both
 * fixed here too but were not part of the demonstrated set above: ls's
 * case was found by direct source audit after the demonstrated printcol()
 * fix revealed the wrapper was only covering one of ls.c's 33
 * cross-invocation symbols; cp's dnesp is flagged by antigravity's own
 * audit as a plausible hazard (a stale nonzero index could corrupt a
 * second `cp -r`'s recursion bookkeeping) but its own follow-up repro
 * suite did NOT reproduce it, so it is fixed at zero marginal cost here
 * without being claimed as a verified fix -- see cp_slots' own comment
 * below.
 *
 * This state comes in three different C storage classes, and this
 * mechanism's reach is different for each -- stated plainly, per an
 * explicit request after the first version of this file only handled one
 * of the three and left two of the five hazards above live while
 * appearing to solve the problem:
 *
 *   - File-scope statics (ls.c's `output`; cat.c's/mv.c's/rm.c's getopt
 *     flags; cp.c's `dnesp`): COVERED. Reachable via objcopy -- see
 *     mechanism 1 below.
 *   - Plain non-static globals (ls.c's `termwidth`/`sortkey`/`rval`/
 *     `blocksize` and its 28 f_* flags; cp.c's `Hflag`/`Lflag`/etc.):
 *     COVERED, more easily than file-scope statics -- these already have
 *     real external linkage, so `extern` by their own name is sufficient
 *     and no rename step is needed at all. Slotted into the exact same
 *     mechanism 1 below, alongside the renamed file-scope statics; the
 *     wrapper does not need to know or care which of the two a given slot
 *     started as.
 *   - Function-scope statics (print.c's printcol()-local `array`/
 *     `lastentries`; mv.c's fastcopy()-local `bp`/`blen`; cat.c's
 *     raw_cat()-local `buf`): PARTIALLY COVERED, and only because each of
 *     the three actual cases here happens to fit one of two narrow
 *     shapes -- see mechanism 2 below for what those shapes are and what
 *     this mechanism would NOT be able to fix if a fourth case did not
 *     fit either one. This is the one honest gap: there is no general
 *     mechanism here for a function-scope static that must itself be
 *     reset (not just kept valid) and has no unmanaged partner variable
 *     to pair it with. Closing that gap for real would mean either
 *     editing pinned source (forbidden) or per-task writable-data copies
 *     or per-command linker sections -- a materially different, larger
 *     piece of work, not a reset-list extension, and out of scope here
 *     unless a real case actually needs it.
 *
 * Two different sub-mechanisms, because file-scope/global scalars and
 * function-scope pointer caches are reachable in two different ways:
 *
 * 1. File-scope scalar statics (ls.c's `output`; cat.c's/mv.c's/rm.c's
 *    getopt flags and accumulated-exit-status variables): given an
 *    external name via objcopy(1) (--globalize-symbol promotes the
 *    compiled symbol from local to global binding, --redefine-sym renames
 *    it -- see each pinned file's own Makefile/CMakeLists build rule for
 *    the exact rename table), this wrapper's start_or_resume memcpy()s
 *    each declared slot's saved bytes into the live global before
 *    delegating to the native executor, then saves the live value back
 *    out afterward (unless the task is now CB_TASK_ZOMBIE/CB_TASK_DEAD,
 *    mirroring TEE-STATE-01-design.md's own rule 3 -- a task that already
 *    exited may have freed memory a pointer-typed slot referenced), then
 *    always clears the live global before returning to the scheduler
 *    (rule 5 -- interleaved tasks of the same program must never see each
 *    other's live values while suspended). A compile-time -D rename
 *    cannot do this job: it can rename an identifier, but not selectively
 *    strip internal linkage from ONE specific declaration while leaving
 *    the same file's actual function-local statics alone -- confirmed by
 *    attempting exactly that (a blanket -Dstatic=) on cat.c and mv.c,
 *    which turned raw_cat()'s own `static char *buf` and fastcopy()'s own
 *    `static char *bp`/`static blksize_t blen` into fresh, uninitialized
 *    automatic variables each call (a real, WORSE bug than the one being
 *    fixed: GCC's own -Wmaybe-uninitialized caught it immediately, not
 *    guessed). objcopy operates on the already-compiled symbol table
 *    instead, precisely enough to touch only the one named symbol.
 *
 * 2. Function-local pointer caches -- print.c's printcol()-local `static
 *    FTSENT **array`/`static int lastentries` (ls), raw_cat()'s own
 *    `static char *buf` paired with the file-scope `bsize` it gates on
 *    (cat), fastcopy()'s own `static char *bp` paired with `static
 *    blksize_t blen` (mv). Each of these three fits one of exactly two
 *    shapes that make an unreachable pointer's staleness safe to leave
 *    alone rather than needing to reset it: (i) the command's own logic
 *    re-derives whatever it needs from the cache on every call regardless
 *    of what the pointer currently holds (ls's printcol() resizes/refills
 *    `array` itself based on the current listing's own entry count; mv's
 *    fastcopy() keeps reusing `bp` sized to whatever `blen` was, which is
 *    also left unmanaged, so the two never desync); or (ii) the pointer's
 *    OWN gate variable is itself left unmanaged alongside it, so gate and
 *    cache move in lockstep and the pair is self-consistent even though
 *    neither resets (cat's `buf`/`bsize`, once `bsize` was moved back out
 *    of mechanism 1's reset list -- see cat_slots' own comment below for
 *    the bug that came from putting it there while `buf` had no partner
 *    treatment). Neither shape is a general answer: a function-local
 *    static whose OWN correctness requires it to become NULL/zero again
 *    between invocations, with no unmanaged partner variable available to
 *    pair it with, is not something this mechanism can fix. A function-
 *    local static's compiled symbol name
 *    is compiler-internal and, at least under GCC, mangled with a
 *    non-deterministic numeric suffix -- not something to build a
 *    portable rename-and-reset mechanism on (clang's own naming, `func.
 *    var`, IS stable and objcopy-reachable, confirmed via nm on cat.o, but
 *    mac68k's Retro68 cross-toolchain is GCC-based, so relying on that
 *    would be a portability trap). The fix instead is to make the memory
 *    each cache holds never become invalid while its program could still
 *    run again -- i.e., by not running the ordinary task-scoped allocation
 *    sweep for that program at all -- and to leave the *entire* gate/cache
 *    pair unmanaged rather than resetting only the half objcopy can reach.
 *    (An earlier version of this file reset cat's `bsize` alone, since it
 *    IS file-scope and objcopy-reachable; that desynced it from `buf`,
 *    which stayed non-NULL from a prior invocation and skipped raw_cat()'s
 *    own re-initialization, so a second invocation ran `read(rfd, buf, 0)`
 *    -- a zero-length read a pipe's read() treats as immediate EOF. Caught
 *    by exactly the red test STATICS-RESET-01 was supposed to write: a
 *    multi-invocation session exercising the default, non-`-1` code path
 *    across more than one task.) See CB_EXECUTOR_PERSISTENT_HEAP and
 *    task_release_allocations()'s own comment in core.c: a persistent-heap
 *    executor's task allocations are spliced onto the shared program's own
 *    list instead of released, so a later task of the same program can
 *    safely keep reading through whatever a previous task's own explicit
 *    free() calls didn't already reclaim (which, for each of these three
 *    programs, is exactly and only its own cache above -- everything else
 *    they allocate, cb_fts.c's own entries included, is already explicitly
 *    freed within a single invocation's own traversal, matching what a
 *    real BSD process would never explicitly free either, relying on
 *    process exit; this executor's own program_destroy frees the
 *    accumulated arena for real at genuine kernel teardown). rm.c has no
 *    such pattern (checked directly against its source, not assumed) and
 *    is registered without the capability bit.
 */

#include "internal.h"

struct cb_static_slot {
    void *address;
    size_t size;
};

/* Extends struct cb_executor_ops (checked via struct_size, the same
   versioned-extension idiom used throughout this codebase) with the slot
   table a specific command's registration needs -- one generic set of
   callback functions below, reused across every struct instance of this
   type rather than one bespoke wrapper per command. */
struct cb_static_reset_ops {
    struct cb_executor_ops common;
    const struct cb_static_slot *slots;
    size_t slot_count;
};

struct cb_static_reset_execution {
    struct cb_execution common;
    struct cb_execution *inner;
    unsigned char *saved;
    size_t saved_size;
};

static int sr_prepare(struct cb_kernel *kernel,
                      const struct cb_executor_ops *executor,
                      const void *source, struct cb_program **program_out)
{
    /* Passes `executor` (this wrapper's own ops, not native's) straight
       through: native_prepare stores whatever executor it is called with
       into the resulting program's own .executor field, exactly the
       delegation TEE-STATE-01-design.md's synthetic proof already
       established (tests/tee_state_probe.c's own prepare()). */
    return cb_native_executor()->prepare(kernel, executor, source, program_out);
}

static struct cb_execution *sr_instance_create(struct cb_task *task,
                                               const struct cb_program *program)
{
    const struct cb_static_reset_ops *ops =
        (const struct cb_static_reset_ops *)program->executor;
    struct cb_static_reset_execution *execution;
    struct cb_program *mutable_program = (struct cb_program *)program;

    execution = cb_allocate(task->kernel, sizeof(*execution));
    if (execution == NULL)
        return NULL;
    execution->inner = cb_native_executor()->instance_create(task, program);
    if (execution->inner == NULL) {
        cb_release(task->kernel, execution);
        return NULL;
    }
    execution->common.executor = program->executor;
    execution->common.task = task;
    execution->common.program = program;
    /* CB_EXECUTOR_PERSISTENT_HEAP: adopt whatever a previous task of this
       same program left in the shared arena as THIS task's own
       allocations, not merely something that survives in memory -- a
       fresh task's own task->allocations starts NULL (real BSD assumption
       print.c's printcol() relies on either way, see its own comment),
       so this is a plain assignment, not a merge. Necessary, not just
       nice-to-have: cb_libc_realloc()/free() (api_resize()/api_release()
       in core.c) look the pointer up in the CURRENT task's own
       task->allocations list and fail EINVAL if it is not there --
       found by running the real multi-invocation session this ID's own
       red test exercises and reading ls's own "invalid argument"
       diagnostic, not guessed. task_release_allocations() (core.c) moves
       it back to program->persistent_allocations when this task exits,
       exactly reversing this adoption. */
    if (cb_executor_supports_persistent_heap(program->executor)) {
        task->allocations = mutable_program->persistent_allocations;
        mutable_program->persistent_allocations = NULL;
    }
    execution->saved_size = 0;
    {
        size_t i;
        for (i = 0; i < ops->slot_count; i++)
            execution->saved_size += ops->slots[i].size;
    }
    /* Captured once per program, from each slot's own live value, the
       first time any task of this program is ever created -- i.e. before
       any task has run and had a chance to mutate them, so this is still
       each slot's real compile-time initializer. NOT always zero-fill:
       see cb_program's own static_defaults comment in internal.h for why
       ls.c's `termwidth` (initializes to 80) needs this instead of a
       blind zero-fill. */
    if (mutable_program->static_defaults == NULL && execution->saved_size != 0) {
        unsigned char *cursor;
        size_t i, j;
        mutable_program->static_defaults =
            cb_allocate(task->kernel, execution->saved_size);
        if (mutable_program->static_defaults == NULL) {
            cb_native_executor()->instance_destroy(execution->inner);
            cb_release(task->kernel, execution);
            return NULL;
        }
        cursor = mutable_program->static_defaults;
        for (i = 0; i < ops->slot_count; i++) {
            const unsigned char *slot = ops->slots[i].address;
            for (j = 0; j < ops->slots[i].size; j++)
                cursor[j] = slot[j];
            cursor += ops->slots[i].size;
        }
    }
    execution->saved = cb_allocate(task->kernel, execution->saved_size);
    if (execution->saved == NULL && execution->saved_size != 0) {
        cb_native_executor()->instance_destroy(execution->inner);
        cb_release(task->kernel, execution);
        return NULL;
    }
    /* Seed this brand-new task's own saved buffer from the program's
       captured defaults, not zero: sr_start_or_resume's own restore step
       (its first one, before this task has ever run) needs to write the
       real compile-time default into each live slot, and this is the
       only copy of that default this wrapper keeps. */
    {
        size_t i;
        for (i = 0; i < execution->saved_size; i++)
            execution->saved[i] = mutable_program->static_defaults[i];
    }
    return &execution->common;
}

static void sr_start_or_resume(struct cb_execution *common)
{
    struct cb_static_reset_execution *execution =
        (struct cb_static_reset_execution *)common;
    const struct cb_static_reset_ops *ops =
        (const struct cb_static_reset_ops *)common->executor;
    size_t i;
    unsigned char *cursor;

    cursor = execution->saved;
    for (i = 0; i < ops->slot_count; i++) {
        size_t j;
        unsigned char *slot = ops->slots[i].address;
        for (j = 0; j < ops->slots[i].size; j++)
            slot[j] = cursor[j];
        cursor += ops->slots[i].size;
    }

    cb_native_executor()->start_or_resume(execution->inner);

    /* A task that just exited may hold a pointer-typed slot referencing
       memory task_release_allocations() already freed (ls's own `output`
       is a plain int and would be safe to save back regardless, but this
       mechanism is generic -- a future consumer's slot might not be) --
       discard rather than capture, matching TEE-STATE-01-design.md's own
       rule 3. Either way, the live globals are cleared below before
       returning to the scheduler, so a discarded value is never
       observable again. */
    if (common->task->state != CB_TASK_ZOMBIE &&
        common->task->state != CB_TASK_DEAD) {
        cursor = execution->saved;
        for (i = 0; i < ops->slot_count; i++) {
            size_t j;
            const unsigned char *slot = ops->slots[i].address;
            for (j = 0; j < ops->slots[i].size; j++)
                cursor[j] = slot[j];
            cursor += ops->slots[i].size;
        }
    }

    /* Never leave one task's live values exposed to another interleaved
       task of the same program while this one is suspended or gone --
       matching TEE-STATE-01-design.md's own rule 5. */
    for (i = 0; i < ops->slot_count; i++) {
        size_t j;
        unsigned char *slot = ops->slots[i].address;
        for (j = 0; j < ops->slots[i].size; j++)
            slot[j] = 0;
    }
}

static void sr_suspend(struct cb_execution *common)
{
    cb_native_executor()->suspend(
        ((struct cb_static_reset_execution *)common)->inner);
}

static void sr_request_termination(struct cb_execution *common)
{
    cb_native_executor()->request_termination(
        ((struct cb_static_reset_execution *)common)->inner);
}

static void sr_instance_destroy(struct cb_execution *common)
{
    struct cb_static_reset_execution *execution =
        (struct cb_static_reset_execution *)common;
    struct cb_kernel *kernel = common->task->kernel;
    cb_native_executor()->instance_destroy(execution->inner);
    cb_release(kernel, execution->saved);
    cb_release(kernel, execution);
}

static void sr_program_destroy(struct cb_kernel *kernel, struct cb_program *program)
{
    /* Real kernel teardown, not a task exit: safe to free whatever a
       persistent-heap program's own tasks left behind (print.c's
       printcol() cache, for ls) for real -- see task_release_allocations()
       and CB_EXECUTOR_PERSISTENT_HEAP's own comments for why this can
       never happen any earlier. A no-op for a program with the capability
       unset, or one that never accumulated anything. */
    if (program->persistent_allocations != NULL) {
        cb_task_release_allocation_list(kernel, program->persistent_allocations);
        program->persistent_allocations = NULL;
    }
    if (program->static_defaults != NULL) {
        cb_release(kernel, program->static_defaults);
        program->static_defaults = NULL;
    }
    cb_native_executor()->program_destroy(kernel, program);
}

#define CB_STATIC_RESET_OPS_COMMON \
    CB_ABI_VERSION_V1, sizeof(struct cb_static_reset_ops), \
    sr_prepare, sr_instance_create, sr_start_or_resume, sr_suspend, \
    sr_request_termination, sr_instance_destroy, sr_program_destroy

/* Every instance below ORs in CB_EXECUTOR_COOPERATIVE_INTERRUPT: this
   wrapper's start_or_resume/suspend/etc. all genuinely delegate to
   cb_native_executor(), which has always advertised that capability
   (src/executor.c's own native_ops) -- omitting it here made
   cb_executor_supports_interrupt() (checked elsewhere for signal/pipe-
   wakeup scheduling decisions) treat every one of these tasks as if it
   could NOT be cooperatively interrupted, silently breaking multi-stage
   pipelines through ls/cat/mv/rm (found by running "echo abc | cat | tr
   a-z A-Z" through the real test suite and getting empty output with a
   clean exit status, not guessed). A wrapper must advertise every
   capability its inner executor actually provides, not just the ones it
   adds itself. */

/* LS-02's own leaked "static int output" (a stray "\ndirname:\n" header
   on a later listing) plus the persistent-heap capability for print.c's
   printcol() cache -- see this file's own top comment. cb_ls_output is
   the only one of ls.c's own cross-invocation statics that is itself
   `static` (hence the objcopy rename); ls.c's remaining 32 -- blocksize,
   termwidth, sortkey, rval, and 28 f_* option flags -- are plain
   non-static globals, declared with real external linkage already, so
   they need no rename at all: `extern` by their own real name is
   sufficient. Found late (STATICS-RESET-01 shipped with only `output`
   managed for one review cycle) because every test up to that point
   only ever ran ls once per session, or -- for the interleave test --
   ran two ls invocations whose flag state happened to agree; a session
   mixing e.g. `ls -l` then a plain `ls` would have carried f_longform
   and rval across, and did on main until this list grew to cover them.
   termwidth is the one slot here that must NOT reset to zero: it
   initializes to 80 and is only ever reassigned when isatty() and a
   real TIOCGWINSZ both succeed, which this runtime's virtual console
   never reports, so a zero-fill would leave every ls after the first
   computing column widths against a zero-width terminal -- see
   cb_program's own static_defaults comment (internal.h) for the
   mechanism that restores 80, not 0. */
extern int cb_ls_output;
extern long blocksize;
extern int termwidth, sortkey, rval;
extern int f_accesstime, f_column, f_columnacross, f_flags, f_grouponly,
           f_humanize, f_commas, f_inode, f_listdir, f_listdot, f_longform,
           f_nonprint, f_nosort, f_numericonly, f_octal, f_octal_escape,
           f_recursive, f_reversesort, f_sectime, f_singlecol, f_size,
           f_statustime, f_stream, f_type, f_typedir, f_whiteout,
           f_fullpath, f_leafonly;
static const struct cb_static_slot ls_slots[] = {
    { &cb_ls_output, sizeof(cb_ls_output) },
    { &blocksize, sizeof(blocksize) },
    { &termwidth, sizeof(termwidth) },
    { &sortkey, sizeof(sortkey) },
    { &rval, sizeof(rval) },
    { &f_accesstime, sizeof(f_accesstime) },
    { &f_column, sizeof(f_column) },
    { &f_columnacross, sizeof(f_columnacross) },
    { &f_flags, sizeof(f_flags) },
    { &f_grouponly, sizeof(f_grouponly) },
    { &f_humanize, sizeof(f_humanize) },
    { &f_commas, sizeof(f_commas) },
    { &f_inode, sizeof(f_inode) },
    { &f_listdir, sizeof(f_listdir) },
    { &f_listdot, sizeof(f_listdot) },
    { &f_longform, sizeof(f_longform) },
    { &f_nonprint, sizeof(f_nonprint) },
    { &f_nosort, sizeof(f_nosort) },
    { &f_numericonly, sizeof(f_numericonly) },
    { &f_octal, sizeof(f_octal) },
    { &f_octal_escape, sizeof(f_octal_escape) },
    { &f_recursive, sizeof(f_recursive) },
    { &f_reversesort, sizeof(f_reversesort) },
    { &f_sectime, sizeof(f_sectime) },
    { &f_singlecol, sizeof(f_singlecol) },
    { &f_size, sizeof(f_size) },
    { &f_statustime, sizeof(f_statustime) },
    { &f_stream, sizeof(f_stream) },
    { &f_type, sizeof(f_type) },
    { &f_typedir, sizeof(f_typedir) },
    { &f_whiteout, sizeof(f_whiteout) },
    { &f_fullpath, sizeof(f_fullpath) },
    { &f_leafonly, sizeof(f_leafonly) },
};
static const struct cb_static_reset_ops ls_static_reset_ops = {
    { CB_STATIC_RESET_OPS_COMMON,
      CB_EXECUTOR_COOPERATIVE_INTERRUPT | CB_EXECUTOR_PERSISTENT_HEAP },
    ls_slots, sizeof(ls_slots) / sizeof(ls_slots[0])
};

const struct cb_executor_ops *cb_ls_static_reset_executor(void)
{
    return &ls_static_reset_ops.common;
}

/* cat.c's getopt flags (never reset at the top of main) plus its own
   accumulated exit-status variable (cb_cat_rval) reset per invocation.
   cb_cat_bsize is deliberately NOT reset, and is paired with
   CB_EXECUTOR_PERSISTENT_HEAP instead -- found the hard way: an earlier
   version of this file DID reset bsize to 0 each invocation while leaving
   raw_cat()'s own function-local `static char *buf` alone (unreachable by
   name from here, or so it seemed -- see below). raw_cat()'s own gate is
   `if (buf == NULL) { ... recompute bsize, allocate buf ... }`, so
   resetting bsize without also resetting buf desynced the pair: buf
   stayed non-NULL from the first invocation, so the second invocation's
   gate never fired, and `read(rfd, buf, bsize)` ran with the
   freshly-reset bsize == 0 -- a zero-length read a pipe's read() treats
   as EOF before ever checking whether the writer is still open, so
   `echo abc | cat | tr a-z A-Z` produced no output. This is exactly
   mv.c's fastcopy() `bp`/`blen` shape (see mv.c's own comment below) and
   the fix is the same: leave the whole gate/cache pair -- bsize AND
   buf -- unmanaged and let CB_EXECUTOR_PERSISTENT_HEAP keep buf's target
   valid across invocations, rather than resetting one half of a pair
   whose other half objcopy cannot reach. (raw_cat.buf's compiled symbol
   name *is* stable and objcopy-reachable under clang, confirmed via nm;
   this fix doesn't need that fact, since not resetting it at all sidesteps
   the portability question of whether the same holds under mac68k's
   GCC-based cross-compiler, which mangles function-local statics with a
   non-deterministic numeric suffix instead.) */
extern int cb_cat_bflag, cb_cat_eflag, cb_cat_fflag, cb_cat_lflag,
           cb_cat_nflag, cb_cat_sflag, cb_cat_tflag, cb_cat_vflag,
           cb_cat_rval;
static const struct cb_static_slot cat_slots[] = {
    { &cb_cat_bflag, sizeof(cb_cat_bflag) },
    { &cb_cat_eflag, sizeof(cb_cat_eflag) },
    { &cb_cat_fflag, sizeof(cb_cat_fflag) },
    { &cb_cat_lflag, sizeof(cb_cat_lflag) },
    { &cb_cat_nflag, sizeof(cb_cat_nflag) },
    { &cb_cat_sflag, sizeof(cb_cat_sflag) },
    { &cb_cat_tflag, sizeof(cb_cat_tflag) },
    { &cb_cat_vflag, sizeof(cb_cat_vflag) },
    { &cb_cat_rval, sizeof(cb_cat_rval) },
};
static const struct cb_static_reset_ops cat_static_reset_ops = {
    { CB_STATIC_RESET_OPS_COMMON,
      CB_EXECUTOR_COOPERATIVE_INTERRUPT | CB_EXECUTOR_PERSISTENT_HEAP },
    cat_slots, sizeof(cat_slots) / sizeof(cat_slots[0])
};

const struct cb_executor_ops *cb_cat_static_reset_executor(void)
{
    return &cat_static_reset_ops.common;
}

/* mv.c's getopt flags. stdin_ok/pinfo, also file-scope statics in mv.c,
   are deliberately not managed: stdin_ok is always recomputed via
   isatty() before use (moot regardless, since isatty() always reports
   true in this runtime) and pinfo is a SIGINFO progress-report counter
   with no correctness impact if stale. CB_EXECUTOR_PERSISTENT_HEAP covers
   fastcopy()'s own function-local `static char *bp`/`static blksize_t
   blen` buffer cache, the same class of hazard as cat.c's `buf` above --
   see this file's own top comment. */
extern int cb_mv_fflg, cb_mv_hflg, cb_mv_iflg, cb_mv_vflg;
static const struct cb_static_slot mv_slots[] = {
    { &cb_mv_fflg, sizeof(cb_mv_fflg) },
    { &cb_mv_hflg, sizeof(cb_mv_hflg) },
    { &cb_mv_iflg, sizeof(cb_mv_iflg) },
    { &cb_mv_vflg, sizeof(cb_mv_vflg) },
};
static const struct cb_static_reset_ops mv_static_reset_ops = {
    { CB_STATIC_RESET_OPS_COMMON,
      CB_EXECUTOR_COOPERATIVE_INTERRUPT | CB_EXECUTOR_PERSISTENT_HEAP },
    mv_slots, sizeof(mv_slots) / sizeof(mv_slots[0])
};

const struct cb_executor_ops *cb_mv_static_reset_executor(void)
{
    return &mv_static_reset_ops.common;
}

/* rm.c's getopt flags plus its own accumulated exit-status variable
   (cb_rm_eval). stdin_ok/pinfo left unmanaged, same reasoning as mv.c's
   own. rm.c has no function-local pointer-cache pattern like cat.c's
   `buf` or mv.c's `bp`/`blen` (checked directly against its own source,
   not assumed) -- no CB_EXECUTOR_PERSISTENT_HEAP needed. */
extern int cb_rm_dflag, cb_rm_eval, cb_rm_fflag, cb_rm_iflag, cb_rm_Pflag,
           cb_rm_vflag, cb_rm_Wflag, cb_rm_xflag;
static const struct cb_static_slot rm_slots[] = {
    { &cb_rm_dflag, sizeof(cb_rm_dflag) },
    { &cb_rm_eval, sizeof(cb_rm_eval) },
    { &cb_rm_fflag, sizeof(cb_rm_fflag) },
    { &cb_rm_iflag, sizeof(cb_rm_iflag) },
    { &cb_rm_Pflag, sizeof(cb_rm_Pflag) },
    { &cb_rm_vflag, sizeof(cb_rm_vflag) },
    { &cb_rm_Wflag, sizeof(cb_rm_Wflag) },
    { &cb_rm_xflag, sizeof(cb_rm_xflag) },
};
static const struct cb_static_reset_ops rm_static_reset_ops = {
    { CB_STATIC_RESET_OPS_COMMON, CB_EXECUTOR_COOPERATIVE_INTERRUPT },
    rm_slots, sizeof(rm_slots) / sizeof(rm_slots[0])
};

const struct cb_executor_ops *cb_rm_static_reset_executor(void)
{
    return &rm_static_reset_ops.common;
}

/* cp.c's own getopt flags (Hflag, Lflag, Rflag, Pflag, fflag, iflag,
   lflag, pflag, rflag, vflag, Nflag) are plain non-static globals, same
   storage class and same reasoning as ls.c's own f_* flags above -- no
   rename needed. cp.c's own main() already resets four of these eleven
   itself (Hflag, Lflag, Pflag, Rflag); the other seven -- fflag, iflag,
   lflag, pflag, rflag, vflag, Nflag -- leak across invocations on main
   today, DEMONSTRATED (not just structurally argued) by
   tests/test_statics_repro.sh's own cp case: `cp -v a a_out; cp b b_out`
   leaks vflag into the second, unrelated call. -v is the cosmetic member
   of that leaking set; -f (skip the overwrite confirmation), -i, and -p
   change semantics, not just output, which is why all eleven are reset
   here rather than only the one antigravity's repro happened to
   demonstrate -- resetting all eleven from outside makes cp.c's own
   partial internal reset moot, not conflicting with it.
   cb_cp_dnesp (renamed from cp.c's own file-scope `static ssize_t
   dnesp`, pushdne()/popdne()'s recursion-depth index into the file-scope
   `static int dnestack[MAXPATHLEN]` array below it) DOES need the
   rename: unlike dnesp, dnestack itself needs no slot at all, since
   resetting the index back to 0 is sufficient to make every later
   push/pop start from a clean stack regardless of what stale entries
   dnestack still holds above that index -- they get overwritten before
   ever being read again. UNLIKE the flag leakage above, a stale dnesp
   corrupting a second `cp -r`'s recursion bookkeeping is NOT
   demonstrated -- antigravity's own audit flagged it, but the follow-up
   repro suite did not reproduce it, and the user was explicit that it
   remains a plausible, unproven hazard from the audit, not an
   established fact. It is reset here anyway, at zero marginal cost,
   since it is the same file-scope-static storage class and same
   objcopy-rename mechanism as every other slot in this file, but the
   fix is not claimed as verified for this specific slot the way the
   flag leakage and the printcol()/fastcopy()/raw_cat() UAFs are. No
   CB_EXECUTOR_PERSISTENT_HEAP needed: cp.c/utils.c have no function-local
   pointer-cache pattern like cat.c's `buf` or mv.c's `bp`/`blen` (checked
   directly against both files' own source, not assumed). */
extern int Hflag, Lflag, Rflag, Pflag, fflag, iflag, lflag, pflag, rflag,
           vflag, Nflag;
/* cb_ssize_t, not int: cp.c declares dnesp `static ssize_t`, and ssize_t
   is itself typedef'd to cb_ssize_t (libc/include/sys/types.h) -- must
   match the real underlying type exactly, not just its width, since this
   slot's `size` field is sizeof() of whatever type is declared here. */
extern cb_ssize_t cb_cp_dnesp;
static const struct cb_static_slot cp_slots[] = {
    { &Hflag, sizeof(Hflag) },
    { &Lflag, sizeof(Lflag) },
    { &Rflag, sizeof(Rflag) },
    { &Pflag, sizeof(Pflag) },
    { &fflag, sizeof(fflag) },
    { &iflag, sizeof(iflag) },
    { &lflag, sizeof(lflag) },
    { &pflag, sizeof(pflag) },
    { &rflag, sizeof(rflag) },
    { &vflag, sizeof(vflag) },
    { &Nflag, sizeof(Nflag) },
    { &cb_cp_dnesp, sizeof(cb_cp_dnesp) },
};
static const struct cb_static_reset_ops cp_static_reset_ops = {
    { CB_STATIC_RESET_OPS_COMMON, CB_EXECUTOR_COOPERATIVE_INTERRUPT },
    cp_slots, sizeof(cp_slots) / sizeof(cp_slots[0])
};

const struct cb_executor_ops *cb_cp_static_reset_executor(void)
{
    return &cp_static_reset_ops.common;
}
