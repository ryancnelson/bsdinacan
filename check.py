import re
import hashlib
import os

with open("UPSTREAM.md") as f:
    content = f.read()

# find all blocks starting with "## NetBSD"
blocks = content.split("## NetBSD ")[1:]
for block in blocks:
    name = block.split("\n")[0].strip()
    
    # find Local path or similar
    path_match = re.search(r'`(upstream/netbsd/[^`]+)`', block)
    if not path_match:
        continue
    path = path_match.group(1)
    
    # find SHA-256
    sha_match = re.search(r'SHA-256:\s*`([a-f0-9]{64})`', block)
    if not sha_match:
        continue
    expected_sha = sha_match.group(1)
    
    if os.path.exists(path):
        with open(path, "rb") as f:
            actual_sha = hashlib.sha256(f.read()).hexdigest()
        
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            has_copyright = "Copyright" in f.read()
            
        print(f"[{name}] {path}")
        print(f"  Expected SHA: {expected_sha}")
        print(f"  Actual SHA:   {actual_sha}")
        if expected_sha != actual_sha:
            print("  MISMATCH!")
        print(f"  License notice: {'Yes' if has_copyright else 'No'}")
        
