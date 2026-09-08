import re

with open("src/internal.h", "r") as f:
    text = f.read()

# I need to add executable to struct cb_vfs_node
old_node = """struct cb_vfs_node {
    const struct cb_vfs_node_ops *ops;
    struct cb_vfs_mount *mount;
};"""
new_node = """struct cb_vfs_node {
    const struct cb_vfs_node_ops *ops;
    struct cb_vfs_mount *mount;
    struct cb_program *executable;
};"""
text = text.replace(old_node, new_node)

with open("src/internal.h", "w") as f:
    f.write(text)

with open("src/core.c", "r") as f:
    text = f.read()
text = text.replace("node->object.executable", "node->executable")
with open("src/core.c", "w") as f:
    f.write(text)

with open("src/vfs.c", "r") as f:
    text = f.read()
text = text.replace("node->object.executable", "node->executable")
with open("src/vfs.c", "w") as f:
    f.write(text)

with open("src/ramfs.c", "r") as f:
    text = f.read()
text = text.replace("node->object.executable", "node->executable")
text = text.replace("if (type != CB_NODE_REGULAR && type != CB_NODE_DIRECTORY)", "if (type != CB_NODE_REGULAR && type != CB_NODE_DIRECTORY && type != CB_NODE_EXECUTABLE)")
with open("src/ramfs.c", "w") as f:
    f.write(text)

