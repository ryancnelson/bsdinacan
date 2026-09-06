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

enum cb_node_kind {
    CB_FS_DIRECTORY,
    CB_FS_REGULAR
};

struct cb_node {
    enum cb_node_kind kind;
    uint64_t inode;
    uint32_t mode;
    char *name;
    struct cb_node *parent;
    struct cb_node *children;
    struct cb_node *next_sibling;
    unsigned char *data;
    size_t size;
    size_t capacity;
    unsigned open_references;
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
        struct cb_node *node;
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
    struct cb_host_context *context;
    const struct cb_program_v1 *program;
    char **argv;
    int argc;
    char **environment;
    struct cb_fd_entry descriptors[CB_MAX_FDS];
    struct cb_node *root;
    struct cb_node *cwd;
    int error;
    int exit_status;
    cb_pid_t waiting_for;
    enum cb_wake_reason wake_reason;
    const struct cb_program_v1 *pending_program;
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
    struct cb_node *fs_root;
    uint64_t next_inode;
    const struct cb_program_v1 *programs[CB_MAX_PROGRAMS];
    size_t program_count;
    struct cb_api_v1 api;
    struct cb_capabilities_v1 capabilities;
};

struct cb_kernel *cb_kernel_create(const struct cb_host_ops_v1 *host);
void cb_kernel_destroy(struct cb_kernel *kernel);
int cb_kernel_register(struct cb_kernel *kernel,
                       const struct cb_program_v1 *program);
int cb_kernel_boot(struct cb_kernel *kernel, const char *command);
int cb_kernel_run(struct cb_kernel *kernel);
const struct cb_api_v1 *cb_kernel_api(struct cb_kernel *kernel);

int cb_fs_initialize(struct cb_kernel *kernel);
void cb_fs_destroy(struct cb_kernel *kernel);
struct cb_open_file *cb_fs_open(struct cb_task *task, const char *path,
                                int flags, uint32_t mode);
int cb_fs_stat_path(struct cb_task *task, const char *path,
                    struct cb_stat_v1 *stat_buffer);
int cb_fs_mkdir_path(struct cb_task *task, const char *path, uint32_t mode);
int cb_fs_unlink_path(struct cb_task *task, const char *path);
int cb_fs_chdir_path(struct cb_task *task, const char *path);
char *cb_fs_getcwd_path(struct cb_task *task, char *buffer, size_t size);

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
