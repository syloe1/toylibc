/**
 * syscall.c — 系统调用包装
 *
 * x86_64 Linux 系统调用约定 (syscall 指令):
 *   ┌─────────────┬──────────────────────────────────────┐
 *   │ 寄存器       │ 用途                                 │
 *   ├─────────────┼──────────────────────────────────────┤
 *   │ %rax        │ syscall 编号 + 返回值                 │
 *   │ %rdi        │ 第 1 个参数                           │
 *   │ %rsi        │ 第 2 个参数                           │
 *   │ %rdx        │ 第 3 个参数                           │
 *   │ %r10        │ 第 4 个参数 (不是 %rcx! 见下文)       │
 *   │ %r8         │ 第 5 个参数                           │
 *   │ %r9         │ 第 6 个参数                           │
 *   │ %rcx        │ 被 syscall 破坏 (内核保存 rip)        │
 *   │ %r11        │ 被 syscall 破坏 (内核保存 rflags)      │
 *   └─────────────┴──────────────────────────────────────┘
 *
 * 为什么第 4 个参数是 %r10 而不是 %rcx?
 *   syscall 指令把 rip 存入 rcx、rflags 存入 r11,
 *   然后跳到内核入口。所以 rcx 在执行 syscall 时被覆盖了,
 *   不能用来传参数。System V ABI 用 r10 代偿。
 *
 * 返回值:
 *   - 大多数系统调用成功时返回非负数, 失败时返回 -errno (在 rax 中)
 *   - 这里选择直接透传内核返回值, 由调用者自行判断
 *
 * 内联汇编模板:
 *   __asm__ volatile (
 *       "syscall"
 *       : "=a"(ret)           // 输出约束: rax → ret
 *       : "a"(nr),            // 输入约束: nr → rax
 *         "D"(a1), "S"(a2), "d"(a3), "r"(a4), "r"(a5), "r"(a6)
 *       : "rcx", "r11", "memory"  // 破坏列表
 *   );
 *
 * 约束字母含义 (GCC inline asm):
 *   "a" = rax    "D" = rdi    "S" = rsi    "d" = rdx
 *   "r" = 任意通用寄存器 (编译器分配)
 *   "memory" = 可能修改任意内存 (用于写入缓冲区的系统调用)
 */

#include "toylibc.h"

// =========================================================================
// read — syscall 0
// =========================================================================
long toylibc_read(int fd, void *buf, unsigned long count)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(0),
          "D"((long)fd),
          "S"(buf),
          "d"(count)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// =========================================================================
// write — syscall 1
// =========================================================================
long toylibc_write(int fd, const void *buf, unsigned long count)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(1),
          "D"((long)fd),
          "S"(buf),
          "d"(count)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// =========================================================================
// brk — syscall 12
//
//   int brk(void *addr);
//
// 把 program break (数据段末尾, BSS 之后) 设置为 addr。
// 调用 brk(0) 只查询当前 break 而不修改。
// Linux 内核保证 program break 的初始值 = BSS 段末尾 (已页对齐)。
//
// 这本质上是扩展 (收缩) 进程的 data segment。
// 所有超过 break 的地址在第一次访问时触发缺页异常,
// 内核在缺页处理中分配物理页框。
// =========================================================================
// 注意: 返回 void*, 因为内核返回的是程序断点地址 (64 位指针)
void *toylibc_brk(void *addr)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(12),
          "D"(addr)
        : "rcx", "r11"
    );
    // 内核 brk 系统调用返回新的 program break 地址
    // (brk(NULL) 返回当前 break 而不修改)
    return (void *)ret;
}

// =========================================================================
// mmap — syscall 9
//
//   void *mmap(void *addr, size_t length, int prot, int flags,
//              int fd, off_t offset);
//
// 在调用进程的虚拟地址空间中创建新的内存映射。
//
// prot (内存保护):
//   PROT_NONE  = 0   — 不可访问 (通常用于 guard page)
//   PROT_READ  = 1   — 可读
//   PROT_WRITE = 2   — 可写
//   PROT_EXEC  = 4   — 可执行
//
// flags (映射属性):
//   MAP_SHARED    = 1     — 修改对其他进程可见 + 写回文件
//   MAP_PRIVATE   = 2     — COW, 修改不写回文件 (malloc 用这个)
//   MAP_FIXED     = 0x10  — 精确地址, 失败则覆盖已有映射 (危险)
//   MAP_ANONYMOUS = 0x20  — 不与文件关联, fd 被忽略
//
// malloc 对大对象 (>128KB) 的调用:
//   mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
//      ↑ 让内核选地址         ↑ 读写                ↑ 匿名映射 (纯内存)
//
// 内核在 mmap 返回时并不立即分配物理页 (demand paging):
//   只创建 VMA (vm_area_struct), 第一次访问时触发缺页异常才分配物理页。
// =========================================================================
void *toylibc_mmap(void *addr, unsigned long length, int prot, int flags,
                   int fd, long offset)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(9),
          "D"(addr),
          "S"(length),
          "d"(prot),
          "r"((long)flags),   // 第 4 个参数 → %r10 (任意寄存器, 编译器选)
          "r"((long)fd),      // 第 5 个参数 → %r8
          "r"(offset)         // 第 6 个参数 → %r9
        : "rcx", "r11", "memory"
    );
    return (void *)ret;
}

// =========================================================================
// munmap — syscall 11
//
//   int munmap(void *addr, size_t length);
//
// 解除 addr 开始的 length 字节虚拟内存映射。
// 之后访问该区域 → SIGSEGV。
// 内核释放对应的物理页框 (如果有且未被其他地方引用)。
// =========================================================================
int toylibc_munmap(void *addr, unsigned long length)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(11),
          "D"(addr),
          "S"(length)
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

// =========================================================================
// exit — syscall 60
// =========================================================================
void toylibc_exit(int status)
{
    __asm__ volatile (
        "syscall"
        :
        : "a"(60),
          "D"((long)status)
        : "rcx", "r11"
    );
    __builtin_unreachable();
}
