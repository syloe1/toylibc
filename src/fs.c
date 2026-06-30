/**
 * fs.c — POSIX 文件 I/O 系统调用
 *
 * open(2)      — syscall  2 — 打开/创建文件
 * close(2)     — syscall  3 — 关闭 fd
 * lseek(2)     — syscall  8 — 移动文件偏移
 * stat(2)      — syscall 262 (newfstatat) — 获取文件信息
 * getcwd(2)    — syscall 79 — 获取当前目录
 * chdir(2)     — syscall 80 — 切换当前目录
 */

#include "toylibc.h"

// =========================================================================
// open — syscall 2
//
//   int open(const char *path, int flags, mode_t mode);
//
// flags 常用组合:
//   O_RDONLY          — 只读
//   O_WRONLY|O_CREAT   — 只写 + 不存在则创建
//   O_RDWR|O_TRUNC     — 读写 + 打开时清空
// 返回: fd (≥0), 失败返回负数 (-errno)
// =========================================================================
int toylibc_open(const char *path, int flags, int mode)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(2),                       // __NR_open
          "D"(path),                    // rdi = path（第 1 参数）
          "S"((long)flags),             // rsi = flags（第 2 参数）
          "d"((long)mode)               // rdx = mode（第 3 参数）
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

// =========================================================================
// close — syscall 3
//
//   int close(int fd);
//
// 关闭文件描述符，释放内核中的文件表条目。
// 返回: 0=成功, -1=失败
// =========================================================================
int toylibc_close(int fd)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(3),                       // __NR_close
          "D"((long)fd)                 // rdi = fd（第 1 参数）
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

// =========================================================================
// lseek — syscall 8
//
//   off_t lseek(int fd, off_t offset, int whence);
//
// whence:
//   SEEK_SET=0 — 从文件头
//   SEEK_CUR=1 — 从当前位置
//   SEEK_END=2 — 从文件尾
// 返回: 新的文件偏移, 失败返回负数
// =========================================================================
long toylibc_lseek(int fd, long offset, int whence)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(8),                       // __NR_lseek
          "D"((long)fd),                // rdi = fd（第 1 参数）
          "S"(offset),                  // rsi = offset（第 2 参数）
          "d"((long)whence)             // rdx = whence（第 3 参数）
        : "rcx", "r11"
    );
    return ret;
}

// =========================================================================
// stat — 用 newfstatat (syscall 262) 实现
//
//   int stat(const char *path, struct stat *buf);
//
// newfstatat(AT_FDCWD, path, buf, 0) ≈ stat(path, buf)
// AT_FDCWD = -100 (0xffffff9c), 表示"相对当前目录"
// 第 4 个参数 flags=0 表示不跟随符号链接
// =========================================================================
int toylibc_stat(const char *path, struct toylibc_stat *st)
{
    long ret;
    long at_fdcwd = -100;               // AT_FDCWD
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(262),                     // __NR_newfstatat
          "D"(at_fdcwd),                // rdi = dirfd = AT_FDCWD
          "S"(path),                    // rsi = pathname
          "d"(st),                      // rdx = statbuf
          "r"((long)0)                  // r10 = flags = 0
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

// =========================================================================
// getcwd — syscall 79
//
//   long getcwd(char *buf, size_t size);
//
// 把当前工作目录的绝对路径写入 buf。
// 返回: buf 指针 (成功), NULL/0 (失败)
// =========================================================================
int toylibc_getcwd(char *buf, unsigned long size)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(79),                      // __NR_getcwd
          "D"(buf),                     // rdi = buf
          "S"(size)                     // rsi = size
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

// =========================================================================
// chdir — syscall 80
//
//   int chdir(const char *path);
//
// 切换当前进程的工作目录。
// 返回: 0=成功, -1=失败
// =========================================================================
int toylibc_chdir(const char *path)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(80),                      // __NR_chdir
          "D"(path)                     // rdi = path
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}
