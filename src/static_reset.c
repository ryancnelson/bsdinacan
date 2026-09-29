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
 *     exercises it today. Managed anyway, as a pair of slots like ls's
 *     and cat's caches, for whenever a second mount makes it reachable.
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
 * STATICS-CACHE-02 found three defects in the first version of this file's
 * treatment of function-local caches, each reproduced by
 * tests/statics_cache_probe.c: cat -B growing from 2048 to 4096 overflowed
 * a retained 2048-byte buf; a second kernel in the same host process wrote
 * through the first kernel's released buf; and two cat tasks in one
 * pipeline shared buf/fb_buf, so `cat big | cat` corrupted its output.
 * Retaining task allocations across task exit (the former
 * CB_EXECUTOR_PERSISTENT_HEAP) kept some pointers valid but never gave
 * each task its own cache. It is gone; every piece of state below is now
 * owned the same way.
 *
 * One mechanism covers all three C storage classes:
 *
 *   - Plain non-static globals (ls.c's `termwidth`/`sortkey`/`rval`/
 *     `blocksize` and its 28 f_* flags; cp.c's `Hflag`/`Lflag`/etc.)
 *     already have external linkage and are named directly.
 *   - File-scope statics (ls.c's `output`; cat.c's/mv.c's/rm.c's getopt
 *     flags and cat's `bsize`; cp.c's `dnesp`) are given an external name
 *     by objcopy --redefine-sym/--globalize-symbol in each build rule. A
 *     -D rename cannot do this: it can rename an identifier but not strip
 *     internal linkage from one declaration (a blanket -Dstatic= turned
 *     raw_cat()'s and fastcopy()'s statics into uninitialized automatics).
 *   - Function-local statics (print.c printcol()'s `array`/`lastentries`;
 *     cat.c raw_cat()'s `buf`/`fb_buf`; mv.c fastcopy()'s `bp`/`blen`)
 *     get the same objcopy treatment, but the compiler chooses their local
 *     symbol name: GCC (host GCC 14 and Retro68's m68k GCC 16) emits
 *     `var.N`, Clang emits `func.var`. tools/globalize-function-static.sh
 *     finds the single matching local symbol in the compiled object and
 *     fails the build if there is not exactly one.
 *
 * Ownership rules, applied identically to every slot:
 *
 * 1. Each task has its own saved copy of every slot, seeded from the
 *    program's compiled defaults (not zero: ls.c's `termwidth` is 80 and
 *    printcol()'s `lastentries` is -1).
 * 2. start_or_resume copies the task's saved values into the live
 *    globals, runs the task until it yields, copies the live values back
 *    out, then restores the live globals to the compiled defaults. Between
 *    any two task runs the live globals therefore hold exactly their
 *    compiled defaults, so interleaved tasks never see each other's state
 *    (a cat blocked in write() keeps its own fb_buf contents while another
 *    cat reads into fb_buf), and defaults captured by a later kernel's
 *    program are still the real compiled values.
 * 3. A task that has exited is not saved back: its pointer slots may name
 *    memory its exit just released. Its heap allocations are released at
 *    exit like any other task's, so a cache never outlives its task, and
 *    the next task starts with NULL/-1/0 rather than a stale pointer --
 *    in the same kernel or a later one.
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
    execution->saved_size = 0;
    {
        size_t i;
        for (i = 0; i < ops->slot_count; i++)
            execution->saved_size += ops->slots[i].size;
    }
    /* Captured once per program from the live slots. Outside
       sr_start_or_resume the live slots always hold their compiled
       defaults (rule 2 in this file's top comment), so this is correct
       for the first kernel and for every kernel created after it. */
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
       task while this one is suspended or gone (TEE-STATE-01-design.md's
       rule 5), and leave the compiled defaults, not zeros, for whatever
       runs or is created next. */
    cursor = common->program->static_defaults;
    for (i = 0; i < ops->slot_count; i++) {
        size_t j;
        unsigned char *slot = ops->slots[i].address;
        for (j = 0; j < ops->slots[i].size; j++)
            slot[j] = cursor[j];
        cursor += ops->slots[i].size;
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
   on a later listing) plus print.c's printcol() cache slots -- see this
   file's own top comment. cb_ls_output is
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
/* STATICS-CACHE-02: print.c printcol()'s own function-local `static
   FTSENT **array` and `static int lastentries = -1`, renamed by the build
   (tools/globalize-function-static.sh). Declared through a plain object
   pointer: this file cannot see FTSENT, and only the storage size matters
   to a slot. lastentries' -1 default is what makes each task's first
   printcol() allocate its own array. */
extern void *cb_ls_printcol_array;
extern int cb_ls_printcol_lastentries;
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
    { &cb_ls_printcol_array, sizeof(cb_ls_printcol_array) },
    { &cb_ls_printcol_lastentries, sizeof(cb_ls_printcol_lastentries) },
};
static const struct cb_static_reset_ops ls_static_reset_ops = {
    { CB_STATIC_RESET_OPS_COMMON, CB_EXECUTOR_COOPERATIVE_INTERRUPT },
    ls_slots, sizeof(ls_slots) / sizeof(ls_slots[0])
};

const struct cb_executor_ops *cb_ls_static_reset_executor(void)
{
    return &ls_static_reset_ops.common;
}

/* cat.c's getopt flags (never reset at the top of main) plus its own
   accumulated exit-status variable (cb_cat_rval) reset per invocation,
   and -- STATICS-CACHE-02 -- raw_cat()'s buffer state: file-scope `bsize`
   and function-local `buf` and `fb_buf[BUFSIZ]`. These three are one
   unit: raw_cat() only sizes and allocates buf while buf is NULL, and
   falls back to pointing buf at fb_buf. Resetting bsize alone once left a
   stale buf reading 0 bytes; retaining buf alone let a later -B 4096
   read into an earlier 2048-byte buffer; sharing fb_buf let two cats in
   one pipeline overwrite each other's pending data. As slots, every cat
   task starts with bsize 0 and buf NULL and keeps its own fb_buf contents
   while suspended. */
extern int cb_cat_bflag, cb_cat_eflag, cb_cat_fflag, cb_cat_lflag,
           cb_cat_nflag, cb_cat_sflag, cb_cat_tflag, cb_cat_vflag,
           cb_cat_rval;
extern size_t cb_cat_bsize;
extern char *cb_cat_raw_cat_buf;
/* 1024 is libc/include/stdio.h's BUFSIZ, fb_buf's declared size. */
extern char cb_cat_raw_cat_fb_buf[1024];
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
    { &cb_cat_bsize, sizeof(cb_cat_bsize) },
    { &cb_cat_raw_cat_buf, sizeof(cb_cat_raw_cat_buf) },
    { cb_cat_raw_cat_fb_buf, sizeof(cb_cat_raw_cat_fb_buf) },
};
static const struct cb_static_reset_ops cat_static_reset_ops = {
    { CB_STATIC_RESET_OPS_COMMON, CB_EXECUTOR_COOPERATIVE_INTERRUPT },
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
   with no correctness impact if stale. STATICS-CACHE-02: fastcopy()'s own
   function-local `static char *bp`/`static blksize_t blen` buffer cache
   is managed as a pair, like cat.c's buf/bsize above (blksize_t is
   int32_t in libc/include/sys/types.h). */
extern int cb_mv_fflg, cb_mv_hflg, cb_mv_iflg, cb_mv_vflg;
extern char *cb_mv_fastcopy_bp;
extern int32_t cb_mv_fastcopy_blen;
static const struct cb_static_slot mv_slots[] = {
    { &cb_mv_fflg, sizeof(cb_mv_fflg) },
    { &cb_mv_hflg, sizeof(cb_mv_hflg) },
    { &cb_mv_iflg, sizeof(cb_mv_iflg) },
    { &cb_mv_vflg, sizeof(cb_mv_vflg) },
    { &cb_mv_fastcopy_bp, sizeof(cb_mv_fastcopy_bp) },
    { &cb_mv_fastcopy_blen, sizeof(cb_mv_fastcopy_blen) },
};
static const struct cb_static_reset_ops mv_static_reset_ops = {
    { CB_STATIC_RESET_OPS_COMMON, CB_EXECUTOR_COOPERATIVE_INTERRUPT },
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
   not assumed) -- no cache slots needed. */
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
   cache slots: cp.c/utils.c have no function-local pointer cache like
   cat.c's `buf` or mv.c's `bp`/`blen`. utils.c copy_file()'s `static char
   buf[MAXBSIZE]` is scratch filled and drained by one read/write pair; it
   is shared between cp tasks, which is safe only while that write cannot
   yield (STATICS-CACHE-02 records this as unverified for blocking
   destinations). */
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
