#!/bin/bash
grep -E 'Local path:|SHA-256:' UPSTREAM.md | paste - - | while read line; do
  path=$(echo "$line" | grep -o 'upstream/netbsd/[^`]*')
  expected=$(echo "$line" | grep -o 'SHA-256: `[^`]*`' | awk -F'`' '{print $2}')
  if [ -n "$path" ]; then
    if [ -f "$path" ]; then
      actual=$(shasum -a 256 "$path" | awk '{print $1}')
      if [ "$actual" == "$expected" ]; then
        echo "OK: $path ($actual)"
      else
        echo "MISMATCH: $path"
        echo "  Expected: $expected"
        echo "  Actual:   $actual"
      fi
      
      # Check license
      if grep -qi "Copyright" "$path"; then
        echo "  License notice present."
      else
        echo "  MISSING LICENSE NOTICE: $path"
      fi
    else
      echo "FILE NOT FOUND: $path"
    fi
  fi
done
