#ifndef CANNEDBSD_SYS_STAT_H
#define CANNEDBSD_SYS_STAT_H

#include "cannedbsd/libc.h"
#include "sys/types.h"

/*
 * File type test macros (POSIX.1-2001)
 */
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/*
 * File mode permission bits
 */
#define S_ISUID 0004000 /* Set user id on execution */
#define S_ISGID 0002000 /* Set group id on execution */
#define S_ISVTX 0001000 /* Sticky bit */

#define S_IRWXU 0000700 /* RWX mask for owner */
#define S_IRUSR 0000400 /* Read by owner */
#define S_IWUSR 0000200 /* Write by owner */
#define S_IXUSR 0000100 /* Execute by owner */

#define S_IRWXG 0000070 /* RWX mask for group */
#define S_IRGRP 0000040 /* Read by group */
#define S_IWGRP 0000020 /* Write by group */
#define S_IXGRP 0000010 /* Execute by group */

#define S_IRWXO 0000007 /* RWX mask for other */
#define S_IROTH 0000004 /* Read by other */
#define S_IWOTH 0000002 /* Write by other */
#define S_IXOTH 0000001 /* Execute by other */

#define DEFFILEMODE 0666
#define ACCESSPERMS (S_IRWXU|S_IRWXG|S_IRWXO) /* 0777 */
#define ALLPERMS    (S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO) /* 07777 */

#if defined(__GNUC__) || defined(__clang__)
int stat(const char *path, struct stat *sb) __asm__("cb_libc_stat");
int fstat(int fd, struct stat *sb) __asm__("cb_libc_fstat");
int lstat(const char *path, struct stat *sb) __asm__("cb_libc_lstat");
int mkdir(const char *path, mode_t mode) __asm__("cb_libc_mkdir");
int fchmod(int fd, mode_t mode) __asm__("cb_libc_fchmod");
int fchown(int fd, uid_t uid, gid_t gid) __asm__("cb_libc_fchown");
int fchflags(int fd, uint32_t flags) __asm__("cb_libc_fchflags");
#else
int stat(const char *path, struct stat *sb);
int fstat(int fd, struct stat *sb);
int lstat(const char *path, struct stat *sb);
int mkdir(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int fchown(int fd, uid_t uid, gid_t gid);
int fchflags(int fd, uint32_t flags);
#define stat cb_libc_stat
#define fstat cb_libc_fstat
#define lstat cb_libc_lstat
#define mkdir cb_libc_mkdir
#define fchmod cb_libc_fchmod
#define fchown cb_libc_fchown
#define fchflags cb_libc_fchflags
#endif

#endif
