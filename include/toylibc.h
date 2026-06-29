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

// =========================================================================
// toylibc.h — 最小 libc 的公开接口
//
// 系统调用（每个函数对应一条 syscall 指令）:
//   read, write, _exit, brk, mmap, munmap
//
// 字符串/内存（纯用户态）:
//   strlen, memcpy, memset
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
//  系统调用包装
// =====================================================================

/**
 * read(2) — 从 fd 读 count 字节到 buf
 * syscall 编号: 0 (__NR_read)
 * 返回: 实际读取字节数, 0=EOF, 负数=errno
 */
long toylibc_read(int fd, void *buf, unsigned long count);

/**
 * write(2) — 向 fd 写 count 字节
 * syscall 编号: 1 (__NR_write)
 * 返回: 实际写入字节数, 负数=errno
 */
long toylibc_write(int fd, const void *buf, unsigned long count);

/**
 * _exit(2) — 立即终止进程, 不调用 atexit, 不刷新 stdio
 * syscall 编号: 60 (__NR_exit_group=231 用于多线程, 这里用 60)
 */
void toylibc_exit(int status) __attribute__((noreturn));

/**
 * brk(2) — 设置进程的数据段末尾 (program break)
 *
 *   int brk(void *addr);
 *
 * syscall 编号: 12 (__NR_brk)
 * 参数:  %rdi = 新的 program break 地址
 * 返回:  0 = 成功, -1 = 失败 (errno=ENOMEM)
 *
 * 调用 brk(NULL) / brk(0) 返回当前 break（不改变它）。
 * 这是堆内存的"传统"来源——glibc malloc 对小对象用 brk, 大对象用 mmap。
 */
void *toylibc_brk(void *addr);

/**
 * mmap(2) — 创建虚拟内存映射
 *
 *   void *mmap(void *addr, size_t length, int prot, int flags,
 *              int fd, off_t offset);
 *
 * syscall 编号: 9 (__NR_mmap)
 * 参数:  %rdi=addr  %rsi=length  %rdx=prot
 *        %r10=flags  %r8=fd  %r9=offset
 *
 * Linux 内核的真实 mmap 入参在第 4 个参数用了 %r10 而非 %rcx,
 * 因为 syscall 指令本身会覆盖 rcx（保存 rip）。
 *
 * prot  (保护):
 *   PROT_NONE=0  PROT_READ=1  PROT_WRITE=2  PROT_EXEC=4
 * flags (映射方式):
 *   MAP_PRIVATE=2     MAP_ANONYMOUS=0x20     MAP_FIXED=0x10
 *   通常用 MAP_PRIVATE | MAP_ANONYMOUS 来分配匿名内存。
 */
void *toylibc_mmap(void *addr, unsigned long length, int prot, int flags,
                   int fd, long offset);

/**
 * munmap(2) — 解除虚拟内存映射
 * syscall 编号: 11 (__NR_munmap)
 * 参数: %rdi=addr  %rsi=length
 */
int toylibc_munmap(void *addr, unsigned long length);

// =====================================================================
//  字符串 / 内存（纯用户态, 无系统调用）
// =====================================================================

unsigned long toylibc_strlen(const char *s);
void *toylibc_memcpy(void *dest, const void *src, unsigned long n);
void *toylibc_memset(void *dest, int c, unsigned long n);

// 辅助: 比较 n 字节内存是否相等
int toylibc_memcmp(const void *a, const void *b, unsigned long n);

// =====================================================================
//  堆管理: malloc / free / calloc / realloc
// =====================================================================

/**
 * malloc — 分配 size 字节的堆内存
 *
 * 实现策略 (toylibc):
 *   小对象 (< 128 KB) → 从 brk 堆分配 (隐式空闲链表 + 边界标签)
 *   大对象 (≥ 128 KB) → 直接用 mmap/MAP_ANONYMOUS 分配
 *
 * 返回 16 字节对齐的指针, 失败返回 NULL。
 */
void *toylibc_malloc(unsigned long size);

/**
 * free — 释放 malloc/calloc/realloc 分配的内存
 * 传入 NULL 是安全的 (no-op)。
 */
void toylibc_free(void *ptr);

/**
 * calloc — 分配 nmemb * size 字节并清零
 */
void *toylibc_calloc(unsigned long nmemb, unsigned long size);

/**
 * realloc — 调整已分配内存的大小
 * 若 ptr==NULL 等同 malloc; 若 size==0 等同 free。
 */
void *toylibc_realloc(void *ptr, unsigned long size);

// =====================================================================
//  标准 I/O: printf / puts
// =====================================================================

/**
 * printf — 格式化输出到 stdout (fd=1)
 *
 * 支持的格式说明符:
 *   %d  %i  — 有符号十进制整数
 *   %u      — 无符号十进制整数
 *   %x      — 十六进制 (小写)
 *   %s      — 字符串
 *   %c      — 单个字符
 *   %p      — 指针地址 (十六进制)
 *   %%      — 字面百分号
 *
 * 不支持: 宽度、精度、长度修饰符、浮点数
 *
 * 返回: 写入的字符总数 (不含 '\0')
 */
int toylibc_printf(const char *fmt, ...);

/**
 * puts — 输出字符串 + 换行到 stdout (fd=1)
 * 返回: 写入的字符数 (含换行), 出错返回 -1
 */
int toylibc_puts(const char *s);

#ifdef __cplusplus
}
#endif
