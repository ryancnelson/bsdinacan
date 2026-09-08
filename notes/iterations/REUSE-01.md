# REUSE-01: MacPerl and GUSI source reuse audit

Date: 2026-09-07. Base: `c93e2ba` (`origin/main` after fetch).
Branch: `work/REUSE-01`. This is a source audit and documentation change only;
no upstream code is imported, compiled, linked, or substituted into cannedBSD.

## Finding

**Yes: historical MacPerl contains useful work we should inspect before writing
classic-Mac host services. GUSI is the main POSIX/socket library, and MacPerl's
bundled MoreFiles sources offer a smaller, plain-C filesystem starting point.**
Reuse is most promising below the host-volume and network-adapter boundaries.
Neither package supplies cannedBSD's internal-task/VFS contract unchanged.
No direct Retro68 compatibility is claimed: this audit read the actual source,
including compiler and callback dependencies, but did not attempt that port.

The author's [1996 MacPerl article](https://www.foo.be/docs/tpj/issues/vol1_2/tpj0102-0005.html)
explains the original `opendir`/`stat` additions, GUSI's MacTCP/ADSP/PAP/PPC
work, and the lack of ordinary fork/exec. The [2002 MacPerl release notice](https://sourceforge.net/p/macperl/news/)
explicitly identifies GUSI as its sockets/POSIX provider. These are historical
context; the function/module claims below were checked against source.

## Primary source pins and reproduction

Official project repositories:

- [GUSI CVS repository](https://sourceforge.net/p/gusi/code/), module `GUSI2`.
  [Official CVS snapshot](https://sourceforge.net/code-snapshots/cvs/g/gu/gusi.zip)
  downloaded on the audit date; ZIP SHA256:
  `e214d1b5d6aef8d980da831a647a3355f8280bc628747b776685fd7783e72fc4`.
  Checked-out `README` revision 1.46 identifies GUSI 2.2.3, 18Nov02. This is a
  pinned CVS-head snapshot, not an assertion that it is byte-identical to the
  [2.2.3 release archive](https://sourceforge.net/projects/gusi/files/GUSI/2.2.3/).
- [MacPerl CVS repository](https://sourceforge.net/p/macperl/cvs/), module `perl`.
  [Official CVS snapshot](https://sourceforge.net/code-snapshots/cvs/m/ma/macperl.zip)
  downloaded on the audit date; ZIP SHA256:
  `afc00b6009d79d37b7146116e6ce1a44a4f530303b693cb217c9de0604bd6222`.
  Do not infer a released MacPerl version from this mixed CVS tree's generic
  Perl README or its older `macos/README.html`; use the per-file pins below.

A CVS repository has per-file revisions rather than one Git commit. The
appendix records revisions and SHA256 of the checked-out files (including CVS
keyword expansion). Keep those bytes and notices when selecting a future
import; add its own `UPSTREAM.md` entry and hash check at that time.

Reproduction on Linux, after downloading and checking the two ZIP hashes:

```sh
unzip gusi.zip
unzip macperl.zip
cvs -Q -d "$PWD/gusi" co -d gusi-head GUSI2
cvs -Q -d "$PWD/macperl" co -d macperl-head perl
```

The ZIPs contain a few legacy non-UTF-8 project filenames; macOS `unzip` can
reject those names. The examined ASCII-named source/header files were checked
out using Alpine's CVS and hashed. The retained local audit downloads/checkouts
are under `/tmp/cannedbsd-reuse01`; the repository stores this note, not those
archives or old binary compiler libraries.

## Concrete reuse map

Paths in this table are relative to the indicated CVS module. The listed
functions/classes were found in source, rather than inferred from header names.

| cannedBSD need | Source to examine first | What can be reused or learned | Required boundary / limitation |
| --- | --- | --- | --- |
| VFS-03 directory iteration; later explicit host mount | GUSI `src/GUSIMacFile.nw` (`GUSIMacDirectory`), `src/GUSIPOSIX.nw` (`opendir`, `readdir`, `closedir`, `seekdir`, `telldir`), `include/dirent.h` | Existing HFS catalog enumeration, EOF/cursor behavior, and a directory-object interface | Keep the versioned cannedBSD iterator, independent task cursors, allocation cleanup and mutation policy. GUSI's `DIR`/device registry is a different ABI. |
| Smaller host directory implementation | MacPerl `macos/ext/Mac/MoreFiles/MoreFilesSrc/IterateDirectory.c` and `.h` | Plain-C `IterateDirectory`/`FSpIterateDirectory`, `PBGetCatInfoSync`, callback traversal | Callback/recursive traversal is not an `opendir` stream. Reuse the File Manager work behind a cannedBSD node adapter; do not silently adopt traversal or allocation policy. |
| Host path lookup and stable file identity | GUSI `src/GUSIFileSpec.nw`, `src/GUSIFSWrappers.nw`; MoreFiles `FSpCompat.c`, `FullPath.c` | FSSpec construction, parent/volume handling, catalog calls, aliases, full-path presentation | Keep cannedBSD slash paths, mount confinement and per-task cwd. Use volume reference + directory ID + name internally; MoreFiles explicitly warns that full volume names are not unique and paths can change. Alias following must not escape an exported subtree. |
| Host stat, permissions, resource forks and resize | GUSI `src/GUSIMacFile.nw`, `src/GUSIBasics.nw`; MoreFiles `MoreFiles.c`, `FileCopy.c`, `MoreFilesExtras.c` | HFS metadata/error mapping; data/resource-fork access; lock-bit behavior; file-copy and low-level File Manager helpers | Host metadata is an explicit translation, not full Unix metadata. GUSI maps write permission to the Mac lock bit and represents aliases as links. Its `access` code notes that it does not check AppleShare permissions. FS-01's RAMFS semantics are already implemented and should remain. |
| NET-01's eventual real classic-Mac transport | GUSI `src/GUSIMTInet.nw`, `GUSIMTTcp.nw`, `GUSIMTUdp.nw`; `GUSIOpenTransport.nw`, `GUSIOTInet.nw` | Real MacTCP TCP/UDP and Open Transport endpoint/control/completion logic | First retain NET-01's mock stream contract. Adapt one transport beneath it, including partial I/O, EOF, cancellation and wakeups. Do not expose GUSI's descriptor table as cannedBSD descriptors. |
| Later resolver and classic IPC | GUSI `src/GUSIMTNetDB.nw`, `GUSIOTNetDB.nw`, `GUSINetDB.nw`, `GUSIPPC.nw` | MacTCP/OT name lookup, service tables, PPC Toolbox streams | Separate resolver/IPC tasks after transport. The 1996 article discusses ADSP/PAP, but this inspected GUSI2 tree does not establish corresponding GUSI2 backends; do not advertise them based only on the article. |
| IO-01 public polling | GUSI `src/GUSIPOSIX.nw` (`select_once`, timed/indefinite select loops), `GUSISocket.nw`, `GUSITimer.nw` | Readiness polling and async completion/wakeup integration examples | This is `select`, not a verified `poll` implementation. Its loops use GUSI descriptors and `GUSIContext::Yield`. cannedBSD must retain its deterministic peer progress, deadlines, invalid-fd and EOF/HUP contracts. |
| Internal tasks, errno and pipes | GUSI `src/GUSIContext.nw`, `GUSIPThread.nw`, `GUSIDescriptor.nw`, `GUSIPipe.nw` | Useful historical designs for cooperative wakeups and cleanup | GUSI2 uses the Mac Thread Manager and saves/restores errno/h_errno for its own contexts. Threads normally share descriptors; even its optional table-instance switching is not our inheritance/exec model. Do not replace the tested cannedBSD scheduler or bounded pipes wholesale. |
| TERM-01 and guest console | GUSI `src/GUSISIOUX.nw`, `GUSISIOW.nw`, `GUSIDCon.nw` | Examples of connecting descriptor I/O to a classic Mac console and event hooks | They depend on SIOUX/MPW/SIOW/DCon facilities and their event handling. They are not a verified portable termios/PTY implementation. Keep host input/output below our terminal state and root-stack event dispatcher. |

`GUSIFSWrappers` is particularly relevant but not a stand-alone synchronous C
library: its own description and implementation tie pseudo-synchronous calls
to `GUSIContext`, `StartIO`/`FinishIO`, and completion callbacks. Some opens are
intentionally synchronous following historical close-on-exit problems. That
integration work must be accounted for when estimating reuse.

## What MacPerl itself does and does not solve

`macos/PerlGUSIConfig.cp` configures GUSI Internet socket factories and null
file devices. It is concrete evidence of library integration rather than
MacPerl independently implementing every POSIX entry point.

`macos/macish.c` is tightly coupled to the Perl interpreter and its allocator,
IO layer, globals and exception reporting. Its `execv`, `execvp` and `do_exec`
paths report unsupported operations. `Perl_my_popen` uses a temporary file and
`SubLaunch`, with special emulations such as `pwd`/`hostname`; that is not our
concurrent internal-process pipe model. Its global environment and superuser
UID/GID placeholders are also unsuitable substitutes for task-local state.

`macos/HandleSocket.cp` is a handle-backed GUSI socket example, but the checked
in revision explicitly describes its integration as unstable. It is lower
priority than the tested RAMFS and must not be presented as ready-to-use memory
file code.

## License findings, by file family

These are the actual notices in the inspected source, not a blanket license
claim for the entire MacPerl distribution:

| Family | Notice inspected | Reuse implications to preserve in a future import |
| --- | --- | --- |
| GUSI implementation | `README` 1.46 (1992–2001 Matthias Neeracher) and `doc/pod/GUSI.pod` 1.10 (1992–2002) contain the GUSI User License. SourceForge categorizes it as zlib/libpng. | Its text permits use for any purpose and redistribution, disclaims liability, prohibits misrepresenting origin, and requires modified copies to be plainly marked. Preserve the actual text and attribution; do not substitute an assumed modern SPDX text. An about-box acknowledgement is requested beyond the stated obligations, not imposed as a condition there. |
| GUSI BSD-derived public headers | `include/dirent.h`, `include/sys/stat.h` and socket-related headers retain individual notices. Inspected dirent/stat notices retain clauses 1, 2 and 4, explicitly recording removal of advertising clause 3 on 22Jul99. `sys/stat.h` also retains USL provenance. | Preserve source notices, binary-distribution notices/disclaimer and non-endorsement condition. Do not relabel these headers solely with the GUSI notice. Prefer cannedBSD's already-pinned NetBSD public surface when possible. |
| MoreFiles core helpers | `MoreFiles.c`, `FSpCompat.c`, `FullPath.c`, `FileCopy.c` carry Apple sample-code notices; dates vary by file. | They permit incorporation without restriction, place operation responsibility on the user, and require modified redistributed source to identify its Apple-sample origin and alterations rather than masquerading as original DSC Sample Code. Preserve each file's notice; this is not the MacPerl/Perl license. |
| MoreFiles directory iterator | `IterateDirectory.c` / `.h`: 1995–1999 Jim Luther and Apple Computer, Inc. | These inspected notices permit incorporation without restriction and disclaim responsibility; their text differs from the other MoreFiles files. Keep that notice intact rather than copying one generic family label. |
| Perl/MacPerl code without a separate component notice | Top-level `README`, `Artistic`, `Copying`; `macish.c` has an author header and interpreter dependencies | The top-level grant offers GPL version 1 or later, or the bundled original Artistic License. It is not an Artistic 2.0 or BSD grant. A chosen excerpt needs its provenance/license recorded separately; bundled third-party components have their own notices. |

No source import or license choice was made by this audit. The next worker
should pin the smallest selected files and their dependencies, retaining every
applicable notice, instead of copying the whole distribution under one label.

## System 7 / 68K / Retro68 constraints

1. The [GUSI project description](https://sourceforge.net/projects/gusi/) targets
   classic Mac OS 7–9. The source supports 68K and PPC, but the inspected MPW
   makefile uses `-mc68020`; cannedBSD's documented target is 68000-compatible.
   Do not equate historical 68K support with our exact CPU/build configuration.
2. `README` lists CodeWarrior Pro 5+, SC/SCpp 8.8.4d1c1+, and MrC/MrCpp
   4.1.0a6c1+, with STLport for MPW rebuilds. `src/Makefile.nw` generates `.cp`
   and headers from noweb `.nw` files and links old compiler-specific library
   variants. A Retro68 build needs reproducible extraction and CMake targets;
   those prebuilt `.Lib` files are not a verified input to our GNU toolchain.
3. `GUSIBasics.nw` contains compiler-specific register-argument completion
   wrappers, `ONEWORDINLINE` instructions, CFM conditionals and namespace
   assumptions. `GUSIMacFile.nw` explicitly receives 68K completion parameters
   in A0 and saves A5. These require ABI review and a real completion test,
   not just making modern C++ accept the syntax.
4. GUSI's Thread Manager hooks, signal handling and asynchronous waits overlap
   our root-stack Toolbox dispatcher. Toolbox/callback work must respect the
   already-fixed A5/stack/LoadSeg rules. Our single resident CODE segment and
   current context implementation stay authoritative.
5. MacTCP/Open Transport require the corresponding guest networking software;
   System 7 alone is not evidence that either transport is configured. Verify
   the actual guest stack and local test endpoint when that adapter task starts.
6. The plain-C MoreFiles subset avoids GUSI's descriptor/scheduler framework,
   but still uses Toolbox types, `pascal`, structure-alignment conditionals and
   callback conventions. It too requires a pinned-toolchain compile and exact
   System 7 artifact test. It has not passed those gates in this audit.

## Recommended next steps for the coordinator's backlog

Priority is an audit recommendation; this note does not silently claim or
change the shared queue.

1. **Before implementing a Mac host-volume backend, do a small MoreFiles reuse
   spike.** Pin `FSpCompat`, `IterateDirectory` and only their required helpers;
   compile a read-only catalog probe with our existing pinned Retro68 image.
   Test a known directory's names, file IDs, type/size, independent cursors and
   missing-file errors in the exact guest. Make fork mapping explicit. No new global
   cwd or automatic alias escape. Stop at the concrete compiler/API blockers.
2. **Use the audit now in VFS-03 review.** Keep its portable iterator contract
   and RAMFS tests; compare EOF/error and cursor edge cases with GUSI/MoreFiles
   rather than inventing classic File Manager behavior later. VFS-02 executable
   nodes and existing FS-01 semantics still belong to cannedBSD.
3. **After IO-01 and NET-01, prototype one GUSI-derived transport adapter.**
   Select MacTCP or OT based on what the guest actually provides, then wrap
   connect/read/write/readiness behind the mock-tested contract. Reuse tested
   protocol glue before considering a new MacTCP implementation. DNS, listen,
   datagrams and other classic protocols remain separate slices.
4. **Keep process, poll and terminal semantics in the project.** Mine the source
   for cases and low-level mechanisms, but do not replace internal task state,
   exec cleanup, scheduler wakeups or termios with GUSI's global wrappers. Avoid
   importing its console/C runtime wholesale merely to obtain one file call.

## Verification and scope

- Source evidence: official CVS snapshots downloaded, archive SHA256 recorded,
  named headers/modules/license text read, and per-file revisions/hashes below
  checked against the extracted working copies.
- Documentation checks: `git diff --check`; source path/revision/hash table
  checked programmatically against the audit checkout. No synthetic code red
  test was invented for a documentation-only source investigation.
- Runtime tests and Mac guest acceptance: not required for this documentation
  change; no runtime or build configuration was modified. CI status for the
  pushed note is reported separately, never as a claim that GUSI was compiled.

## Checked-out source manifest

All GUSI paths are in `GUSI2`; all MacPerl paths are in `perl`. Hashes describe
CVS-expanded source bytes, not the `,v` repository files.

| Repository | Path | CVS revision | SHA256 |
| --- | --- | --- | --- |
| gusi | `README` | 1.46 | `95fdb249dfc84386a7973b434b32dd39a34e923fccfb7f75f8dbcb563272f6e1` |
| gusi | `doc/pod/GUSI.pod` | 1.10 | `33221375194091b1df54c41edaed46ad5f51b35171872a8b2580b4c04f702f3a` |
| gusi | `src/GUSIBasics.nw` | 1.21 | `ebf794a0b9696e467351ff407f7981a9e25cb112035fa3fb9fca8490fdfc0821` |
| gusi | `src/GUSIMacFile.nw` | 1.29 | `5819d75d4ecae4f8d18be5519aab358d4dde426952962156974d74d76fa97df9` |
| gusi | `src/GUSIFileSpec.nw` | 1.21 | `cce91dccccd0d837e513d44f324b58bf61ead01c2c5598e8e9fa5e77de1be658` |
| gusi | `src/GUSIFSWrappers.nw` | 1.8 | `759b18682e76b5045a1c01aa1dc6fe7ed64dc31c77cc6312552ed45490008a61` |
| gusi | `src/GUSIPOSIX.nw` | 1.27 | `a1f90fdb4f4eaea747318d2a46dc579ff017d15d6a934943b40d24e59bd0d295` |
| gusi | `src/GUSIDescriptor.nw` | 1.16 | `92a8ed53b3c1c2b1364ac1e1828f56f0e708240aa3d06e70726e4f4e84fbd595` |
| gusi | `src/GUSIMTInet.nw` | 1.14 | `54d5708557b8de547ef17570e07a388fb5718cb958b1174f15e55a4b6aef64b2` |
| gusi | `src/GUSIMTTcp.nw` | 1.18 | `8fd0d9f83ebf844ce72923979ce1118bc365f9ad399feaaf4c099c27a70966ce` |
| gusi | `src/GUSIMTUdp.nw` | 1.12 | `460455c186ff7a76a8c20bd8be001deb5947a1e98533d1a2a41c1ae9d254c6ad` |
| gusi | `src/GUSIOpenTransport.nw` | 1.25 | `521a8cf834d5999fb9e376811f7063e34fbd53c633a3ce0fca849adba054cd67` |
| gusi | `src/GUSIOTInet.nw` | 1.12 | `806e1b1465270324007becffa39125ac6f57133e63ad20d0920487791c0dbc69` |
| gusi | `src/GUSIMTNetDB.nw` | 1.8 | `55db0833cf8587d5cddbc0a805a943e1554f9120572a8ab745f0f3283f90ca17` |
| gusi | `src/GUSIOTNetDB.nw` | 1.13 | `50da0fd12943a691ff96165d491e1681b1831e843d947ece28ac1f137f16d56e` |
| gusi | `src/GUSIPPC.nw` | 1.11 | `ab1090be241616027ac4f075b228f2dfeb9cdddd757eb1586716ceaa067bfc9d` |
| gusi | `src/GUSIContext.nw` | 1.26 | `ec779cc3d27e48cfa6a79cb670a0ae963fabf4dbb0ad9671fc0061797c3845bc` |
| gusi | `src/GUSIPThread.nw` | 1.15 | `f8cad2567146d2a385bad9b39b761ddaede68916bb8665d90d62a55b6ad318a8` |
| gusi | `src/GUSIPipe.nw` | 1.13 | `9a9680072edf1f681813850a882a0644f62a38efac0c32bb13cf8757e2c7b64d` |
| gusi | `src/GUSISIOUX.nw` | 1.8 | `1d7879b78133f168a4e02a62d57a274f750bcd03a5ec66d8673048a9fda6ebe6` |
| gusi | `src/GUSISIOW.nw` | 1.2 | `f54e1f90f5ba154fb607e29d60e1f7db604dad66bd9b5301bbf8743fb91973df` |
| gusi | `src/GUSIDCon.nw` | 1.4 | `65193e57ee7bc771ec82a14a600b5ae6a821c8a3c99c9fa37c96ddfae6ff4729` |
| gusi | `src/Makefile.nw` | 1.27 | `eda5030e10bfc478344471d27f544738fb2aef039ad331299d27705d3577e356` |
| gusi | `include/dirent.h` | 1.4 | `578cb7cef222025b5424d1565c350bcf43ddf777f81e5f0267d8253bf46af474` |
| gusi | `include/sys/stat.h` | 1.4 | `06ae42663831a97b4e5a77bc5bf8d2629617121db2802215b805c9184d626e8d` |
| macperl | `README` | 1.2 | `9e5ef1712656389bdc78ccdb72b4d3cbc87512f008ea155544e150c46e074267` |
| macperl | `Copying` | 1.1.1.1 | `9e57f5bc2cfc54e08afc80163c29006f38d9f9c890ebd4efe3c25f0d48b65a52` |
| macperl | `Artistic` | 1.1.1.1 | `b7fd9b73ea99602016a326e0b62e6646060d18febdd065ceca8bb482208c3d88` |
| macperl | `macos/macish.c` | 1.26 | `36fb2df63257ab29508c7b678bc0689dfd68128ca4ec6d22cf35f79aadfb189b` |
| macperl | `macos/PerlGUSIConfig.cp` | 1.2 | `ee6a5c542b769389e173002297a4928d7fb67c37e1702cd2c4988b9d6a16a83f` |
| macperl | `macos/HandleSocket.cp` | 1.1 | `0617fffddf0eef2a2ac84596c7db46d73057bc2b5f8311ceba0645971ee8d26b` |
| macperl | `macos/ext/Mac/MoreFiles/MoreFilesSrc/MoreFiles.c` | 1.1 | `376550f58c0f41d9ec6f956c6c35166fbc9d92471d7334800d5b01f8997de440` |
| macperl | `macos/ext/Mac/MoreFiles/MoreFilesSrc/FSpCompat.c` | 1.1 | `87afaa15b65d5926bae64ee1dffedc7b0247c1fe3f3af932077fa23be643fbbb` |
| macperl | `macos/ext/Mac/MoreFiles/MoreFilesSrc/FullPath.c` | 1.1 | `a2a1929c94bbe3cf3edd06b85480f06f83bd34d769e14dc41f10b39876489159` |
| macperl | `macos/ext/Mac/MoreFiles/MoreFilesSrc/IterateDirectory.c` | 1.1 | `0e2cd7812068d0d2b95d3dc023a2e10114504e8183302ea178c742d4359de882` |
| macperl | `macos/ext/Mac/MoreFiles/MoreFilesSrc/FileCopy.c` | 1.1 | `f297cf815d796314fb87bc58ee44f666c7c4f05ab70dea1965cf747c46db6324` |
| gusi | `src/GUSISocket.nw` | 1.18 | `70a2115b5d258ffa9e0e255987d95db8b07c45428b9020ebc8924c8692624757` |
| gusi | `src/GUSITimer.nw` | 1.13 | `1be4b2b4ef539f86a49f92235e929587f1999a6a02fdcb2cecb52538fb671caa` |
| gusi | `src/GUSINetDB.nw` | 1.13 | `5e35f4b0212903ac2de47d932dd0fde35a2cfe62e1b0f0d74fdc38934f814234` |
| gusi | `include/sys/socket.h` | 1.4 | `704e1e1f3a0a87c7caf76e787e81ca9c5e33d0e13558f4595f2383482b799cbf` |
| macperl | `macos/ext/Mac/MoreFiles/MoreFilesSrc/MoreFilesExtras.c` | 1.1 | `56d03290e6ac1912c8c4eb7b7945b5f154a244fe17a9b01982502bc94d920865` |
| macperl | `macos/ext/Mac/MoreFiles/MoreFilesSrc/IterateDirectory.h` | 1.1 | `6746204bebd5d25c1dabda3d1f4693c67628b7490333fe8346fd12d82dd7ac31` |
| macperl | `macos/ext/Mac/MoreFiles/MoreFilesSrc/FSpCompat.h` | 1.1 | `66fdc2c0f457f42cfd964f73c827ce34f2c83fe83634353fcd966b713e5795d4` |
