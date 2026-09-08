#include "internal.h"

#include <stdio.h>
#include <string.h>

static int mount_ops_valid(const struct cb_vfs_mount_ops *ops)
{
    return ops != NULL && ops->abi_version == CB_ABI_VERSION_V1 &&
           ops->struct_size >= sizeof(*ops) && ops->root != NULL &&
           ops->destroy != NULL;
}

static int node_ops_valid(const struct cb_vfs_node_ops *ops)
{
    return ops != NULL && ops->abi_version == CB_ABI_VERSION_V1 &&
           ops->struct_size >= sizeof(*ops) && ops->retain != NULL &&
           ops->release != NULL && ops->lookup != NULL &&
           ops->create != NULL && ops->unlink != NULL && ops->open != NULL &&
           ops->stat != NULL && ops->parent != NULL && ops->name != NULL;
}

void cb_vfs_node_retain(struct cb_vfs_node *node)
{
    if (node != NULL)
        node->ops->retain(node);
}

void cb_vfs_node_release(struct cb_vfs_node *node)
{
    if (node != NULL)
        node->ops->release(node);
}

static int cwd_string(struct cb_task *task, char *buffer, size_t size)
{
    struct cb_vfs_node *parts[CB_PATH_MAX / 2];
    struct cb_vfs_node *node = task->cwd;
    size_t count = 0;
    size_t used = 0;
    size_t index;

    while (node != task->root) {
        if (node == NULL || !node_ops_valid(node->ops) ||
            count == sizeof(parts) / sizeof(parts[0]))
            return -CB_EIO;

        /* CROSS MOUNT BOUNDARY UPWARDS */
        if (task->kernel != NULL) {
            for (size_t i = 0; i < task->kernel->mount_count; ++i) {
                struct cb_vfs_node *mounted_root = task->kernel->mounts[i].mount->ops->root(task->kernel->mounts[i].mount);
                if (node == mounted_root) {
                    node = task->kernel->mounts[i].mount_point;
                    break;
                }
            }
        }

        if (node == task->root)
            break;

        parts[count++] = node;
        node = node->ops->parent(node);
    }
    if (size < 2)
        return -CB_ENAMETOOLONG;
    buffer[used++] = '/';
    for (index = count; index > 0; --index) {
        const char *name = parts[index - 1]->ops->name(parts[index - 1]);
        size_t length;
        if (name == NULL)
            return -CB_EIO;
        length = strlen(name);
        if (used + length + (index > 1 ? 1 : 0) + 1 > size)
            return -CB_ENAMETOOLONG;
        memcpy(buffer + used, name, length);
        used += length;
        if (index > 1)
            buffer[used++] = '/';
    }
    buffer[used] = '\0';
    return 0;
}

int cb_test_path_normalize(const char *cwd, const char *path,
                           char *output, size_t output_size)
{
    char input[CB_PATH_MAX * 2];
    size_t component_starts[CB_PATH_MAX / 2];
    size_t depth = 0;
    size_t used = 1;
    const char *cursor;

    if (cwd == NULL || path == NULL || output == NULL || output_size < 2 ||
        cwd[0] != '/')
        return -CB_EINVAL;
    if (path[0] == '/') {
        if (strlen(path) >= sizeof(input))
            return -CB_ENAMETOOLONG;
        strcpy(input, path);
    } else {
        int count = snprintf(input, sizeof(input), "%s%s%s", cwd,
                             strcmp(cwd, "/") == 0 ? "" : "/", path);
        if (count < 0 || (size_t)count >= sizeof(input))
            return -CB_ENAMETOOLONG;
    }

    output[0] = '/';
    output[1] = '\0';
    cursor = input;
    while (*cursor != '\0') {
        const char *start;
        size_t length;
        while (*cursor == '/')
            ++cursor;
        if (*cursor == '\0')
            break;
        start = cursor;
        while (*cursor != '\0' && *cursor != '/')
            ++cursor;
        length = (size_t)(cursor - start);
        if (length == 1 && start[0] == '.')
            continue;
        if (length == 2 && start[0] == '.' && start[1] == '.') {
            if (depth > 0) {
                used = component_starts[--depth];
                output[used] = '\0';
            }
            continue;
        }
        if (depth == sizeof(component_starts) / sizeof(component_starts[0]))
            return -CB_ENAMETOOLONG;
        component_starts[depth++] = used;
        if (used > 1) {
            if (used + 1 >= output_size)
                return -CB_ENAMETOOLONG;
            output[used++] = '/';
        }
        if (used + length >= output_size)
            return -CB_ENAMETOOLONG;
        memcpy(output + used, start, length);
        used += length;
        output[used] = '\0';
    }
    return 0;
}

static int normalize_for_task(struct cb_task *task, const char *path,
                              char normalized[CB_PATH_MAX])
{
    char cwd[CB_PATH_MAX];
    int result = cwd_string(task, cwd, sizeof(cwd));
    if (result < 0)
        return result;
    return cb_test_path_normalize(cwd, path, normalized, CB_PATH_MAX);
}

static int resolve_normalized(struct cb_task *task, const char *normalized,
                              struct cb_vfs_node **node_out)
{
    struct cb_vfs_node *node = task->root;
    const char *cursor = normalized;
    while (*cursor == '/')
        ++cursor;
    while (*cursor != '\0') {
        const char *start = cursor;
        struct cb_vfs_node *next = NULL;
        size_t length;
        int result;
        while (*cursor != '\0' && *cursor != '/')
            ++cursor;
        length = (size_t)(cursor - start);
        if (node == NULL || !node_ops_valid(node->ops))
            return -CB_EIO;
        result = node->ops->lookup(node, start, length, &next);
        if (result < 0)
            return result;
        if (next == NULL || !node_ops_valid(next->ops))
            return -CB_EIO;

        /* CROSS MOUNT BOUNDARY DOWNWARDS */
        if (task->kernel != NULL) {
            for (size_t i = 0; i < task->kernel->mount_count; ++i) {
                if (next == task->kernel->mounts[i].mount_point) {
                    struct cb_vfs_node *mounted_root = task->kernel->mounts[i].mount->ops->root(task->kernel->mounts[i].mount);
                    next = mounted_root;
                    break;
                }
            }
        }

        node = next;
        while (*cursor == '/')
            ++cursor;
    }
    *node_out = node;
    return 0;
}

static int resolve_parent(struct cb_task *task, const char *normalized,
                          struct cb_vfs_node **parent_out,
                          const char **name_out)
{
    char parent_path[CB_PATH_MAX];
    const char *slash = strrchr(normalized, '/');
    size_t length;
    if (slash == NULL || slash[1] == '\0')
        return -CB_EINVAL;
    *name_out = slash + 1;
    length = (size_t)(slash - normalized);
    if (length == 0)
        strcpy(parent_path, "/");
    else {
        memcpy(parent_path, normalized, length);
        parent_path[length] = '\0';
    }
    return resolve_normalized(task, parent_path, parent_out);
}

int cb_vfs_initialize(struct cb_kernel *kernel)
{
    struct cb_vfs_mount *mount = cb_ramfs_mount_create(kernel);
    if (mount == NULL)
        return -CB_ENOMEM;
    if (cb_vfs_set_root_mount(kernel, mount) < 0) {
        mount->ops->destroy(mount);
        return -CB_EIO;
    }
    return 0;
}

int cb_vfs_set_root_mount(struct cb_kernel *kernel,
                          struct cb_vfs_mount *mount)
{
    struct cb_vfs_node *root;
    if (kernel == NULL || mount == NULL || kernel->root_mount != NULL ||
        mount->kernel != kernel || !mount_ops_valid(mount->ops))
        return -CB_EINVAL;
    root = mount->ops->root(mount);
    if (root == NULL || !node_ops_valid(root->ops) || root->mount != mount)
        return -CB_EINVAL;
    kernel->root_mount = mount;
    kernel->vfs_root = root;
    return 0;
}

void cb_vfs_destroy(struct cb_kernel *kernel)
{
    /* First release all mount point references */
    for (size_t i = 0; i < kernel->mount_count; ++i) {
        if (kernel->mounts[i].mount_point != NULL)
            cb_vfs_node_release(kernel->mounts[i].mount_point);
        kernel->mounts[i].mount_point = NULL;
    }

    /* Then destroy mounts in reverse order so nested mounts are destroyed before their parent mounts */
    while (kernel->mount_count > 0) {
        size_t i = --kernel->mount_count;
        if (kernel->mounts[i].mount != NULL)
            kernel->mounts[i].mount->ops->destroy(kernel->mounts[i].mount);
    }
    kernel->mount_count = 0;

    if (kernel->root_mount != NULL)
        kernel->root_mount->ops->destroy(kernel->root_mount);
    kernel->root_mount = NULL;
    kernel->vfs_root = NULL;
}

struct cb_open_file *cb_vfs_open(struct cb_task *task, const char *path,
                                 int flags, uint32_t mode)
{
    char normalized[CB_PATH_MAX];
    struct cb_vfs_node *node = NULL;
    struct cb_open_file *file = NULL;
    int result = normalize_for_task(task, path, normalized);
    if (result < 0)
        goto fail;
    result = resolve_normalized(task, normalized, &node);
    if (result == -CB_ENOENT && (flags & CB_O_CREAT)) {
        struct cb_vfs_node *parent;
        const char *name;
        result = resolve_parent(task, normalized, &parent, &name);
        if (result < 0)
            goto fail;
        result = parent->ops->create(parent, name, CB_NODE_REGULAR,
                                     mode == 0 ? 0666 : mode, &node);
    }
    if (result < 0)
        goto fail;
    result = node->ops->open(node, task, flags, &file);
    if (result < 0)
        goto fail;
    cb_task_set_error(task, 0);
    return file;

fail:
    cb_task_set_error(task, -result);
    return NULL;
}

int cb_vfs_stat_path(struct cb_task *task, const char *path,
                     struct cb_stat_v1 *stat_buffer)
{
    char normalized[CB_PATH_MAX];
    struct cb_vfs_node *node;
    int result;
    if (stat_buffer == NULL) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    result = normalize_for_task(task, path, normalized);
    if (result >= 0)
        result = resolve_normalized(task, normalized, &node);
    if (result >= 0)
        result = node->ops->stat(node, stat_buffer);
    if (result < 0) {
        cb_task_set_error(task, -result);
        return -1;
    }
    cb_task_set_error(task, 0);
    return 0;
}

int cb_vfs_mkdir_path(struct cb_task *task, const char *path, uint32_t mode)
{
    char normalized[CB_PATH_MAX];
    struct cb_vfs_node *node;
    struct cb_vfs_node *parent;
    const char *name;
    int result = normalize_for_task(task, path, normalized);
    if (result < 0)
        goto fail;
    result = resolve_normalized(task, normalized, &node);
    if (result == 0) {
        result = -CB_EEXIST;
        goto fail;
    }
    if (result != -CB_ENOENT)
        goto fail;
    result = resolve_parent(task, normalized, &parent, &name);
    if (result < 0)
        goto fail;
    result = parent->ops->create(parent, name, CB_NODE_DIRECTORY,
                                 mode == 0 ? 0777 : mode, &node);
    if (result < 0)
        goto fail;
    cb_task_set_error(task, 0);
    return 0;

fail:
    cb_task_set_error(task, -result);
    return -1;
}

int cb_vfs_unlink_path(struct cb_task *task, const char *path)
{
    char normalized[CB_PATH_MAX];
    struct cb_vfs_node *node;
    int result = normalize_for_task(task, path, normalized);
    if (result >= 0)
        result = resolve_normalized(task, normalized, &node);
    if (result >= 0) {
        for (size_t i = 0; i < task->kernel->mount_count; ++i) {
            if (task->kernel->mounts[i].mount_point == node ||
                task->kernel->mounts[i].mount->ops->root(task->kernel->mounts[i].mount) == node) {
                result = -CB_EPERM;
                break;
            }
        }
    }
    if (result >= 0)
        result = node->ops->unlink(node);
    if (result < 0) {
        cb_task_set_error(task, -result);
        return -1;
    }
    cb_task_set_error(task, 0);
    return 0;
}

int cb_vfs_chdir_path(struct cb_task *task, const char *path)
{
    char normalized[CB_PATH_MAX];
    struct cb_vfs_node *node;
    struct cb_stat_v1 stat_buffer;
    int result = normalize_for_task(task, path, normalized);
    if (result >= 0)
        result = resolve_normalized(task, normalized, &node);
    if (result >= 0)
        result = node->ops->stat(node, &stat_buffer);
    if (result >= 0 && stat_buffer.type != CB_NODE_DIRECTORY)
        result = -CB_ENOTDIR;
    if (result < 0) {
        cb_task_set_error(task, -result);
        return -1;
    }
    cb_vfs_node_retain(node);
    cb_vfs_node_release(task->cwd);
    task->cwd = node;
    cb_task_set_error(task, 0);
    return 0;
}

char *cb_vfs_getcwd_path(struct cb_task *task, char *buffer, size_t size)
{
    int result;
    if (buffer == NULL || size == 0) {
        cb_task_set_error(task, CB_EINVAL);
        return NULL;
    }
    result = cwd_string(task, buffer, size);
    if (result < 0) {
        cb_task_set_error(task, -result);
        return NULL;
    }
    cb_task_set_error(task, 0);
    return buffer;
}

int cb_vfs_mount_path(struct cb_task *task, const char *path,
                      struct cb_vfs_mount *mount)
{
    char normalized[CB_PATH_MAX];
    struct cb_vfs_node *node = NULL;
    struct cb_vfs_node *cand_root;
    struct cb_stat_v1 statbuf;
    int result;
    size_t i;

    if (task == NULL || task->kernel == NULL || path == NULL || mount == NULL)
        return -CB_EINVAL;

    if (mount->kernel != task->kernel || !mount_ops_valid(mount->ops))
        return -CB_EINVAL;

    cand_root = mount->ops->root(mount);
    if (cand_root == NULL || !node_ops_valid(cand_root->ops) || cand_root->mount != mount)
        return -CB_EINVAL;

    if (cand_root->ops->stat(cand_root, &statbuf) < 0 || statbuf.type != CB_NODE_DIRECTORY)
        return -CB_ENOTDIR;

    if (mount == task->kernel->root_mount)
        return -CB_EINVAL;

    for (i = 0; i < task->kernel->mount_count; ++i) {
        if (task->kernel->mounts[i].mount == mount)
            return -CB_EINVAL;
    }

    if (task->kernel->mount_count >= 4)
        return -CB_ENOMEM;

    result = normalize_for_task(task, path, normalized);
    if (result < 0)
        return result;

    result = resolve_normalized(task, normalized, &node);
    if (result < 0)
        return result;

    if (node == task->kernel->vfs_root)
        return -CB_EINVAL;

    if (node->ops->stat(node, &statbuf) < 0 || statbuf.type != CB_NODE_DIRECTORY)
        return -CB_ENOTDIR;

    for (i = 0; i < task->kernel->mount_count; ++i) {
        if (task->kernel->mounts[i].mount_point == node ||
            task->kernel->mounts[i].mount->ops->root(task->kernel->mounts[i].mount) == node) {
            return -CB_EEXIST;
        }
    }

    cb_vfs_node_retain(node);
    task->kernel->mounts[task->kernel->mount_count].mount_point = node;
    task->kernel->mounts[task->kernel->mount_count].mount = mount;
    task->kernel->mount_count++;

    return 0;
}
