#ifndef CANNEDBSD_INTERNAL_H
#define CANNEDBSD_INTERNAL_H

#include "cannedbsd/abi.h"

#include <stddef.h>
#include <stdint.h>

struct cb_host_context;

struct cb_host_ops_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void *(*allocate)(size_t size);
    void *(*resize)(void *pointer, size_t size);
    void (*release)(void *pointer);
    struct cb_host_context *(*context_root)(void);
    struct cb_host_context *(*context_create)(void (*entry)(void *), void *arg,
                                               size_t stack_size);
    void (*context_switch)(struct cb_host_context *from,
                           struct cb_host_context *to);
    void (*context_destroy)(struct cb_host_context *context);
    int (*console_poll)(int timeout_ms);
    cb_ssize_t (*console_read)(void *buffer, size_t count);
    cb_ssize_t (*console_write)(int stream, const void *buffer, size_t count);
    uint64_t (*monotonic_millis)(void);
    uint64_t (*wall_clock_millis)(void);
    void (*yield_host)(void);
    void (*fatal)(const char *message);
};

const struct cb_host_ops_v1 *cb_linux_host_ops(void);

struct cb_kernel;
struct cb_task;
struct cb_open_file;
struct cb_program;
struct cb_execution;
struct cb_vfs_mount;
struct cb_vfs_node;

struct cb_executor_ops {
    uint32_t abi_version;
    uint32_t struct_size;
    int (*prepare)(struct cb_kernel *kernel,
                   const struct cb_executor_ops *executor,
                   const void *source,
                   struct cb_program **program_out);
    struct cb_execution *(*instance_create)(struct cb_task *task,
                                            const struct cb_program *program);
    void (*start_or_resume)(struct cb_execution *execution);
    void (*suspend)(struct cb_execution *execution);
    void (*request_termination)(struct cb_execution *execution);
    void (*instance_destroy)(struct cb_execution *execution);
    void (*program_destroy)(struct cb_kernel *kernel,
                            struct cb_program *program);
};

struct cb_program {
    const struct cb_executor_ops *executor;
    const char *name;
};

struct cb_execution {
    const struct cb_executor_ops *executor;
    struct cb_task *task;
    const struct cb_program *program;
};

enum cb_task_state {
    CB_TASK_RUNNABLE,
    CB_TASK_RUNNING,
    CB_TASK_BLOCKED_PIPE,
    CB_TASK_BLOCKED_CONSOLE,
    CB_TASK_BLOCKED_WAIT,
    CB_TASK_EXEC_PENDING,
    CB_TASK_ZOMBIE,
    CB_TASK_DEAD
};

enum cb_wake_reason {
    CB_WAKE_NONE,
    CB_WAKE_PIPE_CHANGED,
    CB_WAKE_CONSOLE_READY,
    CB_WAKE_CHILD_EXITED
};

struct cb_vfs_mount_ops {
    uint32_t abi_version;
    uint32_t struct_size;
    struct cb_vfs_node *(*root)(struct cb_vfs_mount *mount);
    void (*destroy)(struct cb_vfs_mount *mount);
};

struct cb_vfs_node_ops {
    uint32_t abi_version;
    uint32_t struct_size;
    void (*retain)(struct cb_vfs_node *node);
    void (*release)(struct cb_vfs_node *node);
    int (*lookup)(struct cb_vfs_node *directory, const char *name,
                  size_t name_length, struct cb_vfs_node **node_out);
    int (*create)(struct cb_vfs_node *directory, const char *name,
                  uint32_t type, uint32_t mode,
                  struct cb_vfs_node **node_out);
    int (*unlink)(struct cb_vfs_node *node);
    int (*open)(struct cb_vfs_node *node, struct cb_task *task, int flags,
                struct cb_open_file **file_out);
    int (*stat)(struct cb_vfs_node *node, struct cb_stat_v1 *stat_buffer);
    struct cb_vfs_node *(*parent)(struct cb_vfs_node *node);
    const char *(*name)(struct cb_vfs_node *node);
};

struct cb_vfs_mount {
    const struct cb_vfs_mount_ops *ops;
    struct cb_kernel *kernel;
};

struct cb_vfs_node {
    const struct cb_vfs_node_ops *ops;
    struct cb_vfs_mount *mount;
};

struct cb_file_ops {
    cb_ssize_t (*read)(struct cb_open_file *, struct cb_task *, void *, size_t);
    cb_ssize_t (*write)(struct cb_open_file *, struct cb_task *,
                        const void *, size_t);
    cb_off_t (*lseek)(struct cb_open_file *, struct cb_task *, cb_off_t, int);
    int (*stat)(struct cb_open_file *, struct cb_stat_v1 *);
    void (*last_close)(struct cb_open_file *);
};

struct cb_pipe {
    unsigned char data[4096];
    size_t read_position;
    size_t used;
    unsigned readers;
    unsigned writers;
};

struct cb_open_file {
    unsigned references;
    int flags;
    cb_off_t offset;
    const struct cb_file_ops *ops;
    struct cb_kernel *kernel;
    union {
        struct cb_vfs_node *node;
        struct cb_pipe *pipe;
        int console_stream;
    } object;
};

struct cb_fd_entry {
    struct cb_open_file *file;
    int close_on_exec;
};

struct cb_task {
    struct cb_kernel *kernel;
    cb_pid_t pid;
    cb_pid_t ppid;
    enum cb_task_state state;
    struct cb_execution *execution;
    const struct cb_program *program;
    char **argv;
    int argc;
    char **environment;
    struct cb_fd_entry descriptors[CB_MAX_FDS];
    struct cb_vfs_node *root;
    struct cb_vfs_node *cwd;
    int error;
    int exit_status;
    cb_pid_t waiting_for;
    enum cb_wake_reason wake_reason;
    const struct cb_program *pending_program;
    char **pending_argv;
    int pending_argc;
    char **pending_environment;
    struct cb_task *next;
};

#define CB_MAX_PROGRAMS 64

struct cb_kernel {
    const struct cb_host_ops_v1 *host;
    struct cb_host_context *scheduler_context;
    struct cb_task *tasks;
    struct cb_task *current;
    struct cb_task *schedule_cursor;
    cb_pid_t next_pid;
    cb_pid_t boot_pid;
    int boot_status;
    int boot_finished;
    struct cb_vfs_mount *root_mount;
    struct cb_vfs_node *vfs_root;
    uint64_t next_inode;
    struct cb_program *programs[CB_MAX_PROGRAMS];
    size_t program_count;
    struct cb_api_v1 api;
    struct cb_capabilities_v1 capabilities;
};

struct cb_kernel *cb_kernel_create(const struct cb_host_ops_v1 *host);
void cb_kernel_destroy(struct cb_kernel *kernel);
int cb_kernel_register(struct cb_kernel *kernel,
                       const struct cb_program_v1 *program);
int cb_kernel_register_executor(struct cb_kernel *kernel,
                                const struct cb_executor_ops *executor,
                                const void *source);
int cb_kernel_boot(struct cb_kernel *kernel, const char *command);
int cb_kernel_run(struct cb_kernel *kernel);
const struct cb_api_v1 *cb_kernel_api(struct cb_kernel *kernel);

const struct cb_executor_ops *cb_native_executor(void);
int cb_executor_prepare(struct cb_kernel *kernel,
                        const struct cb_executor_ops *executor,
                        const void *source, struct cb_program **program_out);
struct cb_execution *cb_executor_instance_create(
    struct cb_task *task, const struct cb_program *program);
void cb_executor_start_or_resume(struct cb_execution *execution);
void cb_executor_suspend(struct cb_execution *execution);
void cb_executor_request_termination(struct cb_execution *execution);
void cb_executor_instance_destroy(struct cb_execution *execution);
void cb_executor_program_destroy(struct cb_kernel *kernel,
                                 struct cb_program *program);

int cb_vfs_initialize(struct cb_kernel *kernel);
int cb_vfs_set_root_mount(struct cb_kernel *kernel,
                          struct cb_vfs_mount *mount);
void cb_vfs_destroy(struct cb_kernel *kernel);
struct cb_open_file *cb_vfs_open(struct cb_task *task, const char *path,
                                 int flags, uint32_t mode);
int cb_vfs_stat_path(struct cb_task *task, const char *path,
                     struct cb_stat_v1 *stat_buffer);
int cb_vfs_mkdir_path(struct cb_task *task, const char *path, uint32_t mode);
int cb_vfs_unlink_path(struct cb_task *task, const char *path);
int cb_vfs_chdir_path(struct cb_task *task, const char *path);
char *cb_vfs_getcwd_path(struct cb_task *task, char *buffer, size_t size);
void cb_vfs_node_retain(struct cb_vfs_node *node);
void cb_vfs_node_release(struct cb_vfs_node *node);

struct cb_vfs_mount *cb_ramfs_mount_create(struct cb_kernel *kernel);

void *cb_allocate(struct cb_kernel *kernel, size_t size);
void *cb_resize(struct cb_kernel *kernel, void *pointer, size_t size);
void cb_release(struct cb_kernel *kernel, void *pointer);
char *cb_string_duplicate(struct cb_kernel *kernel, const char *text);
void cb_task_set_error(struct cb_task *task, int error);
void cb_task_yield_as(struct cb_task *task, enum cb_task_state state);
void cb_wake_pipe_tasks(struct cb_kernel *kernel);
struct cb_open_file *cb_open_file_create(struct cb_kernel *kernel,
                                          const struct cb_file_ops *ops,
                                          int flags);
void cb_open_file_retain(struct cb_open_file *file);
void cb_open_file_release(struct cb_open_file *file);

void cb_register_base_programs(struct cb_kernel *kernel);
extern const struct cb_program_v1 cb_shell_program;
extern const struct cb_program_v1 cb_shell_builtin_program;

/* Test hooks exercise the real core without exposing internals to programs. */
int cb_test_path_normalize(const char *cwd, const char *path,
                           char *output, size_t output_size);
enum cb_wake_reason cb_test_current_wake_reason(void);

#endif
