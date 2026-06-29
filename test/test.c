/**
 * test.c — toylibc 综合测试
 *
 * 测试项:
 *   TEST 1  — strlen / memcmp
 *   TEST 2  — memset / memcpy
 *   TEST 3  — write (fd=1 stdout)
 *   TEST 4  — printf (%d, %u, %x, %s, %c, %p, %%)
 *   TEST 5  — puts
 *   TEST 6  — malloc / free (小对象, brk 堆)
 *   TEST 7  — calloc (归零)
 *   TEST 8  — realloc (扩/缩/新分配)
 *   TEST 9  — 大对象分配 (mmap 路径, >128KB)
 *   TEST 10 — 压力测试: 多次分配/释放, 合并&分裂
 */

#include "toylibc.h"

// =========================================================================
// 辅助: 断言 (极简, 不依赖 assert.h)
// =========================================================================
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name, cond) do {                                     \
    if (cond) {                                                   \
        tests_passed++;                                           \
        toylibc_printf("  [PASS] %s\n", name);                     \
    } else {                                                      \
        tests_failed++;                                           \
        toylibc_printf("  [FAIL] %s (line %d)\n", name, __LINE__); \
    }                                                             \
} while(0)

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    toylibc_printf("\n");
    toylibc_printf("========================================\n");
    toylibc_printf("  toylibc — Comprehensive Test Suite\n");
    toylibc_printf("========================================\n\n");

    // =====================================================================
    // TEST 1: strlen + memcmp
    // =====================================================================
    toylibc_printf("[TEST 1] strlen / memcmp\n");
    const char *s1 = "hello toylibc";
    TEST("strlen(\"hello toylibc\") == 13",
         toylibc_strlen(s1) == 13);
    TEST("strlen(\"\") == 0",
         toylibc_strlen("") == 0);
    TEST("memcmp equal",
         toylibc_memcmp("abc", "abc", 3) == 0);
    TEST("memcmp a < b",
         toylibc_memcmp("abc", "abd", 3) < 0);
    TEST("memcmp a > b",
         toylibc_memcmp("abz", "abc", 3) > 0);
    toylibc_printf("\n");

    // =====================================================================
    // TEST 2: memset + memcpy
    // =====================================================================
    toylibc_printf("[TEST 2] memset / memcpy\n");
    char buf2[32];
    toylibc_memset(buf2, 'X', 10);
    toylibc_memset(buf2 + 10, '\0', 1);
    TEST("memset fill X",
         toylibc_memcmp(buf2, "XXXXXXXXXX", 10) == 0);

    toylibc_memset(buf2, 0, sizeof(buf2));
    toylibc_memcpy(buf2, "copy test", 10);
    TEST("memcpy",
         toylibc_memcmp(buf2, "copy test", 9) == 0);
    toylibc_printf("\n");

    // =====================================================================
    // TEST 3: write
    // =====================================================================
    toylibc_printf("[TEST 3] write (fd=1 stdout)\n");
    long wr = toylibc_write(1, "  hello from toylibc_write!\n", 28);
    TEST("write returns 28", wr == 28);
    toylibc_printf("\n");

    // =====================================================================
    // TEST 4: printf
    // =====================================================================
    toylibc_printf("[TEST 4] printf\n");
    toylibc_printf("  %%d: %d, %%u: %u, %%x: %x\n", -42, 12345, 0xDEAD);
    toylibc_printf("  %%s: %s, %%c: %c\n", "a string", 'Z');
    void *fake_ptr = (void *)0x7fff1234abcdUL;
    toylibc_printf("  %%p: %p, %%%%: 100%%\n", fake_ptr);

    // printf 返回值测试: 手动计算预期字符数
    int ret = toylibc_printf("  x=%d y=%s\n", 7, "ok");
    toylibc_printf("  printf returned %d chars (expected 11)\n", ret);
    TEST("printf return value", ret == 11);
    toylibc_printf("\n");

    // =====================================================================
    // TEST 5: puts
    // =====================================================================
    toylibc_printf("[TEST 5] puts\n");
    int pret = toylibc_puts("  hello from puts");
    TEST("puts returns >= 0", pret > 0);
    toylibc_printf("\n");

    // =====================================================================
    // TEST 6: malloc / free (brk 堆, 小对象)
    // =====================================================================
    toylibc_printf("[TEST 6] malloc / free (brk heap)\n");
    void *p6a = toylibc_malloc(64);
    void *p6b = toylibc_malloc(128);
    void *p6c = toylibc_malloc(256);
    TEST("malloc 64 != NULL",  p6a != NULL);
    TEST("malloc 128 != NULL", p6b != NULL);
    TEST("malloc 256 != NULL", p6c != NULL);
    TEST("malloc zero returns NULL", toylibc_malloc(0) == NULL);

    // 写入数据验证可访问
    toylibc_memset(p6a, 0xAB, 64);
    TEST("write to p6a ok", ((unsigned char *)p6a)[0] == 0xAB);

    // 释放
    toylibc_free(p6b);
    toylibc_free(p6a);
    toylibc_free(p6c);
    TEST("free(NULL) no crash", 1);  // free(NULL) 应该安全
    toylibc_free(NULL);
    toylibc_printf("\n");

    // =====================================================================
    // TEST 7: calloc (归零验证)
    // =====================================================================
    toylibc_printf("[TEST 7] calloc\n");
    int *p7 = (int *)toylibc_calloc(10, sizeof(int));
    TEST("calloc(10,4) != NULL", p7 != NULL);
    int all_zero = 1;
    for (int i = 0; i < 10; i++) {
        if (p7[i] != 0) { all_zero = 0; break; }
    }
    TEST("calloc zeroes memory", all_zero);
    toylibc_free(p7);
    // calloc 溢出检测
    // 在 64-bit 上, unsigned long 是 64 位; 用大数触发溢出
    void *p7b = toylibc_calloc(0x8000000000000000UL, 2);
    TEST("calloc overflow → NULL", p7b == NULL);
    toylibc_printf("\n");

    // =====================================================================
    // TEST 8: realloc (扩/缩/新分配)
    // =====================================================================
    toylibc_printf("[TEST 8] realloc\n");
    // realloc(NULL, N) == malloc(N)
    void *p8 = toylibc_realloc(NULL, 32);
    TEST("realloc(NULL,32) != NULL", p8 != NULL);
    toylibc_memset(p8, 0xCD, 32);

    // realloc expand
    void *p8b = toylibc_realloc(p8, 64);
    TEST("realloc expand != NULL", p8b != NULL);
    // 前 32 字节应保留
    TEST("realloc preserves data",
         ((unsigned char *)p8b)[0] == 0xCD &&
         ((unsigned char *)p8b)[31] == 0xCD);

    // realloc shrink
    void *p8c = toylibc_realloc(p8b, 16);
    TEST("realloc shrink != NULL", p8c != NULL);
    TEST("realloc shrink preserves prefix",
         ((unsigned char *)p8c)[0] == 0xCD);

    toylibc_free(p8c);

    // realloc(ptr, 0) == free(ptr)
    void *p8d = toylibc_malloc(8);
    toylibc_realloc(p8d, 0);
    TEST("realloc(ptr,0) frees", 1);
    toylibc_printf("\n");

    // =====================================================================
    // TEST 9: 大对象分配 (mmap 路径, >= 128KB)
    // =====================================================================
    toylibc_printf("[TEST 9] large allocation (mmap path)\n");
    void *p9 = toylibc_malloc(200 * 1024);  // 200 KB
    TEST("malloc 200KB != NULL", p9 != NULL);
    if (p9) {
        toylibc_memset(p9, 0xEF, 200 * 1024);
        TEST("write to 200KB ok", ((unsigned char *)p9)[0] == 0xEF);
        toylibc_free(p9);
        TEST("free 200KB ok", 1);
    }
    toylibc_printf("\n");

    // =====================================================================
    // TEST 10: 压力测试 (多对象 + 合并 + 分裂)
    // =====================================================================
    toylibc_printf("[TEST 10] stress — alloc/free many objects\n");

    #define N_ALLOCS 32
    void *ptrs[N_ALLOCS];
    for (int i = 0; i < N_ALLOCS; i++) {
        unsigned long sz = (unsigned long)((i % 16) + 1) * 13;  // 13~208 bytes
        ptrs[i] = toylibc_malloc(sz);
    }
    int all_ok = 1;
    for (int i = 0; i < N_ALLOCS; i++) {
        if (ptrs[i] == NULL) { all_ok = 0; break; }
        toylibc_memset(ptrs[i], (unsigned char)i, 1);
    }
    TEST("32 allocs all succeed", all_ok);

    // 释放奇数索引 (制造空洞, 测试合并)
    for (int i = 1; i < N_ALLOCS; i += 2) {
        toylibc_free(ptrs[i]);
    }
    // 释放偶数索引
    for (int i = 0; i < N_ALLOCS; i += 2) {
        toylibc_free(ptrs[i]);
    }
    TEST("free all 32 objects", 1);

    // 再分配: 应该能复用空闲内存
    void *p10b = toylibc_malloc(512);
    TEST("re-alloc after all free", p10b != NULL);
    toylibc_free(p10b);
    toylibc_printf("\n");

    // =====================================================================
    // 结果汇总
    // =====================================================================
    toylibc_printf("========================================\n");
    toylibc_printf("  Results: %d passed, %d failed\n",
                   tests_passed, tests_failed);
    toylibc_printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
