# DOC-FORMAT-01 — accepted formatter documentation

Base: `ad95f3d5957f76a95df4483602db316531306f86` (accepted main).
Scope: documentation only. LIBC.md still claimed that formatted output supported
only strings and literal percent signs. The actual accepted format_output in
libc/cb_libc.c implements signed int, unsigned int/long/long-long and optional
literal widths 1–32. Its rejection paths exclude flags, precision, dynamic
width, signed long/long-long and size_t conversions.

Compared the documented contract against each dispatch branch and the bounded
width parser. Existing tests/libc_format_probe.c covers signed boundaries,
width padding/non-truncation, string widths and malformed formats. No new
runtime test is needed for this prose correction. Do not describe unmerged
ls/tee formatter extensions as accepted. Exact documentation CI and independent
review remain required; new Mac/Solaris execution is not required because no
executable source, build configuration or acceptance cases changed.
