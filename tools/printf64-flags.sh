#!/bin/sh
# Compile only: use the target's actual stdint typedefs, never sizeof guesses.
# All arguments are the compiler command and its target/preprocessor flags.
set -eu
for kind in signed unsigned; do
    case $kind in
        signed) fixed=int64_t; prefix=; macro=CB_PRId64_KIND ;;
        unsigned) fixed=uint64_t; prefix='unsigned '; macro=CB_PRIu64_KIND ;;
    esac
    found=no
    for length in 1 2; do
        case $length in 1) type="${prefix}long" ;; 2) type="${prefix}long long" ;; esac
        # A conflicting extern declaration is a constraint error even without
        # -Werror; unlike a pointer conversion it cannot silently pass.
        if printf '#include <stdint.h>\nextern %s cb_probe;\nextern %s cb_probe;\n' \
            "$fixed" "$type" | "$@" -x c -c -o /dev/null - >/dev/null 2>&1; then
            printf '%s ' "-D$macro=$length"
            found=yes
            break
        fi
    done
    if test "$found" = no; then
        echo "cannot determine exact $fixed printf type" >&2
        exit 1
    fi
done
printf '\n'
