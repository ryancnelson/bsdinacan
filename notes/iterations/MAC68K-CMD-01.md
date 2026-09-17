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

- **Compiler Idiom Mitigation on `cb_memset`:**
  - `platform/mac68k/CMakeLists.txt` includes `CheckCCompilerFlag` and checks for `-fno-tree-loop-distribute-patterns` via `check_c_compiler_flag`.
  - `cb_memset` is built with `-fno-builtin-memset` and conditionally `-fno-tree-loop-distribute-patterns` if supported by the compiler, avoiding hardcoded compiler assumptions.
- **Targets added to `platform/mac68k/CMakeLists.txt`:**
  - `cb_ls` (`commands/ls.c`, `-Ilibc/include`)
  - `cb_rm` (`upstream/netbsd/bin/rm/rm.c`, `-Icompat/netbsd/include -Ilibc/include`, `-Dmain=cb_rm_main`)
  - `cb_mv` (`upstream/netbsd/bin/mv/mv.c`, `-Icompat/netbsd/include -Ilibc/include -Iupstream/netbsd/bin/mv`, `-Dmain=cb_mv_main`)
  - `cb_cat` (`upstream/netbsd/bin/cat/cat.c`, `-Icompat/netbsd/include -Ilibc/include`, `-Dmain=cb_cat_main`)
  - `cb_cp` (`upstream/netbsd/bin/cp/cp.c`, `-Icompat/netbsd/include -Ilibc/include -Iupstream/netbsd/bin/cp`, `-DSMALL -Dmain=cb_cp_main`)
  - `cb_cp_utils` (`upstream/netbsd/bin/cp/utils.c`, `-Icompat/netbsd/include -Ilibc/include -Iupstream/netbsd/bin/cp`, `-DSMALL`)
  - `cb_strrchr` (`upstream/netbsd/common/lib/libc/string/strrchr.c`, `-Icompat/netbsd/include -Ilibc/include`)
  - `cb_memset` (`upstream/netbsd/common/lib/libc/string/memset.c`, `-Icompat/netbsd/include -Ilibc/include`, `-DCANNEDBSD_BUILDING_LIBC_MEMSET -fno-builtin-memset -fno-tree-loop-distribute-patterns`)
  - `cb_fts` (`libc/cb_fts.c`, `-Icompat/netbsd/include -Ilibc/include`)
  - Command modules: `ls_module.c`, `rm_module.c`, `mv_module.c`, `cat_module.c`, `cp_module.c` added to `add_application(CannedBSD ...)`.
  - Target objects linked into `CannedBSD`.

---

## 3. Empirical Disassembly Verification on m68k at -Os

All six link-name-bound imports (`memset`, `memcmp`, `memcpy`, `memmove`, `strcpy`, `strcmp`) were disassembled from their compiled Retro68 m68k `.c.obj` files using `m68k-apple-macos-objdump -d` and inspected for subroutine calls (`bsr`/`jsr` or recursive standard library symbol references):

1. **`cb_libc_memset`:**
   - Raw `m68k-apple-macos-objdump -d` excerpt:
     ```text
     CMakeFiles/cb_memset.dir/src/upstream/netbsd/common/lib/libc/string/memset.c.obj:     file format elf32-m68k

     Disassembly of section .text.cb_libc_memset:

     00000000 <cb_libc_memset>:
        0:	202f 0004      	movel %sp@(4),%d0
        4:	2200           	movel %d0,%d1
        6:	d2af 000c      	addl %sp@(12),%d1
        a:	2040           	moveal %d0,%a0
        c:	b288           	cmpl %a0,%d1
        e:	6602           	bnes 12 <cb_libc_memset+0x12>
       10:	4e75           	rts
       12:	10ef 000b      	moveb %sp@(11),%a0@+
       16:	60f4           	bras c <cb_libc_memset+0xc>
     ```
   - Branches/calls: Only local branch `bras` and `bnes`. **Zero `bsr`/`jsr` calls.**
2. **`cb_libc_memcmp`:**
   - Disassembly: straight-line longword/byte compare loops.
   - Branches/calls: Only local branches `beqs`, `bnes`, `bras`, `bcss`. **Zero `bsr`/`jsr` calls.**
3. **`cb_libc_memcpy`:**
   - Disassembly: byte copy loop `moveb %a0@(0,%d1:l),%a1@(0,%d1:l)`.
   - Branches/calls: Only local branches `bnes`, `bras`. **Zero `bsr`/`jsr` calls.**
4. **`cb_libc_memmove`:**
   - Disassembly: forward/backward indexed byte moves.
   - Branches/calls: Only local branches `blss`, `bnes`, `bras`. **Zero `bsr`/`jsr` calls.**
5. **`cb_libc_strcpy`:**
   - Disassembly: byte copy and test loop `moveb %d2,%a1@(0,%d1:l); addql #1,%d1; tstb %d2; bnes a; rts`.
   - Branches/calls: Only local branch `bnes`. **Zero `bsr`/`jsr` calls.**
6. **`cb_libc_strcmp`:**
   - Disassembly: byte compare and loop `cmpb %d0,%d1; beqs 2a; ...; bnes 8; rts`.
   - Branches/calls: Only local branches `beqs`, `bnes`, `bras`. **Zero `bsr`/`jsr` calls.**

Conclusion: Every routine compiles to pure straight-line / branch loops with zero subroutine calls or self-recursion at `-Os` on Retro68 m68k.
