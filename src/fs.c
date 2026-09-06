#include "internal.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static cb_ssize_t node_read(struct cb_open_file *file, struct cb_task *task,
                            void *buffer, size_t count);
static cb_ssize_t node_write(struct cb_open_file *file, struct cb_task *task,
                             const void *buffer, size_t count);
static cb_off_t node_lseek(struct cb_open_file *file, struct cb_task *task,
                           cb_off_t offset, int whence);
static int node_stat(struct cb_open_file *file,
                     struct cb_stat_v1 *stat_buffer);
static void node_last_close(struct cb_open_file *file);

static const struct cb_file_ops node_file_ops = {
    node_read,
    node_write,
    node_lseek,
    node_stat,
    node_last_close
};

static struct cb_node *node_create(struct cb_kernel *kernel,
                                   struct cb_node *parent,
                                   const char *name,
                                   enum cb_node_kind kind,
                                   uint32_t mode)
{
    struct cb_node *node = cb_allocate(kernel, sizeof(*node));
    if (node == NULL)
        return NULL;
    node->name = cb_string_duplicate(kernel, name);
    if (node->name == NULL) {
        cb_release(kernel, node);
        return NULL;
    }
    node->kind = kind;
    node->inode = ++kernel->next_inode;
    node->mode = mode;
    node->parent = parent;
    if (parent != NULL) {
        node->next_sibling = parent->children;
        parent->children = node;
    }
    return node;
}

static void node_destroy(struct cb_kernel *kernel, struct cb_node *node)
{
    struct cb_node *child = node->children;
    while (child != NULL) {
        struct cb_node *next = child->next_sibling;
        node_destroy(kernel, child);
        child = next;
    }
    cb_release(kernel, node->data);
    cb_release(kernel, node->name);
    cb_release(kernel, node);
}

static struct cb_node *find_child(struct cb_node *directory,
                                  const char *name, size_t length)
{
    struct cb_node *child;
    for (child = directory->children; child != NULL;
         child = child->next_sibling) {
        if (strlen(child->name) == length &&
            memcmp(child->name, name, length) == 0)
            return child;
    }
    return NULL;
}

static int cwd_string(struct cb_task *task, char *buffer, size_t size)
{
    struct cb_node *parts[CB_PATH_MAX / 2];
    struct cb_node *node = task->cwd;
    size_t count = 0;
    size_t used = 0;
    size_t index;

    while (node != task->root) {
        if (node == NULL || count == sizeof(parts) / sizeof(parts[0]))
            return -CB_EIO;
        parts[count++] = node;
        node = node->parent;
    }
    if (size < 2)
        return -CB_ENAMETOOLONG;
    buffer[used++] = '/';
    for (index = count; index > 0; --index) {
        size_t length = strlen(parts[index - 1]->name);
        if (used + length + (index > 1 ? 1 : 0) + 1 > size)
            return -CB_ENAMETOOLONG;
        memcpy(buffer + used, parts[index - 1]->name, length);
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
        int count;
        count = snprintf(input, sizeof(input), "%s%s%s", cwd,
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

static struct cb_node *resolve_normalized(struct cb_task *task,
                                          const char *normalized)
{
    struct cb_node *node = task->root;
    const char *cursor = normalized;
    while (*cursor == '/')
        ++cursor;
    while (*cursor != '\0') {
        const char *start = cursor;
        size_t length;
        while (*cursor != '\0' && *cursor != '/')
            ++cursor;
        length = (size_t)(cursor - start);
        if (node->kind != CB_FS_DIRECTORY) {
            cb_task_set_error(task, CB_ENOTDIR);
            return NULL;
        }
        node = find_child(node, start, length);
        if (node == NULL) {
            cb_task_set_error(task, CB_ENOENT);
            return NULL;
        }
        while (*cursor == '/')
            ++cursor;
    }
    return node;
}

static int resolve_parent(struct cb_task *task, const char *normalized,
                          struct cb_node **parent_out, const char **name_out)
{
    char parent_path[CB_PATH_MAX];
    const char *slash = strrchr(normalized, '/');
    struct cb_node *parent;
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
    parent = resolve_normalized(task, parent_path);
    if (parent == NULL)
        return -task->error;
    if (parent->kind != CB_FS_DIRECTORY)
        return -CB_ENOTDIR;
    *parent_out = parent;
    return 0;
}

int cb_fs_initialize(struct cb_kernel *kernel)
{
    struct cb_node *root;
    struct cb_node *home;
    root = node_create(kernel, NULL, "", CB_FS_DIRECTORY, 0755);
    if (root == NULL)
        return -CB_ENOMEM;
    kernel->fs_root = root;
    if (node_create(kernel, root, "bin", CB_FS_DIRECTORY, 0755) == NULL ||
        node_create(kernel, root, "tmp", CB_FS_DIRECTORY, 0777) == NULL)
        return -CB_ENOMEM;
    home = node_create(kernel, root, "home", CB_FS_DIRECTORY, 0755);
    if (home == NULL ||
        node_create(kernel, home, "user", CB_FS_DIRECTORY, 0755) == NULL)
        return -CB_ENOMEM;
    return 0;
}

void cb_fs_destroy(struct cb_kernel *kernel)
{
    if (kernel->fs_root != NULL)
        node_destroy(kernel, kernel->fs_root);
    kernel->fs_root = NULL;
}

struct cb_open_file *cb_fs_open(struct cb_task *task, const char *path,
                                int flags, uint32_t mode)
{
    char normalized[CB_PATH_MAX];
    struct cb_node *node;
    struct cb_open_file *file;
    int result;

    result = normalize_for_task(task, path, normalized);
    if (result < 0) {
        cb_task_set_error(task, -result);
        return NULL;
    }
    node = resolve_normalized(task, normalized);
    if (node == NULL && task->error == CB_ENOENT && (flags & CB_O_CREAT)) {
        struct cb_node *parent;
        const char *name;
        result = resolve_parent(task, normalized, &parent, &name);
        if (result < 0) {
            cb_task_set_error(task, -result);
            return NULL;
        }
        node = node_create(task->kernel, parent, name, CB_FS_REGULAR,
                           mode == 0 ? 0666 : mode);
        if (node == NULL) {
            cb_task_set_error(task, CB_ENOMEM);
            return NULL;
        }
        cb_task_set_error(task, 0);
    }
    if (node == NULL)
        return NULL;
    if (node->kind == CB_FS_DIRECTORY) {
        cb_task_set_error(task, CB_EISDIR);
        return NULL;
    }
    if ((flags & CB_O_TRUNC) && (flags & CB_O_ACCMODE) != CB_O_RDONLY)
        node->size = 0;
    file = cb_open_file_create(task->kernel, &node_file_ops, flags);
    if (file == NULL) {
        cb_task_set_error(task, CB_ENOMEM);
        return NULL;
    }
    file->object.node = node;
    ++node->open_references;
    file->offset = (flags & CB_O_APPEND) ? (cb_off_t)node->size : 0;
    cb_task_set_error(task, 0);
    return file;
}

static void fill_node_stat(struct cb_node *node,
                           struct cb_stat_v1 *stat_buffer)
{
    stat_buffer->abi_version = CB_ABI_VERSION_V1;
    stat_buffer->struct_size = sizeof(*stat_buffer);
    stat_buffer->inode = node->inode;
    stat_buffer->size = node->size;
    stat_buffer->mode = node->mode;
    stat_buffer->type = node->kind == CB_FS_DIRECTORY ?
                        CB_NODE_DIRECTORY : CB_NODE_REGULAR;
}

int cb_fs_stat_path(struct cb_task *task, const char *path,
                    struct cb_stat_v1 *stat_buffer)
{
    char normalized[CB_PATH_MAX];
    struct cb_node *node;
    int result;
    if (stat_buffer == NULL) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    result = normalize_for_task(task, path, normalized);
    if (result < 0) {
        cb_task_set_error(task, -result);
        return -1;
    }
    node = resolve_normalized(task, normalized);
    if (node == NULL)
        return -1;
    fill_node_stat(node, stat_buffer);
    cb_task_set_error(task, 0);
    return 0;
}

int cb_fs_mkdir_path(struct cb_task *task, const char *path, uint32_t mode)
{
    char normalized[CB_PATH_MAX];
    struct cb_node *parent;
    const char *name;
    int result = normalize_for_task(task, path, normalized);
    if (result < 0) {
        cb_task_set_error(task, -result);
        return -1;
    }
    if (resolve_normalized(task, normalized) != NULL) {
        cb_task_set_error(task, CB_EEXIST);
        return -1;
    }
    if (task->error != CB_ENOENT)
        return -1;
    result = resolve_parent(task, normalized, &parent, &name);
    if (result < 0) {
        cb_task_set_error(task, -result);
        return -1;
    }
    if (node_create(task->kernel, parent, name, CB_FS_DIRECTORY,
                    mode == 0 ? 0777 : mode) == NULL) {
        cb_task_set_error(task, CB_ENOMEM);
        return -1;
    }
    cb_task_set_error(task, 0);
    return 0;
}

int cb_fs_unlink_path(struct cb_task *task, const char *path)
{
    char normalized[CB_PATH_MAX];
    struct cb_node *node;
    struct cb_node **link;
    int result = normalize_for_task(task, path, normalized);
    if (result < 0) {
        cb_task_set_error(task, -result);
        return -1;
    }
    node = resolve_normalized(task, normalized);
    if (node == NULL)
        return -1;
    if (node->kind == CB_FS_DIRECTORY) {
        cb_task_set_error(task, node->children == NULL ? CB_EISDIR :
                          CB_ENOTEMPTY);
        return -1;
    }
    link = &node->parent->children;
    while (*link != node)
        link = &(*link)->next_sibling;
    *link = node->next_sibling;
    node->parent = NULL;
    node->next_sibling = NULL;
    if (node->open_references == 0)
        node_destroy(task->kernel, node);
    cb_task_set_error(task, 0);
    return 0;
}

int cb_fs_chdir_path(struct cb_task *task, const char *path)
{
    char normalized[CB_PATH_MAX];
    struct cb_node *node;
    int result = normalize_for_task(task, path, normalized);
    if (result < 0) {
        cb_task_set_error(task, -result);
        return -1;
    }
    node = resolve_normalized(task, normalized);
    if (node == NULL)
        return -1;
    if (node->kind != CB_FS_DIRECTORY) {
        cb_task_set_error(task, CB_ENOTDIR);
        return -1;
    }
    task->cwd = node;
    cb_task_set_error(task, 0);
    return 0;
}

char *cb_fs_getcwd_path(struct cb_task *task, char *buffer, size_t size)
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

static cb_ssize_t node_read(struct cb_open_file *file, struct cb_task *task,
                            void *buffer, size_t count)
{
    struct cb_node *node = file->object.node;
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
    struct cb_node *node = file->object.node;
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
    cb_off_t base;
    cb_off_t result;
    if (whence == CB_SEEK_SET)
        base = 0;
    else if (whence == CB_SEEK_CUR)
        base = file->offset;
    else if (whence == CB_SEEK_END)
        base = (cb_off_t)file->object.node->size;
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

static int node_stat(struct cb_open_file *file,
                     struct cb_stat_v1 *stat_buffer)
{
    if (stat_buffer == NULL)
        return -CB_EINVAL;
    fill_node_stat(file->object.node, stat_buffer);
    return 0;
}

static void node_last_close(struct cb_open_file *file)
{
    struct cb_node *node = file->object.node;
    if (--node->open_references == 0 && node->parent == NULL)
        node_destroy(file->kernel, node);
}
