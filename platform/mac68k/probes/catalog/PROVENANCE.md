# Explicitly altered MoreFiles derivative

`catalog.c` is owned adaptation, not unchanged upstream or original DSC Sample
Code. Original files remain byte-identical in `upstream/morefiles/`, verified
by its `pins.json` and documented in `UPSTREAM.md`.

Source: official MacPerl CVS snapshot
<https://sourceforge.net/code-snapshots/cvs/m/ma/macperl.zip>, archive SHA256
`afc00b6009d79d37b7146116e6ce1a44a4f530303b693cb217c9de0604bd6222`.
Module: `perl/macos/ext/Mac/MoreFiles/MoreFilesSrc/`.

- `IterateDirectory.c` revision 1.1: `IterateDirectoryLevel`, lines 63–117,
  supplies the enumerate-by-positive-index pattern and temporary catalog name.
  The revised code only scans one directory and copies metadata immediately.
  Jim Luther/Apple 1995–1999 notice permits incorporation without restriction,
  as-is and at the user's responsibility; retained in the derivative.
- `MoreFilesExtras.c` revision 1.1: `GetCatInfoNoName`, lines 376–401,
  supplies the distinction between named lookup (index 0) and directory-ID
  lookup (index -1), including an empty name buffer for the latter.
  Apple 1992–1999 sample-code permission requires identifying altered source;
  the derivative explicitly identifies its origin/changes and retains notice.
- `MoreFiles.h` revision 1.1 documents `afpAccessDenied = -5000` (for example
  line 92). Used only as the injected error value; unlike upstream enumeration,
  the derivative propagates it. This is not a real AFP permission test.
- `perl/macos/ext/Mac/Files/Files.pm`, revision 1.18, line 177, independently
  declares `kioFlAttribDir` as `0x10`. SHA256:
  `4beb123f43d6bf12afa2b3b120920f2b21e7d88cdaf34f224e155d6008875c61`.
  This file is inspected provenance for a numeric constant, not imported code.

Deliberate changes: explicit nonzero volume/directory IDs; ordinary owned C
callback; no recursion, global current-directory changes or input path parsing;
zero-initialized real SDK parameter blocks; caller-copyable names/metadata;
positive index/capacity bounds; propagate every native error except enumeration
`fnfErr`; no deletion/write operations. The public query seam allows testing
native error and index bounds using the SDK's actual `CInfoPBRec`, without
invented host-side substitutes. It is a synchronous root-stack interface.

The existing pinned SDK's `Files.h` exposes Multiversal definitions of
`CInfoPBRec`, `HFileInfo`, `DirInfo`, `HParamBlockRec` and their trap declarations.
No Toolbox record, packing, trap glue, `pascal` keyword or CPU ABI is redefined.
The owned callback is not registered with the Toolbox.

The host-only fixture generator uses the pinned image's libhfs, Copyright
1996–1998 Robert Leslie, GPL-2.0-or-later. Its source is explicitly GPL-2.0-or-later
compatible. The linked generator is temporary, excluded from published probe
archives, and never linked into the Mac app. Time is fixed only inside that
fixture generator to make creation/modification timestamps reproducible.

## Protected-fixture desktop initialization

The host-only `fixture_builder.c` additionally adapts the empty Desktop DB/DF
initialization in cdrtools `mkisofs/desktop.c`, SCCS version 1.10 (2009-07-09).
Inspected pinned source mirror:
<https://github.com/Distrotech/cdrtools/blob/8adb0d06e070464f13d017b1de7187bf23dacd87/mkisofs/desktop.c>.
Source SHA256: `ae26130a602c979ab60d4e7b15c268590d12d2e7035d7807019bca73064b39ad`.
Original copyright James Pearson 1997–2000 and J. Schilling 2000–2009,
GPL-2.0-or-later; attribution and license are retained in the already GPL-compatible
host generator. `COPYING` reproduces the accompanying upstream GPL-2.0 text
from `mkisofs/COPYING` at the same pinned revision. That license applies to the
host fixture generator; it does not replace the retained MoreFiles notices or
relicense the separate Mac probe. No code from this initializer enters the Mac app.

The source initializes invisible `Desktop DB` (BTFL/DMGR) with an empty database
header in one volume clump and `Desktop DF` (DTFL/DMGR) as an empty file, intended
for read-only HFS media. The adaptation uses actual libhfs create/write/stat/setattr
APIs rather than mkisofs's private output buffer. Big-endian disk bytes are
written explicitly, the clump size is bounded, and every allocation/write/close
is checked. This adds two explicit manifest entries without moving the six
original fixture objects or changing their IDs. It remains a candidate pending guest verification,
not a substitute for checking warning-free boot and post-shutdown fixture hash.

## Directory ID error contract

Apple's *Inside Macintosh: Files*, File Manager pp. 2-191–2-192,
[PBGetCatInfo](https://leopard-adc.pepas.com/documentation/mac/pdf/Files/File_Manager.pdf),
distinguishes index 0 named file/directory lookup from negative-index directory-ID
lookup. Negative index ignores the name and selects only a directory. This is
why a regular file's CNID cannot be passed as though it were a directory ID.
Original MoreFiles `IterateDirectory` first used named lookup and imposed
`dirNFErr` after seeing a file; this ID-only derivative instead preserves the
native lookup error. The System 7 fixture returned `fnfErr` (-43) for its file
CNID. Its test checks that exact native behavior against an independent raw
PBGetCatInfo call, plus zero callbacks; no error whitelist was introduced.
