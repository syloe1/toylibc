/**
 * string.c — strlen / memcpy / memset / memcmp / memmove / strcmp / strcpy
 *
 * 全部纯用户态实现，不依赖任何系统调用。
 * 复杂度为 O(n)，不做 SIMD 优化，追求代码简洁可读。
 */

#include "toylibc.h"

// =========================================================================
// toylibc_strlen — 计算字符串长度（不含 '\0'）
// =========================================================================
unsigned long toylibc_strlen(const char *s)
{
    unsigned long n = 0;
    while (*s != '\0') {
        n++;
        s++;
    }
    return n;
}

// =========================================================================
// toylibc_memcpy — 逐字节复制内存（dest 和 src 不能重叠）
// =========================================================================
void *toylibc_memcpy(void *dest, const void *src, unsigned long n)
{
    unsigned char       *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    for (unsigned long i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

// =========================================================================
// toylibc_memmove — 安全复制，允许 dest 和 src 重叠
//
// 与 memcpy 的区别:
//   memcpy 假设 dest 和 src 不重叠（UB 如果重叠）
//   memmove 先判方向：src < dest 时从后往前复制，保证正确性
// =========================================================================
void *toylibc_memmove(void *dest, const void *src, unsigned long n)
{
    unsigned char       *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d < s) {
        // 源在目标之后 → 从头复制
        for (unsigned long i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        // 源在目标之前 → 从尾复制（防止覆盖还未读取的源数据）
        for (unsigned long i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    // d == s: 无需复制
    return dest;
}

// =========================================================================
// toylibc_memset — 用常量字节填充内存
// =========================================================================
void *toylibc_memset(void *dest, int c, unsigned long n)
{
    unsigned char *d = (unsigned char *)dest;
    unsigned char byte = (unsigned char)c;

    for (unsigned long i = 0; i < n; i++) {
        d[i] = byte;
    }
    return dest;
}

// =========================================================================
// toylibc_memcmp — 逐字节比较内存区域
//
// 返回: 0 = 相等, <0 = a < b, >0 = a > b
// =========================================================================
int toylibc_memcmp(const void *a, const void *b, unsigned long n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (unsigned long i = 0; i < n; i++) {
        if (pa[i] != pb[i]) {
            return (int)pa[i] - (int)pb[i];
        }
    }
    return 0;
}

// =========================================================================
// toylibc_strcmp — 逐字节比较字符串
//
// 返回: 0 = 相等, <0 = a < b, >0 = a > b
// =========================================================================
int toylibc_strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return (int)(unsigned char)*a - (int)(unsigned char)*b;
        }
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

// =========================================================================
// toylibc_strcpy — 复制字符串（包含 '\0'）
//
// 返回 dest 指针。调用者保证 dest 足够大。
// =========================================================================
char *toylibc_strcpy(char *dest, const char *src)
{
    char *d = dest;
    while (*src != '\0') {
        *d = *src;
        d++;
        src++;
    }
    *d = '\0';
    return dest;
}
