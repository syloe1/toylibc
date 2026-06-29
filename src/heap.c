/**
 * heap.c — 堆内存分配器 (malloc / free / calloc / realloc)
 *
 * =========================================================================
 * 架构概述
 * =========================================================================
 *
 * 双层分配策略 (与 glibc malloc 一致):
 *   小对象 (< 128 KB) →  brk(2) 堆, 隐式空闲链表 + 边界标签
 *   大对象 (≥ 128 KB) →  mmap(2) / munmap(2) 匿名映射
 *
 * =========================================================================
 * brk 堆: 隐式空闲链表 + 边界标签
 * =========================================================================
 *
 * 块布局 (示意图):
 *
 *   ┌──────────────────┬──────────────────────┬──────────────────┐
 *   │  header (8B)     │  user data (N bytes) │  footer (8B)     │
 *   │  total_size|flag │  对齐到 16B           │  total_size|flag │
 *   └──────────────────┴──────────────────────┴──────────────────┘
 *   ↑                                       ↑
 *   block 起始                              返回给用户的指针
 *   (header 地址)                           = block + 8
 *
 *   用户指针 → header:  ((unsigned long*)ptr) - 1
 *   用户指针 → footer:  (unsigned long*)((char*)ptr + usable_size)
 *
 * Header/Footer 格式 (8 字节 unsigned long):
 *   bit 0  — ALLOCATED (1=已分配, 0=空闲)
 *   bit 1  — PREV_FREE (1=前一块空闲)
 *   bit 2+ — 块总大小 (header + payload + footer, 16B 对齐的)
 *
 * total_blk = OVERHEAD + align_up(user_size, 16)
 *   OVERHEAD = 8 (header) + 8 (footer) = 16
 *
 * 合并:
 *   free 时检查前后邻块, 空闲则合并→更新边界标签→更新邻块 prev_free 标志。
 *
 * 分裂:
 *   malloc 找到的空闲块若 ≥ 请求大小 + OVERHEAD + 16, 切分为两块。
 *
 * 搜索: first-fit (从头遍历隐式空闲链表)
 *
 * =========================================================================
 * mmap 大对象
 * =========================================================================
 *
 * ≥ 128 KB: mmap(MAP_PRIVATE | MAP_ANONYMOUS) 创建独立映射。
 * 映射起始 8 字节存 (total_size | FLAG_ALLOCATED), 用户指针=映射+8。
 * free 时识别 mmap 块 → munmap 整个映射。
 */

#include "toylibc.h"

// ---- 常量 ----
#define ALIGN          16       // 对齐粒度
#define HDR_SZ         8        // sizeof(unsigned long): header 大小
#define OVERHEAD       16       // header + footer = 16B
#define MMAP_LIMIT     131072   // 128KB: 大对象阈值

#define FLAG_ALLOC     0x1UL    // bit0: 已分配
#define FLAG_PREV_FREE 0x2UL    // bit1: 前一块空闲

#define GET_SZ(s)      ((s) & ~0x7UL)
#define IS_ALLOC(s)    ((s) & FLAG_ALLOC)
#define IS_FREE(s)     (!IS_ALLOC(s))

// 向上对齐
#define ALIGN_UP(n, a) (((n) + (a) - 1) & ~((unsigned long)(a) - 1))

// ---- 全局堆状态 ----
static void *heap_lo = NULL;   // 堆起始
static void *heap_hi = NULL;   // 堆结束 (= program break)
static int   heap_ok = 0;      // 是否已初始化

// ---- 前向声明 ----
static void  heap_init(void);
static void *brk_grow(unsigned long min_sz);
static void *brk_alloc(unsigned long need);
static void  brk_release(void *ptr);
static void  merge(void *blk);

// =========================================================================
// 公共 API
// =========================================================================

void *toylibc_malloc(unsigned long sz)
{
    if (sz == 0) return NULL;

    // ---- 大对象 → mmap ----
    if (sz >= MMAP_LIMIT) {
        unsigned long total = sz + HDR_SZ;            // header only (no footer needed)
        unsigned long pg = 4096;
        total = ALIGN_UP(total, pg);

        void *map = toylibc_mmap(NULL, total,
                                 TOY_PROT_READ | TOY_PROT_WRITE,
                                 TOY_MAP_PRIVATE | TOY_MAP_ANONYMOUS,
                                 -1, 0);
        if (map == (void *)-1) return NULL;

        unsigned long *hdr = (unsigned long *)map;
        *hdr = total | FLAG_ALLOC;
        return (void *)(hdr + 1);   // user ptr = map + 8
    }

    // ---- 小对象 → brk 堆 ----
    if (!heap_ok) heap_init();

    // 请求的块总大小 = overhead + 对齐后的 payload
    unsigned long need = OVERHEAD + ALIGN_UP(sz, ALIGN);
    return brk_alloc(need);
}

void toylibc_free(void *ptr)
{
    if (ptr == NULL) return;

    unsigned long *hdr = ((unsigned long *)ptr) - 1;
    unsigned long hdrv  = *hdr;
    unsigned long blksz = GET_SZ(hdrv);

    // ---- mmap 块: 检查是否在 brk 堆外 ----
    if (blksz >= MMAP_LIMIT && (ptr < heap_lo || ptr >= heap_hi)) {
        toylibc_munmap(hdr, blksz);   // hdr = 映射起始地址
        return;
    }

    // ---- brk 块 ----
    brk_release(ptr);
}

void *toylibc_calloc(unsigned long n, unsigned long sz)
{
    unsigned long total = n * sz;
    if (n != 0 && total / n != sz) return NULL;  // overflow

    void *p = toylibc_malloc(total);
    if (p == NULL) return NULL;
    toylibc_memset(p, 0, total);
    return p;
}

void *toylibc_realloc(void *ptr, unsigned long newsz)
{
    if (ptr == NULL) return toylibc_malloc(newsz);
    if (newsz == 0)  { toylibc_free(ptr); return NULL; }

    unsigned long *hdr   = ((unsigned long *)ptr) - 1;
    unsigned long old_sz = GET_SZ(*hdr) - OVERHEAD;  // usable payload

    // mmap 大块: 新分配+拷贝+释放
    if (old_sz >= MMAP_LIMIT) {
        void *np = toylibc_malloc(newsz);
        if (!np) return NULL;
        unsigned long cp = old_sz < newsz ? old_sz : newsz;
        toylibc_memcpy(np, ptr, cp);
        toylibc_free(ptr);
        return np;
    }

    // brk 块: 若新大小 ≤ 旧可用空间, 原地复用
    if (newsz <= old_sz) return ptr;

    // 否则新分配+拷贝+释放
    void *np = toylibc_malloc(newsz);
    if (!np) return NULL;
    toylibc_memcpy(np, ptr, old_sz);
    toylibc_free(ptr);
    return np;
}

// =========================================================================
// 堆内部实现
// =========================================================================

static void heap_init(void)
{
    void *cur = toylibc_brk(NULL);
    unsigned long al = ALIGN_UP((unsigned long)cur, ALIGN);
    if (al != (unsigned long)cur) toylibc_brk((void *)al);
    heap_lo = (void *)al;
    heap_hi = heap_lo;
    heap_ok = 1;
}

/**
 * brk_grow — 扩展堆, 返回新空闲块的 header 地址.
 * min_sz 是需要的字节数, 实际至少扩展一页.
 */
static void *brk_grow(unsigned long min_sz)
{
    unsigned long pg = 4096;
    unsigned long sz = ALIGN_UP(min_sz, pg);

    void *new_brk = (char *)heap_hi + sz;
    if (toylibc_brk(new_brk) != new_brk) return NULL;  // 内核返回的断点 ≠ 请求值 → 失败

    // 确定前一块是否空闲: 读旧 heap_hi 之前最后 8 字节
    unsigned long prev_flag = 0;
    if (heap_hi > heap_lo) {
        unsigned long *last_ftr =
            (unsigned long *)((char *)heap_hi - HDR_SZ);
        if (IS_FREE(*last_ftr))
            prev_flag = FLAG_PREV_FREE;
    }

    // 在旧 heap_hi 处创建新空闲块
    unsigned long *hdr = (unsigned long *)heap_hi;
    *hdr = sz | prev_flag;           // ← 修复: 根据实际情况设置 PREV_FREE

    // footer 在块末尾
    unsigned long *ftr = (unsigned long *)((char *)heap_hi + sz - HDR_SZ);
    *ftr = sz;

    void *block = heap_hi;
    heap_hi = new_brk;

    // 如果前一块空闲, 合并
    merge(block);

    return block;
}

/**
 * brk_alloc — first-fit 从隐式空闲链表中分配 need 字节大小的块
 */
static void *brk_alloc(unsigned long need)
{
    void *cur = heap_lo;

    while (cur < heap_hi) {
        unsigned long *hdr = (unsigned long *)cur;
        unsigned long hdrv  = *hdr;
        unsigned long blksz = GET_SZ(hdrv);

        if (blksz == 0) break;  // 防御: 损坏堆

        if (IS_FREE(hdrv) && blksz >= need) {
            unsigned long rem = blksz - need;

            if (rem >= OVERHEAD + ALIGN) {
                // ---- 分裂: [ALLOC(need)][FREE(rem)] ----
                *hdr = need | FLAG_ALLOC | (hdrv & FLAG_PREV_FREE);
                // footer: 分配块的末尾
                unsigned long *ftr_alloc =
                    (unsigned long *)((char *)cur + need - HDR_SZ);
                *ftr_alloc = need | FLAG_ALLOC;

                // 剩余空闲块
                void *next = (char *)cur + need;
                unsigned long *next_hdr = (unsigned long *)next;
                *next_hdr = rem | FLAG_PREV_FREE;  // prev 是分配块→prev_free=0
                unsigned long *next_ftr =
                    (unsigned long *)((char *)next + rem - HDR_SZ);
                *next_ftr = rem;

                // 更新 next+rem 处的块的 PREV_FREE (next 是空闲的)
                void *after = (char *)next + rem;
                if (after < heap_hi) {
                    unsigned long *ah = (unsigned long *)after;
                    *ah = (*ah & ~0x7UL) | FLAG_PREV_FREE;
                }
            } else {
                // ---- 不分列: 整块使用 ----
                *hdr = blksz | FLAG_ALLOC | (hdrv & FLAG_PREV_FREE);
                unsigned long *ftr =
                    (unsigned long *)((char *)cur + blksz - HDR_SZ);
                *ftr = blksz | FLAG_ALLOC;

                // 更新下一个块的 PREV_FREE=0
                void *next = (char *)cur + blksz;
                if (next < heap_hi) {
                    unsigned long *nh = (unsigned long *)next;
                    *nh = (*nh & ~FLAG_PREV_FREE);
                }
            }

            // 用户指针 = header + 8
            return (void *)(hdr + 1);
        }

        cur = (char *)cur + blksz;
    }

    // ---- 未找到: 扩展 ----
    void *block = brk_grow(need);
    if (block == NULL) return NULL;

    // 从新扩展块分配
    unsigned long *hdr   = (unsigned long *)block;
    unsigned long blksz  = GET_SZ(*hdr);
    unsigned long pfree  = *hdr & FLAG_PREV_FREE;
    *hdr = blksz | FLAG_ALLOC | pfree;

    unsigned long *ftr = (unsigned long *)((char *)block + blksz - HDR_SZ);
    *ftr = blksz | FLAG_ALLOC;

    // 下面这行在 brk_grow 后几乎不可能 > heap_hi, 但保持完整性
    void *next = (char *)block + blksz;
    if (next < heap_hi) {
        unsigned long *nh = (unsigned long *)next;
        *nh = (*nh & ~FLAG_PREV_FREE);
    }

    return (void *)(hdr + 1);
}

/**
 * brk_release — 释放 brk 堆中的块 + 合并相邻空闲块
 */
static void brk_release(void *ptr)
{
    unsigned long *hdr  = ((unsigned long *)ptr) - 1;
    unsigned long hdrv  = *hdr;
    unsigned long blksz = GET_SZ(hdrv);
    unsigned long pfree = hdrv & FLAG_PREV_FREE;

    // 标记为空闲
    *hdr = blksz | pfree;  // bit0 = 0

    // footer
    unsigned long *ftr = (unsigned long *)((char *)hdr + blksz - HDR_SZ);
    *ftr = blksz;  // bit0 = 0

    // 更新下一个块的 PREV_FREE = 1
    void *next = (char *)hdr + blksz;
    if (next < heap_hi) {
        unsigned long *nh = (unsigned long *)next;
        *nh = (*nh & ~0x7UL) | FLAG_PREV_FREE;
    }

    // 合并
    merge(hdr);
}

/**
 * merge — 与相邻空闲块合并
 */
static void merge(void *blk)
{
    unsigned long *hdr  = (unsigned long *)blk;
    unsigned long hdrv  = *hdr;
    unsigned long blksz = GET_SZ(hdrv);
    unsigned long pfree = hdrv & FLAG_PREV_FREE;

    void  *start = blk;
    unsigned long total = blksz;

    // ---- 向前合并 ----
    if (pfree) {
        // 前一块的 footer 紧邻当前 header 之前 8 字节
        unsigned long *prev_ftr = hdr - 1;
        unsigned long  prev_sz  = GET_SZ(*prev_ftr);
        start = (char *)blk - prev_sz;
        total += prev_sz;
    }

    // ---- 向后合并 ----
    void *next = (char *)blk + blksz;
    if (next < heap_hi) {
        unsigned long *nh = (unsigned long *)next;
        if (IS_FREE(*nh)) {
            total += GET_SZ(*nh);
        }
    }

    if (total != blksz) {
        unsigned long orig_pfree = (start != blk) ?
            (*(unsigned long *)start & FLAG_PREV_FREE) : pfree;

        unsigned long *new_hdr = (unsigned long *)start;
        *new_hdr = total | orig_pfree;

        unsigned long *new_ftr = (unsigned long *)((char *)start + total
                                                   - HDR_SZ);
        *new_ftr = total;

        // 更新下一个块的 PREV_FREE
        void *after = (char *)start + total;
        if (after < heap_hi) {
            unsigned long *ah = (unsigned long *)after;
            *ah = (*ah & ~0x7UL) | FLAG_PREV_FREE;
        }
    }
}
