# UPSTREAM-AUDIT-01

## Objective
Inventory actually imported NetBSD source files from `UPSTREAM.md` and build wiring. Verify each local SHA against the declared pinned source, retained license notice, and compile-time symbol renaming. Map to existing source fence tests and distinguish `cannedBSD`-owned commands from imports.

## Context
- **Base SHA (worktree origin):** `2bc2008`
- **Pinned Upstream Revision:** `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c` (NetBSD)

## Import Inventory & Verification (Read-Only)

### `cannedBSD`-Owned Commands vs. Imports
- **`cannedBSD`-Owned:** `shell`, `wc`, `cat`, `tr`, `true`, `false`, and the user-facing `echo` (defined in `src/programs.c` and `commands/`).
- **Imports:** `yes`, `printenv`, `dirname`, `basename`, `head`, and the imported `netbsdecho` (registered distinct from the built-in `echo`).

### Execution Evidence
Locally running the automated source fence tests on macOS:
```
$ BUILD_PATH=build ./tests/test_netbsd_source.sh
FAIL: pinned NetBSD yes was not compiled as a command object
Exit: 1
```
This local failure is due to the macOS `nm` utility prepending an underscore `_` to symbols (e.g. `_cb_yes_main`), failing the strict regex `[[:space:]]T[[:space:]]+cb_yes_main$`. A Mac `nm` underscore failure is **failed/unsupported locally** and is *not* considered passing the Linux source fence. The exact green Woodpecker source-check evidence was observed passing in CI Pipeline 378 on Linux, confirming strict compliance.

### Verified Imports

1. **`yes`**
   - **Path:** `upstream/netbsd/usr.bin/yes/yes.c`
   - **SHA-256:** `f57930fc157302e11ea0ec67e3afe42b96c512d0f3243a2fde5425bd5c9812d8`
   - **License:** Regents 3-clause.
   - **Renaming:** `-Dmain=cb_yes_main`

2. **`printenv`**
   - **Path:** `upstream/netbsd/usr.bin/printenv/printenv.c`
   - **SHA-256:** `d355c07fc5a351d38e2f8552899b456f1300a61408ebf2e2af47c5f52de974db`
   - **License:** Regents 3-clause.
   - **Renaming:** `-Dmain=cb_printenv_main`

3. **`dirname` (Command and Libc)**
   - **Paths:** `upstream/netbsd/usr.bin/dirname/dirname.c` (`839bee201d87fd1024fac687ce8b26963b33919b6c1086373d7ddfdaf6882cd9`) and `upstream/netbsd/lib/libc/gen/dirname.c` (`05ad1f66a7a5a4ceee33fe767a3410c60aa76e4ed670b5d57ab9198a0a2a892b`)
   - **License:** Command is Regents 3-clause; **Libc `dirname.c` is NetBSD Foundation 2-clause** (retained verbatim as declared in `UPSTREAM.md` line 167).
   - **Renaming:** `-Dmain=cb_dirname_main` and `-Ddirname=cb_libc_dirname_upstream`

4. **`basename` (Command and Libc)**
   - **Paths:** `upstream/netbsd/usr.bin/basename/basename.c` (`717fc4757e656e2ff70e39e6aee6e8e79aca06b3459caff99f1908a26ee670bd`) and `upstream/netbsd/lib/libc/gen/basename.c` (`f6202a8d1a89118f4743a2aa5880bc985ed8a6ca01b1c4ca654d789ff14adb87`)
   - **License:** Command is Regents 3-clause; **Libc `basename.c` is NetBSD Foundation 2-clause**.
   - **Renaming:** `-Dmain=cb_basename_main` and `-Dbasename=cb_libc_basename_upstream`

5. **`echo`**
   - **Path:** `upstream/netbsd/bin/echo/echo.c`
   - **SHA-256:** `06d241a7305b4631b5154fe2ba72b433199e945f573dea46b9b0f17a4eeaed04`
   - **License:** Regents 3-clause.
   - **Renaming:** `-Dmain=cb_netbsdecho_main`

6. **`head`**
   - **Path:** `upstream/netbsd/usr.bin/head/head.c`
   - **SHA-256:** `33745355975529ef5b33256578bee822dae8e80fbb27dc615a1761385d7eb18a`
   - **License:** Regents 3-clause.
   - **Renaming:** `-Dmain=cb_head_main`

7. **Libc String Handlers**
   - `strlen.c` (`08969942df6b9b53bb3500e39ec47f514b876809202383088ac9d36e007d64e1`)
   - `strcmp.c` (`f06298e20a2c02e9fbe11aeb06123d8b2ad6c8d5a9a04ad68fdae2aa142524f6`)
   - `strcpy.c` (`36754cc692e0df72390e24cfd585a1fb9343257ae6edc4052771b1e5a47c9fad`)
   - `memcpy.c` (`27954650049d23535119c13fec0d333929e6ad17bb80d3c5e33ac9938f57f2a4`) wrapping `bcopy.c` (`915b194678b2855a522755dad71ae4fb4f366d3f0518fd089bc6c2cb35722c44`)
   - `memmove.c` (`a28ca02301f0800d67b1d8b35e1d1021b7600deb6b7e82f4179023aa22f7756b`) wrapping `bcopy.c`
   - `memcmp.c` (`a926ba117d7a044631da27bc301769607072bdf42995e4d49dbc00139643d5ce`)
   - `strchr.c` (`ebe71501c3aa96b35445642eeb72ab6c73f0fa561ce83b9f78d4c0e06c155cb9`)
   - **Verification:** All local SHAs EXACTLY match their declared pins. `bcopy.c` retains the required copyright block (covering `memcpy` and `memmove` which act purely as wrappers defining `MEMCOPY`/`MEMMOVE`). All other files explicitly include 3-clause Regents or 2-clause Foundation notices.
   - **Renaming:** 
     - `strchr.c` is correctly renamed via namespace compatibility: the standard `<string.h>` header simply `#define`s `strchr` to `cb_libc_strchr`, and `compat/netbsd/include/namespace.h` shields NetBSD's internal aliases from being exposed. This relies on clean header inclusion rather than a blanket `-D/CANNEDBSD` flag.
     - Other string functions use `-DCANNEDBSD_BUILDING_LIBC_*` macro defenses to avoid host conflicts.

8. **`strtoimax` & `_strtol.h`**
   - **Paths:** `upstream/netbsd/common/lib/libc/stdlib/strtoimax.c` (`c2476abb39e6ab8dd1fe2d745aeef66309d6cc90adb10f4beee64ae38c1f1ae5`) and `upstream/netbsd/common/lib/libc/stdlib/_strtol.h` (`f6ad43531aab239f6bb1c669e01b9df9ebc0c3e0a035cc27a89b74d404dbb74c`)
   - **License:** 2-clause DragonFly/Citrus in `strtoimax.c`; 3-clause Regents in `_strtol.h`.
   - **Renaming:** `-Dstrtoimax=cb_libc_strtoimax`

## Concrete Mismatches
- **NO concrete source mismatches found.** Every file in the upstream hierarchy precisely matches its declared `UPSTREAM.md` configuration and all automated Linux source-fence tests assert boundary safety.
