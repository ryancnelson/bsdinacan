#include "internal.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static struct cb_kernel *active_kernel;

static void initialize_api(struct cb_kernel *kernel);

struct cb_task_allocation {
    void *pointer;
    size_t size;
    struct cb_task_allocation *next;
};

void *cb_allocate(struct cb_kernel *kernel, size_t size)
{
    void *pointer = kernel->host->allocate(size);
    if (pointer != NULL)
        memset(pointer, 0, size);
    return pointer;
}

void *cb_resize(struct cb_kernel *kernel, void *pointer, size_t size)
{
    return kernel->host->resize(pointer, size);
}

void cb_release(struct cb_kernel *kernel, void *pointer)
{
    if (pointer != NULL)
        kernel->host->release(pointer);
}

char *cb_string_duplicate(struct cb_kernel *kernel, const char *text)
{
    size_t length;
    char *copy;
    if (text == NULL)
        return NULL;
    length = strlen(text);
    if (length == SIZE_MAX)
        return NULL;
    ++length;
    copy = cb_allocate(kernel, length);
    if (copy != NULL)
        memcpy(copy, text, length);
    return copy;
}

void cb_task_set_error(struct cb_task *task, int error)
{
    if (task != NULL && task->error_cell != NULL)
        *task->error_cell = error;
}

enum cb_wake_reason cb_test_current_wake_reason(void)
{
    return active_kernel == NULL || active_kernel->current == NULL ?
           CB_WAKE_NONE : active_kernel->current->wake_reason;
}

int cb_test_current_descriptor_poll(int descriptor, int events)
{
    struct cb_task *task;
    struct cb_open_file *file;
    if (active_kernel == NULL || active_kernel->current == NULL)
        return -CB_EINVAL;
    task = active_kernel->current;
    if (descriptor < 0 || descriptor >= CB_MAX_FDS ||
        (file = task->descriptors[descriptor].file) == NULL)
        return -CB_EBADF;
    return file->ops->poll(file, events);
}

size_t cb_test_task_allocation_count(cb_pid_t pid)
{
    struct cb_task *task;
    size_t count = 0;
    if (active_kernel == NULL)
        return SIZE_MAX;
    for (task = active_kernel->tasks; task != NULL; task = task->next) {
        struct cb_task_allocation *allocation;
        if (task->pid != pid)
            continue;
        for (allocation = task->allocations; allocation != NULL;
             allocation = allocation->next)
            ++count;
        return count;
    }
    return SIZE_MAX;
}

static size_t string_vector_count(char *const vector[])
{
    size_t count = 0;
    if (vector != NULL) {
        while (vector[count] != NULL) {
            if (count == SIZE_MAX / sizeof(*vector) - 1)
                return SIZE_MAX;
            ++count;
        }
    }
    return count;
}

static char **string_vector_copy(struct cb_kernel *kernel,
                                 char *const vector[])
{
    size_t count = string_vector_count(vector);
    size_t index;
    char **copy;
    if (count == SIZE_MAX)
        return NULL;
    copy = cb_allocate(kernel, (count + 1) * sizeof(*copy));
    if (copy == NULL)
        return NULL;
    for (index = 0; index < count; ++index) {
        copy[index] = cb_string_duplicate(kernel, vector[index]);
        if (copy[index] == NULL) {
            while (index > 0)
                cb_release(kernel, copy[--index]);
            cb_release(kernel, copy);
            return NULL;
        }
    }
    copy[count] = NULL;
    return copy;
}

static void string_vector_destroy(struct cb_kernel *kernel, char **vector)
{
    size_t index;
    if (vector == NULL)
        return;
    for (index = 0; vector[index] != NULL; ++index)
        cb_release(kernel, vector[index]);
    cb_release(kernel, vector);
}

struct cb_open_file *cb_open_file_create(struct cb_kernel *kernel,
                                          const struct cb_file_ops *ops,
                                          int flags)
{
    struct cb_open_file *file;
    if (kernel == NULL || ops == NULL || ops->poll == NULL ||
        ops->stat == NULL)
        return NULL;
    file = cb_allocate(kernel, sizeof(*file));
    if (file == NULL)
        return NULL;
    file->references = 1;
    file->flags = flags;
    file->ops = ops;
    file->kernel = kernel;
    return file;
}

void cb_open_file_retain(struct cb_open_file *file)
{
    ++file->references;
}

void cb_open_file_release(struct cb_open_file *file)
{
    struct cb_kernel *kernel;
    if (file == NULL)
        return;
    if (--file->references != 0)
        return;
    kernel = file->kernel;
    if (file->ops->last_close != NULL)
        file->ops->last_close(file);
    cb_release(kernel, file);
}

static int fd_install_at(struct cb_task *task, struct cb_open_file *file,
                         int descriptor, int retain)
{
    if (descriptor < 0 || descriptor >= CB_MAX_FDS) {
        cb_task_set_error(task, CB_EBADF);
        return -1;
    }
    if (task->descriptors[descriptor].file != NULL)
        cb_open_file_release(task->descriptors[descriptor].file);
    task->descriptors[descriptor].file = file;
    task->descriptors[descriptor].close_on_exec = 0;
    if (retain)
        cb_open_file_retain(file);
    return descriptor;
}

static int fd_install(struct cb_task *task, struct cb_open_file *file,
                      int minimum)
{
    int descriptor;
    for (descriptor = minimum; descriptor < CB_MAX_FDS; ++descriptor) {
        if (task->descriptors[descriptor].file == NULL) {
            task->descriptors[descriptor].file = file;
            task->descriptors[descriptor].close_on_exec = 0;
            return descriptor;
        }
    }
    cb_task_set_error(task, CB_EMFILE);
    return -1;
}

static int fd_close(struct cb_task *task, int descriptor)
{
    struct cb_open_file *file;
    if (descriptor < 0 || descriptor >= CB_MAX_FDS ||
        task->descriptors[descriptor].file == NULL) {
        cb_task_set_error(task, CB_EBADF);
        return -1;
    }
    file = task->descriptors[descriptor].file;
    task->descriptors[descriptor].file = NULL;
    task->descriptors[descriptor].close_on_exec = 0;
    cb_open_file_release(file);
    return 0;
}

static void fd_close_all(struct cb_task *task)
{
    int descriptor;
    for (descriptor = 0; descriptor < CB_MAX_FDS; ++descriptor) {
        if (task->descriptors[descriptor].file != NULL)
            fd_close(task, descriptor);
    }
}

// program_find removed

static cb_ssize_t console_read(struct cb_open_file *file,
                               struct cb_task *task, void *buffer,
                               size_t count)
{
    cb_ssize_t result;
    (void)file;
    if (count == 0) {
        cb_task_set_error(task, 0);
        return 0;
    }
    while (task->kernel->host->console_poll(0) == 0)
        cb_task_yield_as(task, CB_TASK_BLOCKED_CONSOLE);
    result = task->kernel->host->console_read(buffer, count);
    if (result < 0) {
        cb_task_set_error(task, (int)-result);
        return -1;
    }
    cb_task_set_error(task, 0);
    return result;
}

static cb_ssize_t console_write(struct cb_open_file *file,
                                struct cb_task *task, const void *buffer,
                                size_t count)
{
    cb_ssize_t result = task->kernel->host->console_write(
        file->object.console_stream, buffer, count);
    if (result < 0) {
        cb_task_set_error(task, (int)-result);
        return -1;
    }
    return result;
}

static int no_truncate(struct cb_open_file *file, struct cb_task *task,
                        cb_off_t length)
{
    (void)file;
    (void)length;
    cb_task_set_error(task, CB_ESPIPE);
    return -1;
}

static cb_off_t no_seek(struct cb_open_file *file, struct cb_task *task,
                        cb_off_t offset, int whence)
{
    (void)file;
    (void)offset;
    (void)whence;
    cb_task_set_error(task, CB_ESPIPE);
    return -1;
}

static int console_input_poll(struct cb_open_file *file, int events)
{
    if ((events & CB_POLL_READ) != 0 &&
        file->kernel->host->console_poll(0) > 0)
        return CB_POLL_READ;
    return 0;
}

static int console_output_poll(struct cb_open_file *file, int events)
{
    (void)file;
    return events & CB_POLL_WRITE;
}

static int terminal_stat(struct cb_open_file *file,
                         struct cb_stat_v1 *stat_buffer)
{
    (void)file;
    if (stat_buffer == NULL)
        return -CB_EINVAL;
    memset(stat_buffer, 0, sizeof(*stat_buffer));
    stat_buffer->abi_version = CB_ABI_VERSION_V1;
    stat_buffer->struct_size = sizeof(*stat_buffer);
    stat_buffer->type = CB_NODE_TERMINAL;
    stat_buffer->mode = 0600;
    return 0;
}

static const struct cb_file_ops console_input_ops = {
    console_read, NULL, no_seek, console_input_poll, terminal_stat, NULL,
    no_truncate
};

static const struct cb_file_ops console_output_ops = {
    NULL, console_write, no_seek, console_output_poll, terminal_stat, NULL,
    no_truncate
};

void cb_wake_pipe_tasks(struct cb_kernel *kernel)
{
    struct cb_task *task;
    for (task = kernel->tasks; task != NULL; task = task->next) {
        if (task->state == CB_TASK_BLOCKED_PIPE) {
            task->wake_reason = CB_WAKE_PIPE_CHANGED;
            task->state = CB_TASK_RUNNABLE;
        }
    }
}

static cb_ssize_t pipe_read(struct cb_open_file *file, struct cb_task *task,
                            void *buffer, size_t count)
{
    struct cb_pipe *pipe = file->object.pipe;
    unsigned char *destination = buffer;
    size_t amount;
    size_t first;
    if (count == 0) {
        cb_task_set_error(task, 0);
        return 0;
    }
    while (pipe->used == 0) {
        if (pipe->writers == 0)
            return 0;
        cb_task_yield_as(task, CB_TASK_BLOCKED_PIPE);
    }
    amount = count < pipe->used ? count : pipe->used;
    first = sizeof(pipe->data) - pipe->read_position;
    if (first > amount)
        first = amount;
    memcpy(destination, pipe->data + pipe->read_position, first);
    memcpy(destination + first, pipe->data, amount - first);
    pipe->read_position = (pipe->read_position + amount) % sizeof(pipe->data);
    pipe->used -= amount;
    cb_wake_pipe_tasks(task->kernel);
    cb_task_set_error(task, 0);
    return (cb_ssize_t)amount;
}

static cb_ssize_t pipe_write(struct cb_open_file *file, struct cb_task *task,
                             const void *buffer, size_t count)
{
    struct cb_pipe *pipe = file->object.pipe;
    const unsigned char *source = buffer;
    size_t total = 0;
    while (total < count) {
        size_t free_space;
        size_t write_position;
        size_t amount;
        size_t first;
        if (pipe->readers == 0) {
            cb_task_set_error(task, CB_EPIPE);
            return total == 0 ? -1 : (cb_ssize_t)total;
        }
        free_space = sizeof(pipe->data) - pipe->used;
        if (free_space == 0) {
            cb_task_yield_as(task, CB_TASK_BLOCKED_PIPE);
            continue;
        }
        amount = count - total;
        if (amount > free_space)
            amount = free_space;
        write_position = (pipe->read_position + pipe->used) %
                         sizeof(pipe->data);
        first = sizeof(pipe->data) - write_position;
        if (first > amount)
            first = amount;
        memcpy(pipe->data + write_position, source + total, first);
        memcpy(pipe->data, source + total + first, amount - first);
        pipe->used += amount;
        total += amount;
        cb_wake_pipe_tasks(task->kernel);
    }
    cb_task_set_error(task, 0);
    return (cb_ssize_t)total;
}

static int pipe_stat(struct cb_open_file *file,
                     struct cb_stat_v1 *stat_buffer)
{
    if (stat_buffer == NULL)
        return -CB_EINVAL;
    memset(stat_buffer, 0, sizeof(*stat_buffer));
    stat_buffer->abi_version = CB_ABI_VERSION_V1;
    stat_buffer->struct_size = sizeof(*stat_buffer);
    stat_buffer->type = CB_NODE_PIPE;
    stat_buffer->mode = 0600;
    stat_buffer->size = file->object.pipe->used;
    return 0;
}

static int pipe_read_poll(struct cb_open_file *file, int events)
{
    struct cb_pipe *pipe = file->object.pipe;
    if ((events & CB_POLL_READ) != 0 &&
        (pipe->used != 0 || pipe->writers == 0))
        return CB_POLL_READ;
    return 0;
}

static int pipe_write_poll(struct cb_open_file *file, int events)
{
    struct cb_pipe *pipe = file->object.pipe;
    if ((events & CB_POLL_WRITE) != 0 &&
        (pipe->used < sizeof(pipe->data) || pipe->readers == 0))
        return CB_POLL_WRITE;
    return 0;
}

static void pipe_read_close(struct cb_open_file *file)
{
    struct cb_pipe *pipe = file->object.pipe;
    --pipe->readers;
    cb_wake_pipe_tasks(file->kernel);
    if (pipe->readers == 0 && pipe->writers == 0)
        cb_release(file->kernel, pipe);
}

static void pipe_write_close(struct cb_open_file *file)
{
    struct cb_pipe *pipe = file->object.pipe;
    --pipe->writers;
    cb_wake_pipe_tasks(file->kernel);
    if (pipe->readers == 0 && pipe->writers == 0)
        cb_release(file->kernel, pipe);
}

static const struct cb_file_ops pipe_read_ops = {
    pipe_read, NULL, no_seek, pipe_read_poll, pipe_stat, pipe_read_close,
    no_truncate
};

static const struct cb_file_ops pipe_write_ops = {
    NULL, pipe_write, no_seek, pipe_write_poll, pipe_stat, pipe_write_close,
    no_truncate
};

void cb_task_yield_as(struct cb_task *task, enum cb_task_state state)
{
    struct cb_kernel *kernel = task->kernel;
    task->wake_reason = CB_WAKE_NONE;
    task->state = state;
    kernel->current = NULL;
    cb_executor_suspend(task->execution);
    kernel->current = task;
    task->state = CB_TASK_RUNNING;
}

static void task_release_allocations(struct cb_task *task)
{
    struct cb_task_allocation *allocation = task->allocations;
    while (allocation != NULL) {
        struct cb_task_allocation *next = allocation->next;
        cb_release(task->kernel, allocation->pointer);
        cb_release(task->kernel, allocation);
        allocation = next;
    }
    task->allocations = NULL;
}

static void task_destroy(struct cb_task *task)
{
    struct cb_kernel *kernel = task->kernel;
    task_release_allocations(task);
    fd_close_all(task);
    cb_executor_instance_destroy(task->execution);
    if (task->executable_node != NULL)
        cb_vfs_node_release(task->executable_node);
    if (task->pending_executable_node != NULL)
        cb_vfs_node_release(task->pending_executable_node);
    string_vector_destroy(kernel, task->argv);
    string_vector_destroy(kernel, task->environment);
    string_vector_destroy(kernel, task->pending_argv);
    string_vector_destroy(kernel, task->pending_environment);
    cb_vfs_node_release(task->cwd);
    cb_vfs_node_release(task->root);
    cb_release(kernel, task->error_cell);
    cb_release(kernel, task);
}

static int task_apply_actions(struct cb_task *task,
                              const struct cb_spawn_action_v1 *actions,
                              size_t action_count)
{
    size_t index;
    for (index = 0; index < action_count; ++index) {
        const struct cb_spawn_action_v1 *action = &actions[index];
        if (action->abi_version != CB_ABI_VERSION_V1 ||
            action->struct_size < sizeof(*action)) {
            cb_task_set_error(task, CB_EINVAL);
            return -1;
        }
        if (action->type == CB_SPAWN_DUP2) {
            int source = action->from_fd;
            int target = action->to_fd;
            if (source < 0 || source >= CB_MAX_FDS || target < 0 ||
                target >= CB_MAX_FDS ||
                task->descriptors[source].file == NULL) {
                cb_task_set_error(task, CB_EBADF);
                return -1;
            }
            if (source != target)
                fd_install_at(task, task->descriptors[source].file, target, 1);
            task->descriptors[target].close_on_exec = 0;
        } else if (action->type == CB_SPAWN_CLOSE) {
            int descriptor = action->from_fd;
            if (descriptor >= 0 && descriptor < CB_MAX_FDS &&
                task->descriptors[descriptor].file != NULL)
                fd_close(task, descriptor);
        } else {
            cb_task_set_error(task, CB_EINVAL);
            return -1;
        }
    }
    return 0;
}

static struct cb_task *task_create(struct cb_kernel *kernel,
                                   struct cb_task *parent,
                                   const struct cb_program *program,
                                   char *const argv[], char *const envp[],
                                   const struct cb_spawn_action_v1 *actions,
                                   size_t action_count)
{
    size_t argument_count = string_vector_count(argv);
    struct cb_task *task;
    int descriptor;
    if (argument_count == SIZE_MAX || argument_count > INT_MAX)
        return NULL;
    task = cb_allocate(kernel, sizeof(*task));
    if (task == NULL)
        return NULL;
    task->kernel = kernel;
    task->error_cell = cb_allocate(kernel, sizeof(*task->error_cell));
    if (task->error_cell == NULL)
        goto fail;
    task->pid = ++kernel->next_pid;
    task->ppid = parent == NULL ? 0 : parent->pid;
    task->program = program;
    task->argv = string_vector_copy(kernel, argv);
    task->argc = (int)argument_count;
    task->environment = string_vector_copy(kernel,
        envp != NULL ? envp : (parent != NULL ? parent->environment : NULL));
    if (task->argv == NULL || task->environment == NULL)
        goto fail;
    task->getopt_state.optind = 1;
    task->getopt_state.opterr = 1;
    task->getopt_state.optopt = 0;
    task->getopt_state.optarg = NULL;
    task->getopt_state.place = (char *)"";
    task->root = parent == NULL ? kernel->vfs_root : parent->root;
    task->cwd = parent == NULL ? kernel->vfs_root : parent->cwd;
    cb_vfs_node_retain(task->root);
    cb_vfs_node_retain(task->cwd);
    if (parent != NULL) {
        for (descriptor = 0; descriptor < CB_MAX_FDS; ++descriptor) {
            task->descriptors[descriptor] = parent->descriptors[descriptor];
            if (task->descriptors[descriptor].file != NULL)
                cb_open_file_retain(task->descriptors[descriptor].file);
        }
    }
    if (task_apply_actions(task, actions, action_count) < 0)
        goto fail;
    task->execution = cb_executor_instance_create(task, program);
    if (task->execution == NULL)
        goto fail;
    task->state = CB_TASK_RUNNABLE;
    task->next = kernel->tasks;
    kernel->tasks = task;
    return task;

fail:
    fd_close_all(task);
    string_vector_destroy(kernel, task->argv);
    string_vector_destroy(kernel, task->environment);
    cb_vfs_node_release(task->cwd);
    cb_vfs_node_release(task->root);
    cb_release(kernel, task->error_cell);
    cb_release(kernel, task);
    return NULL;
}

static void wake_waiting_parent(struct cb_task *child)
{
    struct cb_task *task;
    for (task = child->kernel->tasks; task != NULL; task = task->next) {
        if (task->pid == child->ppid && task->state == CB_TASK_BLOCKED_WAIT &&
            (task->waiting_for == child->pid || task->waiting_for == -1)) {
            task->wake_reason = CB_WAKE_CHILD_EXITED;
            task->state = CB_TASK_RUNNABLE;
            return;
        }
    }
}

static void task_finish_exec(struct cb_task *task)
{
    struct cb_kernel *kernel = task->kernel;
    int descriptor;
    cb_executor_instance_destroy(task->execution);
    task->execution = NULL;
    task_release_allocations(task);
    for (descriptor = 0; descriptor < CB_MAX_FDS; ++descriptor) {
        if (task->descriptors[descriptor].file != NULL &&
            task->descriptors[descriptor].close_on_exec)
            fd_close(task, descriptor);
    }
    string_vector_destroy(kernel, task->argv);
    string_vector_destroy(kernel, task->environment);
    task->program = task->pending_program;
    task->argv = task->pending_argv;
    task->argc = task->pending_argc;
    task->environment = task->pending_environment;
    if (task->executable_node != NULL)
        cb_vfs_node_release(task->executable_node);
    task->executable_node = task->pending_executable_node;
    task->pending_executable_node = NULL;
    task->pending_program = NULL;
    task->pending_argv = NULL;
    task->pending_environment = NULL;
    task->pending_argc = 0;
    *task->error_cell = 0;
    task->getopt_state.optind = 1;
    task->getopt_state.opterr = 1;
    task->getopt_state.optopt = 0;
    task->getopt_state.optarg = NULL;
    task->getopt_state.place = (char *)"";
    task->execution = cb_executor_instance_create(task, task->program);
    if (task->execution == NULL)
        kernel->host->fatal("unable to create execution after exec");
    task->state = CB_TASK_RUNNABLE;
}

static struct cb_task *pick_runnable(struct cb_kernel *kernel)
{
    struct cb_task *start;
    struct cb_task *task;
    if (kernel->tasks == NULL)
        return NULL;
    start = kernel->schedule_cursor != NULL &&
            kernel->schedule_cursor->next != NULL ?
            kernel->schedule_cursor->next : kernel->tasks;
    task = start;
    do {
        if (task->state == CB_TASK_RUNNABLE) {
            kernel->schedule_cursor = task;
            return task;
        }
        task = task->next != NULL ? task->next : kernel->tasks;
    } while (task != start);
    return NULL;
}

static int has_console_waiter(struct cb_kernel *kernel)
{
    struct cb_task *task;
    for (task = kernel->tasks; task != NULL; task = task->next)
        if (task->state == CB_TASK_BLOCKED_CONSOLE)
            return 1;
    return 0;
}

static void wake_console_waiters(struct cb_kernel *kernel)
{
    struct cb_task *task;
    for (task = kernel->tasks; task != NULL; task = task->next)
        if (task->state == CB_TASK_BLOCKED_CONSOLE) {
            task->wake_reason = CB_WAKE_CONSOLE_READY;
            task->state = CB_TASK_RUNNABLE;
        }
}

static cb_pid_t api_getpid(void)
{
    return active_kernel->current->pid;
}

static cb_pid_t api_getppid(void)
{
    return active_kernel->current->ppid;
}


static int resolve_executable_node(struct cb_task *task, const char *program_name, struct cb_vfs_node **node_out)
{
    char path[CB_PATH_MAX];
    struct cb_vfs_node *node;
    struct cb_stat_v1 statbuf;
    int result;
    
    if (strchr(program_name, '/') == NULL) {
        result = snprintf(path, sizeof(path), "/bin/%s", program_name);
    } else {
        result = snprintf(path, sizeof(path), "%s", program_name);
    }
    
    if (result < 0 || (size_t)result >= sizeof(path)) {
        return -CB_ENAMETOOLONG;
    }
    
    result = cb_vfs_lookup_node(task, path, &node);
    if (result < 0) return result;
    
    result = node->ops->stat(node, &statbuf);
    if (result < 0) return result;
    
    if (statbuf.type == CB_NODE_DIRECTORY || statbuf.type == CB_NODE_TERMINAL)
        return -CB_EACCES;
    if (statbuf.type != CB_NODE_EXECUTABLE)
        return -CB_ENOEXEC;
        
    *node_out = node;
    return 0;
}

static int api_spawn(const char *program_name, char *const argv[],
                     char *const envp[],
                     const struct cb_spawn_action_v1 *actions,
                     size_t action_count, cb_pid_t *pid_out)
{
    struct cb_kernel *kernel = active_kernel;
    struct cb_task *parent = kernel->current;
    struct cb_vfs_node *node;
    struct cb_task *child;
    int result;
    if (program_name == NULL || argv == NULL || argv[0] == NULL ||
        (action_count != 0 && actions == NULL)) {
        cb_task_set_error(parent, CB_EINVAL);
        return -1;
    }
    if (string_vector_count(argv) > INT_MAX) {
        cb_task_set_error(parent, CB_EINVAL);
        return -1;
    }
    
    result = resolve_executable_node(parent, program_name, &node);
    if (result < 0) {
        cb_task_set_error(parent, -result);
        return -1;
    }
    
    cb_vfs_node_retain(node);
    child = task_create(kernel, parent, node->executable, argv, envp, actions, action_count);
    if (child == NULL) {
        cb_vfs_node_release(node);
        cb_task_set_error(parent, CB_ENOMEM);
        return -1;
    }
    
    child->executable_node = node;
    
    if (pid_out != NULL)
        *pid_out = child->pid;
    cb_task_set_error(parent, 0);
    return 0;
}

static int api_exec(const char *program_name, char *const argv[],
                    char *const envp[])
{
    struct cb_task *task = active_kernel->current;
    struct cb_vfs_node *node;
    char **new_argv;
    char **new_environment;
    size_t argument_count;
    int result;
    if (program_name == NULL || argv == NULL || argv[0] == NULL) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    argument_count = string_vector_count(argv);
    if (argument_count == SIZE_MAX || argument_count > INT_MAX) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    
    result = resolve_executable_node(task, program_name, &node);
    if (result < 0) {
        cb_task_set_error(task, -result);
        return -1;
    }
    
    cb_vfs_node_retain(node);
    
    new_argv = string_vector_copy(task->kernel, argv);
    new_environment = string_vector_copy(task->kernel,
        envp != NULL ? envp : task->environment);
    if (new_argv == NULL || new_environment == NULL) {
        string_vector_destroy(task->kernel, new_argv);
        string_vector_destroy(task->kernel, new_environment);
        cb_vfs_node_release(node);
        cb_task_set_error(task, CB_ENOMEM);
        return -1;
    }
    
    task->pending_executable_node = node;
    task->pending_program = node->executable;
    task->pending_argv = new_argv;
    task->pending_argc = (int)argument_count;
    task->pending_environment = new_environment;
    cb_task_yield_as(task, CB_TASK_EXEC_PENDING);
    task->kernel->host->fatal("successful exec returned");
    return -1;
}

static void api_exit(int status)
{
    struct cb_task *task = active_kernel->current;
    task_release_allocations(task);
    fd_close_all(task);
    task->exit_status = status & 0xff;
    task->state = CB_TASK_ZOMBIE;
    if (task->pid == task->kernel->boot_pid) {
        task->kernel->boot_finished = 1;
        task->kernel->boot_status = task->exit_status;
    }
    wake_waiting_parent(task);
    task->kernel->current = NULL;
    cb_executor_request_termination(task->execution);
    task->kernel->host->fatal("terminated execution returned");
}

static struct cb_task *find_waitable_child(struct cb_task *parent,
                                            cb_pid_t pid,
                                            int *has_child)
{
    struct cb_task *task;
    *has_child = 0;
    for (task = parent->kernel->tasks; task != NULL; task = task->next) {
        if (task->ppid != parent->pid)
            continue;
        if (pid != -1 && task->pid != pid)
            continue;
        *has_child = 1;
        if (task->state == CB_TASK_ZOMBIE)
            return task;
    }
    return NULL;
}

static cb_pid_t api_waitpid(cb_pid_t pid, int *status)
{
    struct cb_task *parent = active_kernel->current;
    for (;;) {
        struct cb_task *child;
        struct cb_task **link;
        int has_child;
        cb_pid_t result;
        child = find_waitable_child(parent, pid, &has_child);
        if (child != NULL) {
            result = child->pid;
            if (status != NULL)
                *status = child->exit_status;
            link = &parent->kernel->tasks;
            while (*link != child)
                link = &(*link)->next;
            *link = child->next;
            if (parent->kernel->schedule_cursor == child)
                parent->kernel->schedule_cursor = NULL;
            child->state = CB_TASK_DEAD;
            task_destroy(child);
            cb_task_set_error(parent, 0);
            return result;
        }
        if (!has_child) {
            cb_task_set_error(parent, CB_ECHILD);
            return -1;
        }
        parent->waiting_for = pid;
        cb_task_yield_as(parent, CB_TASK_BLOCKED_WAIT);
    }
}

static void api_yield(void)
{
    cb_task_yield_as(active_kernel->current, CB_TASK_RUNNABLE);
}

static int api_open(const char *path, int flags, uint32_t mode)
{
    struct cb_task *task = active_kernel->current;
    struct cb_open_file *file = cb_vfs_open(task, path, flags, mode);
    int descriptor;
    if (file == NULL)
        return -1;
    descriptor = fd_install(task, file, 0);
    if (descriptor < 0)
        cb_open_file_release(file);
    return descriptor;
}

static int api_close(int descriptor)
{
    struct cb_task *task = active_kernel->current;
    int result = fd_close(task, descriptor);
    if (result == 0)
        cb_task_set_error(task, 0);
    return result;
}

static cb_ssize_t api_read(int descriptor, void *buffer, size_t count)
{
    struct cb_task *task = active_kernel->current;
    struct cb_open_file *file;
    if (descriptor < 0 || descriptor >= CB_MAX_FDS ||
        (file = task->descriptors[descriptor].file) == NULL ||
        file->ops->read == NULL) {
        cb_task_set_error(task, CB_EBADF);
        return -1;
    }
    if (count > INT64_MAX) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    if (count != 0 && buffer == NULL) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    return file->ops->read(file, task, buffer, count);
}

static cb_ssize_t api_write(int descriptor, const void *buffer, size_t count)
{
    struct cb_task *task = active_kernel->current;
    struct cb_open_file *file;
    if (descriptor < 0 || descriptor >= CB_MAX_FDS ||
        (file = task->descriptors[descriptor].file) == NULL ||
        file->ops->write == NULL) {
        cb_task_set_error(task, CB_EBADF);
        return -1;
    }
    if (count > INT64_MAX) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    if (count != 0 && buffer == NULL) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    return file->ops->write(file, task, buffer, count);
}

static cb_off_t api_lseek(int descriptor, cb_off_t offset, int whence)
{
    struct cb_task *task = active_kernel->current;
    struct cb_open_file *file;
    if (descriptor < 0 || descriptor >= CB_MAX_FDS ||
        (file = task->descriptors[descriptor].file) == NULL ||
        file->ops->lseek == NULL) {
        cb_task_set_error(task, CB_EBADF);
        return -1;
    }
    return file->ops->lseek(file, task, offset, whence);
}

static int api_ftruncate(int descriptor, cb_off_t length)
{
    struct cb_task *task = active_kernel->current;
    struct cb_open_file *file;
    if (descriptor < 0 || descriptor >= CB_MAX_FDS ||
        (file = task->descriptors[descriptor].file) == NULL ||
        file->ops->truncate == NULL) {
        cb_task_set_error(task, CB_EBADF);
        return -1;
    }
    if (length < 0 || (uint64_t)length > SIZE_MAX) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    return file->ops->truncate(file, task, length);
}

static int api_truncate(const char *path, cb_off_t length)
{
    return cb_vfs_truncate_path(active_kernel->current, path, length);
}

static int api_dup(int descriptor)
{
    struct cb_task *task = active_kernel->current;
    struct cb_open_file *file;
    int result;
    if (descriptor < 0 || descriptor >= CB_MAX_FDS ||
        (file = task->descriptors[descriptor].file) == NULL) {
        cb_task_set_error(task, CB_EBADF);
        return -1;
    }
    cb_open_file_retain(file);
    result = fd_install(task, file, 0);
    if (result < 0)
        cb_open_file_release(file);
    else
        cb_task_set_error(task, 0);
    return result;
}

static int api_dup2(int old_descriptor, int new_descriptor)
{
    struct cb_task *task = active_kernel->current;
    struct cb_open_file *file;
    if (old_descriptor < 0 || old_descriptor >= CB_MAX_FDS ||
        (file = task->descriptors[old_descriptor].file) == NULL ||
        new_descriptor < 0 || new_descriptor >= CB_MAX_FDS) {
        cb_task_set_error(task, CB_EBADF);
        return -1;
    }
    if (old_descriptor != new_descriptor) {
        fd_install_at(task, file, new_descriptor, 1);
        task->descriptors[new_descriptor].close_on_exec = 0;
    }
    cb_task_set_error(task, 0);
    return new_descriptor;
}

static int api_set_cloexec(int descriptor, int enabled)
{
    struct cb_task *task = active_kernel->current;
    if (descriptor < 0 || descriptor >= CB_MAX_FDS ||
        task->descriptors[descriptor].file == NULL) {
        cb_task_set_error(task, CB_EBADF);
        return -1;
    }
    task->descriptors[descriptor].close_on_exec = enabled != 0;
    cb_task_set_error(task, 0);
    return 0;
}

static int api_pipe(int descriptors[2])
{
    struct cb_task *task = active_kernel->current;
    struct cb_pipe *pipe;
    struct cb_open_file *reader;
    struct cb_open_file *writer;
    int read_descriptor;
    int write_descriptor;
    if (descriptors == NULL) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    pipe = cb_allocate(task->kernel, sizeof(*pipe));
    if (pipe == NULL) {
        cb_task_set_error(task, CB_ENOMEM);
        return -1;
    }
    reader = cb_open_file_create(task->kernel, &pipe_read_ops, CB_O_RDONLY);
    if (reader == NULL) {
        cb_release(task->kernel, pipe);
        cb_task_set_error(task, CB_ENOMEM);
        return -1;
    }
    pipe->readers = 1;
    reader->object.pipe = pipe;
    writer = cb_open_file_create(task->kernel, &pipe_write_ops, CB_O_WRONLY);
    if (writer == NULL) {
        cb_open_file_release(reader);
        cb_task_set_error(task, CB_ENOMEM);
        return -1;
    }
    pipe->writers = 1;
    writer->object.pipe = pipe;
    read_descriptor = fd_install(task, reader, 0);
    if (read_descriptor < 0) {
        cb_open_file_release(reader);
        cb_open_file_release(writer);
        return -1;
    }
    write_descriptor = fd_install(task, writer, 0);
    if (write_descriptor < 0) {
        fd_close(task, read_descriptor);
        cb_open_file_release(writer);
        return -1;
    }
    descriptors[0] = read_descriptor;
    descriptors[1] = write_descriptor;
    cb_task_set_error(task, 0);
    return 0;
}

static int api_fstat(int descriptor, struct cb_stat_v1 *stat_buffer)
{
    struct cb_task *task = active_kernel->current;
    struct cb_open_file *file;
    int result;
    if (descriptor < 0 || descriptor >= CB_MAX_FDS ||
        (file = task->descriptors[descriptor].file) == NULL) {
        cb_task_set_error(task, CB_EBADF);
        return -1;
    }
    result = file->ops->stat(file, stat_buffer);
    if (result < 0) {
        cb_task_set_error(task, -result);
        return -1;
    }
    cb_task_set_error(task, 0);
    return 0;
}

static int api_stat(const char *path, struct cb_stat_v1 *stat_buffer)
{
    return cb_vfs_stat_path(active_kernel->current, path, stat_buffer);
}

static int api_mkdir(const char *path, uint32_t mode)
{
    return cb_vfs_mkdir_path(active_kernel->current, path, mode);
}

static int api_unlink(const char *path)
{
    return cb_vfs_unlink_path(active_kernel->current, path);
}

static int api_chdir(const char *path)
{
    return cb_vfs_chdir_path(active_kernel->current, path);
}

static char *api_getcwd(char *buffer, size_t size)
{
    return cb_vfs_getcwd_path(active_kernel->current, buffer, size);
}

static int valid_environment_name(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;
    if (cursor == NULL || (!isalpha(*cursor) && *cursor != '_'))
        return 0;
    for (++cursor; *cursor != '\0'; ++cursor)
        if (!isalnum(*cursor) && *cursor != '_')
            return 0;
    return 1;
}

static const char *api_getenv(const char *name)
{
    struct cb_task *task = active_kernel->current;
    size_t length;
    size_t index;
    if (!valid_environment_name(name)) {
        cb_task_set_error(task, CB_EINVAL);
        return NULL;
    }
    length = strlen(name);
    for (index = 0; task->environment[index] != NULL; ++index) {
        if (strncmp(task->environment[index], name, length) == 0 &&
            task->environment[index][length] == '=')
            return task->environment[index] + length + 1;
    }
    return NULL;
}

static int api_setenv(const char *name, const char *value, int overwrite)
{
    struct cb_task *task = active_kernel->current;
    size_t count;
    size_t index;
    size_t name_length;
    size_t value_length;
    char *entry;
    char **resized;
    if (!valid_environment_name(name) || value == NULL) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    name_length = strlen(name);
    value_length = strlen(value);
    if (value_length > SIZE_MAX - 2 ||
        name_length > SIZE_MAX - value_length - 2) {
        cb_task_set_error(task, CB_ENOMEM);
        return -1;
    }
    count = string_vector_count(task->environment);
    if (count == SIZE_MAX) {
        cb_task_set_error(task, CB_ENOMEM);
        return -1;
    }
    for (index = 0; index < count; ++index) {
        if (strncmp(task->environment[index], name, name_length) == 0 &&
            task->environment[index][name_length] == '=') {
            if (!overwrite)
                return 0;
            break;
        }
    }
    entry = cb_allocate(task->kernel, name_length + value_length + 2);
    if (entry == NULL) {
        cb_task_set_error(task, CB_ENOMEM);
        return -1;
    }
    sprintf(entry, "%s=%s", name, value);
    if (index < count) {
        cb_release(task->kernel, task->environment[index]);
        task->environment[index] = entry;
    } else {
        resized = cb_resize(task->kernel, task->environment,
                            (count + 2) * sizeof(*resized));
        if (resized == NULL) {
            cb_release(task->kernel, entry);
            cb_task_set_error(task, CB_ENOMEM);
            return -1;
        }
        task->environment = resized;
        task->environment[count] = entry;
        task->environment[count + 1] = NULL;
    }
    cb_task_set_error(task, 0);
    return 0;
}

static int api_unsetenv(const char *name)
{
    struct cb_task *task = active_kernel->current;
    size_t count;
    size_t index;
    size_t name_length;
    if (!valid_environment_name(name)) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    name_length = strlen(name);
    count = string_vector_count(task->environment);
    for (index = 0; index < count; ++index) {
        if (strncmp(task->environment[index], name, name_length) == 0 &&
            task->environment[index][name_length] == '=') {
            cb_release(task->kernel, task->environment[index]);
            memmove(&task->environment[index], &task->environment[index + 1],
                    (count - index) * sizeof(*task->environment));
            break;
        }
    }
    cb_task_set_error(task, 0);
    return 0;
}

static const char *api_strerror(int error)
{
    switch (error) {
    case 0: return "no error";
    case CB_EPERM: return "operation not permitted";
    case CB_ENOENT: return "no such file or directory";
    case CB_EINTR: return "interrupted system call";
    case CB_EIO: return "input/output error";
    case CB_EBADF: return "bad file descriptor";
    case CB_ECHILD: return "no child processes";
    case CB_ENOMEM: return "cannot allocate memory";
    case CB_EACCES: return "permission denied";
    case CB_EEXIST: return "file exists";
    case CB_ENOTDIR: return "not a directory";
    case CB_EISDIR: return "is a directory";
    case CB_EINVAL: return "invalid argument";
    case CB_ENFILE: return "too many open files in system";
    case CB_EMFILE: return "too many open files";
    case CB_ENOSPC: return "no space left";
    case CB_ESPIPE: return "illegal seek";
    case CB_EPIPE: return "broken pipe";
    case CB_ENAMETOOLONG: return "file name too long";
    case CB_ENOSYS: return "function not implemented";
    case CB_ENOTEMPTY: return "directory not empty";
    default: return "unknown error";
    }
}

static int api_get_errno(void)
{
    return *active_kernel->current->error_cell;
}

static void api_set_errno(int error)
{
    *active_kernel->current->error_cell = error;
}

static int *api_errno_location(void)
{
    return active_kernel->current->error_cell;
}

static char ***api_environ_location(void)
{
    return &active_kernel->current->environment;
}

static struct cb_getopt_state_v1 *api_getopt_state_location(void)
{
    return &active_kernel->current->getopt_state;
}

static const char *api_getprogname(void)
{
    return active_kernel->current->argv[0];
}

static const struct cb_capabilities_v1 *api_capabilities(void)
{
    return &active_kernel->capabilities;
}

static struct cb_task_allocation **task_allocation_link(
    struct cb_task *task, void *pointer)
{
    struct cb_task_allocation **link = &task->allocations;
    while (*link != NULL && (*link)->pointer != pointer)
        link = &(*link)->next;
    return link;
}

static void *api_allocate(size_t size)
{
    struct cb_task *task = active_kernel->current;
    struct cb_task_allocation *allocation;
    void *pointer;
    if (size == 0)
        size = 1;
    pointer = cb_allocate(task->kernel, size);
    if (pointer == NULL) {
        cb_task_set_error(task, CB_ENOMEM);
        return NULL;
    }
    allocation = cb_allocate(task->kernel, sizeof(*allocation));
    if (allocation == NULL) {
        cb_release(task->kernel, pointer);
        cb_task_set_error(task, CB_ENOMEM);
        return NULL;
    }
    allocation->pointer = pointer;
    allocation->size = size;
    allocation->next = task->allocations;
    task->allocations = allocation;
    cb_task_set_error(task, 0);
    return pointer;
}

static void *api_resize(void *pointer, size_t size)
{
    struct cb_task *task = active_kernel->current;
    struct cb_task_allocation **link;
    void *resized;
    if (pointer == NULL)
        return api_allocate(size);
    link = task_allocation_link(task, pointer);
    if (*link == NULL) {
        cb_task_set_error(task, CB_EINVAL);
        return NULL;
    }
    if (size == 0) {
        struct cb_task_allocation *allocation = *link;
        *link = allocation->next;
        cb_release(task->kernel, allocation->pointer);
        cb_release(task->kernel, allocation);
        cb_task_set_error(task, 0);
        return NULL;
    }
    resized = cb_resize(task->kernel, pointer, size);
    if (resized == NULL) {
        cb_task_set_error(task, CB_ENOMEM);
        return NULL;
    }
    (*link)->pointer = resized;
    (*link)->size = size;
    cb_task_set_error(task, 0);
    return resized;
}

static void api_release(void *pointer)
{
    struct cb_task *task = active_kernel->current;
    struct cb_task_allocation **link;
    struct cb_task_allocation *allocation;
    if (pointer == NULL)
        return;
    link = task_allocation_link(task, pointer);
    if (*link == NULL) {
        cb_task_set_error(task, CB_EINVAL);
        return;
    }
    allocation = *link;
    *link = allocation->next;
    cb_release(task->kernel, allocation->pointer);
    cb_release(task->kernel, allocation);
    cb_task_set_error(task, 0);
}

static void initialize_api(struct cb_kernel *kernel)
{
    struct cb_api_v1 *api = &kernel->api;
    api->abi_version = CB_ABI_VERSION_V1;
    api->struct_size = sizeof(*api);
    api->getpid = api_getpid;
    api->getppid = api_getppid;
    api->spawn = api_spawn;
    api->exec = api_exec;
    api->exit = api_exit;
    api->waitpid = api_waitpid;
    api->yield = api_yield;
    api->open = api_open;
    api->close = api_close;
    api->read = api_read;
    api->write = api_write;
    api->lseek = api_lseek;
    api->dup = api_dup;
    api->dup2 = api_dup2;
    api->set_cloexec = api_set_cloexec;
    api->pipe = api_pipe;
    api->fstat = api_fstat;
    api->stat = api_stat;
    api->mkdir = api_mkdir;
    api->unlink = api_unlink;
    api->chdir = api_chdir;
    api->getcwd = api_getcwd;
    api->getenv = api_getenv;
    api->setenv = api_setenv;
    api->unsetenv = api_unsetenv;
    api->strerror = api_strerror;
    api->get_errno = api_get_errno;
    api->set_errno = api_set_errno;
    api->capabilities = api_capabilities;
    api->allocate = api_allocate;
    api->resize = api_resize;
    api->release = api_release;
    api->errno_location = api_errno_location;
    api->environ_location = api_environ_location;
    api->getopt_state_location = api_getopt_state_location;
    api->truncate = api_truncate;
    api->ftruncate = api_ftruncate;
    api->getprogname = api_getprogname;
}

static int host_ops_valid(const struct cb_host_ops_v1 *host)
{
    return host != NULL && host->abi_version == CB_ABI_VERSION_V1 &&
           host->struct_size >= sizeof(*host) && host->allocate != NULL &&
           host->resize != NULL && host->release != NULL &&
           host->context_root != NULL && host->context_create != NULL &&
           host->context_switch != NULL && host->context_destroy != NULL &&
           host->console_poll != NULL && host->console_read != NULL &&
           host->console_write != NULL && host->monotonic_millis != NULL &&
           host->wall_clock_millis != NULL && host->yield_host != NULL &&
           host->fatal != NULL;
}

struct cb_kernel *cb_kernel_create(const struct cb_host_ops_v1 *host)
{
    struct cb_kernel *kernel;
    if (!host_ops_valid(host))
        return NULL;
    kernel = host->allocate(sizeof(*kernel));
    if (kernel == NULL)
        return NULL;
    memset(kernel, 0, sizeof(*kernel));
    kernel->host = host;
    kernel->scheduler_context = host->context_root();
    if (kernel->scheduler_context == NULL) {
        host->release(kernel);
        return NULL;
    }
    kernel->capabilities.abi_version = CB_ABI_VERSION_V1;
    kernel->capabilities.struct_size = sizeof(kernel->capabilities);
    kernel->capabilities.native_modules = 1;
    kernel->capabilities.cooperative_tasks = 1;
    kernel->capabilities.spawn = 1;
    kernel->capabilities.exec = 1;
    kernel->capabilities.ramfs = 1;
    initialize_api(kernel);
    active_kernel = kernel;
    if (cb_vfs_initialize(kernel) < 0) {
        cb_kernel_destroy(kernel);
        return NULL;
    }
    return kernel;
}

void cb_kernel_destroy(struct cb_kernel *kernel)
{
    struct cb_task *task;
    size_t index;
    if (kernel == NULL)
        return;
    task = kernel->tasks;
    while (task != NULL) {
        struct cb_task *next = task->next;
        task_destroy(task);
        task = next;
    }
    for (index = 0; index < kernel->program_count; ++index)
        cb_executor_program_destroy(kernel, kernel->programs[index]);
    cb_vfs_destroy(kernel);
    kernel->host->context_destroy(kernel->scheduler_context);
    if (active_kernel == kernel)
        active_kernel = NULL;
    kernel->host->release(kernel);
}

int cb_kernel_register(struct cb_kernel *kernel,
                       const struct cb_program_v1 *program)
{
    return cb_kernel_register_executor(kernel, cb_native_executor(), program);
}

int cb_kernel_register_executor(struct cb_kernel *kernel,
                                const struct cb_executor_ops *executor,
                                const void *source)
{
    struct cb_program *program;
    char path[CB_PATH_MAX];
    int result;
    
    if (kernel == NULL) {
        return -1;
    }
    
    if (kernel->program_count >= CB_MAX_PROGRAMS) {
        return -1;
    }
    
    if (cb_executor_prepare(kernel, executor, source, &program) < 0) {
        return -1;
    }
    
    kernel->program_count++;
        
    if (strchr(program->name, '/') != NULL) {
        cb_executor_program_destroy(kernel, program);
        return -1;
    }
    
    result = snprintf(path, sizeof(path), "/bin/%s", program->name);
    if (result < 0 || (size_t)result >= sizeof(path)) {
        cb_executor_program_destroy(kernel, program);
        return -CB_ENAMETOOLONG;
    }
    
    result = cb_vfs_create_executable(kernel, path, program);
    if (result < 0) {
        cb_executor_program_destroy(kernel, program);
        return -1;
    }
    
    return 0;
}

int cb_kernel_boot(struct cb_kernel *kernel, const char *command)
{
    struct cb_vfs_node *shell_node;
    struct cb_task dummy_task;
    struct cb_task *task;
    int result;
    
    if (kernel == NULL || kernel->boot_pid != 0)
        return -1;
    
    memset(&dummy_task, 0, sizeof(dummy_task));
    dummy_task.kernel = kernel;
    dummy_task.cwd = kernel->vfs_root;
    dummy_task.root = kernel->vfs_root;
    
    result = resolve_executable_node(&dummy_task, "sh", &shell_node);
    if (result < 0) return -1;
    
    char *interactive_argv[] = {(char *)"sh", NULL};
    char *command_argv[] = {(char *)"sh", (char *)"-c", (char *)command,
                            NULL};
    char *environment[] = {(char *)"HOME=/home/user", (char *)"PATH=/bin",
                           NULL};
    struct cb_open_file *input;
    struct cb_open_file *output;
    struct cb_open_file *error;
    
    cb_vfs_node_retain(shell_node);
    task = task_create(kernel, NULL, shell_node->executable,
                       command == NULL ? interactive_argv : command_argv,
                       environment, NULL, 0);
    if (task == NULL) {
        cb_vfs_node_release(shell_node);
        return -1;
    }
    task->executable_node = shell_node;
    input = cb_open_file_create(kernel, &console_input_ops, CB_O_RDONLY);
    output = cb_open_file_create(kernel, &console_output_ops, CB_O_WRONLY);
    error = cb_open_file_create(kernel, &console_output_ops, CB_O_WRONLY);
    if (input == NULL || output == NULL || error == NULL) {
        cb_open_file_release(input);
        cb_open_file_release(output);
        cb_open_file_release(error);
        return -1;
    }
    input->object.console_stream = 0;
    output->object.console_stream = 1;
    error->object.console_stream = 2;
    fd_install_at(task, input, 0, 0);
    fd_install_at(task, output, 1, 0);
    fd_install_at(task, error, 2, 0);
    kernel->boot_pid = task->pid;
    return 0;
}

int cb_kernel_run(struct cb_kernel *kernel)
{
    unsigned idle_rounds = 0;
    if (kernel == NULL || kernel->boot_pid == 0)
        return -1;
    active_kernel = kernel;
    while (!kernel->boot_finished) {
        struct cb_task *task;
        if (kernel->host->console_poll(0) > 0)
            wake_console_waiters(kernel);
        task = pick_runnable(kernel);
        if (task == NULL) {
            if (has_console_waiter(kernel)) {
                if (kernel->host->console_poll(-1) >= 0)
                    wake_console_waiters(kernel);
                continue;
            }
            if (++idle_rounds > 1) {
                kernel->host->fatal("deadlock: tasks blocked without host events");
                return -1;
            }
            kernel->host->yield_host();
            continue;
        }
        idle_rounds = 0;
        kernel->current = task;
        task->state = CB_TASK_RUNNING;
        cb_executor_start_or_resume(task->execution);
        kernel->current = NULL;
        if (task->state == CB_TASK_EXEC_PENDING)
            task_finish_exec(task);
    }
    return kernel->boot_status;
}

const struct cb_api_v1 *cb_kernel_api(struct cb_kernel *kernel)
{
    return kernel == NULL ? NULL : &kernel->api;
}
