# toylibc 学习路线

## 你已完成 ✅

| 文件 | 学到的东西 |
|---|---|
| `include/toylibc.h` | 全部 13 个公开函数接口、mmap 常量、设计分层 |
| `src/start.S` | `_start` 入口、argc/argv 入栈、`call main`、`call exit` |

---

## 第一站：`src/syscall.c` — 用户态怎么进内核

### 核心知识点

- **syscall 指令** — 用户态主动陷入内核的唯一方式
- **内联汇编** — `__asm__ volatile("syscall" : ...)` 语法
- **寄存器传参** — 为什么 rcx 不能用（syscall 会覆盖它），r10 代替 rcx
- **返回值** — 成功返回非负，失败返回 `-errno`

### 对照内核源码

系统调用号来自 `arch/x86/entry/syscalls/syscall_64.tbl`，你的本地也能查到：

```bash
grep -E '__NR_read|__NR_write|__NR_brk|__NR_mmap|__NR_munmap|__NR_exit' \
  /usr/include/x86_64-linux-gnu/asm/unistd_64.h
```

### 实验

用 GDB 在 `toylibc_write` 打端点，单步到 `syscall` 指令前后对比寄存器：

```
(gdb) break toylibc_write
(gdb) run
(gdb) si              # 单步到 syscall 前
(gdb) p/x $rax        # 此时 rax = __NR_write = 1
(gdb) si              # 执行 syscall
(gdb) p/x $rax        # 此时 rax = 实际写入字节数
```

**看完你应该能回答：** `syscall` 指令执行时，CPU 做了什么？为什么第 4 个参数用 r10 而不是 rcx？

---

## 第二站：`src/string.c` — 纯用户态基础运算

### 核心知识点

- **strlen** — 逐字节扫描 `\0`，glibc 为什么按 word 对齐一次比较 8 字节
- **memcpy** — 为什么要求 dest/src 不重叠（重叠需 memmove）
- **memset** — 最简单的内存填充
- **memcmp** — 逐字节差值比较

### 对比 glibc

glibc 的 memcpy 对大量数据会用 SIMD（`movdqa` 一次搬 16 字节），但逻辑本质一样。

### 实验

```bash
# 确认 string.c 不产生任何 syscall
strace -e trace=write ./build/hello 2>&1 | grep -v write
# 你会看到只有 write/brk/exit — strlen/memcpy/memset 纯用户态
```

**看完你应该能回答：** 为什么 memcpy 不检查重叠？如果重叠了会发生什么？

---

## 第三站：`src/stdio.c` — 格式化输出

### 核心知识点

- **printf 实现策略** — 格式解析 → 栈上缓冲区 → 批量 write
- **va_list** — 可变参数的底层原理（`va_start` / `va_arg` / `va_end`）
- **itoa / utoa / xtoa** — 整数转字符串，不用 libc 的 `sprintf`
- **字符串反转** — 数字转换低位在前，需要反转

### 格式符支持

| 格式符 | 含义 | 核心转换函数 |
|---|---|---|
| `%d` `%i` | 有符号整数 | `itoa` |
| `%u` | 无符号整数 | `utoa` |
| `%x` | 十六进制 | `xtoa` |
| `%s` | 字符串 | 直接复用 `strlen` + `write` |
| `%c` | 单字符 | 直接写缓冲区 |
| `%p` | 指针 | `"0x"` + `xtoa` |
| `%%` | 百分号 | 直接写 `%` |

### 实验

在 `printf` 里加断点，观察 `va_arg` 怎么从栈上取参数。

**看完你应该能回答：** 为什么不直接逐字符 write，而是先收集到缓冲区再批量输出？

---

## 第四站：`src/heap.c` — 最复杂的模块

### 学习顺序（由易到难）

#### 4.1 先看懂常量

```
ALIGN = 16        → 对齐粒度（malloc 返回的指针 16 字节对齐）
OVERHEAD = 16     → 每块额外开销（8B header + 8B footer）
MMAP_LIMIT = 128KB → 大对象阈值
```

#### 4.2 再看懂块结构

```
┌──────────┬──────────────────┬──────────┐
│ header   │   user data      │ footer   │
│ 8 字节   │   N 字节         │ 8 字节   │
│ size|flag│   (16 对齐)      │ size|flag│
└──────────┴──────────────────┴──────────┘
```

- **bit 0** = 是否已分配
- **bit 1** = 前一块是否空闲（用于合并）
- **bit 2+** = 块总大小

#### 4.3 双层分配策略

```
malloc(size)
  │
  ├─ size < 128KB → brk_alloc()    ← 从堆分配
  │     ├─ 遍历隐式空闲链表 (first-fit)
  │     ├─ 找到足够大的空闲块 → 分裂或整块用
  │     └─ 没找到 → brk_grow() 扩展堆
  │
  └─ size ≥ 128KB → mmap()        ← 直接向内核要
        └─ 用完 munmap() 归还
```

#### 4.4 四个关键操作

| 操作 | 函数 | 核心逻辑 |
|---|---|---|
| 分配 | `brk_alloc` | first-fit 搜索 → 分裂 / 整块 |
| 扩展 | `brk_grow` | syscall brk → 创建新空闲块 → 合并 |
| 释放 | `brk_release` | 标记空闲 → 通知下一块 → 合并 |
| 合并 | `merge` | 前邻空闲 → 向前合并；后邻空闲 → 向后合并 |

#### 4.5 calloc / realloc

- **calloc** = malloc + memset（零填充），加了个乘法溢出检查
- **realloc** = 小→大：新分配 + 拷贝 + 释放；原地够用：直接返回

### 实验

```bash
# 追踪 malloc 的 syscall
strace -e trace=brk,mmap,munmap ./build/test 2>&1

# 你会看到小对象用 brk，大对象（≥128KB）走 mmap
```

在 GDB 中断点到 `brk_grow` 和 `merge`，亲眼观察空闲链表的合并过程：

```
(gdb) break brk_grow
(gdb) break merge
(gdb) run
(gdb) bt              # 谁调用了它？
```

### 不需要深入的部分

边界标签合并、first-fit 分裂的 bit 运算细节 —— 这些是实现细节，理解思路即可。

**看完你应该能回答：** 为什么 glibc 小对象用 brk、大对象用 mmap？两种方式各有什么优缺点？

---

## 第五站：`test/test.c` — 验证一切

### 对照看

从头到尾读一遍 `test/test.c`，每看到一个测试用例，回想它验证的是哪个函数的什么行为。

```bash
./build/test   # 31 项全通过才算学完
```

---

## 延伸阅读（可选）

| 主题 | 资源 |
|---|---|
| Linux syscall 完整列表 | `/usr/include/x86_64-linux-gnu/asm/unistd_64.h` |
| System V AMD64 ABI | [x86-64 psABI](https://gitlab.com/x86-psABIs/x86-64-ABI) |
| glibc malloc 源码 | `glibc/malloc/malloc.c` — Doug Lea 的 dlmalloc 后代 |
| ELF 文件格式 | `man 5 elf` |
| 内核 syscall 定义 | `arch/x86/entry/syscalls/syscall_64.tbl`（内核源码） |

---

## 学习检查清单

- [ ] 能解释 `syscall` 指令的完整流程（用户态→内核→返回）
- [ ] 能画出堆块的内存布局（header + payload + footer）
- [ ] 能说出 brk 和 mmap 两种分配方式何时切换
- [ ] 能写出可变参数函数（用 va_list）
- [ ] `strace ./build/test` 的每行输出都能对应到源码
- [ ] GDB 里能熟练用 `si` / `ni` / `bt` / `i r`
