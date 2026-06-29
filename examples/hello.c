/**
 * hello.c — 链接 toylibc 的最小程序
 *
 * 编译后完全不依赖 glibc，elf 里 NEEDED 是空的。
 */

#include "toylibc.h"

int main(void)
{
    toylibc_puts("Hello from toylibc!");
    toylibc_printf("malloc test: %p\n", toylibc_malloc(32));
    return 0;
}
