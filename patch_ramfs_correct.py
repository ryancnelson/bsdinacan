import re

with open("src/ramfs.c", "r") as f:
    text = f.read()

# Fix node_destroy
old_destroy = """static void node_destroy(struct cb_ramfs_node *node)
{
    struct cb_kernel *kernel = node->common.mount->kernel;
    struct cb_ramfs_node *child = node->children;
    while (child != NULL) {
        struct cb_ramfs_node *next = child->next_sibling;
        child->parent = NULL;
        child->next_sibling = NULL;
        ramfs_node_release(&child->common);
        child = next;
    }
    cb_release(kernel, node->data);
    cb_release(kernel, node->name);
    cb_release(kernel, node);
}"""

new_destroy = """static void node_destroy(struct cb_ramfs_node *node)
{
    struct cb_kernel *kernel = node->common.mount->kernel;
    struct cb_ramfs_node *child = node->children;
    while (child != NULL) {
        struct cb_ramfs_node *next = child->next_sibling;
        child->parent = NULL;
        child->next_sibling = NULL;
        ramfs_node_release(&child->common);
        child = next;
    }
    if (node->common.executable != NULL) {
        cb_executor_program_destroy(kernel, node->common.executable);
    }
    cb_release(kernel, node->data);
    cb_release(kernel, node->name);
    cb_release(kernel, node);
}"""
text = text.replace(old_destroy, new_destroy)

# Fix node_read
old_read = """    if (node->type != CB_NODE_REGULAR)
        return node->type == CB_NODE_DIRECTORY ? -CB_EISDIR : -CB_ESPIPE;"""
new_read = """    if (node->type == CB_NODE_EXECUTABLE)
        return 0;
    if (node->type != CB_NODE_REGULAR)
        return node->type == CB_NODE_DIRECTORY ? -CB_EISDIR : -CB_ESPIPE;"""
text = text.replace(old_read, new_read)

# Fix node_write
old_write = """    if (node->type != CB_NODE_REGULAR)
        return node->type == CB_NODE_DIRECTORY ? -CB_EISDIR : -CB_ESPIPE;"""
new_write = """    if (node->type == CB_NODE_EXECUTABLE)
        return -CB_EPERM;
    if (node->type != CB_NODE_REGULAR)
        return node->type == CB_NODE_DIRECTORY ? -CB_EISDIR : -CB_ESPIPE;"""
text = text.replace(old_write, new_write)

# Fix ramfs_truncate
old_trunc = """    if (node->type != CB_NODE_REGULAR)
        return node->type == CB_NODE_DIRECTORY ? -CB_EISDIR : -CB_ESPIPE;"""
new_trunc = """    if (node->type == CB_NODE_EXECUTABLE)
        return -CB_EINVAL;
    if (node->type != CB_NODE_REGULAR)
        return node->type == CB_NODE_DIRECTORY ? -CB_EISDIR : -CB_ESPIPE;"""
text = text.replace(old_trunc, new_trunc)

# Ensure mode=0555 in stat for executable
old_stat = """    stat_buffer->size = node->size;
    stat_buffer->mode = node->mode;
    stat_buffer->type = node->type;"""
new_stat = """    stat_buffer->size = node->size;
    stat_buffer->mode = node->type == CB_NODE_EXECUTABLE ? 0555 : node->mode;
    stat_buffer->type = node->type;"""
text = text.replace(old_stat, new_stat)

with open("src/ramfs.c", "w") as f:
    f.write(text)
