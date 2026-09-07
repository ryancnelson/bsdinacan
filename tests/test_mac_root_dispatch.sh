#!/bin/sh
set -eu
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
${CC:-cc} -D_XOPEN_SOURCE=700 -std=c99 -Wall -Wextra -Werror -Wpedantic \
    -Iplatform/mac68k tests/test_mac_root_dispatch.c \
    platform/mac68k/root_dispatch.c -o "$scratch/test_mac_root_dispatch" \
    ${LDLIBS:-}
"$scratch/test_mac_root_dispatch"
