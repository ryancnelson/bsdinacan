#include "internal.h"
#include <string.h>

/* Actual pinned uniq tasks. The controller uses the real task API; deterministic
   pipe backpressure exercises the same production executor as ordinary commands. */
static struct cb_kernel *kernel;
static struct cb_api_v1 real;
static unsigned char output[20000], errors[1024];
static size_t output_size, errors_size;
static unsigned fault, fault_hits, reads;
static cb_ssize_t console_write(int stream, const void *p, size_t n)
{
    unsigned char *b = stream == 1 ? output : errors;
    size_t *used = stream == 1 ? &output_size : &errors_size;
    size_t cap = stream == 1 ? sizeof(output) : sizeof(errors);
    if (n > cap - *used) return -CB_EIO;
    memcpy(b + *used, p, n); *used += n;
    return (cb_ssize_t)n;
}
static int is_uniq(void)
{
    return kernel->current && strcmp(kernel->current->program->name, "uniq") == 0;
}
static void *allocate(size_t n)
{
    if (is_uniq() && fault == 3 && !fault_hits) {
        ++fault_hits; real.set_errno(CB_ENOMEM); return NULL;
    }
    return real.allocate(n);
}
static cb_ssize_t read_file(int fd, void *p, size_t n)
{
    if (is_uniq() && fault == 1 && ++reads == 3) {
        ++fault_hits; real.set_errno(CB_EIO); return -1;
    }
    return real.read(fd, p, n);
}
static cb_ssize_t write_file(int fd, const void *p, size_t n)
{
    if (is_uniq() && fault == 2 && fd == 1) {
        ++fault_hits; real.set_errno(CB_EIO); return -1;
    }
    return real.write(fd, p, n);
}
static struct cb_task *task(cb_pid_t pid)
{
    struct cb_task *t;
    for (t = kernel->tasks; t; t = t->next) if (t->pid == pid) return t;
    return NULL;
}
static int wait_state(const struct cb_api_v1 *api, cb_pid_t pid, enum cb_task_state state)
{
    unsigned i;
    for (i = 0; i < 200; ++i) {
        struct cb_task *t = task(pid);
        if (!t) return -1;
        if (t->state == state) return 0;
        api->yield();
    }
    return -1;
}
static int collect(const struct cb_api_v1 *api, cb_pid_t pid, int expected)
{
    struct cb_task *t;
    int status, fd;
    if (wait_state(api, pid, CB_TASK_ZOMBIE)) return -1;
    t = task(pid);
    /* Check heap and descriptor cleanup before reap can hide it. Includes
       obsolete's replaced argv allocations and both fgetln/line buffers. */
    if (t->allocations) return -2;
    for (fd = 0; fd < CB_MAX_FDS; ++fd) if (t->descriptors[fd].file) return -3;
    return api->waitpid(pid, &status) == pid && status == expected ? 0 : -4;
}
static int put(const struct cb_api_v1 *api, const char *path, const void *p, size_t n)
{
    int fd = api->open(path, CB_O_CREAT | CB_O_TRUNC | CB_O_WRONLY, 0666);
    if (fd < 0 || api->write(fd, p, n) != (cb_ssize_t)n || api->close(fd)) return -1;
    return 0;
}
static int check(const struct cb_api_v1 *api, const char *path, const void *p, size_t n)
{
    unsigned char b[128];
    size_t pos = 0;
    int fd = api->open(path, CB_O_RDONLY, 0);
    if (fd < 0) return -1;
    while (pos < n) {
        size_t part = n-pos < sizeof(b) ? n-pos : sizeof(b);
        if (api->read(fd, b, part) != (cb_ssize_t)part ||
            memcmp(b, (const unsigned char *)p+pos, part)) return -1;
        pos += part;
    }
    return api->read(fd, b, 1) == 0 && api->close(fd) == 0 ? 0 : -1;
}
static int run(const struct cb_api_v1 *api, char *const envp[], char *const args[],
               const void *input, size_t size, const void *expected, size_t n,
               int status, int diagnostic)
{
    struct cb_spawn_action_v1 a[2] = {
        {CB_ABI_VERSION_V1, sizeof(a[0]), CB_SPAWN_DUP2, 0, 0},
        {CB_ABI_VERSION_V1, sizeof(a[0]), CB_SPAWN_CLOSE, 0, 0}
    };
    cb_pid_t pid;
    int fd;
    output_size = errors_size = reads = fault_hits = 0;
    if (put(api, "/input", input, size)) return 1;
    fd = api->open("/input", CB_O_RDONLY, 0);
    if (fd < 0) return 2;
    a[0].from_fd = a[1].from_fd = fd;
    if (api->spawn("uniq", args, envp, a, 2, &pid) || api->close(fd)) return 3;
    if (collect(api, pid, status)) return 4;
    if (output_size != n || memcmp(output, expected, n)) return 5;
    if ((diagnostic && !errors_size) || (!diagnostic && errors_size)) return 6;
    if (fault && !fault_hits) return 7;
    return 0;
}
static int pipe_child(const struct cb_api_v1 *api, char *const envp[],
                      char *const args[], const char *path, cb_pid_t *pid, int *writer)
{
    int fds[2], fd, i;
    struct cb_spawn_action_v1 a[CB_MAX_FDS+2];
    size_t n = 2;
    if (api->pipe(fds)) return -1;
    fd = api->open(path, CB_O_CREAT | CB_O_TRUNC | CB_O_WRONLY, 0666);
    if (fd < 0) return -1;
    a[0] = (struct cb_spawn_action_v1){CB_ABI_VERSION_V1, sizeof(a[0]), CB_SPAWN_DUP2, fds[0], 0};
    a[1] = (struct cb_spawn_action_v1){CB_ABI_VERSION_V1, sizeof(a[0]), CB_SPAWN_DUP2, fd, 1};
    for (i = 3; i < CB_MAX_FDS; ++i)
        a[n++] = (struct cb_spawn_action_v1){CB_ABI_VERSION_V1, sizeof(a[0]), CB_SPAWN_CLOSE, i, 0};
    if (api->spawn("uniq", args, envp, a, n, pid) || api->close(fds[0]) || api->close(fd)) return -1;
    *writer = fds[1]; return 0;
}
static int interleave(const struct cb_api_v1 *api, char *const envp[], char *const args[],
                      const char *input, const char *tail, const char *expected)
{
    char *plain[] = {(char *)"uniq", NULL};
    cb_pid_t a, b;
    int wa, wb;
    if (pipe_child(api, envp, args, "/a", &a, &wa) ||
        wait_state(api, a, CB_TASK_BLOCKED_PIPE) ||
        api->write(wa, input, strlen(input)) != (cb_ssize_t)strlen(input) ||
        wait_state(api, a, CB_TASK_BLOCKED_PIPE)) return 1;
    /* A is suspended inside actual fgetln after flags and repeats changed.
       Run B to completion with conflicting defaults, then resume A. */
    if (pipe_child(api, envp, plain, "/b", &b, &wb) ||
        wait_state(api, b, CB_TASK_BLOCKED_PIPE) ||
        api->write(wb, "p\np\nq\n", 6) != 6 || api->close(wb) || collect(api, b, 0) ||
        check(api, "/b", "p\nq\n", 4)) return 2;
    if (api->write(wa, tail, strlen(tail)) != (cb_ssize_t)strlen(tail) ||
        api->close(wa) || collect(api, a, 0) || check(api, "/a", expected, strlen(expected))) return 3;
    return 0;
}
static int write_block(const struct cb_api_v1 *api, char *const envp[])
{
    char *args[] = {(char *)"uniq", (char *)"-c", (char *)"/big", NULL};
    char *plain[] = {(char *)"uniq", NULL};
    static char input[4000], expected[14000];
    struct cb_spawn_action_v1 a[CB_MAX_FDS+1];
    cb_pid_t pid, downstream;
    size_t i, n = 1;
    int fds[2], fd;
    /* Many distinct short lines exceed the real pipe buffer. */
    for (i = 0; i < 2000; ++i) {
        input[2*i] = i % 2 ? 'b' : 'a'; input[2*i+1] = '\n';
        memcpy(expected+7*i, "   1 a\n", 7); expected[7*i+5] = input[2*i];
    }
    if (put(api, "/big", input, 4000) || api->pipe(fds)) return 1;
    a[0] = (struct cb_spawn_action_v1){CB_ABI_VERSION_V1, sizeof(a[0]), CB_SPAWN_DUP2, fds[1], 1};
    for (fd = 3; fd < CB_MAX_FDS; ++fd)
        a[n++] = (struct cb_spawn_action_v1){CB_ABI_VERSION_V1, sizeof(a[0]), CB_SPAWN_CLOSE, fd, 0};
    if (api->spawn("uniq", args, envp, a, n, &pid) || api->close(fds[1]) ||
        wait_state(api, pid, CB_TASK_BLOCKED_PIPE)) return 2;
    if (run(api, envp, plain, "z\nz\n", 4, "z\n", 2, 0, 0)) return 3;
    output_size = errors_size = 0;
    a[0] = (struct cb_spawn_action_v1){CB_ABI_VERSION_V1, sizeof(a[0]), CB_SPAWN_DUP2, fds[0], 0};
    n = 1;
    for (fd = 3; fd < CB_MAX_FDS; ++fd)
        a[n++] = (struct cb_spawn_action_v1){CB_ABI_VERSION_V1, sizeof(a[0]), CB_SPAWN_CLOSE, fd, 0};
    if (api->spawn("uniq", plain, envp, a, n, &downstream) || api->close(fds[0]) ||
        collect(api, pid, 0) || collect(api, downstream, 0) ||
        output_size != sizeof(expected) || memcmp(output, expected, sizeof(expected)) || errors_size) return 4;
    return 0;
}
static int controller(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    char *plain[] = {(char *)"uniq", NULL};
    char *count[] = {(char *)"uniq", (char *)"-c", NULL};
    char *duplicate[] = {(char *)"uniq", (char *)"-d", NULL};
    char *unique[] = {(char *)"uniq", (char *)"-u", NULL};
    char *field[] = {(char *)"uniq", (char *)"-f", (char *)"1", NULL};
    char *chars[] = {(char *)"uniq", (char *)"-s", (char *)"1", NULL};
    char *oldf[] = {(char *)"uniq", (char *)"-1", NULL};
    char *olds[] = {(char *)"uniq", (char *)"+1", NULL};
    char *files[] = {(char *)"uniq", (char *)"-c", (char *)"/input", (char *)"/out", NULL};
    char *boundary[] = {(char *)"uniq", (char *)"-f", (char *)"2147483647", NULL};
    char *stop[] = {(char *)"uniq", (char *)"--", (char *)"/input", NULL};
    char *missing[] = {(char *)"uniq", (char *)"/missing", NULL};
    char *bad[] = {(char *)"uniq", (char *)"-f", (char *)"-1", NULL};
    static char longline[6001];
    static const char binary[] = {'a',0,'x','\n','a',0,'y','\n'};
    int r; unsigned i;
    (void)argc; (void)argv;
#define RUN(id, args, text, expected) do { r=run(api,envp,args,text,sizeof(text)-1,expected,sizeof(expected)-1,0,0); if(r) return id*100+r; } while(0)
    RUN(1,count,"a\na\nb\n","   2 a\n   1 b\n"); RUN(2,plain,"a\na\nb\n","a\nb\n");
    RUN(3,count,"a\na\n","   2 a\n"); RUN(4,count,"a\na\n","   2 a\n");
    RUN(5,duplicate,"a\na\nb\n","a\n"); RUN(6,plain,"a\na\nb\n","a\nb\n");
    RUN(7,unique,"a\na\nb\n","b\n"); RUN(8,plain,"a\na\nb\n","a\nb\n");
    RUN(9,field,"x a\ny a\n","x a\n"); RUN(10,plain,"x a\ny a\n","x a\ny a\n");
    RUN(11,chars,"xa\nya\n","xa\n"); RUN(12,plain,"xa\nya\n","xa\nya\n");
    RUN(13,oldf,"x a\ny a\n","x a\n"); RUN(14,olds,"xa\nya\n","xa\n");
    if(strcmp(oldf[1],"-1") || strcmp(olds[1],"+1")) return 1500;
    RUN(16,plain,"",""); RUN(17,plain,"a\na","a\na");
    RUN(18,files,"a\na\nb\n","");
    if(check(api,"/out","   2 a\n   1 b\n",14)) return 1809;
    for(i=0;i<6000;++i) longline[i]='L';
    longline[6000]='\n';
    if(run(api,envp,plain,longline,sizeof(longline),longline,sizeof(longline),0,0)) return 1900;
    /* Equal lengths and strcmp's first NUL collapse the two binary lines;
       show emits only the prefix. This records the inherited C-string limit. */
    if(run(api,envp,plain,binary,sizeof(binary),"a",1,0,0)) return 2000;
    if(run(api,envp,bad,"",0,"",0,1,1)) return 2100;
    RUN(30,boundary,"x\ny\n","x\n");
    boundary[2]=(char *)"2147483648";
    if(run(api,envp,boundary,"x\ny\n",4,sizeof(long)>sizeof(int) ? "" : "x\n",
           sizeof(long)>sizeof(int) ? 0 : 2,sizeof(long)>sizeof(int) ? 1 : 0,
           sizeof(long)>sizeof(int) ? 1 : 0)) return 3100;
    boundary[2]=(char *)"4294967296";
    if(run(api,envp,boundary,"x\ny\n",4, sizeof(long)>sizeof(int) ? "x\ny\n" : "x\n",
           sizeof(long)>sizeof(int) ? 4 : 2,0,0)) return 3200;
    boundary[2]=(char *)"999999999999999999999999999";
    if(run(api,envp,boundary,"x\ny\n",4,sizeof(long)>sizeof(int) ? "" : "x\n",
           sizeof(long)>sizeof(int) ? 0 : 2,sizeof(long)>sizeof(int) ? 1 : 0,
           sizeof(long)>sizeof(int) ? 1 : 0)) return 3300;
    RUN(34,stop,"a\na\n","a\n");
    if(run(api,envp,missing,"",0,"",0,1,1)) return 3500;
    if(put(api,"/out","LONG PREVIOUS OUTPUT",20)) return 3600;
    RUN(36,files,"a\na\n","");
    if(check(api,"/out","   2 a\n",7)) return 3609;
    fault=3;
    /* fgetln allocation failure is treated as EOF by pinned main, status 0. */
    if(run(api,envp,plain,"a\n",2,"",0,0,0)) return 3700;
    fault=0;
    fault=1; RUN(22,plain,"a\nb\n","a\n"); fault=0;
    fault=2; RUN(23,plain,"a\na\n",""); fault=0;
#undef RUN
#define INTER(id,args,input,tail,expected) do { r=interleave(api,envp,args,input,tail,expected); if(r) return id*100+r; } while(0)
    INTER(24,count,"a\na\n","a\nb\n","   3 a\n   1 b\n");
    INTER(25,duplicate,"a\na\n","b\n","a\n");
    INTER(26,unique,"a\na\n","b\n","b\n");
    /* First tail line still compares equal only with the resumed offset. */
    INTER(27,field,"x a\ny a\n","z a\nw b\n","x a\nw b\n");
    INTER(28,chars,"xa\nya\n","za\nwb\n","xa\nwb\n");
#undef INTER
    r=write_block(api,envp); return r ? 2900+r : 0;
}
static const struct cb_program_v1 program = {
    CB_ABI_VERSION_V1, sizeof(program), "uniqprobe", 0, 64*1024, controller
};
int cb_uniq_probe(const struct cb_host_ops_v1 *base)
{
    struct cb_host_ops_v1 copy=*base;
    int result; unsigned iteration;
    for (iteration=0; iteration<2; ++iteration) {
    kernel=NULL; fault=fault_hits=reads=0; output_size=errors_size=0;
    copy.console_write=console_write;
    kernel=cb_kernel_create(&copy); if(!kernel) return 90;
    cb_register_base_programs(kernel); real=kernel->api;
    kernel->api.read=read_file; kernel->api.write=write_file; kernel->api.allocate=allocate;
    if(cb_kernel_register(kernel,&program) || cb_kernel_boot(kernel,"uniqprobe")) {
        cb_kernel_destroy(kernel); kernel=NULL; return 91;
    }
    result=cb_kernel_run(kernel); cb_kernel_destroy(kernel); kernel=NULL;
    if(result) return result;
    }
    return 0;
}
