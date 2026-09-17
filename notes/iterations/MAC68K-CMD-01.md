# MAC68K-CMD-01: Add milestone file manipulation commands and libc routines to Mac68k CMake build

- **Status:** In progress (pushed for Woodpecker compilation verification).
- **Base SHA:** `0d4c686` (`work/MAC68K-INC-01`).
- **Branch:** `work/MAC68K-CMD-01`.
- **Scope:** Add `cb_ls`, `cb_rm`, `cb_mv`, `cb_cat`, `cb_cp` (and `cb_cp_utils`), `cb_strrchr`, `cb_memset`, `cb_fts`, and their command module wrappers to `platform/mac68k/CMakeLists.txt`, mirroring the exact Linux include and define configurations.
- **Hypothesis:** Adding compile targets for all five milestone file manipulation commands to `platform/mac68k/CMakeLists.txt` restores link integrity for `CannedBSD` and establishes true System 7 / Retro68 m68k compilation coverage for the full milestone command suite.

---

## 1. Red (First-Failure Diagnostic Measurement)

- **Command:** Woodpecker `mac68k` / `build-mac68k`, pipeline 453 step 4724.
- **Observed first failure:**
  ```text
  /Retro68-build/toolchain/m68k-apple-macos/bin/ld.real: CMakeFiles/CannedBSD.dir/woodpecker/src/github.com/ryancnelson/bsdinacan/src/programs.c.obj:(.rodata+0x34): undefined reference to `cb_ls_program'
  /Retro68-build/toolchain/m68k-apple-macos/bin/ld.real: CMakeFiles/CannedBSD.dir/woodpecker/src/github.com/ryancnelson/bsdinacan/src/programs.c.obj:(.rodata+0x38): undefined reference to `cb_rm_program'
  /Retro68-build/toolchain/m68k-apple-macos/bin/ld.real: CMakeFiles/CannedBSD.dir/woodpecker/src/github.com/ryancnelson/bsdinacan/src/programs.c.obj:(.rodata+0x3c): undefined reference to `cb_mv_program'
  /Retro68-build/toolchain/m68k-apple-macos/bin/ld.real: CMakeFiles/CannedBSD.dir/woodpecker/src/github.com/ryancnelson/bsdinacan/src/programs.c.obj:(.rodata+0x40): undefined reference to `cb_cat_program'
  /Retro68-build/toolchain/m68k-apple-macos/bin/ld.real: CMakeFiles/CannedBSD.dir/woodpecker/src/github.com/ryancnelson/bsdinacan/src/programs.c.obj:(.rodata+0x44): undefined reference to `cb_cp_program'
  collect2: error: ld returned 1 exit status
  gmake[2]: *** [CMakeFiles/CannedBSD.dir/build.make:1024: CannedBSD.code.bin] Error 1
  ```

---

## 2. Green (Implementation & Verification)

- **Targets added to `platform/mac68k/CMakeLists.txt`:**
  - `cb_ls` (`commands/ls.c`, `-Ilibc/include`)
  - `cb_rm` (`upstream/netbsd/bin/rm/rm.c`, `-Icompat/netbsd/include -Ilibc/include`, `-Dmain=cb_rm_main`)
  - `cb_mv` (`upstream/netbsd/bin/mv/mv.c`, `-Icompat/netbsd/include -Ilibc/include -Iupstream/netbsd/bin/mv`, `-Dmain=cb_mv_main`)
  - `cb_cat` (`upstream/netbsd/bin/cat/cat.c`, `-Icompat/netbsd/include -Ilibc/include`, `-Dmain=cb_cat_main`)
  - `cb_cp` (`upstream/netbsd/bin/cp/cp.c`, `-Icompat/netbsd/include -Ilibc/include -Iupstream/netbsd/bin/cp`, `-DSMALL -Dmain=cb_cp_main`)
  - `cb_cp_utils` (`upstream/netbsd/bin/cp/utils.c`, `-Icompat/netbsd/include -Ilibc/include -Iupstream/netbsd/bin/cp`, `-DSMALL`)
  - `cb_strrchr` (`upstream/netbsd/common/lib/libc/string/strrchr.c`, `-Icompat/netbsd/include -Ilibc/include`)
  - `cb_memset` (`upstream/netbsd/common/lib/libc/string/memset.c`, `-Icompat/netbsd/include -Ilibc/include`, `-DCANNEDBSD_BUILDING_LIBC_MEMSET -fno-builtin-memset`)
  - `cb_fts` (`libc/cb_fts.c`, `-Icompat/netbsd/include -Ilibc/include`)
  - Command modules: `ls_module.c`, `rm_module.c`, `mv_module.c`, `cat_module.c`, `cp_module.c` added to `add_application(CannedBSD ...)`.
  - Target objects linked into `CannedBSD`.
