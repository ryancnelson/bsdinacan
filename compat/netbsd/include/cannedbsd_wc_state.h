#ifndef CANNEDBSD_WC_STATE_H
#define CANNEDBSD_WC_STATE_H

/*
 * Interim per-command state reset for pinned upstream NetBSD wc.c.
 *
 * Pinned NetBSD wc.c defines 10 file-scope static scalar variables:
 * - Counters: tlinect, twordct, tcharct, tlongest (wc_count_t)
 * - Option flags: doline, doword, dobyte, dochar, dolongest (bool)
 * - Error accumulator: rval (int)
 *
 * Upstream wc.c's main() does not zero these on entry, relying instead on process
 * BSS zeroing in a standard multi-process BSD. In cannedBSD's cooperative single-process
 * guest runtime where multiple commands execute in the same data segment, flags like
 * dobyte (set by wc -c) would persist and pollute subsequent wc invocations.
 *
 * Upstream wc.c unconditionally calls (void)setlocale(LC_ALL, ""); as its very first
 * statement in main() before getopt() processing. We hook setlocale via this
 * translation-unit-scoped header to re-zero all 10 scalar variables on every execution
 * while leaving upstream/netbsd/usr.bin/wc/wc.c 100% byte-for-byte unmodified.
 *
 * NOTE: This correctness depends on a property of THIS specific pinned source (the
 * unconditional setlocale call at entry) rather than our runtime. This is an interim
 * per-command mechanism and will be subsumed when the generalized statics reset
 * mechanism (STATICS-RESET-01) lands.
 *
 * This header must ONLY be applied to wc.c via -include compiler flags.
 */

#define setlocale(cat, loc) \
    ((void)(tlinect = twordct = tcharct = tlongest = 0, \
            doline = doword = dobyte = dochar = dolongest = false, \
            rval = 0), \
     cb_libc_setlocale(cat, loc))

#endif /* CANNEDBSD_WC_STATE_H */
