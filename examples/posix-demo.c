/**
 * posix-demo.c — 验证 POSIX 新系统调用
 */

#include "toylibc.h"

int main(void)
{
    toylibc_puts("=== POSIX Demo ===");

    // getpid / getppid
    toylibc_printf("pid=%d  ppid=%d\n",
                   (int)toylibc_getpid(), (int)toylibc_getppid());

    // gettimeofday
    struct toylibc_timeval tv;
    toylibc_gettimeofday(&tv, NULL);
    toylibc_printf("time: %d.%d\n", (int)tv.tv_sec, (int)tv.tv_usec);

    // nanosleep (50ms)
    struct toylibc_timespec req = {0, 50000000};  // 0s + 50,000,000ns = 50ms
    struct toylibc_timespec rem;
    toylibc_puts("sleeping 50ms...");
    toylibc_nanosleep(&req, &rem);
    toylibc_puts("done");

    // open / write / close
    int fd = toylibc_open("/tmp/toylibc_test.txt",
                          TOY_O_WRONLY | TOY_O_CREAT | TOY_O_TRUNC, 0644);
    if (fd >= 0) {
        toylibc_printf("fd=%d\n", fd);
        toylibc_write(fd, "hello POSIX\n", 12);
        toylibc_close(fd);
        toylibc_puts("file written: /tmp/toylibc_test.txt");
    } else {
        toylibc_printf("open failed: %d\n", fd);
    }

    // getcwd
    char cwd[256];
    if (toylibc_getcwd(cwd, sizeof(cwd)) > 0) {
        toylibc_printf("cwd: %s\n", cwd);
    }

    // pipe
    int fds[2];
    if (toylibc_pipe(fds) == 0) {
        toylibc_printf("pipe: read=%d write=%d\n", fds[0], fds[1]);
        toylibc_close(fds[0]);
        toylibc_close(fds[1]);
    }

    // fork demo (简单)
    toylibc_puts("\n=== Fork Test ===");
    long pid = toylibc_fork();
    if (pid == 0) {
        // 子进程
        toylibc_printf("  child: my pid=%d\n", (int)toylibc_getpid());
    } else if (pid > 0) {
        // 父进程
        toylibc_printf("  parent: child pid=%d\n", (int)pid);
        int status;
        toylibc_waitpid(pid, &status, 0);
        toylibc_puts("  parent: child exited");
    } else {
        toylibc_puts("  fork failed");
    }

    return 0;
}
