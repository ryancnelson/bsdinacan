/* Production-code callbacks: no host FILE or timing assumptions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../libc/cb_libc.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"fgetln line %d\n",__LINE__); exit(1); } } while (0)
static int err, reads, live, allocations, fail_at = -1, read_error_at = -1, oversized;
static size_t offset, extent;
static unsigned char data[512];
static struct cb_input_state_v1 input;
static struct cb_input_state_v1 *supplied = &input;
static int get_error(void) { return err; }
static void set_error(int e) { err=e; }
static struct cb_input_state_v1 *get_input(void) { err=CB_EIO; return supplied; }
static void *alloc(size_t n) {
    int call=allocations++;
    if (call == fail_at) { err=CB_ENOMEM; return NULL; }
    ++live; err=0; return malloc(n);
}
static void *resize(void *p,size_t n) {
    int call=allocations++;
    if (call == fail_at) { err=CB_ENOMEM; return NULL; }
    if (!p) ++live;
    err=0; return realloc(p,n);
}
static void release(void *p) { if (p) { --live; free(p); } err=CB_EIO; }
static int open_file(const char *p,int f,uint32_t m) { (void)p;(void)f;(void)m; return 3; }
static int close_file(int fd) { (void)fd; err=CB_EPIPE; return -1; }
static cb_ssize_t transfer(int fd,void *p,size_t n) {
    (void)fd; CHECK(n==1); ++reads;
    if ((int)offset==read_error_at) { err=CB_EIO; return -1; }
    if (oversized) return INT64_C(4294967360);
    err=CB_EIO;
    if (offset==extent) return 0;
    *(unsigned char *)p=data[offset++]; return 1;
}
static void reset(void) {
    CHECK(live==0);
    memset(&input,0,sizeof(input)); input.abi_version=CB_ABI_VERSION_V1;
    input.struct_size=sizeof(input); supplied=&input;
    reads=allocations=0; offset=0; extent=0; fail_at=read_error_at=-1; oversized=0;
}
int main(void) {
    struct cb_api_v1 api={0}; struct cb_libc_file *file; char *p,*held;
    size_t n, required, old_reads, size; unsigned char *short_storage;
    api.abi_version=CB_ABI_VERSION_V1; api.struct_size=sizeof(api);
    api.get_errno=get_error; api.set_errno=set_error; api.allocate=alloc;
    api.resize=resize; api.release=release; api.open=open_file;
    api.close=close_file; api.read=transfer; api.input_state_location=get_input; bound_api=&api;
    reset(); data[0]=0;data[1]=255;data[2]='\n';data[3]='z';extent=4;
    file=cb_libc_fopen("x","r"); CHECK(file);
    err=CB_ERANGE; p=cb_libc_fgetln(file,&n);
    CHECK(p && n==3 && memcmp(p,data,3)==0 && p[3]==0 && offset==3 && err==CB_ERANGE);
    held=p; held[0]='X'; old_reads=reads;
    CHECK(cb_libc_fread(NULL,0,1,file)==0 && reads==(int)old_reads && held[0]=='X');
    CHECK(!cb_libc_ferror(file) && !cb_libc_feof(file) && held[0]=='X');
    CHECK(cb_libc_getc(file)=='z' && offset==4);
    CHECK(cb_libc_fgetln(file,&n)==NULL && n==0 && cb_libc_feof(file) && err==CB_ERANGE);
    old_reads=reads; CHECK(!cb_libc_fgetln(file,&n) && reads==(int)old_reads);
    CHECK(cb_libc_fclose(file)==EOF && err==CB_EPIPE && live==0);
    reset(); memset(data,'a',sizeof(data));extent=sizeof(data);
    file=cb_libc_fopen("x","r"); fail_at=allocations+1;
    CHECK(!cb_libc_fgetln(file,&n) && n==0 && offset==63 && err==CB_ENOMEM && cb_libc_ferror(file));
    CHECK(live==2); fail_at=-1; cb_libc_clearerr(file); err=CB_ERANGE;
    p=cb_libc_fgetln(file,&n); CHECK(p && n==449 && cb_libc_feof(file) && !cb_libc_ferror(file) && err==CB_ERANGE);
    CHECK(cb_libc_fclose(file)==EOF && live==0);
    reset(); memcpy(data,"ab\n",3);extent=3;read_error_at=2;
    file=cb_libc_fopen("x","r"); CHECK(!cb_libc_fgetln(file,&n) && n==0 && offset==2 && err==CB_EIO);
    CHECK(cb_libc_ferror(file) && !cb_libc_feof(file)); read_error_at=-1;err=CB_ERANGE;
    p=cb_libc_fgetln(file,&n); CHECK(p && n==1 && p[0]=='\n' && cb_libc_ferror(file) && err==CB_ERANGE);
    CHECK(cb_libc_fclose(file)==EOF && live==0);
    reset(); file=cb_libc_fopen("x","r"); oversized=1;
    CHECK(!cb_libc_fgetln(file,&n) && n==0 && err==CB_EIO && offset==0 && cb_libc_ferror(file));
    CHECK(cb_libc_fclose(file)==EOF && live==0);
    reset(); fail_at=0;
    CHECK(!cb_libc_fgetln(cb_libc_stdin_stream,&n) && n==0 && err==CB_ENOMEM && reads==0 && live==0);
    fail_at=allocations+1;
    CHECK(!cb_libc_fgetln(cb_libc_stdin_stream,&n) && err==CB_ENOMEM && reads==0 && live==1);
    fail_at=-1;cb_libc_clearerr(cb_libc_stdin_stream);memcpy(data,"s\n",2);extent=2;err=CB_ERANGE;
    p=cb_libc_fgetln(cb_libc_stdin_stream,&n);CHECK(p && n==2 && err==CB_ERANGE && live==2);
    CHECK(cb_libc_fclose(cb_libc_stdin_stream)==EOF && input.stdin_line_storage==NULL && live==0 && err==CB_EPIPE);
    reset(); file=cb_libc_fopen("x","w");
    CHECK(!cb_libc_fgetln(file,&n) && err==CB_EINVAL && reads==0 && !cb_libc_ferror(file));
    CHECK(!cb_libc_fgetln((void *)1,&n) && err==CB_EINVAL && reads==0);
    CHECK(!cb_libc_fgetln(cb_libc_stdout_stream,&n) && err==CB_EINVAL && reads==0);
    CHECK(!cb_libc_fgetln(file,NULL) && err==CB_EINVAL && reads==0);
    CHECK(cb_libc_fclose(file)==EOF && live==0);
    reset();
    /* Real short allocations, including a partially present appended pointer. */
    for (size=CB_INPUT_STATE_V1_MIN_SIZE; size<CB_INPUT_LINE_V1_MIN_SIZE; ++size) {
        short_storage=malloc(size);CHECK(short_storage);
        input.struct_size=(uint32_t)size;memcpy(short_storage,&input,size);supplied=(void *)short_storage;
        old_reads=reads; n=99;
        CHECK(!cb_libc_fgetln(cb_libc_stdin_stream,&n) && n==0 && err==CB_ENOSYS && reads==(int)old_reads && live==0);
        if (size>=CB_INPUT_STREAMS_V1_MIN_SIZE) {
            offset=0;extent=1;data[0]='\n';
            file=cb_libc_fopen("x","r");CHECK(file);
            CHECK(cb_libc_fgetln(file,&n) && n==1);
            CHECK(cb_libc_fclose(file)==EOF && live==0);
        }
        free(short_storage);
    }
    supplied=&input;input.struct_size=sizeof(input);
    size=offsetof(struct cb_api_v1,input_state_location);
    short_storage=malloc(size);CHECK(short_storage);api.struct_size=(uint32_t)size;memcpy(short_storage,&api,size);bound_api=(void *)short_storage;
    CHECK(!cb_libc_fgetln(cb_libc_stdin_stream,&n) && err==CB_ENOSYS && live==0);
    free(short_storage);bound_api=&api;api.struct_size=sizeof(api);
    api.input_state_location=NULL;CHECK(!cb_libc_fgetln(cb_libc_stdin_stream,&n) && err==CB_ENOSYS);
    api.input_state_location=get_input;supplied=NULL;CHECK(!cb_libc_fgetln(cb_libc_stdin_stream,&n) && err==CB_ENOSYS);
    supplied=&input;input.abi_version=0;CHECK(!cb_libc_fgetln(cb_libc_stdin_stream,&n) && err==CB_ENOSYS);
    CHECK(line_requirement(SIZE_MAX-1,&required)==-1 && line_requirement(SIZE_MAX-2,&required)==0 && required==SIZE_MAX);
    puts("fgetln callback tests passed");return 0;
}
