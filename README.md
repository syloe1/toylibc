# toylibc — 最小化 C 运行时库

完全不依赖 glibc 的 freestanding C 库，目标平台 **x86_64 Linux**。

## 设计理念

- **零依赖**——不链接 glibc，所有功能从零实现
- **教学优先**——代码简洁可读，每个模块都有详尽的实现注释
- **自举**——用自带的 `_start` 入口 + 系统调用，构建完整的静态可执行文件

## 模块总览

```
src/
├── start.S      # _start 入口（汇编，设置 argc/argv → call main → exit）
├── syscall.c    # 系统调用包装（read / write / exit / brk / mmap / munmap）
├── string.c     # strlen / memcpy / memset / memcmp（纯用户态）
├── heap.c       # malloc / free / calloc / realloc（brk + mmap 双层分配）
└── stdio.c      # printf / puts（va_list 格式解析 + write）

include/
└── toylibc.h    # 公开接口头文件

test/
└── test.c       # 功能测试
```

## API

### 系统调用

| 函数 | 对应 syscall | 说明 |
|---|---|---|
| `toylibc_read` | `read (0)` | 从文件描述符读取 |
| `toylibc_write` | `write (1)` | 写入文件描述符 |
| `toylibc_exit` | `exit (60)` | 立即终止进程 |
| `toylibc_brk` | `brk (12)` | 设置/查询 program break |
| `toylibc_mmap` | `mmap (9)` | 创建虚拟内存映射 |
| `toylibc_munmap` | `munmap (11)` | 解除虚拟内存映射 |

### 字符串/内存

| 函数 | 说明 |
|---|---|
| `toylibc_strlen` | 计算字符串长度 |
| `toylibc_memcpy` | 复制内存（不处理重叠） |
| `toylibc_memset` | 填充内存 |
| `toylibc_memcmp` | 比较内存 |

### 堆管理

| 函数 | 说明 |
|---|---|
| `toylibc_malloc` | 分配内存（小对象 → brk，≥128KB → mmap） |
| `toylibc_free` | 释放内存 |
| `toylibc_calloc` | 分配并清零 |
| `toylibc_realloc` | 调整已分配内存大小 |

分配策略：隐式空闲链表 + 边界标签，支持块分裂与相邻合并，16 字节对齐，first-fit 搜索。

### 标准 I/O

| 函数 | 说明 |
|---|---|
| `toylibc_printf` | 格式化输出（`%d` `%u` `%x` `%s` `%c` `%p` `%%`） |
| `toylibc_puts` | 输出字符串 + 换行 |

## 构建

```bash
make          # 编译 libtoy.a + 测试程序
make test     # 编译并运行测试
make clean    # 清理产物
```

产物在 `build/` 目录：

| 文件 | 说明 |
|---|---|
| `libtoy.a` | 静态库 |
| `test` | 测试可执行文件（静态链接，无 glibc） |

## 编译器参数说明

```
-ffreestanding    # 不依赖宿主 libc
-nostartfiles     # 不用 crt0，用自己的 _start
-nodefaultlibs    # 不自动链接 -lc -lm -lpthread
-static           # 纯静态链接
-mno-red-zone      # 禁用红色区域（内核态安全）
```

## 限制

- 仅支持 x86_64 Linux
- `printf` 不支持宽度、精度、长度修饰符、浮点数、`%o`、`%n`
- `memcpy` 不处理重叠（重叠场景需 memmove）
- 堆分配器无线程安全机制
- 未实现 `atexit`、信号、文件操作（`open`/`close`）

## 系统调用号参考

所有 syscall 编号来自 Linux 内核 `arch/x86/entry/syscalls/syscall_64.tbl`，与发行版无关：

| 系统调用 | 编号 |
|---|---|
| `read` | 0 |
| `write` | 1 |
| `mmap` | 9 |
| `munmap` | 11 |
| `brk` | 12 |
| `exit` | 60 |

## 项目文件

```
.
├── src/
│   ├── start.S          # _start 入口
│   ├── syscall.c         # 系统调用包装（内联汇编详解注释）
│   ├── string.c          # strlen / memcpy / memset / memcmp
│   ├── heap.c            # malloc / free / calloc / realloc
│   └── stdio.c           # printf / puts
├── include/
│   └── toylibc.h         # 公开接口（13 个函数）
├── test/
│   └── test.c            # 31 项功能测试
├── examples/
│   └── hello.c           # 使用 toylibc 的最小示例
├── learn.md              # libc 五模块学习路线
├── learn-elf.md          # ELF 文件格式动手教程
├── Makefile              # 构建系统
├── README.md             # 项目说明
└── .gitignore
```

## 学习路线

```
★★★★★ CPU           — 已掌握
★★★★★ 汇编          — 已掌握（start.S + 内联汇编 syscall）
★★★★★ syscall       — 已掌握（6 个系统调用 + strace 验证）
★★★★☆ libc          — 已完成（自举运行时库，13 个函数）
★★★★★ ELF           — 已完成（learn-elf.md）
★★★★★ Loader        — 已完成（learn-loader.md）
★★★★☆ 动态链接       — 已完成（learn-dynlink.md）
☆☆☆☆☆ POSIX         ← 进行中
☆☆☆☆☆ Shell
```

### 学习文档

| 文档 | 内容 |
|---|---|
| [learn.md](learn.md) | libc 五模块源码学习路线 |
| [learn-elf.md](learn-elf.md) | ELF 文件格式动手教程 |
| [learn-loader.md](learn-loader.md) | 内核 execve 加载流程 |

## 示例程序

```bash
make examples        # 编译 examples/hello.c → build/hello
./build/hello        # 运行，ldd 显示 "not a dynamic executable"
```

## 许可证

MIT
