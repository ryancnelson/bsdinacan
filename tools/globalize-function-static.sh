#!/bin/sh
# STATICS-CACHE-02: give one function-local static in a compiled pinned
# object an external name, so src/static_reset.c can own it per task.
#
# usage: globalize-function-static.sh OBJCOPY NM OBJECT FUNCTION VARIABLE NAME
#
# The compiler, not the source, names a function-local static: GCC (host
# GCC 14 and Retro68's m68k GCC 16 alike) emits VARIABLE.N with a per-file
# counter, Clang emits FUNCTION.VARIABLE. Neither is hard-coded here: the
# object's own symbol table must contain exactly one local data symbol of
# either shape, or the build fails rather than renaming the wrong one.
# Rerunning on an already renamed object (CMake's PRE_LINK step repeats on
# relink) is a no-op.
set -eu

if [ "$#" -ne 6 ]; then
    echo "usage: $0 OBJCOPY NM OBJECT FUNCTION VARIABLE NAME" >&2
    exit 2
fi
objcopy=$1
nm=$2
object=$3
function=$4
variable=$5
name=$6

symbols=$("$nm" -P "$object")

if printf '%s\n' "$symbols" |
    awk -v name="$name" '$1 == name && $2 ~ /^[BDS]$/ { found = 1 }
        END { exit !found }'; then
    exit 0
fi

matches=$(printf '%s\n' "$symbols" |
    awk -v f="$function" -v v="$variable" '
        $2 ~ /^[bds]$/ && ($1 == f "." v || $1 ~ ("^" v "\\.[0-9]+$")) {
            print $1
        }')
count=$(printf '%s\n' "$matches" | grep -c . || true)
if [ "$count" -ne 1 ]; then
    echo "$0: expected one local symbol for $function()'s static" \
        "$variable in $object, found $count:" $matches >&2
    exit 1
fi

exec "$objcopy" --redefine-sym "$matches=$name" --globalize-symbol="$name" \
    "$object"
