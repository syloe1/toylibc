/**
 * time.c — POSIX 时间系统调用
 *
 * nanosleep(2)    — syscall 35 — 高精度睡眠（纳秒级）
 * gettimeofday(2) — syscall 96 — 获取当前时间
 *
 * =========================================================================
 * nanosleep 与 sleep(3) 的区别:
 *   - sleep(3) 是 libc 函数，精度为秒（底层可能用 nanosleep）
 *   - nanosleep(2) 是系统调用，精度为纳秒（但受内核 HZ 限制）
 *
 * gettimeofday:
 *   - 返回自 Epoch (1970-01-01 00:00:00 UTC) 以来的秒数 + 微秒数
 *   - tz 参数已过时，始终传 NULL
 * =========================================================================
 */

#include "toylibc.h"

// =========================================================================
// nanosleep — syscall 35
//
//   int nanosleep(const struct timespec *req, struct timespec *rem);
//
// 挂起进程至少 req 指定的时间。
// 如果被信号中断，rem 填入剩余时间（可传 NULL 忽略）。
//
// struct timespec:
//   tv_sec  — 秒
//   tv_nsec — 纳秒 (0 ~ 999,999,999)
//
// 返回: 0=成功, -1=失败（被信号中断时返回 -EINTR）
// =========================================================================
int toylibc_nanosleep(const struct toylibc_timespec *req,
                       struct toylibc_timespec *rem)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(35),                      // __NR_nanosleep
          "D"(req),                     // rdi = req
          "S"(rem)                      // rsi = rem
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

// =========================================================================
// gettimeofday — syscall 96
//
//   int gettimeofday(struct timeval *tv, struct timezone *tz);
//
// 获取当前时间（自 Epoch 起的秒数 + 微秒数）。
// tz 已废弃，始终传 NULL。
//
// struct timeval:
//   tv_sec  — 秒 (自 1970-01-01)
//   tv_usec — 微秒 (0 ~ 999,999)
//
// 返回: 0=成功, -1=失败
// =========================================================================
int toylibc_gettimeofday(struct toylibc_timeval *tv, void *tz)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(96),                      // __NR_gettimeofday
          "D"(tv),                      // rdi = tv
          "S"(tz)                       // rsi = tz = NULL
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}
