/**
 * shell.c — toylibc 原生 Shell
 *
 * 支持功能:
 *   - 执行外部命令 (ls, cat, echo, ...)
 *   - PATH 解析 (/bin, /usr/bin 等)
 *   - 内置命令: cd, pwd, exit, help
 *   - 空行处理、命令未找到提示
 *
 * 实现:
 *   1. 显示提示符 "$ "
 *   2. read() 读取一行
 *   3. 解析成 argv[] (按空格分割)
 *   4. 如果是内置命令 → 直接执行
 *   5. 否则 → fork() + execve() + waitpid()
 */

#include "toylibc.h"

// 最大输入长度
#define MAX_INPUT  512
// 最大参数个数
#define MAX_ARGS   64
// PATH 搜索目录列表
#define PATH_COUNT 4

static const char *path_dirs[PATH_COUNT] = {
    "/bin",
    "/usr/bin",
    "/usr/local/bin",
    ""
};

// ---- 全局环境变量（execve 传给孩子） ----
static char *g_envp[] = {
    "PATH=/bin:/usr/bin:/usr/local/bin",
    NULL
};

// ---- 前向声明 ----
static int  parse_line(char *input, char *argv[]);
static void run_command(char *argv[]);
static int  try_exec(char *argv[]);
static int  builtin_cd(char *argv[]);
static int  builtin_pwd(void);
static int  builtin_exit(void);
static int  builtin_help(void);
static void print_prompt(void);

// =========================================================================
// main — Shell 主循环
// =========================================================================
int main(void)
{
    char  input[MAX_INPUT];
    char *argv[MAX_ARGS];

    toylibc_puts("\n"
        "╔══════════════════════════════╗\n"
        "║   toysh — toylibc shell     ║\n"
        "║   type 'help' for commands  ║\n"
        "╚══════════════════════════════╝\n");

    while (1) {
        print_prompt();

        // 逐字符读取一行（停止在 '\n'）
        int pos = 0;
        while (pos < MAX_INPUT - 1) {
            char c;
            long n = toylibc_read(0, &c, 1);
            if (n <= 0) {
                // EOF (Ctrl+D) → 退出
                toylibc_puts("");
                return 0;
            }
            if (c == '\n') break;
            input[pos++] = c;
        }
        input[pos] = '\0';

        // 跳过空行
        if (input[0] == '\0') continue;

        // 解析命令行
        int argc = parse_line(input, argv);
        if (argc == 0) continue;

        // 执行
        run_command(argv);
    }

    return 0;
}

// =========================================================================
// parse_line — 按空格分割命令行
//
// 支持引号: "hello world" → 一个参数
// 返回 argc
// =========================================================================
static int parse_line(char *input, char *argv[])
{
    int argc = 0;
    int in_quote = 0;
    char *out = input;

    argv[argc++] = out;

    while (*input != '\0') {
        if (*input == '"') {
            in_quote = !in_quote;
            input++;
            continue;
        }

        if (*input == ' ' && !in_quote) {
            *out = '\0';
            out++;
            input++;
            // 跳过连续空格
            while (*input == ' ') input++;
            if (*input != '\0' && argc < MAX_ARGS - 1) {
                argv[argc++] = out;
            }
            continue;
        }

        *out = *input;
        out++;
        input++;
    }

    *out = '\0';
    argv[argc] = NULL;
    return argc;
}

// =========================================================================
// run_command — 分发命令
// =========================================================================
static void run_command(char *argv[])
{
    if (argv[0] == NULL || argv[0][0] == '\0') return;

    // 内置命令
    if (toylibc_strcmp(argv[0], "cd") == 0)    { builtin_cd(argv); return; }
    if (toylibc_strcmp(argv[0], "pwd") == 0)   { builtin_pwd(); return; }
    if (toylibc_strcmp(argv[0], "exit") == 0)  { builtin_exit(); return; }
    if (toylibc_strcmp(argv[0], "help") == 0)  { builtin_help(); return; }

    // 外部命令 → fork + execve
    long pid = toylibc_fork();

    if (pid == 0) {
        // ---- 子进程 ----
        if (try_exec(argv) != 0) {
            // execve 失败
            toylibc_printf("toy: %s: command not found\n", argv[0]);
        }
        toylibc_exit(127);  // 命令未找到
    } else if (pid > 0) {
        // ---- 父进程 ----
        int status;
        toylibc_waitpid(pid, &status, 0);
    } else {
        toylibc_puts("toy: fork failed");
    }
}

// =========================================================================
// try_exec — 尝试在 PATH 目录中执行命令
//
// 依次尝试: ./cmd, /bin/cmd, /usr/bin/cmd, /usr/local/bin/cmd
// 如果路径中含 /，直接尝试原路径
// 返回: 0=成功(不返回), -1=失败
// =========================================================================
static int try_exec(char *argv[])
{
    char fullpath[256];

    // 如果命令含 /，直接尝试
    for (int i = 0; argv[0][i] != '\0'; i++) {
        if (argv[0][i] == '/') {
            toylibc_execve(argv[0], argv, g_envp);
            return -1;
        }
    }

    // 尝试当前目录
    toylibc_strcpy(fullpath, "./");
    int len = toylibc_strlen(fullpath);
    toylibc_strcpy(fullpath + len, argv[0]);
    toylibc_execve(fullpath, argv, g_envp);

    // 尝试 PATH 中的每个目录
    for (int i = 0; i < PATH_COUNT; i++) {
        if (path_dirs[i][0] == '\0') continue;

        toylibc_strcpy(fullpath, path_dirs[i]);
        len = toylibc_strlen(fullpath);
        fullpath[len] = '/';
        toylibc_strcpy(fullpath + len + 1, argv[0]);
        toylibc_execve(fullpath, argv, g_envp);
    }

    return -1;
}

// =========================================================================
// builtin_cd — 切换目录
// =========================================================================
static int builtin_cd(char *argv[])
{
    if (argv[1] == NULL) {
        toylibc_puts("cd: missing argument");
        return -1;
    }
    if (toylibc_chdir(argv[1]) != 0) {
        toylibc_printf("cd: %s: no such directory\n", argv[1]);
        return -1;
    }
    return 0;
}

// =========================================================================
// builtin_pwd — 打印当前目录
// =========================================================================
static int builtin_pwd(void)
{
    char cwd[256];
    if (toylibc_getcwd(cwd, sizeof(cwd)) > 0) {
        toylibc_puts(cwd);
    } else {
        toylibc_puts("pwd: error");
    }
    return 0;
}

// =========================================================================
// builtin_exit — 退出 shell
// =========================================================================
static int builtin_exit(void)
{
    toylibc_puts("bye!");
    toylibc_exit(0);
    return 0;
}

// =========================================================================
// builtin_help — 显示帮助
// =========================================================================
static int builtin_help(void)
{
    toylibc_puts(
        "\n"
        "  toysh — built with toylibc\n"
        "  ═══════════════════════════\n"
        "  Built-in commands:\n"
        "    cd  <dir>    change directory\n"
        "    pwd          print working directory\n"
        "    exit         quit the shell\n"
        "    help         show this message\n"
        "\n"
        "  External commands:\n"
        "    ls, cat, echo, date, ...\n"
        "    (searched in /bin, /usr/bin, etc.)\n"
    );
    return 0;
}

// =========================================================================
// print_prompt — 显示提示符
// =========================================================================
static void print_prompt(void)
{
    toylibc_write(1, "$ ", 2);
}
