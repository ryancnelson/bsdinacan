# VFS-02 integration review

The candidate merges reviewed `cc27c7f` (Woodpecker #192, all three
workflows green) into current main, starting from `553b10f` and including
the subsequent roadmap notes at `0423c31`.

The two content conflicts preserve VFS-03's directory cleanup and every
existing test fixture while removing the replaced basename-only program
lookup. Public API fields retain their established order, directory
operations remain after the dirname accessor, and `CB_MAX_PROGRAMS`
remains 64. The new executable node type is appended without changing
existing node values. The legacy `fix_core.py` worker edit script is not
included in the integration tree.

The shared `tests/vfs_executable_probe.c` contains the existing native
regression body, linked once into both native tests and the Mac app. It
checks executable metadata, rejected truncation and writes, preserved
metadata after rejection, nonexistent paths and overlong names. Both
harnesses explicitly register the probe and check its status. The shared
Mac acceptance suite now expects 30 records including contexts. The
remaining native lifecycle tests retain the four independently injected
argv/environment allocation failures, unlink during allocation, and
program destruction checks before task teardown. The caller's fixture
registration is now explicitly checked too.

Host protocol tests pass. Full exact-commit CI and exact-artifact System 7
execution remain pending for this candidate. The coordinator owns guest
acceptance; no guest execution or main merge is claimed here.
