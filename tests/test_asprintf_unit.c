/* White-box count boundary and allocator-fault tests. Ordinary source and
 * real task ownership are tested separately; no fabricated huge strings. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "../libc/cb_libc.c"
static int error_value, fail_allocate, allocation_calls, release_calls;
static size_t requested;
static unsigned char storage[8194];
static jmp_buf failed_legacy;
static int get_error(void) { return error_value; }
static void set_error(int value) { error_value = value; }
static void *allocate(size_t size)
{
    ++allocation_calls; requested = size;
    if (fail_allocate) { error_value = CB_ENOMEM; return NULL; }
    if (size > sizeof(storage) - 2) abort();
    memset(storage, 0xa5, sizeof(storage));
    error_value = CB_EIO; /* Success must restore caller errno. */
    return storage + 1;
}
static void release(void *pointer)
{
    if (pointer != storage + 1) abort();
    ++release_calls;
}
static void legacy_error(int status, const char *format, ...)
{
    if (status != 1 || strcmp(format, "malloc") || error_value != CB_ENOMEM)
        abort();
    longjmp(failed_legacy, 1);
}
#define asprintf cb_libc_asprintf
#define isdigit cb_libc_isdigit
#define err legacy_error
#include "fixtures/uniq_obsolete.h"
#undef err
#undef isdigit
#undef asprintf
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "asprintf unit failure line %d\n", __LINE__); exit(1); \
} } while (0)
int main(void)
{
    struct cb_api_v1 mock = {0};
    char *p;
    size_t total;
    unsigned i;
    char long_text[4097];
    const char *bad[] = {"%", "%d", "%%", "%2s", "%.1s", "%ls", "%*s", "%n", "%zu"};
    mock.get_errno = get_error; mock.set_errno = set_error;
    mock.allocate = allocate; mock.release = release; bound_api = &mock;
    error_value = CB_ENOENT;
    CHECK(cb_libc_asprintf(&p, "a%c%s%c%s", 0, "x", 255, "") == 4);
    CHECK(memcmp(p, "a\0x\xff\0", 5) == 0 && requested == 5);
    CHECK(storage[0] == 0xa5 && storage[6] == 0xa5 && error_value == CB_ENOENT);
    cb_libc_free(p);
    CHECK(release_calls == 1);
    CHECK(cb_libc_asprintf(&p, "%s", "") == 0 && requested == 1 && p[0] == 0);
    memset(long_text, 'x', sizeof(long_text)-1); long_text[4096] = 0;
    CHECK(cb_libc_asprintf(&p, "-%c%s", 'f', long_text) == 4098);
    CHECK(requested == 4099 && p[0] == '-' && p[1] == 'f' && p[4097] == 'x' && p[4098] == 0);
    CHECK(storage[4100] == 0xa5);
    for (i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        int before = allocation_calls;
        p = (char *)"sentinel";
        CHECK(cb_libc_asprintf(&p, bad[i]) == -1 && p == NULL && error_value == CB_EINVAL);
        CHECK(allocation_calls == before);
    }
    p = (char *)"sentinel";
    CHECK(cb_libc_asprintf(&p, NULL) == -1 && p == NULL && error_value == CB_EINVAL);
    CHECK(cb_libc_asprintf(NULL, "x") == -1 && error_value == CB_EINVAL);
    CHECK(cb_libc_asprintf(&p, "%s", (const char *)NULL) == -1 && p == NULL && error_value == CB_EINVAL);
    fail_allocate = 1; p = (char *)"sentinel";
    CHECK(cb_libc_asprintf(&p, "-%c%s", 's', "3") == -1 && p == NULL && error_value == CB_ENOMEM);
    if (setjmp(failed_legacy) == 0) {
        char *args[] = {(char *)"uniq", (char *)"-3", NULL};
        obsolete(args);
        CHECK(0); /* Actual caller must detect NULL and reach err. */
    }
    total = (size_t)INT_MAX - 1;
    CHECK(asprintf_add(&total, 1) == 0 && total == (size_t)INT_MAX);
    CHECK(asprintf_add(&total, 1) == -1 && total == (size_t)INT_MAX);
    total = 0; CHECK(asprintf_add(&total, SIZE_MAX) == -1 && total == 0);
    total = SIZE_MAX - 1;
    CHECK(asprintf_add(&total, 2) == -1 && total == SIZE_MAX - 1);
    CHECK(asprintf_add(&total, 1) == -1 && total == SIZE_MAX - 1);
    puts("asprintf count and allocation tests passed");
    return 0;
}
