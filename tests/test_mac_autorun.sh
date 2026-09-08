#!/bin/sh
set -eu
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -Wpedantic -Iplatform/mac68k \
    tests/test_mac_autorun.c platform/mac68k/autorun.c \
    -o "$scratch/test_mac_autorun"
"$scratch/test_mac_autorun"
