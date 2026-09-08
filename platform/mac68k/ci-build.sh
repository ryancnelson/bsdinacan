#!/usr/bin/env bash
set -euo pipefail
cmake -S platform/mac68k -B build-mac68k \
    -DCMAKE_TOOLCHAIN_FILE=/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
cmake --build build-mac68k -j2
python3 - <<'REPORT'
from pathlib import Path
root = Path('build-mac68k')
reports = sorted(root.rglob('*head*.su'))
if not reports:
    raise SystemExit('missing head compiler stack-usage report')
text = ''.join(str(p.relative_to(root)) + '\n' + p.read_text() for p in reports)
(root / 'head-stack-usage.txt').write_text(text)
print(text)
REPORT
test -s build-mac68k/CannedBSD.bin
test -s build-mac68k/.rsrc/CannedBSD.APPL
python3 platform/mac68k/check_code_resources.py build-mac68k/.rsrc/CannedBSD.APPL
# The dedicated runner mounts its artifact store here. Keep each CI run separate.
artifact_root="/artifacts/${CI_PIPELINE_NUMBER:?}-${CI_COMMIT_SHA:?}"
mkdir -p "$artifact_root"
artifact_dir=$(mktemp -d "$artifact_root/run.XXXXXX")
chmod 755 "$artifact_dir"
tar -C build-mac68k -czf "$artifact_dir/CannedBSD.tar.gz" \
    CannedBSD.bin CannedBSD.dsk CannedBSD.APPL .rsrc .finf head-stack-usage.txt
printf '%s\n' "$CI_COMMIT_SHA" > "$artifact_dir/commit.txt"
(cd "$artifact_dir" && sha256sum CannedBSD.tar.gz > SHA256SUMS)
printf 'Mac application artifact: %s\n' "$artifact_dir"
