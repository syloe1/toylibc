/**
 * peek.c — 自旋等待，给你时间看 /proc/PID/maps
 *
 * 用法:
 *   ./build/peek &
 *   cat /proc/$!/maps
 *   kill $!
 */

#include "toylibc.h"

int main(void)
{
    toylibc_puts("Waiting 10 seconds — check /proc/PID/maps now!");

    // 自旋 10 秒（没有 sleep 系统调用，手动计数）
    volatile unsigned long i;
    for (i = 0; i < 5000000000UL; i++) {
        // busy wait
    }

    toylibc_puts("Done.");
    return 0;
}
