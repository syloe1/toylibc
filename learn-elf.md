# ELF 阶段 — 动手学习

> 用你自己的 toylibc 产物来理解 Linux 可执行文件格式

## 前置：三种 ELF 文件

```bash
file build/start.o      # relocatable — 可重定位目标文件
file build/libtoy.a     # ar archive — 静态库（.o 的打包）
file build/test         # executable — 可执行文件
```

| 类型 | 谁产生的 | 能直接运行？ | 地址是否确定？ |
|---|---|---|---|
| `.o` | gcc -c | 不能 | 地址都是 0 |
| `.a` | ar rcs | 不能 | 地址都是 0 |
| 可执行文件 | ld / gcc | 能 | 全部确定 |

---

## 第一课：ELF 头 — 文件的第一页

```bash
readelf -h build/test
```

```
Magic:  7f 45 4c 46    ← . E L F
Class:  ELF64          ← 64 位
Entry:  0x401ce1       ← 程序第一条指令在哪
```

用 `xxd build/test | head -1` 直接看二进制：

```
7f45 4c46 0201 0100  0000 0000 0000 0000
│    │    ││  │                       └─ 保留
│    │    ││  └─ ABI: System V
│    │    │└─ 版本
│    │    └─ 64位 (01=32位, 02=64位)
│    └─ E L F — 魔数
└─ DEL (0x7f) — 魔数起始
```

**亲手做：** 用 GDB 看 ELF 头的内存加载

```
(gdb) info files           # 看到所有 section 的加载地址
(gdb) x/16xb 0x400000      # 看 .text 之前的 ELF 头（已被内核读取）
```

---

## 第二课：两套视图 — Sections vs Segments

ELF 设计上分两套"地图"：

```
        编译/链接阶段              加载/执行阶段
        ============              ============
        ld 读取 sections          内核读取 segments

        .text  ─┐                ┌─ LOAD (RX)
        .rodata ┤                ├─ LOAD (R)
        .bss   ─┤──→ 合并为 →   ├─ LOAD (RW)
        .data  ─┘                └─ NOTE
```

```bash
# 链接视图 — 编译器/链接器关心的
readelf -S build/test

# 执行视图 — 内核关心的
readelf -l build/test
```

关键观察 — Section to Segment mapping：

```
02     .rodata .eh_frame          ← 只读数据合并到一个 LOAD 段
03     .bss                       ← 未初始化数据单独一个 LOAD 段
```

**亲手做：** 用 strace 看内核怎么加载

```bash
strace -e trace=mmap ./build/test 2>&1 | head -5
# 只有 malloc 大对象才走 mmap，代码段加载在 execve 阶段就完成了
```

---

## 第三课：符号表 — 谁定义了谁

```bash
nm build/start.o
```

```
                 U main            ← U = Undefined，等链接器填
0000000000000000 T _start          ← T = Text (代码)，地址还是 0
                 U toylibc_exit
```

```bash
nm build/test | grep toylibc_write
```

```
0000000000401d10 T toylibc_write   ← 地址已确定: 0x401d10
```

| 符号类型 | 含义 |
|---|---|
| `T` | 代码段定义的全局函数 |
| `t` | 代码段定义的本地函数（static） |
| `U` | 未定义 — 需要链接器从别的 .o 找来填 |
| `B` | BSS 段（未初始化全局变量） |
| `D` | Data 段（已初始化全局变量） |

**亲手做：** 对比 .o 和可执行文件的 main

```bash
nm build/test.o | grep main      # 地址 = ?
nm build/test | grep main        # 地址 = ?
```

---

## 第四课：重定位 — 链接器的工作

```bash
readelf -r build/test.o | head -30
```

每一行是一个"还没填的地址"：

```
Offset          Type           Sym. Name + Addend
00000000001b   R_X86_64_PLT32  toylibc_printf - 4
```

意思是：`test.o` 里偏移 `0x1b` 的位置需要填上 `toylibc_printf` 的最终地址。

可执行文件没有重定位（已全部填写）：

```bash
readelf -r build/test
# 输出为空 — 所有地址已确定
```

**亲手做：** 用 objdump 看重定位前后对比

```bash
# .o 里 call printf → 目标地址还是 0（占位）
objdump -d build/test.o | grep -A1 'call.*toylibc_printf' | head -4

# 可执行文件里 → 地址已填好
objdump -d build/test | grep -A1 'call.*toylibc_printf' | head -4
```

---

## 第五课：从 _start 到 main 的全链路

```bash
objdump -d build/test --start-address=0x401ce1 --stop-address=0x401d60
```

```
_start:
  401ce1:  mov    (%rsp),%rdi        ← argc → rdi
  401ce5:  lea    0x8(%rsp),%rsi     ← argv → rsi
  401cea:  xor    %rbp,%rbp          ← 清空栈帧（GDB 回溯终点）
  401ced:  call   main               ← 进入 C 世界
  401cf2:  mov    %rax,%rdi          ← main 返回值 → exit 参数
  401cf5:  call   toylibc_exit       ← 永不返回
  401cfa:  ud2                       ← 保险：如果 exit 返回就崩溃
```

**亲手做：** 用 GDB 跟踪这条链路

```
(gdb) break _start
(gdb) run
(gdb) si           # 单步看 mov (%rsp),%rdi
(gdb) si           # lea 0x8(%rsp),%rsi
(gdb) si           # xor %rbp,%rbp
(gdb) si           # call main — 进去了！
(gdb) bt           # 调用栈：_start → main
```

---

## 第六课：.bss 段 — 不占文件空间的变量

```bash
size build/test
```

```
text    data    bss     dec     hex
9792    0       32      9824    2660
```

`bss = 32` — 但用 `ls -l` 看文件大小，比 text+data+bss 的总和小得多。

因为 `.bss` 段在文件里**不占空间**（`NOBITS`），内核加载时直接分配零页：

```bash
readelf -S build/test | grep bss
```

```
.bss  NOBITS  0000000000404000  0000000000000020  WA  0  0  8
       ↑NOBITS = 文件里没有实际数据                           ↑
```

你的 `heap_ok`、`heap_lo`、`heap_hi` 这些全局变量就躺在 .bss 里：

```bash
nm build/test | grep -E 'heap_ok|heap_lo|heap_hi'
```

---

## 检查清单

- [ ] 能说出 `.o`、`.a`、可执行文件三者的区别
- [ ] 知道 ELF 魔数 `7f 45 4c 46` 在文件第几个字节
- [ ] 能解释 Section 视图和 Segment 视图分别给谁用
- [ ] 知道 `nm` 输出里 `T`、`t`、`U`、`B` 的含义
- [ ] 能解释为什么 `.o` 里的函数地址是 0，可执行文件里是 0x401xxx
- [ ] 用 GDB 从 `_start` 单步到 `main`
- [ ] 能说出 `.bss` 段为什么在文件里不占空间

---

## 常用命令速查

| 命令 | 用途 |
|---|---|
| `file <bin>` | 快速识别 ELF 类型 |
| `readelf -h` | ELF 头（入口点、魔数） |
| `readelf -S` | 段表（链接视图） |
| `readelf -l` | 程序头（执行视图） |
| `readelf -s` | 完整符号表 |
| `readelf -r` | 重定位表（只对 .o 有用） |
| `nm <file>` | 简化符号表 |
| `objdump -d` | 反汇编 |
| `objdump -x` | 全部信息 dump |
| `size <file>` | text/data/bss 大小 |
| `xxd <file> \| head -1` | 肉眼验证 ELF 魔数 |
| `strip <file>` | 去掉符号和调试信息 |
