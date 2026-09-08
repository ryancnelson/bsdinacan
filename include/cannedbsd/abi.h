#ifndef CANNEDBSD_ABI_H
#define CANNEDBSD_ABI_H

#include <stddef.h>
#include <stdint.h>

#define CB_ABI_VERSION_V1 UINT32_C(0x00010000)
#define CB_MAX_FDS 64
#define CB_PATH_MAX 1024

typedef int32_t cb_pid_t;
typedef int64_t cb_off_t;
typedef int64_t cb_ssize_t;

enum cb_error {
    CB_EPERM = 1,
    CB_ENOENT = 2,
    CB_EINTR = 4,
    CB_EIO = 5,
    CB_EBADF = 9,
    CB_ECHILD = 10,
    CB_ENOMEM = 12,
    CB_EACCES = 13,
    CB_EEXIST = 17,
    CB_ENOTDIR = 20,
    CB_EISDIR = 21,
    CB_EINVAL = 22,
    CB_ENFILE = 23,
    CB_EMFILE = 24,
    CB_ENOSPC = 28,
    CB_ESPIPE = 29,
    CB_EPIPE = 32,
    CB_ENAMETOOLONG = 63,
    CB_ENOSYS = 78,
    CB_ENOTEMPTY = 66,
    CB_EFAULT = 14
};

enum cb_open_flag {
    CB_O_RDONLY = 0x0000,
    CB_O_WRONLY = 0x0001,
    CB_O_RDWR = 0x0002,
    CB_O_ACCMODE = 0x0003,
    CB_O_APPEND = 0x0008,
    CB_O_CREAT = 0x0200,
    CB_O_TRUNC = 0x0400
};

enum cb_seek_whence {
    CB_SEEK_SET = 0,
    CB_SEEK_CUR = 1,
    CB_SEEK_END = 2
};

enum cb_node_type {
    CB_NODE_REGULAR = 1,
    CB_NODE_DIRECTORY = 2,
    CB_NODE_TERMINAL = 3,
    CB_NODE_PIPE = 4
};

struct cb_stat_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t inode;
    uint64_t size;
    uint32_t mode;
    uint32_t type;
};

enum cb_spawn_action_type {
    CB_SPAWN_DUP2 = 1,
    CB_SPAWN_CLOSE = 2
};

struct cb_spawn_action_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t type;
    int32_t from_fd;
    int32_t to_fd;
};

struct cb_capabilities_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t native_modules;
    uint32_t cooperative_tasks;
    uint32_t memory_protection;
    uint32_t spawn;
    uint32_t exec;
    uint32_t fork;
    uint32_t vfork;
    uint32_t ramfs;
    uint32_t persistent_fs;
    uint32_t host_mounts;
    uint32_t network_sockets;
    uint32_t unix_sockets;
    uint32_t pty;
    uint32_t wasm_executor;
    uint32_t x11;
};

/*
 * Task-local getopt(3) state, owned by the runtime and returned by address
 * so the libc veneer's optind/optarg/opterr/optopt lvalues, and getopt's own
 * private scan cursor, are isolated per task under cooperative interleaving
 * and reset on a successful exec. Not a public POSIX structure; ordinary
 * source only ever sees the individual fields through macros.
 */
struct cb_getopt_state_v1 {
    int optind;
    int opterr;
    int optopt;
    char *optarg;
    char *place;
};

struct cb_pollfd {
    int fd;
    short events;
    short revents;
};

#define CB_POLLIN   0x001
#define CB_POLLPRI  0x002
#define CB_POLLOUT  0x004
#define CB_POLLERR  0x008
#define CB_POLLHUP  0x010
#define CB_POLLNVAL 0x020

struct cb_api_v1 {
    uint32_t abi_version;
    uint32_t struct_size;

    cb_pid_t (*getpid)(void);
    cb_pid_t (*getppid)(void);
    int (*spawn)(const char *program, char *const argv[], char *const envp[],
                 const struct cb_spawn_action_v1 *actions,
                 size_t action_count, cb_pid_t *pid_out);
    int (*exec)(const char *program, char *const argv[], char *const envp[]);
    void (*exit)(int status);
    cb_pid_t (*waitpid)(cb_pid_t pid, int *status);
    void (*yield)(void);

    int (*open)(const char *path, int flags, uint32_t mode);
    int (*close)(int fd);
    cb_ssize_t (*read)(int fd, void *buffer, size_t count);
    cb_ssize_t (*write)(int fd, const void *buffer, size_t count);
    cb_off_t (*lseek)(int fd, cb_off_t offset, int whence);
    int (*dup)(int fd);
    int (*dup2)(int old_fd, int new_fd);
    int (*set_cloexec)(int fd, int enabled);
    int (*pipe)(int fds[2]);
    int (*fstat)(int fd, struct cb_stat_v1 *stat_buffer);

    int (*stat)(const char *path, struct cb_stat_v1 *stat_buffer);
    int (*mkdir)(const char *path, uint32_t mode);
    int (*unlink)(const char *path);
    int (*chdir)(const char *path);
    char *(*getcwd)(char *buffer, size_t size);

    const char *(*getenv)(const char *name);
    int (*setenv)(const char *name, const char *value, int overwrite);
    int (*unsetenv)(const char *name);

    const char *(*strerror)(int error);
    int (*get_errno)(void);
    void (*set_errno)(int error);
    const struct cb_capabilities_v1 *(*capabilities)(void);

    void *(*allocate)(size_t size);
    void *(*resize)(void *pointer, size_t size);
    void (*release)(void *pointer);
    int *(*errno_location)(void);
    char ***(*environ_location)(void);
    struct cb_getopt_state_v1 *(*getopt_state_location)(void);
    int (*truncate)(const char *path, cb_off_t length);
    int (*ftruncate)(int fd, cb_off_t length);
    const char *(*getprogname)(void);
    int (*poll)(struct cb_pollfd *fds, size_t nfds, int timeout);
};

struct cb_program_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *name;
    uint32_t flags;
    size_t requested_stack_size;
    int (*start)(const struct cb_api_v1 *api, int argc,
                 char *const argv[], char *const envp[]);
};

#endif
