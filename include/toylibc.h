#pragma once

#include <stdarg.h>  // va_list (freestanding header, no libc required)
#include <stddef.h>  // size_t, NULL
#include <stdint.h>  // intptr_t

#ifdef __cplusplus
extern "C" {
#endif

// ---- mmap 常量 (从 <sys/mman.h> 复制, freestanding 环境没有这些) ----
#define TOY_PROT_NONE  0
#define TOY_PROT_READ  1
#define TOY_PROT_WRITE 2
#define TOY_PROT_EXEC  4

#define TOY_MAP_SHARED    1
#define TOY_MAP_PRIVATE   2
#define TOY_MAP_FIXED     0x10
#define TOY_MAP_ANONYMOUS 0x20

// ---- open 标志 ----
#define TOY_O_RDONLY    0
#define TOY_O_WRONLY    1
#define TOY_O_RDWR      2
#define TOY_O_CREAT     0100
#define TOY_O_TRUNC     01000
#define TOY_O_APPEND    02000

// ---- lseek whence ----
#define TOY_SEEK_SET  0
#define TOY_SEEK_CUR  1
#define TOY_SEEK_END  2

// ---- 信号常量 ----
#define TOY_SIGINT   2
#define TOY_SIGKILL  9
#define TOY_SIGTERM  15
#define TOY_SIGCHLD  17
#define TOY_SIG_DFL  ((void *)0)
#define TOY_SIG_IGN  ((void *)1)
#define TOY_SA_RESTORER  0x04000000

// ---- waitpid 选项 ----
#define TOY_WNOHANG  1

// ---- POSIX 结构体 ----
struct toylibc_timespec {
    long tv_sec;
    long tv_nsec;
};

struct toylibc_timeval {
    long tv_sec;
    long tv_usec;
};

// 简化版 stat 结构 (x86_64)
struct toylibc_stat {
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned long st_nlink;
    unsigned int  st_mode;
    unsigned int  st_uid;
    unsigned int  st_gid;
    unsigned int  __pad0;
    unsigned long st_rdev;
    long          st_size;
    long          st_blksize;
    long          st_blocks;
    long          st_atime;
    long          st_atime_nsec;
    long          st_mtime;
    long          st_mtime_nsec;
    long          st_ctime;
    long          st_ctime_nsec;
    long          __unused[3];
};

// sigaction 结构
struct toylibc_sigaction {
    void (*sa_handler)(int);
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    unsigned long sa_mask;
};

// =========================================================================
// toylibc.h — 最小 libc 的公开接口
// =========================================================================
//
// 系统调用:
//   核心:   read, write, exit, brk, mmap, munmap
//   文件:   open, close, lseek, stat, getcwd, chdir
//   进程:   fork, execve, waitpid, getpid, getppid
//   通信:   pipe, dup, dup2
//   信号:   sigaction, kill
//   时间:   nanosleep, gettimeofday
//
// 字符串/内存（纯用户态）:
//   strlen, memcpy, memset, memcmp, memmove, strcmp, strcpy
//
// 堆管理（基于 brk + mmap）:
//   malloc, free, calloc, realloc
//
// 标准 I/O（基于 write）:
//   printf, puts
//
// 目标平台: x86_64 Linux
// =========================================================================

// =====================================================================
//  核心系统调用包装 (src/syscall.c)
// =====================================================================

long   toylibc_read(int fd, void *buf, unsigned long count);
long   toylibc_write(int fd, const void *buf, unsigned long count);
void   toylibc_exit(int status) __attribute__((noreturn));
void  *toylibc_brk(void *addr);
void  *toylibc_mmap(void *addr, unsigned long length, int prot, int flags,
                    int fd, long offset);
int    toylibc_munmap(void *addr, unsigned long length);

// =====================================================================
//  POSIX 文件 I/O (src/fs.c)
// =====================================================================

int    toylibc_open(const char *path, int flags, int mode);
int    toylibc_close(int fd);
long   toylibc_lseek(int fd, long offset, int whence);
int    toylibc_stat(const char *path, struct toylibc_stat *st);
int    toylibc_getcwd(char *buf, unsigned long size);
int    toylibc_chdir(const char *path);

// =====================================================================
//  POSIX 进程管理 (src/proc.c)
// =====================================================================

long   toylibc_fork(void);
long   toylibc_execve(const char *path, char *const argv[], char *const envp[]);
long   toylibc_waitpid(long pid, int *status, int options);
long   toylibc_getpid(void);
long   toylibc_getppid(void);

// =====================================================================
//  POSIX 管道/重定向 (src/pipe.c)
// =====================================================================

int    toylibc_pipe(int fds[2]);
int    toylibc_dup(int oldfd);
int    toylibc_dup2(int oldfd, int newfd);

// =====================================================================
//  POSIX 信号 (src/signal.c)
// =====================================================================

int    toylibc_sigaction(int signum, const struct toylibc_sigaction *act,
                          struct toylibc_sigaction *oldact);
int    toylibc_kill(long pid, int sig);

// =====================================================================
//  POSIX 时间 (src/time.c)
// =====================================================================

int    toylibc_nanosleep(const struct toylibc_timespec *req,
                          struct toylibc_timespec *rem);
int    toylibc_gettimeofday(struct toylibc_timeval *tv, void *tz);

// =====================================================================
//  字符串 / 内存（纯用户态, 无系统调用）(src/string.c)
// =====================================================================

unsigned long toylibc_strlen(const char *s);
void *toylibc_memcpy(void *dest, const void *src, unsigned long n);
void *toylibc_memset(void *dest, int c, unsigned long n);
int   toylibc_memcmp(const void *a, const void *b, unsigned long n);
void *toylibc_memmove(void *dest, const void *src, unsigned long n);
int   toylibc_strcmp(const char *a, const char *b);
char *toylibc_strcpy(char *dest, const char *src);

// =====================================================================
//  堆管理: malloc / free / calloc / realloc (src/heap.c)
// =====================================================================

void *toylibc_malloc(unsigned long size);
void  toylibc_free(void *ptr);
void *toylibc_calloc(unsigned long nmemb, unsigned long size);
void *toylibc_realloc(void *ptr, unsigned long size);

// =====================================================================
//  标准 I/O: printf / puts (src/stdio.c)
// =====================================================================

int toylibc_printf(const char *fmt, ...);
int toylibc_puts(const char *s);

#ifdef __cplusplus
}
#endif
