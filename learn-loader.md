# Loader 阶段 — 内核如何加载 ELF

> 回答：`./build/hello` 回车后，内核做了什么？

## Loader 在整个流程中的位置

```
shell (bash)
  │
  ├─ fork()         ← 克隆一个新进程
  │
  ├─ execve()       ← 替换进程镜像（本节重点）
  │     │
  │     ├─ 1. 打开 ELF 文件，读魔数 7f 45 4c 46
  │     ├─ 2. 读 Program Headers（LOAD 段）
  │     ├─ 3. mmap 各 LOAD 段到虚拟地址空间
  │     ├─ 4. 映射 vdso、vvar
  │     ├─ 5. 设置栈（argc, argv, envp, auxv）
  │     ├─ 6. 设置 rip = ELF entry point（_start）
  │     └─ 7. 返回用户态 → _start 第一条指令执行
  │
  ▼
_start (你的 start.S)
```

---

## 第一课：execve 是加载器的入口

```bash
strace -e trace=execve ./build/hello
```

```
execve("./build/hello", ["./build/hello"], ...) = 0
Hello from toylibc!                    ← 用户态输出
+++ exited with 0 +++
```

execve **成功不返回**（返回 0 是新进程看到的，旧进程已经在 execve 里被替换了）。

---

## 第二课：内存布局 — /proc/PID/maps

```bash
cat /proc/self/maps
```

你刚才看到 bash 的 maps 输出，里面有：

```
地址范围                  权限   映射来源
──────────────────────────────────────────────
624188eda000-624188f0b000 r--p   /usr/bin/bash        ← 代码段（只读）
624188f0b000-62418900e000 r-xp   /usr/bin/bash        ← 代码段（可执行）
62418900e000-624189045000 r--p   /usr/bin/bash        ← 只读数据
624189052000-62418905e000 rw-p                         ← 读写数据 / BSS
62418c9fa000-62418ca3c000 rw-p   [heap]               ← brk 堆
70051308e000-700513090000 r-xp   [vdso]               ← 内核注入的快速系统调用
700513090000-7005130cb000 r-xp   ld-linux-x86-64.so.2  ← 动态链接器
7fff836f5000-7fff83717000 rw-p   [stack]               ← 栈
ffffffffff600000-ffffffffff601000 --xp  [vsyscall]     ← 老的快速系统调用页
```

**你的 toylibc 程序有什么不同？**

Bash 是动态链接的，你的 hello 是静态的。对比一下你 hello 的 LOAD 段：

```bash
readelf -l build/hello | grep LOAD
```

```
LOAD  R      0x400000  ← 只读（.note）
LOAD  R E    0x401000  ← 代码（.text）
LOAD  R      0x403000  ← 只读数据（.rodata）
LOAD  RW     0x404000  ← 读写（.bss）
```

你的程序只有 4 个 LOAD 段，而 bash 有 20+ 个（因为动态链接了 libc、ld 等）。

**亲手做：** 对比 glibc 程序和你的 toylibc 程序的 maps 差异

```bash
# glibc 程序（bash）
cat /proc/self/maps | wc -l          # 你刚才看到约 30 行

# 你的 toylibc 程序（假设能抓到）
# 只有 4-6 行，没有 ld-linux、没有 libc.so
```

---

## 第三课：辅助向量 — auxv

内核在 `_start` 执行前，在栈上放了一个"使用手册"——辅助向量（auxiliary vector）。

栈布局（从高地址到低地址）：

```
[高地址]
  ...
  envp[N]   = NULL        ← 环境变量结束
  envp[0]   = "PATH=/usr/bin:..."
  argv[argc] = NULL
  argv[0]   = "./hello"
  argc                     ← 这就是 _start 里 mov (%rsp),%rdi 读到的
  [辅助向量 auxv]          ← _start 没有读，但我们能读到
    AT_NULL     = 0        ← 辅助向量结束标记
    ...
    AT_RANDOM   = 16 字节随机数地址
    AT_ENTRY    = 程序入口地址（0x401ce1）
    AT_PHDR     = ELF 程序头地址
    AT_PAGESZ   = 4096
[低地址]
```

你的 `start.S` 目前只传了 argc/argv 给 main，没传 envp 和 auxv。但我们可以写程序直接读。

**亲手做：** 写一个读取 auxv 的程序

<details>
<summary>展开代码</summary>

```c
// auxv.c — 读取内核放在栈上的辅助向量
#include "toylibc.h"

// 从 main 参数推导 auxv 位置
// main 是 _start call 进来的，栈布局已知
int main(int argc, char *argv[])
{
    toylibc_printf("argc = %d\n", argc);

    // envp 在 argv[argc+1]
    char **envp = &argv[argc + 1];

    // auxv 在 envp 结束后（跳过 NULL 终止符）
    unsigned long *auxv = (unsigned long *)(envp);
    while (*auxv != 0) auxv++;  // 跳过所有 envp
    auxv++;                      // 跳过 NULL

    toylibc_printf("auxv START at %p\n", auxv);

    // AT_NULL = 0 终止
    while (auxv[0] != 0) {
        toylibc_printf("  type=%lu  val=0x%lx\n", auxv[0], auxv[1]);
        auxv += 2;
    }

    return 0;
}
```

编译：

```bash
gcc -I include -ffreestanding -nostartfiles -nodefaultlibs -static \
    build/start.o auxv.c build/libtoy.a -o build/auxv
./build/auxv
```

</details>

---

## 第四课：内核源码对应

Linux 内核的 ELF 加载器在 `fs/binfmt_elf.c`，核心函数：

```
load_elf_binary()
  ├─ elf_check_arch()        ← 检查是不是 x86_64 的 ELF
  ├─ elf_map() × N           ← 每个 LOAD 段调一次 mmap
  ├─ set_brk()               ← 设置 BSS
  ├─ create_elf_tables()     ← 构建 auxv、envp、argv
  └─ start_thread()          ← 设置 rip = entry，返回用户态
```

你不需要读完整的内核代码，但知道这些函数名有助于后续深入。

---

## 第五课：vdso — 内核注入的"共享库"

```bash
cat /proc/self/maps | grep vdso
```

```
70051308e000-700513090000 r-xp  [vdso]
```

vdso（virtual dynamic shared object）是内核"注入"到每个进程地址空间的一小段代码，包含对时间相关系统调用（`clock_gettime`、`gettimeofday`）的快速实现——不需要真正陷入内核。

```bash
# 用 dd 把 vdso 从进程内存里 dump 出来看
# （仅作了解，不需要深入）
```

---

## 第六课：ASLR — 地址空间随机化

每次运行你的 hello，malloc 返回的指针不一样：

```
第一次: malloc test: 0x35dda008
第二次: malloc test: 0x1eded008
```

这是因为内核在 execve 时给栈、堆、mmap 区域加了随机偏移（Address Space Layout Randomization）。

```bash
# 关闭 ASLR 后地址就固定了
setarch x86_64 -R ./build/hello
# 每次运行 malloc 返回同一个地址
```

---

## 检查清单

- [ ] 能画出 `./hello` 回车到 `_start` 之间的 7 步流程
- [ ] 知道 `/proc/PID/maps` 每一列的含义（地址、权限、文件）
- [ ] 理解为什么你的静态程序没有 `ld-linux.so` 和 `libc.so`
- [ ] 知道 auxv 在栈上的位置和 `AT_ENTRY` 的含义
- [ ] 能解释 vdso 是什么、为什么存在
- [ ] 知道 ASLR 是 execve 时内核加的随机偏移

---

## 总结：Loader 一句话

> Loader 是内核的 `execve` 实现——读 ELF 的 LOAD 段、mmap 到虚拟地址空间、设好栈和 auxv、把 rip 指向 `_start`。
