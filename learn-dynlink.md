# 动态链接阶段 — 为什么你的 hello 是 "not a dynamic executable"

> 你的 toylibc 是静态的，系统 ls 是动态的。区别在哪？内核加载后谁来做链接？

## 两种链接方式：一眼区别

```bash
# 你的 toylibc hello
file build/hello
# → statically linked

ldd build/hello
# → not a dynamic executable
```

```bash
# 系统的 ls
file /bin/ls
# → dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2

ldd /bin/ls
# → libc.so.6, ld-linux-x86-64.so.2, ...
```

| | 静态 | 动态 |
|---|---|---|
| 库代码在哪 | 可执行文件里（全拷进去了） | 单独的 .so 文件（共享） |
| 文件体积 | 大（所有函数都在里面） | 小（只有自己的代码） |
| ldd | `not a dynamic executable` | 列出一堆 .so |
| 链接时机 | 编译时（ld） | 运行时（ld-linux.so） |
| .interp 段 | 无 | `/lib64/ld-linux-x86-64.so.2` |

---

## 第一课：动态链接多了哪些 ELF 段？

```bash
# 你的静态 hello
readelf -S build/hello | grep -E '\.interp|\.dynamic|\.got|\.plt|\.dynsym'
# → 空的，这些段都不存在！
```

```bash
# 动态 ls
readelf -S /bin/ls | grep -E '\.interp|\.dynamic|\.got|\.plt|\.dynsym'
```

```
[ 2] .interp       ← 指定动态链接器路径
[ 4] .dynsym       ← 动态符号表（只包含需要的符号）
[ 8] .rela.dyn     ← 数据重定位
[ 9] .rela.plt     ← PLT 重定位
[11] .plt          ← 过程链接表（跳板）
[28] .dynamic      ← 动态链接信息（类似 PE 的导入表）
[29] .got          ← 全局偏移表（运行时填地址）
```

**亲手做：** 对比你 hello 和系统程序的段数量

```bash
readelf -S build/hello | wc -l    # 你的
readelf -S /bin/ls | wc -l        # 系统的（多很多）
```

---

## 第二课：.interp — 内核加载后的"接力棒"

```bash
readelf -p .interp /bin/ls
```

```
/lib64/ld-linux-x86-64.so.2
```

这就是 **动态链接器**（也叫 `ld.so`、程序解释器）。加载流程：

```
execve("/bin/ls")
  │
  ├─ 内核读 ls 的 ELF 头
  ├─ 发现 .interp = "/lib64/ld-linux-x86-64.so.2"
  │
  ├─ 内核加载 ld-linux.so 到内存
  ├─ 内核把控制权交给 ld-linux.so（不是 ls！）
  │
  ├─ ld-linux.so 读 ls 的 .dynamic 段
  ├─ ld-linux.so 找到 NEEDED: libc.so.6
  ├─ ld-linux.so 加载 libc.so.6 到内存
  ├─ ld-linux.so 填写 GOT 表（重定位）
  │
  └─ ld-linux.so 跳转到 ls 的入口点
       ls 开始执行
       ls 调用 printf → 走 PLT → GOT → libc.so 里的 printf
```

对比你的 toylibc hello：

```
execve("./build/hello")
  │
  ├─ 内核读 hello 的 ELF 头
  ├─ 没有 .interp！直接加载 LOAD 段
  ├─ 设 rip = _start
  │
  └─ _start → main，全是自己的代码
```

**亲手做：** 验证 ld-linux.so 本身也是 ELF

```bash
file /lib64/ld-linux-x86-64.so.2
readelf -h /lib64/ld-linux-x86-64.so.2 | grep Type
# → Type: DYN (Shared object file) — ld-linux.so 本身也是个 .so
```

---

## 第三课：PLT / GOT — 延迟绑定

这是动态链接最精巧的设计。以 ls 调用 `printf` 为例：

```
ls:  call printf@plt
         │
         ▼
      .plt 段（跳板）:
         jmp *GOT[n]          ← 第一次：GOT[n] 指回下一条指令
         push $index          ← 告诉 ld-linux 是第几个函数
         jmp PLT[0]           ← 调用 ld-linux 的解析器
              │
              ▼
         ld-linux.so:
         查 libc.so 的符号表
         找到 printf 的真正地址
         写回 GOT[n]          ← 之后调用不再走这里
         跳转到 printf
         
      .plt 段（跳板）:
第二次:  jmp *GOT[n]          ← GOT[n] 现在指向 libc 里的 printf！
```

PLT 反汇编（你刚才看到的）：

```asm
<__snprintf_chk@plt>:           # 每个外部函数一个 PLT 条目
   jmp    *0x9c711a(%rip)       # 跳转到 GOT 里存的地址
   push   $0x0                  # 延迟绑定：推进函数编号
   jmp    PLT[0]                # 跳转到解析器
```

**亲手做：** 看 GOT 初始值

```bash
objdump -s -j .got /bin/ls | head -5
```

GOT 里的地址在程序启动前是"假的"——指回 PLT 存根。第一次调用后才变成 libc 里的真实地址。

**亲手做：** 关闭延迟绑定对比

```bash
# 正常（延迟绑定）
time /bin/ls > /dev/null

# 全部立即绑定（LD_BIND_NOW）
time LD_BIND_NOW=1 /bin/ls > /dev/null
# 启动稍慢，但运行中不再有绑定开销
```

---

## 第四课：LD_DEBUG — 看动态链接的每一步

```bash
# 看绑定过程
LD_DEBUG=bindings /bin/true 2>&1 | head -20

# 看库搜索
LD_DEBUG=libs /bin/true 2>&1 | head -20

# 看所有信息（非常长）
LD_DEBUG=all /bin/true 2>&1 | head -20
```

输出示例：

```
binding file libc.so.6 to libc.so.6: normal symbol `__vdso_clock_gettime'
file=libc.so.6;  needed by /bin/true
find library=libc.so.6; searching
```

---

## 第五课：LD_PRELOAD — 运行时替换函数

不用重新编译，就能把 ls 的 `write` 换成自己的：

```bash
# 写一个劫持库
cat > /tmp/hijack.c << 'EOF'
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
ssize_t write(int fd, const void *buf, size_t count) {
    fputs("[HIJACKED] ", stderr);
    return fwrite(buf, 1, count, stdout);
}
EOF

gcc -shared -fPIC /tmp/hijack.c -o /tmp/hijack.so

# 劫持 ls
LD_PRELOAD=/tmp/hijack.so /bin/ls
# 所有 ls 的 write 调用前都会打印 [HIJACKED]
```

**你的 toylibc 不受 LD_PRELOAD 影响**——因为它是静态链接的，没有 PLT/GOT，不经过 ld-linux.so。

```bash
LD_PRELOAD=/tmp/hijack.so ./build/hello
# 不会有 [HIJACKED]，因为 hello 直接 syscall
```

---

## 第六课：为什么还需要静态链接？

动态链接省空间、能热更新（换 .so 不用重编译），但：

| 场景 | 用哪种 |
|---|---|
| 容器镜像（scratch/from scratch） | 静态 |
| initramfs（内核急救环境） | 静态 |
| 安全敏感（避免 LD_PRELOAD 劫持） | 静态 |
| 最小化部署 | 静态 |
| 日常桌面/服务器 | 动态 |

你体验一下：

```bash
# 静态的 hello
docker run --rm -v $(pwd)/build/hello:/hello alpine:latest /hello 2>/dev/null || \
  echo "如果装了 docker，Alpine 容器（没有 glibc）也能跑你的 hello"

# 动态的 ls 在 Alpine 容器里会报错
# /bin/ls: not found（因为缺少 ld-linux.so 和 libc.so）
```

---

## 核心命令速查

| 命令 | 用途 |
|---|---|
| `file <bin>` | 看 static/dynamic |
| `ldd <bin>` | 看动态依赖 |
| `readelf -d <bin>` | .dynamic 段（NEEDED, SONAME...） |
| `readelf -p .interp <bin>` | 看解释器路径 |
| `objdump -d -j .plt <bin>` | PLT 表（跳板） |
| `objdump -s -j .got <bin>` | GOT 表的内容 |
| `LD_DEBUG=bindings <bin>` | 看运行时符号绑定 |
| `LD_PRELOAD=xxx.so <bin>` | 劫持函数 |
| `LD_BIND_NOW=1 <bin>` | 禁用延迟绑定 |

---

## 检查清单

- [ ] 能说出静态链接和动态链接各 3 个区别
- [ ] 知道 .interp 段的内容和 ld-linux.so 的角色
- [ ] 能画出 PLT → GOT → libc.so 的延迟绑定流程图
- [ ] 理解为什么你的 toylibc hello 不受 LD_PRELOAD 影响
- [ ] 用 LD_DEBUG=bindings /bin/true 看过一次绑定过程

---

## 总结：动态链接一句话

> 内核加载 ELF 后发现 .interp = ld-linux.so，把控制权交给它；ld-linux 加载 .so、填 GOT、重定位；程序跑起来后通过 PLT→GOT→libc 调用外部函数。你的 toylibc 没这些东西，直接 syscall。

## 你的 toylibc 可以加动态链接吗？

可以，但属于高级阶段。你需要：

1. 把 toylibc 编译成 `libtoy.so`（`-shared -fPIC`）
2. 写一个 `.interp` 指向 ld-linux
3. 用 PLT/GOT 调用 toylibc 函数
4. 设置 soname、符号版本

```bash
# 试一下：把你的 libtoy.a 变成 libtoy.so
gcc -shared -fPIC src/*.c -I include -o build/libtoy.so
file build/libtoy.so
# → ELF 64-bit LSB shared object
```

这在 POSIX 阶段之后可以作为扩展。
