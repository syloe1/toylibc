/**
 * syscall.c — 系统调用包装（内联汇编 + syscall 指令）
 *
 * x86_64 Linux 系统调用约定：
 *   ┌────────┬──────────────────────────────────────┐
 *   │ 寄存器  │ 用途                                  │
 *   ├────────┼──────────────────────────────────────┤
 *   │ %rax   │ 系统调用编号（进入内核前设置）          │
 *   │        │ 返回值（内核返回后读取）                │
 *   │ %rdi   │ 第 1 个参数                            │
 *   │ %rsi   │ 第 2 个参数                            │
 *   │ %rdx   │ 第 3 个参数                            │
 *   │ %r10   │ 第 4 个参数（注意：不是 %rcx！）        │
 *   │ %r8    │ 第 5 个参数                            │
 *   │ %r9    │ 第 6 个参数                            │
 *   │ %rcx   │ 被 syscall 指令破坏（内核保存 rip）     │
 *   │ %r11   │ 被 syscall 指令破坏（内核保存 rflags）  │
 *   └────────┴──────────────────────────────────────┘
 *
 * 为什么第 4 个参数是 %r10 而不是 %rcx？
 *   syscall 指令执行时，CPU 自动把 rip 存入 rcx、rflags 存入 r11，
 *   然后跳转到内核入口。所以 rcx/r11 的值被覆盖了，不能用来传参。
 *
 * GCC 内联汇编约束字母：
 *   "a" = rax    "D" = rdi    "S" = rsi    "d" = rdx
 *   "r" = 任意通用寄存器（编译器自动分配）
 *
 *   破坏列表 "memory" = 告诉编译器"内存可能被修改了"，
 *   防止编译器把变量缓存到寄存器中导致读到过期数据。
 *
 * 返回值：
 *   大多数系统调用成功返回非负值，失败返回 -errno。
 *   这里直接透传内核返回值，由调用者自行判断。
 */

#include "toylibc.h"

// =========================================================================
// read — syscall 0 (__NR_read)
//
//   ssize_t read(int fd, void *buf, size_t count);
//
// 从 fd 读取最多 count 字节到 buf，返回实际读取字节数。
// 返回 0 = EOF，返回负值 = 错误（-errno）。
// =========================================================================
long toylibc_read(int fd, void *buf, unsigned long count)
{
    long ret;
    __asm__ volatile (
        "syscall"                // 唯一汇编指令：触发系统调用进入内核
        : "=a"(ret)              // 输出：rax 的值（内核返回值）→ ret
        : "a"(0),                // 输入1：rax = 0（__NR_read）
          "D"((long)fd),         // 输入2：rdi = fd（第 1 参数）
          "S"(buf),              // 输入3：rsi = buf（第 2 参数）
          "d"(count)             // 输入4：rdx = count（第 3 参数）
        : "rcx", "r11", "memory" // 破坏：rcx/r11 被 syscall 指令覆盖
    );
    return ret;
}

// =========================================================================
// write — syscall 1 (__NR_write)
//
//   ssize_t write(int fd, const void *buf, size_t count);
//
// 将 buf 中 count 字节写入 fd，返回实际写入字节数。
// 返回负值 = 错误（-errno）。
// =========================================================================
long toylibc_write(int fd, const void *buf, unsigned long count)
{
    long ret;
    __asm__ volatile (
        "syscall"                // 唯一汇编指令：触发系统调用进入内核
        : "=a"(ret)              // 输出：rax（内核返回值）→ ret
        : "a"(1),                // 输入1：rax = 1（__NR_write）
          "D"((long)fd),         // 输入2：rdi = fd（第 1 参数）
          "S"(buf),              // 输入3：rsi = buf（第 2 参数）
          "d"(count)             // 输入4：rdx = count（第 3 参数）
        : "rcx", "r11", "memory" // 破坏：rcx/r11 被 syscall 指令覆盖
    );
    return ret;
}

// =========================================================================
// brk — syscall 12 (__NR_brk)
//
//   int brk(void *addr);
//
// 设置进程的 program break（数据段末尾）为 addr。
// 调用 brk(NULL) / brk(0) 只查询当前 break 而不修改。
//
// 内核返回新的 program break 地址：
//   若返回值 == addr → 成功
//   若返回值 != addr → 失败（一般是 ENOMEM）
//
// 这是堆内存的"传统"来源——glibc malloc 对小对象用 brk，大对象用 mmap。
// =========================================================================
void *toylibc_brk(void *addr)
{
    long ret;
    __asm__ volatile (
        "syscall"                // 唯一汇编指令：触发系统调用进入内核
        : "=a"(ret)              // 输出：rax（内核返回的新 break 地址）→ ret
        : "a"(12),               // 输入1：rax = 12（__NR_brk）
          "D"(addr)              // 输入2：rdi = addr（第 1 参数，新的 break）
        : "rcx", "r11"           // 破坏：rcx/r11 被 syscall 指令覆盖
        // 注意：无 "memory"，因为 brk 不涉及用户态缓冲区
    );
    // brk(0) 返回当前 break；brk(addr) 返回新的 break 或 addr（失败时）
    return (void *)ret;
}

// =========================================================================
// mmap — syscall 9 (__NR_mmap)
//
//   void *mmap(void *addr, size_t length, int prot, int flags,
//              int fd, off_t offset);
//
// 在调用进程的虚拟地址空间中创建新的内存映射。
//
// prot（内存保护，位掩码）：
//   PROT_NONE  = 0   — 不可访问（通常用于 guard page）
//   PROT_READ  = 1   — 可读
//   PROT_WRITE = 2   — 可写
//   PROT_EXEC  = 4   — 可执行
//
// flags（映射属性）：
//   MAP_SHARED    = 1     — 修改对其他进程可见 + 写回文件
//   MAP_PRIVATE   = 2     — 写时复制(COW)，修改不写回文件（malloc 用这个）
//   MAP_FIXED     = 0x10  — 精确地址，失败则覆盖已有映射（危险）
//   MAP_ANONYMOUS = 0x20  — 不与文件关联，fd 被忽略（纯内存分配）
//
// malloc 对大对象（≥128KB）的调用：
//   mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
//      ↑ 让内核选地址    ↑ 读写                ↑ 匿名映射（纯内存）
//
// 内核在 mmap 返回时不立即分配物理页（demand paging）：
//   只创建 VMA（vm_area_struct），第一次访问时触发缺页异常才分配物理页。
//
// 注意：这是本文件唯一一个用满 6 个参数的系统调用，
//       第 4 参数 flags 必须用 "r" 约束（编译器自动选 %r10 等寄存器），
//       不能用 "c"（%rcx），因为 syscall 会覆盖 rcx。
// =========================================================================
void *toylibc_mmap(void *addr, unsigned long length, int prot, int flags,
                   int fd, long offset)
{
    long ret;
    __asm__ volatile (
        "syscall"                // 唯一汇编指令：触发系统调用进入内核
        : "=a"(ret)              // 输出：rax（内核返回的映射地址或 -errno）→ ret
        : "a"(9),                // 输入1：rax = 9（__NR_mmap）
          "D"(addr),             // 输入2：rdi = addr（第 1 参数，期望地址）
          "S"(length),           // 输入3：rsi = length（第 2 参数，长度）
          "d"(prot),             // 输入4：rdx = prot（第 3 参数，保护模式）
          "r"((long)flags),      // 输入5：由编译器分配到 r10/r8 等（第 4 参数）
          "r"((long)fd),         // 输入6：由编译器分配到 r8/r9 等（第 5 参数）
          "r"(offset)            // 输入7：由编译器分配到 r9 等（第 6 参数）
        : "rcx", "r11", "memory" // 破坏：rcx/r11 被 syscall 覆盖，
                                 // memory：映射会改变内存布局
    );
    return (void *)ret;
}

// =========================================================================
// munmap — syscall 11 (__NR_munmap)
//
//   int munmap(void *addr, size_t length);
//
// 解除 addr 开始的 length 字节虚拟内存映射。
// 之后访问该区域 → SIGSEGV。
// 内核释放对应的物理页框（如果有且未被其他地方引用）。
// =========================================================================
int toylibc_munmap(void *addr, unsigned long length)
{
    long ret;
    __asm__ volatile (
        "syscall"                // 唯一汇编指令：触发系统调用进入内核
        : "=a"(ret)              // 输出：rax（内核返回值，0=成功，-1=失败）→ ret
        : "a"(11),               // 输入1：rax = 11（__NR_munmap）
          "D"(addr),             // 输入2：rdi = addr（第 1 参数，解除起始地址）
          "S"(length)            // 输入3：rsi = length（第 2 参数，解除长度）
        : "rcx", "r11", "memory" // 破坏：rcx/r11 被 syscall 覆盖，
                                 // memory：解除映射会改变内存布局
    );
    return (int)ret;
}

// =========================================================================
// exit — syscall 60 (__NR_exit)
//
//   void _exit(int status);
//
// 立即终止进程，不调用 atexit 注册的函数，不刷新 stdio 缓冲区。
//
// 这个函数永远不会返回——内联汇编后调用 __builtin_unreachable()
// 告诉编译器"这之后的代码不会被执行"，帮助编译器优化。
//
// 注意：Linux 还有 syscall 231 (exit_group) 用于终止所有线程。
//       单线程程序用 exit(60) 等效；多线程程序应用 exit_group(231)。
// =========================================================================
void toylibc_exit(int status)
{
    __asm__ volatile (
        "syscall"                // 唯一汇编指令：触发系统调用进入内核
        :                        // 无输出——这个调用永远不会返回
        : "a"(60),               // 输入1：rax = 60（__NR_exit）
          "D"((long)status)      // 输入2：rdi = status（第 1 参数，退出码）
        : "rcx", "r11"           // 破坏：rcx/r11 被 syscall 指令覆盖
        // 注意：无 "memory"，因为进程直接终止，不需要同步内存
    );
    __builtin_unreachable();     // 告诉编译器：不会执行到这里
}
