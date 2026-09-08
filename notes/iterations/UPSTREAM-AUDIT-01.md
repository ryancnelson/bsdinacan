# UPSTREAM-AUDIT-01

## Objective
Inventory actually imported NetBSD source files from `UPSTREAM.md` and build wiring, verify each local SHA against the declared pinned source, retained license notice, and compile-time symbol renaming. Map to existing source fence tests and distinguish `cannedBSD`-owned commands from imports.

## Import Inventory & Verification (Read-Only)

All verifications below rely on direct inspection and automated hash scripts (`test_netbsd_source.sh`, `test_netbsd_libc_source.sh`) which check provenance, SHAs, and symbol boundaries using `nm` (with expected Mac environment variance noted, e.g. `_` prefix).

### `cannedBSD`-Owned Commands vs. Imports
- **Imports:** `yes`, `printenv`, `dirname`, `basename`, `echo`, `head` (and their supporting libc equivalents).
- **`cannedBSD`-Owned:** `wc`, `shell` (and their respective module wrappers).

### Verified Imports

1. **`yes`**
   - **Path:** `upstream/netbsd/usr.bin/yes/yes.c`
   - **SHA-256:** `f57930fc157302e11ea0ec67e3afe42b96c512d0f3243a2fde5425bd5c9812d8` (MATCH)
   - **License:** Regents 3-clause (MATCH, retained)
   - **Renaming:** `-Dmain=cb_yes_main`
   - **Test Mapping:** Covered in `test_netbsd_source.sh`.

2. **`printenv`**
   - **Path:** `upstream/netbsd/usr.bin/printenv/printenv.c`
   - **SHA-256:** `d355c07fc5a351d38e2f8552899b456f1300a61408ebf2e2af47c5f52de974db` (MATCH)
   - **License:** Regents 3-clause (MATCH, retained)
   - **Renaming:** `-Dmain=cb_printenv_main`
   - **Test Mapping:** Covered in `test_netbsd_source.sh`.

3. **`dirname`**
   - **Paths:** 
     - Command: `upstream/netbsd/usr.bin/dirname/dirname.c` (SHA: `839bee201d87fd1024fac687ce8b26963b33919b6c1086373d7ddfdaf6882cd9`) (MATCH)
     - Libc: `upstream/netbsd/lib/libc/gen/dirname.c` (SHA: `05ad1f66a7a5a4ceee33fe767a3410c60aa76e4ed670b5d57ab9198a0a2a892b`) (MATCH)
   - **License:** Regents 3-clause (MATCH, retained)
   - **Renaming:** `-Dmain=cb_dirname_main` and `-Ddirname=cb_libc_dirname_upstream`
   - **Test Mapping:** Covered in `test_netbsd_source.sh` & `test_netbsd_libc_source.sh`.

4. **`basename`**
   - **Paths:**
     - Command: `upstream/netbsd/usr.bin/basename/basename.c` (SHA: `717fc4757e656e2ff70e39e6aee6e8e79aca06b3459caff99f1908a26ee670bd`) (MATCH)
     - Libc: `upstream/netbsd/lib/libc/gen/basename.c` (SHA: `f6202a8d1a89118f4743a2aa5880bc985ed8a6ca01b1c4ca654d789ff14adb87`) (MATCH)
   - **License:** Regents 3-clause (MATCH, retained)
   - **Renaming:** `-Dmain=cb_basename_main` and `-Dbasename=cb_libc_basename_upstream`
   - **Test Mapping:** Covered in `test_netbsd_source.sh` & `test_netbsd_libc_source.sh`.

5. **`echo`**
   - **Path:** `upstream/netbsd/bin/echo/echo.c`
   - **SHA-256:** `06d241a7305b4631b5154fe2ba72b433199e945f573dea46b9b0f17a4eeaed04` (MATCH)
   - **License:** Regents 3-clause (MATCH, retained)
   - **Renaming:** `-Dmain=cb_netbsdecho_main`
   - **Test Mapping:** Covered in `test_netbsd_source.sh`.

6. **`head`**
   - **Path:** `upstream/netbsd/usr.bin/head/head.c`
   - **SHA-256:** `33745355975529ef5b33256578bee822dae8e80fbb27dc615a1761385d7eb18a` (MATCH)
   - **License:** Regents 3-clause (MATCH, retained)
   - **Renaming:** `-Dmain=cb_head_main`
   - **Test Mapping:** Covered in `test_netbsd_source.sh`.

7. **Libc String Handlers**
   - `strlen.c` (SHA: `08969942df6b9b53bb3500e39ec47f514b876809202383088ac9d36e007d64e1`)
   - `strcmp.c` (SHA: `f06298e20a2c02e9fbe11aeb06123d8b2ad6c8d5a9a04ad68fdae2aa142524f6`)
   - `strcpy.c` (SHA: `36754cc692e0df72390e24cfd585a1fb9343257ae6edc4052771b1e5a47c9fad`)
   - `memcpy.c` (Wrapper SHA: `27954650049d23535119c13fec0d333929e6ad17bb80d3c5e33ac9938f57f2a4`)
   - `bcopy.c` (Implementation SHA: `915b194678b2855a522755dad71ae4fb4f366d3f0518fd089bc6c2cb35722c44`)
   - `memmove.c` (Wrapper SHA: `a28ca02301f0800d67b1d8b35e1d1021b7600deb6b7e82f4179023aa22f7756b`)
   - `memcmp.c` (SHA: `a926ba117d7a044631da27bc301769607072bdf42995e4d49dbc00139643d5ce`)
   - `strchr.c` (SHA: `ebe71501c3aa96b35445642eeb72ab6c73f0fa561ce83b9f78d4c0e06c155cb9`)
   - **Verification:** All local SHAs EXACTLY match their declared pins.
   - **Licenses:** `bcopy.c` retains the required copyright block (covering `memcpy` and `memmove` which act purely as wrappers defining `MEMCOPY` and `MEMMOVE`, exactly as declared in `UPSTREAM.md`). All other libc sources contain explicit copyright notices.
   - **Renaming:** Controlled via `-DCANNEDBSD_BUILDING_LIBC_*` flags and explicit `-D` flags (e.g. `-Dstrlen=cb_libc_strlen`).
   - **Test Mapping:** Covered entirely by `test_netbsd_libc_source.sh`.

8. **`strtoimax` & `_strtol.h`**
   - **Paths:** `strtoimax.c` and `_strtol.h`
   - **SHAs:** `c2476abb39e6ab8dd1fe2d745aeef66309d6cc90adb10f4beee64ae38c1f1ae5` and `f6ad43531aab239f6bb1c669e01b9df9ebc0c3e0a035cc27a89b74d404dbb74c` respectively (MATCH).
   - **License:** 2-clause DragonFly/Citrus in `strtoimax.c`; 3-clause Regents in `_strtol.h`. Both retained intact.
   - **Renaming:** `-Dstrtoimax=cb_libc_strtoimax`.
   - **Test Mapping:** Covered in `test_netbsd_libc_source.sh`.

## Concrete Mismatches
- **NO concrete source mismatches found.** Every file in `upstream/netbsd/` precisely matches the documented metadata in `UPSTREAM.md`.
- No undocumented upstream files exist.
- Build flags accurately perform the documented renaming.
- Automated `nm` symbol validation scripts enforce the API boundary for all checked-in files.
