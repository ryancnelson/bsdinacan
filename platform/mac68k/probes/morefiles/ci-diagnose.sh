#!/usr/bin/env bash
set -euo pipefail
# A successful step proves the documented blocker reproduced, not that MoreFiles built.
artifact_root="/artifacts/${CI_PIPELINE_NUMBER:?}-${CI_COMMIT_SHA:?}"
mkdir -p "$artifact_root"
artifact_dir=$(mktemp -d "$artifact_root/morefiles-diagnostic.XXXXXX")
chmod 755 "$artifact_dir"
status=0
python3 platform/mac68k/probes/morefiles/diagnose.py \
    --output "$artifact_dir/diagnostic" || status=$?
printf '%s\n' "$CI_COMMIT_SHA" > "$artifact_dir/commit.txt"
printf '%s\n' "$CI_PIPELINE_NUMBER" > "$artifact_dir/pipeline.txt"
tar -C "$artifact_dir" -czf "$artifact_dir/MoreFiles-diagnostic.tar.gz" diagnostic commit.txt pipeline.txt
(cd "$artifact_dir" && sha256sum MoreFiles-diagnostic.tar.gz > SHA256SUMS)
exit "$status"
