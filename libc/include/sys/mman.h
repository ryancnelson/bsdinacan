#ifndef CANNEDBSD_SYS_MMAN_H
#define CANNEDBSD_SYS_MMAN_H

#include <sys/cdefs.h>
#include <sys/types.h>

#define PROT_NONE  0x00
#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define PROT_EXEC  0x04

#define MAP_SHARED  0x0001
#define MAP_PRIVATE 0x0002
#define MAP_FILE    0x0000
#define MAP_ANON    0x1000
#define MAP_FAILED  ((void *)-1)

#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED   3
#define MADV_DONTNEED   4

__BEGIN_DECLS

void *cb_libc_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
int cb_libc_munmap(void *addr, size_t len);
int cb_libc_madvise(void *addr, size_t len, int behav);

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
    __asm__("cb_libc_mmap");
int munmap(void *addr, size_t len)
    __asm__("cb_libc_munmap");
int madvise(void *addr, size_t len, int behav)
    __asm__("cb_libc_madvise");

__END_DECLS

#endif
