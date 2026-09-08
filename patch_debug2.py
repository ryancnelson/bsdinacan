with open("src/vfs.c", "r") as f:
    text = f.read()

debug_func = """#include <stdio.h>
int cb_vfs_create_executable(struct cb_kernel *kernel, const char *path, struct cb_program *program)
{
    char normalized[CB_PATH_MAX];
    struct cb_vfs_node *node;
    struct cb_vfs_node *parent;
    const char *name;
    struct cb_task dummy_task;
    int result;

    memset(&dummy_task, 0, sizeof(dummy_task));
    dummy_task.kernel = kernel;
    dummy_task.cwd = kernel->vfs_root;

    result = normalize_for_task(&dummy_task, path, normalized);
    if (result < 0) { printf("normalize failed %d\\n", result); return result; }

    result = resolve_normalized(&dummy_task, normalized, &node);
    if (result == 0) { return -CB_EEXIST; }
    if (result != -CB_ENOENT) { printf("resolve failed %d\\n", result); return result; }

    result = resolve_parent(&dummy_task, normalized, &parent, &name);
    if (result < 0) { printf("parent failed %d\\n", result); return result; }

    result = parent->ops->create(parent, name, CB_NODE_EXECUTABLE, 0555, &node);
    if (result < 0) { printf("create failed %d\\n", result); return result; }

    node->executable = program;
    return 0;
}
"""

import re
text = re.sub(r'#include <stdio\.h>\nint cb_vfs_create_executable.*?\n}\n', debug_func, text, flags=re.DOTALL)
with open("src/vfs.c", "w") as f:
    f.write(text)
