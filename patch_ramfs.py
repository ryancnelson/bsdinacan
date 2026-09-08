import re

with open("src/ramfs.c", "r") as f:
    text = f.read()

# Add executable ops
exec_ops = """static cb_ssize_t ramfs_executable_read(struct cb_vfs_node *node,
                                        struct cb_task *task, void *buffer,
                                        size_t count, cb_off_t offset)
{
    (void)node; (void)task; (void)buffer; (void)count; (void)offset;
    return 0;
}

static cb_ssize_t ramfs_executable_write(struct cb_vfs_node *node,
                                         struct cb_task *task,
                                         const void *buffer, size_t count,
                                         cb_off_t offset)
{
    (void)node; (void)task; (void)buffer; (void)count; (void)offset;
    return -CB_EPERM;
}

static int ramfs_executable_truncate(struct cb_vfs_node *node, cb_off_t length)
{
    (void)node; (void)length;
    return -CB_EINVAL;
}

static int ramfs_executable_stat(struct cb_vfs_node *node,
                                 struct cb_stat_v1 *stat_buffer)
{
    stat_buffer->abi_version = CB_ABI_VERSION_V1;
    stat_buffer->struct_size = sizeof(*stat_buffer);
    stat_buffer->inode = node->inode;
    stat_buffer->size = 0;
    stat_buffer->mode = node->mode;
    stat_buffer->type = CB_NODE_EXECUTABLE;
    return 0;
}

static void ramfs_executable_destroy(struct cb_vfs_node *node)
{
    struct cb_kernel *kernel = node->mount->kernel;
    if (node->object.executable != NULL) {
        cb_executor_program_destroy(kernel, node->object.executable);
    }
    cb_release(kernel, node->name);
    cb_release(kernel, node);
}

static const struct cb_vfs_node_ops ramfs_executable_ops = {
    CB_ABI_VERSION_V1,
    sizeof(struct cb_vfs_node_ops),
    NULL,
    ramfs_executable_read,
    ramfs_executable_write,
    ramfs_executable_truncate,
    ramfs_executable_stat,
    NULL,
    NULL,
    NULL,
    NULL,
    ramfs_executable_destroy
};

"""
text = text.replace("static cb_ssize_t ramfs_regular_read", exec_ops + "static cb_ssize_t ramfs_regular_read")

# update ramfs_create
old_create = """        node->ops = &ramfs_directory_ops;
    } else {
        node->ops = &ramfs_regular_ops;
    }"""
new_create = """        node->ops = &ramfs_directory_ops;
    } else if (type == CB_NODE_EXECUTABLE) {
        node->ops = &ramfs_executable_ops;
    } else {
        node->ops = &ramfs_regular_ops;
    }"""
text = text.replace(old_create, new_create)

with open("src/ramfs.c", "w") as f:
    f.write(text)
