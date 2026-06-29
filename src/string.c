/**
 * string.c — strlen / memcpy / memset 纯用户态实现
 *
 * 这三个函数不依赖任何系统调用，完全在用户态执行。
 * 复杂度为 O(n)，不做 SIMD 优化，追求代码简洁可读。
 */

#include "toylibc.h"

// =========================================================================
// toylibc_strlen — 计算字符串长度（不含 '\0'）
//
// 逐字节扫描直到遇到 '\0'。
// 极简实现，没有像 glibc 那样按 word 对齐后一次比较 8 字节
// （那是为了性能；这里为了教学清晰度）。
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
// toylibc_memcpy — 逐字节复制内存
//
// 前提：dest 和 src 不能重叠（overlap）。
//      若需要处理重叠，应使用 memmove。
//
// 为何不用 word 复制？
//   - 字节复制对所有对齐都正确
//   - 编译器可能自动向量化（-O2 时）
//   - 保持代码最小、最易懂
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
// toylibc_memset — 用常量字节填充内存
//
// c 被转换为 unsigned char（取低 8 位），然后逐字节写入 dest。
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
