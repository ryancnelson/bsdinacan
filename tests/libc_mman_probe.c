#include <errno.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
    void *p;
    (void)argc;
    (void)argv;

    errno = 0;
    p = mmap(NULL, 1024, PROT_READ, MAP_SHARED | MAP_FILE, -1, 0);
    if (p != MAP_FAILED || errno != ENOSYS)
        return 1;

    errno = 0;
    if (munmap((void *)0x1000, 1024) != -1 || errno != ENOSYS)
        return 2;

    if (madvise((void *)0x1000, 1024, MADV_SEQUENTIAL) != 0)
        return 3;

    return 0;
}
