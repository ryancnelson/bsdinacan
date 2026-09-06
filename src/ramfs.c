#include "internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

struct cb_ramfs_mount;

struct cb_ramfs_node {
    struct cb_vfs_node common;
    uint32_t type;
    uint64_t inode;
    uint32_t mode;
    char *name;
    struct cb_ramfs_node *parent;
    struct cb_ramfs_node *children;
    struct cb_ramfs_node *next_sibling;
    unsigned char *data;
    size_t size;
    size_t capacity;
    unsigned references;
};

struct cb_ramfs_mount {
    struct cb_vfs_mount common;
    struct cb_ramfs_node *root;
};

static cb_ssize_t node_read(struct cb_open_file *file, struct cb_task *task,
                            void *buffer, size_t count);
static cb_ssize_t node_write(struct cb_open_file *file, struct cb_task *task,
                             const void *buffer, size_t count);
static cb_off_t node_lseek(struct cb_open_file *file, struct cb_task *task,
                           cb_off_t offset, int whence);
static int open_file_stat(struct cb_open_file *file,
                          struct cb_stat_v1 *stat_buffer);
static void node_last_close(struct cb_open_file *file);

static void ramfs_node_retain(struct cb_vfs_node *common);
static void ramfs_node_release(struct cb_vfs_node *common);
static int ramfs_lookup(struct cb_vfs_node *common, const char *name,
                        size_t name_length, struct cb_vfs_node **node_out);
static int ramfs_create(struct cb_vfs_node *common, const char *name,
                        uint32_t type, uint32_t mode,
                        struct cb_vfs_node **node_out);
static int ramfs_unlink(struct cb_vfs_node *common);
static int ramfs_open(struct cb_vfs_node *common, struct cb_task *task,
                      int flags, struct cb_open_file **file_out);
static int ramfs_stat(struct cb_vfs_node *common,
                      struct cb_stat_v1 *stat_buffer);
static struct cb_vfs_node *ramfs_parent(struct cb_vfs_node *common);
static const char *ramfs_name(struct cb_vfs_node *common);
static struct cb_vfs_node *ramfs_mount_root(struct cb_vfs_mount *common);
static void ramfs_mount_destroy(struct cb_vfs_mount *common);

static const struct cb_file_ops node_file_ops = {
    node_read,
    node_write,
    node_lseek,
    open_file_stat,
    node_last_close
};

static const struct cb_vfs_node_ops ramfs_node_ops = {
    CB_ABI_VERSION_V1,
    sizeof(ramfs_node_ops),
    ramfs_node_retain,
    ramfs_node_release,
    ramfs_lookup,
    ramfs_create,
    ramfs_unlink,
    ramfs_open,
    ramfs_stat,
    ramfs_parent,
    ramfs_name
};

static const struct cb_vfs_mount_ops ramfs_mount_ops = {
    CB_ABI_VERSION_V1,
    sizeof(ramfs_mount_ops),
    ramfs_mount_root,
    ramfs_mount_destroy
};

static struct cb_ramfs_node *ramfs_node(struct cb_vfs_node *node)
{
    return (struct cb_ramfs_node *)node;
}

static struct cb_ramfs_mount *ramfs_mount(struct cb_vfs_mount *mount)
{
    return (struct cb_ramfs_mount *)mount;
}

static struct cb_ramfs_node *node_create(struct cb_ramfs_mount *mount,
                                         struct cb_ramfs_node *parent,
                                         const char *name, uint32_t type,
                                         uint32_t mode)
{
    struct cb_kernel *kernel = mount->common.kernel;
    struct cb_ramfs_node *node = cb_allocate(kernel, sizeof(*node));
    if (node == NULL)
        return NULL;
    node->name = cb_string_duplicate(kernel, name);
    if (node->name == NULL) {
        cb_release(kernel, node);
        return NULL;
    }
    node->common.ops = &ramfs_node_ops;
    node->common.mount = &mount->common;
    node->type = type;
    node->inode = ++kernel->next_inode;
    node->mode = mode;
    node->parent = parent;
    node->references = 1;
    if (parent != NULL) {
        node->next_sibling = parent->children;
        parent->children = node;
    }
    return node;
}

static void node_destroy(struct cb_ramfs_node *node)
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
}

static void ramfs_node_retain(struct cb_vfs_node *common)
{
    ++ramfs_node(common)->references;
}

static void ramfs_node_release(struct cb_vfs_node *common)
{
    struct cb_ramfs_node *node = ramfs_node(common);
    if (--node->references == 0)
        node_destroy(node);
}

static struct cb_ramfs_node *find_child(struct cb_ramfs_node *directory,
                                        const char *name, size_t length)
{
    struct cb_ramfs_node *child;
    for (child = directory->children; child != NULL;
         child = child->next_sibling) {
        if (strlen(child->name) == length &&
            memcmp(child->name, name, length) == 0)
            return child;
    }
    return NULL;
}

static int ramfs_lookup(struct cb_vfs_node *common, const char *name,
                        size_t name_length, struct cb_vfs_node **node_out)
{
    struct cb_ramfs_node *directory = ramfs_node(common);
    struct cb_ramfs_node *child;
    if (directory->type != CB_NODE_DIRECTORY)
        return -CB_ENOTDIR;
    child = find_child(directory, name, name_length);
    if (child == NULL)
        return -CB_ENOENT;
    *node_out = &child->common;
    return 0;
}

static int ramfs_create(struct cb_vfs_node *common, const char *name,
                        uint32_t type, uint32_t mode,
                        struct cb_vfs_node **node_out)
{
    struct cb_ramfs_node *directory = ramfs_node(common);
    struct cb_ramfs_mount *mount = ramfs_mount(common->mount);
    struct cb_ramfs_node *node;
    if (directory->type != CB_NODE_DIRECTORY)
        return -CB_ENOTDIR;
    if (type != CB_NODE_REGULAR && type != CB_NODE_DIRECTORY)
        return -CB_EINVAL;
    if (name == NULL || name[0] == '\0' || strchr(name, '/') != NULL)
        return -CB_EINVAL;
    if (find_child(directory, name, strlen(name)) != NULL)
        return -CB_EEXIST;
    node = node_create(mount, directory, name, type, mode);
    if (node == NULL)
        return -CB_ENOMEM;
    *node_out = &node->common;
    return 0;
}

static int ramfs_unlink(struct cb_vfs_node *common)
{
    struct cb_ramfs_node *node = ramfs_node(common);
    struct cb_ramfs_node **link;
    if (node->type == CB_NODE_DIRECTORY)
        return node->children == NULL ? -CB_EISDIR : -CB_ENOTEMPTY;
    if (node->parent == NULL)
        return -CB_EPERM;
    link = &node->parent->children;
    while (*link != NULL && *link != node)
        link = &(*link)->next_sibling;
    if (*link == NULL)
        return -CB_EIO;
    *link = node->next_sibling;
    node->parent = NULL;
    node->next_sibling = NULL;
    ramfs_node_release(common);
    return 0;
}

static int ramfs_open(struct cb_vfs_node *common, struct cb_task *task,
                      int flags, struct cb_open_file **file_out)
{
    struct cb_ramfs_node *node = ramfs_node(common);
    struct cb_open_file *file;
    if (node->type == CB_NODE_DIRECTORY)
        return -CB_EISDIR;
    if ((flags & CB_O_TRUNC) && (flags & CB_O_ACCMODE) != CB_O_RDONLY)
        node->size = 0;
    file = cb_open_file_create(task->kernel, &node_file_ops, flags);
    if (file == NULL)
        return -CB_ENOMEM;
    file->object.node = common;
    ramfs_node_retain(common);
    file->offset = (flags & CB_O_APPEND) ? (cb_off_t)node->size : 0;
    *file_out = file;
    return 0;
}

static int ramfs_stat(struct cb_vfs_node *common,
                      struct cb_stat_v1 *stat_buffer)
{
    struct cb_ramfs_node *node = ramfs_node(common);
    if (stat_buffer == NULL)
        return -CB_EINVAL;
    memset(stat_buffer, 0, sizeof(*stat_buffer));
    stat_buffer->abi_version = CB_ABI_VERSION_V1;
    stat_buffer->struct_size = sizeof(*stat_buffer);
    stat_buffer->inode = node->inode;
    stat_buffer->size = node->size;
    stat_buffer->mode = node->mode;
    stat_buffer->type = node->type;
    return 0;
}

static struct cb_vfs_node *ramfs_parent(struct cb_vfs_node *common)
{
    struct cb_ramfs_node *parent = ramfs_node(common)->parent;
    return parent == NULL ? NULL : &parent->common;
}

static const char *ramfs_name(struct cb_vfs_node *common)
{
    return ramfs_node(common)->name;
}

static struct cb_vfs_node *ramfs_mount_root(struct cb_vfs_mount *common)
{
    struct cb_ramfs_node *root = ramfs_mount(common)->root;
    return root == NULL ? NULL : &root->common;
}

static void ramfs_mount_destroy(struct cb_vfs_mount *common)
{
    struct cb_ramfs_mount *mount = ramfs_mount(common);
    if (mount->root != NULL)
        ramfs_node_release(&mount->root->common);
    cb_release(common->kernel, mount);
}

struct cb_vfs_mount *cb_ramfs_mount_create(struct cb_kernel *kernel)
{
    struct cb_ramfs_mount *mount = cb_allocate(kernel, sizeof(*mount));
    struct cb_ramfs_node *home;
    if (mount == NULL)
        return NULL;
    mount->common.ops = &ramfs_mount_ops;
    mount->common.kernel = kernel;
    mount->root = node_create(mount, NULL, "", CB_NODE_DIRECTORY, 0755);
    if (mount->root == NULL)
        goto fail;
    if (node_create(mount, mount->root, "bin", CB_NODE_DIRECTORY, 0755) == NULL ||
        node_create(mount, mount->root, "tmp", CB_NODE_DIRECTORY, 0777) == NULL)
        goto fail;
    home = node_create(mount, mount->root, "home", CB_NODE_DIRECTORY, 0755);
    if (home == NULL ||
        node_create(mount, home, "user", CB_NODE_DIRECTORY, 0755) == NULL)
        goto fail;
    return &mount->common;

fail:
    ramfs_mount_destroy(&mount->common);
    return NULL;
}

static struct cb_ramfs_node *file_node(struct cb_open_file *file)
{
    return ramfs_node(file->object.node);
}

static cb_ssize_t node_read(struct cb_open_file *file, struct cb_task *task,
                            void *buffer, size_t count)
{
    struct cb_ramfs_node *node = file_node(file);
    size_t available;
    if ((file->flags & CB_O_ACCMODE) == CB_O_WRONLY) {
        cb_task_set_error(task, CB_EBADF);
        return -1;
    }
    if (file->offset < 0 || (uint64_t)file->offset > SIZE_MAX) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    if ((size_t)file->offset >= node->size)
        return 0;
    available = node->size - (size_t)file->offset;
    if (count > available)
        count = available;
    memcpy(buffer, node->data + (size_t)file->offset, count);
    file->offset += (cb_off_t)count;
    cb_task_set_error(task, 0);
    return (cb_ssize_t)count;
}

static cb_ssize_t node_write(struct cb_open_file *file, struct cb_task *task,
                             const void *buffer, size_t count)
{
    struct cb_ramfs_node *node = file_node(file);
    size_t start;
    size_t needed;
    size_t capacity;
    unsigned char *new_data;
    if ((file->flags & CB_O_ACCMODE) == CB_O_RDONLY) {
        cb_task_set_error(task, CB_EBADF);
        return -1;
    }
    if (count == 0) {
        cb_task_set_error(task, 0);
        return 0;
    }
    if (file->flags & CB_O_APPEND)
        file->offset = (cb_off_t)node->size;
    if (file->offset < 0 || (uint64_t)file->offset > SIZE_MAX ||
        count > SIZE_MAX - (size_t)file->offset) {
        cb_task_set_error(task, CB_ENOSPC);
        return -1;
    }
    start = (size_t)file->offset;
    needed = start + count;
    if (needed > node->capacity) {
        capacity = node->capacity == 0 ? 64 : node->capacity;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) {
                capacity = needed;
                break;
            }
            capacity *= 2;
        }
        new_data = cb_resize(task->kernel, node->data, capacity);
        if (new_data == NULL) {
            cb_task_set_error(task, CB_ENOMEM);
            return -1;
        }
        if (capacity > node->capacity)
            memset(new_data + node->capacity, 0, capacity - node->capacity);
        node->data = new_data;
        node->capacity = capacity;
    }
    if (start > node->size)
        memset(node->data + node->size, 0, start - node->size);
    memcpy(node->data + start, buffer, count);
    file->offset += (cb_off_t)count;
    if (needed > node->size)
        node->size = needed;
    cb_task_set_error(task, 0);
    return (cb_ssize_t)count;
}

static cb_off_t node_lseek(struct cb_open_file *file, struct cb_task *task,
                           cb_off_t offset, int whence)
{
    struct cb_ramfs_node *node = file_node(file);
    cb_off_t base;
    cb_off_t result;
    if (whence == CB_SEEK_SET)
        base = 0;
    else if (whence == CB_SEEK_CUR)
        base = file->offset;
    else if (whence == CB_SEEK_END)
        base = (cb_off_t)node->size;
    else {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    if ((offset > 0 && base > INT64_MAX - offset) ||
        (offset < 0 && base < INT64_MIN - offset)) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    result = base + offset;
    if (result < 0) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    file->offset = result;
    cb_task_set_error(task, 0);
    return result;
}

static int open_file_stat(struct cb_open_file *file,
                          struct cb_stat_v1 *stat_buffer)
{
    return ramfs_stat(file->object.node, stat_buffer);
}

static void node_last_close(struct cb_open_file *file)
{
    ramfs_node_release(file->object.node);
}
