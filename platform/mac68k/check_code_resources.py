#!/usr/bin/env python3
"""Reject segmented apps: a lazy LoadSeg trap can run on a private task stack."""
import struct
import sys
from pathlib import Path


def check(path):
    resource = Path(path).read_bytes()
    data, mapping, data_size, map_size = struct.unpack_from(">IIII", resource)
    if data + data_size > len(resource) or mapping + map_size > len(resource):
        raise ValueError("truncated resource fork")
    types = mapping + struct.unpack_from(">H", resource, mapping + 24)[0]
    type_count = struct.unpack_from(">H", resource, types)[0] + 1
    code_ids = []
    for index in range(type_count):
        kind, count, references = struct.unpack_from(
            ">4sHH", resource, types + 2 + index * 8)
        if kind != b"CODE":
            continue
        for number in range(count + 1):
            reference = types + references + number * 12
            code_ids.append(struct.unpack_from(">h", resource, reference)[0])
    if sorted(code_ids) != [0, 1]:
        raise ValueError(f"expected only CODE 0/1 (no lazy segments), got {code_ids}")
    print("Mac CODE resources: single executable segment verified")


if __name__ == "__main__":
    try:
        if len(sys.argv) != 2:
            raise ValueError("usage: check_code_resources.py RESOURCE_FORK")
        check(sys.argv[1])
    except (OSError, ValueError, struct.error) as error:
        sys.exit(f"Mac CODE resource check failed: {error}")
