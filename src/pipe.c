/**
 * pipe.c — POSIX 管道与重定向系统调用
 *
 * pipe(2)      — syscall 22 — 创建管道（一对 fd：读端 + 写端）
 * dup(2)       — syscall 32 — 复制文件描述符
 * dup2(2)      — syscall 33 — 复制 fd 到指定编号
 *
 * =========================================================================
 * 管道是 Shell 的核心原语:
 *   $ ls | grep foo
 *   1. shell 调用 pipe() → fds[2] = {读端, 写端}
 *   2. shell fork() → 子进程
 *   3. 子进程 dup2(fds[1], 1) → stdout 指向管道写端
 *   4. 子进程 execve("ls")
 *   5. ls 的输出 → 管道 → 父进程读 fds[0]
 * =========================================================================
 */

#include "toylibc.h"

// =========================================================================
// pipe — syscall 22
//
//   int pipe(int pipefd[2]);
//
// 创建一个单向数据通道。
// pipefd[0] = 读端, pipefd[1] = 写端。
// 写入 pipefd[1] 的数据可以从 pipefd[0] 读出（内核缓冲）。
// 返回: 0=成功, -1=失败
// =========================================================================
int toylibc_pipe(int fds[2])
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(22),                      // __NR_pipe
          "D"(fds)                      // rdi = pipefd 数组地址
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

// =========================================================================
// dup — syscall 32
//
//   int dup(int oldfd);
//
// 复制 oldfd，返回一个新的 fd（最小可用编号）。
// 新旧 fd 共享同一个内核文件表条目（offset、flags 等）。
// 返回: 新 fd (≥0), -1=失败
// =========================================================================
int toylibc_dup(int oldfd)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(32),                      // __NR_dup
          "D"((long)oldfd)              // rdi = oldfd
        : "rcx", "r11"
    );
    return (int)ret;
}

// =========================================================================
// dup2 — syscall 33
//
//   int dup2(int oldfd, int newfd);
//
// 将 oldfd 复制到 newfd。如果 newfd 已经打开，先关闭它。
// 这是实现重定向的关键:
//   dup2(fd, STDOUT_FILENO) → 之后 write(1,...) 就写到 fd 了
// 返回: newfd (≥0), -1=失败
// =========================================================================
int toylibc_dup2(int oldfd, int newfd)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(33),                      // __NR_dup2
          "D"((long)oldfd),             // rdi = oldfd
          "S"((long)newfd)              // rsi = newfd
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}
