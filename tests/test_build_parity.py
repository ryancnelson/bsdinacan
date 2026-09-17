#!/usr/bin/env python3
"""Fail when the Linux and mac68k builds diverge in a way no compiler catches.

`Makefile` and `platform/mac68k/CMakeLists.txt` describe per-source include
paths, defines and flags independently. Every divergence found so far produced a
different symptom, and the dangerous one produced none at all:

  * `cb_file_probe` lacked `-Icompat/netbsd/include` on mac68k only, so
    `MAXBSIZE` was undeclared there -- a compile error, caught immediately but
    only after a push.
  * five commands were registered in `src/programs.c` with no mac68k target at
    all, so `cb_cat_program` and four siblings were undefined -- a link error,
    which hid behind the compile error above.
  * `cb_memset` was compiled for m68k with `-fno-builtin-memset` but without
    the GCC-only `-fno-tree-loop-distribute-patterns` that `UPSTREAM.md`'s
    standing constraint requires. That one has NO SYMPTOM: the resulting
    self-recursion is reachable only at runtime, on the one target whose
    runtime gate is currently offline.

The third is why this check exists. The first two announce themselves; a
missing idiom-recognition flag does not, and no build, test or sanitizer in
this project can see it.

This checks invariants rather than comparing whole flag sets, because the two
builds differ legitimately (`-Os` versus `-O2`, host-specific translation
units) and a naive diff would be noise. Each invariant below corresponds to a
defect that actually happened.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAKEFILE = os.path.join(ROOT, "Makefile")
CMAKELISTS = os.path.join(ROOT, "platform", "mac68k", "CMakeLists.txt")


def logical_lines(text):
    """Join backslash continuations, preserving whether a line began with TAB."""
    out = []
    pending = None
    pending_recipe = False
    for raw in text.split("\n"):
        is_recipe = raw.startswith("\t")
        stripped = raw.rstrip()
        if pending is None:
            pending = stripped
            pending_recipe = is_recipe
        else:
            pending = pending + " " + stripped.lstrip()
        if pending.endswith("\\"):
            pending = pending[:-1]
            continue
        out.append((pending_recipe, pending))
        pending = None
        pending_recipe = False
    if pending is not None:
        out.append((pending_recipe, pending))
    return out


def parse_makefile(text):
    """Map each .c prerequisite to the union of tokens in its rules' recipes."""
    flags = {}
    current_sources = []
    for is_recipe, line in logical_lines(text):
        if is_recipe:
            for source in current_sources:
                flags.setdefault(source, set()).update(line.split())
            continue
        if not line or line.startswith("#"):
            continue
        # A rule, not a variable assignment: ':' must precede any '='.
        colon = line.find(":")
        equals = line.find("=")
        if colon == -1 or (equals != -1 and equals < colon):
            current_sources = []
            continue
        prereqs = line[colon + 1:].lstrip(":=").split()
        current_sources = [p for p in prereqs if p.endswith(".c")]
    return flags


def expand_foreach(text):
    """Expand `foreach(VAR a b c) ... endforeach()` so generated targets exist."""
    pattern = re.compile(
        r"foreach\(\s*(\w+)\s+([^)]*)\)(.*?)endforeach\(\s*\)", re.S)

    def replace(match):
        var, items, body = match.group(1), match.group(2).split(), match.group(3)
        return "\n".join(body.replace("${%s}" % var, item) for item in items)

    while True:
        expanded = pattern.sub(replace, text)
        if expanded == text:
            return expanded
        text = expanded


def parse_cmake(text):
    """Return (target -> source) and (target -> token set) for the mac68k build."""
    text = expand_foreach(text)
    sources = {}
    flags = {}
    for match in re.finditer(
            r"add_library\(\s*(\S+)\s+OBJECT\s+([^)]*)\)", text):
        target, body = match.group(1), match.group(2)
        for source in re.findall(r"\$\{ROOT\}/(\S+\.c)", body):
            sources[target] = source
    for call in ("target_include_directories", "target_compile_definitions",
                 "target_compile_options"):
        for match in re.finditer(
                re.escape(call) + r"\(\s*(\S+)\s+\w+\s+([^)]*)\)", text):
            flags.setdefault(match.group(1), set()).update(match.group(2).split())
    # Sources compiled directly into the application, not as object libraries.
    app_sources = set()
    for match in re.finditer(r"add_application\(([^)]*)\)", text):
        app_sources.update(re.findall(r"\$\{ROOT\}/(\S+\.c)", match.group(1)))
    return sources, flags, app_sources


def main():
    with open(MAKEFILE) as handle:
        linux_flags = parse_makefile(handle.read())
    with open(CMAKELISTS) as handle:
        mac_sources, mac_flags, mac_app_sources = parse_cmake(handle.read())

    mac_by_source = {}
    for target, source in mac_sources.items():
        mac_by_source.setdefault(source, set()).update(mac_flags.get(target, set()))
    built_for_mac = set(mac_by_source) | mac_app_sources

    failures = []

    # Invariant A -- the silent one. UPSTREAM.md's standing constraint: a source
    # bound to a compiler-recognized name via the __asm__ link-name trick needs
    # BOTH -fno-builtin-<name> and, on GCC, -fno-tree-loop-distribute-patterns.
    # Checked as "if Linux passes a no-builtin flag, mac68k must pass it too"
    # rather than by naming memset, so the next such import is covered without
    # editing this file.
    for source, tokens in sorted(linux_flags.items()):
        if source not in mac_by_source:
            continue
        linux_nobuiltin = {t for t in tokens if t.startswith("-fno-builtin-")}
        mac_nobuiltin = {t for t in mac_by_source[source]
                         if t.startswith("-fno-builtin-")}
        missing = linux_nobuiltin - mac_nobuiltin
        for flag in sorted(missing):
            failures.append(
                "%s: Linux build passes %s but the mac68k build does not. "
                "See UPSTREAM.md's standing constraint on idiom recognition: "
                "this class of divergence has no compile-time symptom."
                % (source, flag))
        # The GCC-only companion flag must be present whenever a no-builtin
        # flag is, on both sides. mac68k guards it behind check_c_compiler_flag
        # and the Makefile behind its own probe, so accept either the flag or a
        # probe variable mentioning it.
        if mac_nobuiltin:
            mac_text = " ".join(sorted(mac_by_source[source]))
            if "-fno-tree-loop-distribute-patterns" not in mac_text:
                failures.append(
                    "%s: the mac68k build passes %s but never "
                    "-fno-tree-loop-distribute-patterns. Retro68 is GCC, so "
                    "loop-idiom recognition can rewrite this function's own "
                    "fill loop into a call back to itself, which self-recurses "
                    "at runtime with no compile-time symptom."
                    % (source, ", ".join(sorted(mac_nobuiltin))))

    # Invariant B -- the compile error. A source built on both targets that
    # needs compat headers on Linux needs them on mac68k too.
    for source, tokens in sorted(linux_flags.items()):
        if source not in mac_by_source:
            continue
        needs_compat = any("compat/netbsd/include" in t for t in tokens)
        has_compat = any("compat/netbsd/include" in t
                         for t in mac_by_source[source])
        if needs_compat and not has_compat:
            failures.append(
                "%s: Linux build adds compat/netbsd/include but the mac68k "
                "build does not. Any compat header this source reads will be "
                "undeclared on mac68k only." % source)

    # Invariant C -- the link error, caught statically. Every program descriptor
    # src/programs.c declares must have its defining source in the mac68k build.
    with open(os.path.join(ROOT, "src", "programs.c")) as handle:
        programs_text = handle.read()
    declared = set(re.findall(
        r"extern\s+const\s+struct\s+cb_program_v1\s+(cb_\w+_program)\s*;",
        programs_text))
    definitions = {}
    for dirpath, _dirnames, filenames in os.walk(ROOT):
        if os.sep + ".git" in dirpath or os.sep + "build" in dirpath:
            continue
        for filename in filenames:
            if not filename.endswith(".c"):
                continue
            path = os.path.join(dirpath, filename)
            try:
                # Pinned upstream sources carry non-UTF-8 bytes in their
                # copyright notices; this scan only needs the ASCII symbols.
                with open(path, encoding="utf-8", errors="replace") as handle:
                    body = handle.read()
            except OSError:
                continue
            relative = os.path.relpath(path, ROOT)
            # Both definition forms in this tree: the CB_LIBC_PROGRAM macro and
            # an explicit struct initialiser.
            for symbol in re.findall(
                    r"CB_LIBC_PROGRAM\(\s*(cb_\w+_program)\s*,", body):
                definitions[symbol] = relative
            for symbol in re.findall(
                    r"const\s+struct\s+cb_program_v1\s+(cb_\w+_program)\s*=",
                    body):
                definitions[symbol] = relative
    for symbol in sorted(declared):
        source = definitions.get(symbol)
        if source is None:
            failures.append(
                "%s is declared in src/programs.c but no source defines it."
                % symbol)
        elif source not in built_for_mac:
            failures.append(
                "%s is declared in src/programs.c and defined in %s, but that "
                "source is not compiled by the mac68k build, so the mac68k "
                "link will fail with an undefined reference." % (symbol, source))

    if failures:
        print("FAIL: Linux and mac68k build descriptions diverge", file=sys.stderr)
        for failure in failures:
            print("  - " + failure, file=sys.stderr)
        return 1
    print("build parity checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
