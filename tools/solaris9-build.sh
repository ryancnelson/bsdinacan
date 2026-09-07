#!/bin/ksh
# Run in a Solaris 9 SPARC guest with GCC and GNU make installed.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
PATH=/usr/local/bin:/usr/ccs/bin:/usr/bin:/usr/sbin
export PATH
case "$(uname -s):$(uname -r):$(uname -p)" in
    SunOS:5.9:sparc) ;;
    *) echo 'This gate requires Solaris 9 SPARC' >&2; exit 1 ;;
esac
${CC:-gcc} --version
${MAKE:-make} --version
${MAKE:-make} HOST=solaris9 CC="${CC:-gcc}" AR=/usr/ccs/bin/ar clean
${MAKE:-make} HOST=solaris9 CC="${CC:-gcc}" AR=/usr/ccs/bin/ar test-runtime
file build/bsdinacan
echo 'SOLARIS9_CANNEDBSD_TEST=PASS'
