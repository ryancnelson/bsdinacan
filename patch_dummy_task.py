with open("src/vfs.c", "r") as f:
    text = f.read()

import re
text = re.sub(r'    dummy_task\.cwd = kernel->vfs_root;\n', '    dummy_task.cwd = kernel->vfs_root;\n    dummy_task.root = kernel->vfs_root;\n', text)
with open("src/vfs.c", "w") as f:
    f.write(text)

with open("src/core.c", "r") as f:
    text = f.read()

text = re.sub(r'    dummy_task\.cwd = kernel->vfs_root;\n', '    dummy_task.cwd = kernel->vfs_root;\n    dummy_task.root = kernel->vfs_root;\n', text)
with open("src/core.c", "w") as f:
    f.write(text)
