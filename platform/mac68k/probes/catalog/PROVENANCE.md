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
