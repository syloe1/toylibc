/**
 * proc.c — POSIX 进程管理系统调用
 *
 * fork(2)      — syscall 57  — 创建子进程（复制当前进程）
 * execve(2)    — syscall 59  — 替换进程镜像
 * wait4(2)     — syscall 61  — 等待子进程状态变化
 * getpid(2)    — syscall 39  — 获取当前进程 PID
 * getppid(2)   — syscall 110 — 获取父进程 PID
 *
 * =========================================================================
 * fork 返回值（最特殊的系统调用）:
 *   - 父进程中: fork() 返回子进程的 PID (>0)
 *   - 子进程中: fork() 返回 0
 *   - 失败:     返回 -1 (errno)
 *
 * fork 一次调用，两个返回——因为内核复制了进程，父子各自从 syscall 返回。
 * =========================================================================
 *
 * execve 行为:
 *   成功时不返回（整个进程被替换）
 *   失败时返回 -1
 * =========================================================================
 */

#include "toylibc.h"

// =========================================================================
// fork — syscall 57
//
//   pid_t fork(void);
//
// 内核克隆当前进程：复制页表、文件描述符表、信号处理等。
// 父子进程的区别仅在于 fork 返回值不同。
// =========================================================================
long toylibc_fork(void)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(57)                       // __NR_fork，只有 rax 输入
        : "rcx", "r11", "memory"
    );
    return ret;
}

// =========================================================================
// execve — syscall 59
//
//   int execve(const char *path, char *const argv[], char *const envp[]);
//
// 用 path 指向的可执行文件替换当前进程镜像。
// argv[0] 通常是程序名，argv 以 NULL 结尾。
// envp 是环境变量数组，以 NULL 结尾（传 NULL 表示继承当前环境）。
// 成功时不返回；失败返回 -1。
// =========================================================================
long toylibc_execve(const char *path, char *const argv[], char *const envp[])
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(59),                      // __NR_execve
          "D"(path),                    // rdi = path
          "S"(argv),                    // rsi = argv
          "d"(envp)                     // rdx = envp
        : "rcx", "r11", "memory"
    );
    return ret;
}

// =========================================================================
// waitpid — 用 wait4 (syscall 61) 实现
//
//   pid_t waitpid(pid_t pid, int *status, int options);
//
// wait4(pid, status, options, NULL) ≈ waitpid(pid, status, options)
// 第 4 个参数 rusage 我们传 NULL（不需要资源使用统计）。
//
// options 常用值:
//   WNOHANG=1 — 不阻塞，没有子进程退出时返回 0
// 返回: >0=退出的子进程 PID, -1=错误
// =========================================================================
long toylibc_waitpid(long pid, int *status, int options)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(61),                      // __NR_wait4
          "D"(pid),                     // rdi = pid
          "S"(status),                  // rsi = status
          "d"((long)options),           // rdx = options
          "r"((long)0)                  // r10 = rusage = NULL
        : "rcx", "r11", "memory"
    );
    return ret;
}

// =========================================================================
// getpid — syscall 39
//
//   pid_t getpid(void);
//
// 返回当前进程的 PID（进程 ID）。
// 这个调用不会失败。
// =========================================================================
long toylibc_getpid(void)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(39)                       // __NR_getpid，只有 rax
        : "rcx", "r11"
    );
    return ret;
}

// =========================================================================
// getppid — syscall 110
//
//   pid_t getppid(void);
//
// 返回父进程的 PID。
// 这个调用不会失败。
// =========================================================================
long toylibc_getppid(void)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(110)                      // __NR_getppid，只有 rax
        : "rcx", "r11"
    );
    return ret;
}
