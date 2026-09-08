import re

with open("include/cannedbsd/abi.h", "r") as f:
    text = f.read()
text = text.replace("CB_NODE_PIPE = 4\n};", "CB_NODE_PIPE = 4,\n    CB_NODE_EXECUTABLE = 5\n};")
text = text.replace("    CB_ENAMETOOLONG = 63,\n", "    CB_ENAMETOOLONG = 63,\n    CB_ENOEXEC = 8,\n")
with open("include/cannedbsd/abi.h", "w") as f:
    f.write(text)

with open("src/internal.h", "r") as f:
    text = f.read()

text = text.replace("""struct cb_vfs_node_object {
    struct cb_open_file *pipe_reader;
    struct cb_open_file *pipe_writer;
};""", """struct cb_vfs_node_object {
    struct cb_open_file *pipe_reader;
    struct cb_open_file *pipe_writer;
    struct cb_program *executable;
};""")

# Add task->executable_node and task->pending_executable_node
text = text.replace("""struct cb_task {
    struct cb_kernel *kernel;""", """struct cb_task {
    struct cb_kernel *kernel;
    struct cb_vfs_node *executable_node;
    struct cb_vfs_node *pending_executable_node;""")

with open("src/internal.h", "w") as f:
    f.write(text)

with open("src/vfs.c", "r") as f:
    text = f.read()

text = text.replace("""    if (status.type == CB_NODE_DIRECTORY)
        return -CB_EISDIR;
    if (status.type != CB_NODE_REGULAR)
        return -CB_ESPIPE;""", """    if (status.type == CB_NODE_DIRECTORY)
        return -CB_EISDIR;
    if (status.type == CB_NODE_EXECUTABLE)
        return -CB_EINVAL;
    if (status.type != CB_NODE_REGULAR)
        return -CB_ESPIPE;""")

with open("src/vfs.c", "w") as f:
    f.write(text)

