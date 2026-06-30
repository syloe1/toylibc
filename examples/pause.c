/**
 * pause.c — 按回车才退出，方便查看 /proc/PID/maps
 *
 * 用法:
 *   ./build/pause &
 *   cat /proc/$!/maps
 *   按回车让它退出
 */

#include "toylibc.h"

int main(void)
{
    char buf[1];
    toylibc_puts("Press Enter to see my /proc/PID/maps, then press Enter again to exit...");
    toylibc_read(0, buf, 1);   // 等用户按回车
    return 0;
}
