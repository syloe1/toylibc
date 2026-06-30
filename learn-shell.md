# Shell 阶段 — 用 toylibc 写自己的命令行解释器

> 最终阶段：用你 POSIX 阶段实现的 18 个系统调用，写一个能用的 Shell

## toysh 支持的命令

| 类型 | 命令 | 实现方式 |
|---|---|---|
| 内置 | `help` | 打印帮助信息（纯用户态） |
| 内置 | `cd <dir>` | `toylibc_chdir()` |
| 内置 | `pwd` | `toylibc_getcwd()` |
| 内置 | `exit` | `toylibc_exit()` |
| 外部 | `ls`, `cat`, `echo`, `date`, ... | `fork()` + `execve()` + `waitpid()` |

## 架构

```
while (1) {
    1. 打印 "$ "
    2. read() 逐字符读取一行
    3. parse_line() 按空格/引号分割 → argv[]
    4. 是内置命令？
       ├─ 是 → builtin_xxx() 直接执行
       └─ 否 → fork()
                ├─ 子进程: try_exec(argv) → execve()
                └─ 父进程: waitpid()
}
```

## 核心流程

### 1. 输入解析

```c
static int parse_line(char *input, char *argv[])
```

就地分割，用 `\0` 替换空格，支持双引号 `"hello world"` → 一个参数。

### 2. PATH 解析

```c
static int try_exec(char *argv[])
```

依次尝试：
```
./ls          ← 当前目录
/bin/ls       ← /bin
/usr/bin/ls   ← /usr/bin
```

哪个存在且可执行就 execve，都不存在则返回 "command not found"。

### 3. fork + execve + waitpid

```
父进程 fork() → 子进程 execve() → 父进程 waitpid()
```

execve 成功不返回（子进程被替换），失败则子进程退出码 127。

## 亲手跑

### 交互模式

```bash
./build/toysh
```

```
$ help
$ pwd
/home/cc/toylibc
$ cd /tmp
$ pwd
/tmp
$ ls -la
$ cat README.md
$ date
$ echo hello world
$ exit
bye!
```

### 管道输入（自动化测试）

```bash
printf "help\npwd\nls\nexit\n" | ./build/toysh
```

## 源码阅读顺序

1. `examples/shell.c` — 全部代码（~250 行）
2. `main()` — 主循环：prompt → read → parse → run
3. `parse_line()` — 如何把字符串变成 argv
4. `try_exec()` — PATH 搜索 + execve
5. `builtin_*()` — 四个内置命令

## 你可以扩展的方向

| 功能 | 需要用的 |
|---|---|
| 重定向 `>` `<` | `open` + `dup2` + `close` |
| 管道 `|` | `pipe` + `dup2` + 两次 `fork` |
| Ctrl+C 处理 | `sigaction(SIGINT, SIG_IGN)` |
| 后台执行 `&` | fork 后不 wait |
| 环境变量 `$PATH` | 读 envp 而不是写死 path_dirs[] |
| Tab 补全 | `read` 改成逐字符 + 特殊键处理 |
| 历史记录 | 自己维护一个环形缓冲区 |

## Shell 一句话

> Shell = `read()` 一行 + `fork()` 一个孩子 + `execve()` 替换孩子 + `waitpid()` 等孩子。如此循环。

## 项目完成检查清单

- [ ] `./build/test` — 31/31 测试通过
- [ ] `strace ./build/hello` — 看到你的 write/brk/exit 系统调用
- [ ] `ldd ./build/hello` — "not a dynamic executable"
- [ ] `readelf -h build/test` — 能认出入口点、魔数
- [ ] `cat /proc/self/maps` — 能指出 stack、heap、vdso
- [ ] `LD_DEBUG=bindings /bin/true` — 看过动态绑定
- [ ] `./build/posix-demo` — fork/pipe/sleep 都成功
- [ ] `./build/toysh` — 交互使用自己的 shell
