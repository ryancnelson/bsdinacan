#!/bin/sh
# The pinned source has exactly six file-scope int objects. Reject missing,
# ambiguous storage before exposing typed runtime slots. Trailing -O0 in
# both build recipes prevents optimizer narrowing; sanitizer tests copy ints.
set -eu
if [ "$#" -ne 3 ]; then
    echo "usage: $0 OBJCOPY NM OBJECT" >&2
    exit 2
fi
objcopy=$1
nm=$2
object=$3
for original in cflag dflag uflag numchars numfields repeats; do
    name=cb_uniq_$original
    symbols=$("$nm" -P "$object")
    # PRE_LINK may repeat without recompiling the OBJECT library.
    count=$(printf '%s\n' "$symbols" | awk -v old="$original" -v new="$name" \
        '($1 == old && $2 ~ /^[bds]$/) || ($1 == new && $2 ~ /^[BDS]$/) {n++} END {print n+0}')
    if [ "$count" -ne 1 ]; then
        echo "uniq: expected one data symbol for $original, found $count" >&2
        exit 1
    fi
    if printf '%s\n' "$symbols" | awk -v new="$name" \
        '$1 == new && $2 ~ /^[BDS]$/ {found=1} END {exit !found}'; then
        continue
    fi
    "$objcopy" --redefine-sym "$original=$name" --globalize-symbol="$name" "$object"
done
