import re

with open("src/vfs.c", "r") as f:
    text = f.read()

new_lookup = """
int cb_vfs_lookup_node(struct cb_task *task, const char *path, struct cb_vfs_node **node_out)
{
    char normalized[CB_PATH_MAX];
    int result = normalize_for_task(task, path, normalized);
    if (result < 0) return result;
    return resolve_normalized(task, normalized, node_out);
}
"""
text += new_lookup
with open("src/vfs.c", "w") as f:
    f.write(text)

with open("src/internal.h", "r") as f:
    htext = f.read()
htext = htext.replace("int cb_vfs_create_executable", "int cb_vfs_lookup_node(struct cb_task *task, const char *path, struct cb_vfs_node **node_out);\nint cb_vfs_create_executable")
with open("src/internal.h", "w") as f:
    f.write(htext)
