/**
 * signal.c — POSIX 信号系统调用
 *
 * sigaction(2) — syscall 13 (rt_sigaction) — 设置信号处理器
 * kill(2)      — syscall 62               — 向进程发送信号
 *
 * =========================================================================
 * 信号是 Unix 的"软件中断":
 *   - SIGINT  (2):  Ctrl+C → 默认终止进程
 *   - SIGKILL (9):  强制终止，无法捕获
 *   - SIGCHLD (17):  子进程状态变化（退出/停止）
 *
 * sigaction 注册一个函数，当信号到达时内核调用它（而不是默认行为）。
 * =========================================================================
 */

#include "toylibc.h"

// =========================================================================
// sigaction — syscall 13 (rt_sigaction)
//
//   int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
//
// signum:  要处理的信号编号
// act:     新的处理方式（NULL = 只查询）
// oldact:  旧的处理方式（NULL = 不保存）
//
// x86_64 用 rt_sigaction 而非 sigaction（后者是 32 位 i386 的）。
// rt_sigaction 多一个参数 sigsetsize（固定为 8）。
// =========================================================================
int toylibc_sigaction(int signum, const struct toylibc_sigaction *act,
                       struct toylibc_sigaction *oldact)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(13),                      // __NR_rt_sigaction
          "D"((long)signum),            // rdi = signum
          "S"(act),                     // rsi = act
          "d"(oldact),                  // rdx = oldact
          "r"((long)8)                  // r10 = sigsetsize = 8 字节
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

// =========================================================================
// kill — syscall 62
//
//   int kill(pid_t pid, int sig);
//
// 向 pid 发送信号 sig。
// pid > 0:  发给指定进程
// pid = 0:  发给同进程组的所有进程
// pid = -1: 发给所有进程（需要权限）
// 返回: 0=成功, -1=失败
// =========================================================================
int toylibc_kill(long pid, int sig)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(62),                      // __NR_kill
          "D"(pid),                     // rdi = pid
          "S"((long)sig)                // rsi = sig
        : "rcx", "r11"
    );
    return (int)ret;
}
