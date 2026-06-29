/**
 * stdio.c — printf / puts (基于 write 系统调用)
 *
 * =========================================================================
 * printf 实现策略
 * =========================================================================
 *
 * 1. 格式化 → 字符串缓冲区 (栈上, 2048 字节)
 * 2. 缓冲区满时刷新到 stdout (write syscall)
 * 3. 格式串处理完后, 刷新剩余内容
 * 4. 返回写入总字符数
 *
 * 支持的格式说明符:
 *   %d %i  — 有符号十进制 (int)
 *   %u     — 无符号十进制 (unsigned int)
 *   %x     — 十六进制小写 (unsigned int)
 *   %s     — 字符串 (const char *)
 *   %c     — 字符 (int → char)
 *   %p     — 指针 (void *, 输出 "0x" + 十六进制)
 *   %%     — 字面 '%'
 *
 * 不支持 (保留给读者扩展):
 *   宽度/精度修饰符 (%8d, %.4f)
 *   长度修饰符 (%ld, %lld, %zu)
 *   浮点数 (%f, %e, %g)
 *   八进制 (%o)
 *   %n (写入已输出字符数)
 *
 * 数字转换 (ituoa / itoa) 为手写实现,
 * 不依赖任何 libc 函数。
 *
 * =========================================================================
 * puts 实现
 * =========================================================================
 * 输出字符串 + '\n', 底层调用 write(1, ...)。
 */

#include "toylibc.h"

// ---- printf 内部缓冲区 ----
#define PRINTF_BUF_SIZE 2048

typedef struct {
    char  buf[PRINTF_BUF_SIZE];
    int   pos;       // 缓冲区中已写入的字节数
    int   total;     // 总共已刷新到 stdout 的字节数
} printf_ctx_t;

// 刷新缓冲区到 stdout
static void printf_flush(printf_ctx_t *ctx)
{
    if (ctx->pos > 0) {
        long written = toylibc_write(1, ctx->buf, ctx->pos);
        if (written > 0) {
            ctx->total += (int)written;
        }
        ctx->pos = 0;
    }
}

// 向缓冲区追加一个字符
static void printf_putc(printf_ctx_t *ctx, char c)
{
    if (ctx->pos >= PRINTF_BUF_SIZE - 1) {
        printf_flush(ctx);
    }
    ctx->buf[ctx->pos++] = c;
}

// 向缓冲区追加一个字符串 (不包含 '\0')
static void printf_puts(printf_ctx_t *ctx, const char *s, int len)
{
    for (int i = 0; i < len; i++) {
        printf_putc(ctx, s[i]);
    }
}

// 反转 [start, end) 区间的字符串
static void str_reverse(char *start, char *end)
{
    end--;
    while (start < end) {
        char tmp = *start;
        *start = *end;
        *end = tmp;
        start++;
        end--;
    }
}

/**
 * utoa — 无符号整数 → 十进制字符串
 * 返回字符串长度 (不含 '\0')
 */
static int utoa(unsigned long val, char *buf)
{
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    int i = 0;
    while (val > 0) {
        buf[i++] = '0' + (char)(val % 10);
        val /= 10;
    }
    buf[i] = '\0';
    str_reverse(buf, buf + i);
    return i;
}

/**
 * itoa — 有符号整数 → 十进制字符串
 * 返回字符串长度 (含负号, 不含 '\0')
 */
static int itoa(long val, char *buf)
{
    if (val < 0) {
        buf[0] = '-';
        // 处理 LONG_MIN (取绝对值会溢出)
        unsigned long abs_val = (unsigned long)(-(val + 1)) + 1UL;
        return 1 + utoa(abs_val, buf + 1);
    }
    return utoa((unsigned long)val, buf);
}

/**
 * xtoa — 无符号整数 → 十六进制字符串 (小写)
 * 返回字符串长度 (不含 '\0')
 */
static int xtoa(unsigned long val, char *buf)
{
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    const char hex[] = "0123456789abcdef";
    int i = 0;
    while (val > 0) {
        buf[i++] = hex[val & 0xF];
        val >>= 4;
    }
    buf[i] = '\0';
    str_reverse(buf, buf + i);
    return i;
}

// =========================================================================
// toylibc_printf — 格式化输出
// =========================================================================
int toylibc_printf(const char *fmt, ...)
{
    printf_ctx_t ctx;
    ctx.pos   = 0;
    ctx.total = 0;

    va_list ap;
    va_start(ap, fmt);

    while (*fmt != '\0') {
        if (*fmt != '%') {
            printf_putc(&ctx, *fmt);
            fmt++;
            continue;
        }

        // 遇到 '%' — 解析格式说明符
        fmt++;  // 跳过 '%'

        if (*fmt == '\0') break;  // 格式串以 '%' 结尾, 忽略

        switch (*fmt) {
        case 'd': case 'i': {
            // %d / %i: 有符号整数
            char num_buf[32];
            int  len = itoa(va_arg(ap, int), num_buf);
            printf_puts(&ctx, num_buf, len);
            break;
        }
        case 'u': {
            // %u: 无符号整数
            char num_buf[32];
            int  len = utoa(va_arg(ap, unsigned int), num_buf);
            printf_puts(&ctx, num_buf, len);
            break;
        }
        case 'x': {
            // %x: 十六进制
            char num_buf[32];
            int  len = xtoa(va_arg(ap, unsigned int), num_buf);
            printf_puts(&ctx, num_buf, len);
            break;
        }
        case 's': {
            // %s: 字符串
            const char *s = va_arg(ap, const char *);
            if (s == NULL) {
                printf_puts(&ctx, "(null)", 6);
            } else {
                printf_puts(&ctx, s, (int)toylibc_strlen(s));
            }
            break;
        }
        case 'c': {
            // %c: 单个字符
            char c = (char)va_arg(ap, int);
            printf_putc(&ctx, c);
            break;
        }
        case 'p': {
            // %p: 指针 (输出 0x????...)
            void *ptr = va_arg(ap, void *);
            printf_puts(&ctx, "0x", 2);
            char num_buf[32];
            int  len = xtoa((unsigned long)ptr, num_buf);
            printf_puts(&ctx, num_buf, len);
            break;
        }
        case '%': {
            // %%: 字面 '%'
            printf_putc(&ctx, '%');
            break;
        }
        default:
            // 不识别的格式, 原样输出
            printf_putc(&ctx, '%');
            printf_putc(&ctx, *fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);

    // 刷新剩余缓冲区内容
    printf_flush(&ctx);

    return ctx.total;
}

// =========================================================================
// toylibc_puts — 输出字符串 + 换行
// =========================================================================
int toylibc_puts(const char *s)
{
    if (s == NULL) {
        toylibc_write(1, "(null)\n", 7);
        return 7;
    }
    unsigned long len = toylibc_strlen(s);
    long ret1 = toylibc_write(1, s, len);
    long ret2 = toylibc_write(1, "\n", 1);
    if (ret1 < 0 || ret2 < 0) return -1;
    return (int)(ret1 + ret2);
}
