#include <pmm.h>
#include <list.h>
#include <string.h>
#include <buddy_pmm.h>

// 在 base..base+n 中 carve 出元数据页

static struct Page *buddy_base_page = NULL;   // 可分配区域基址（排除元数据）
static unsigned *buddy_longest = NULL;        // 伙伴树数组（位于 carved 元数据页）
static unsigned buddy_size = 0;               // 管理的总页数（2 的幂）
static size_t nr_free = 0;                    // 当前空闲页数

// 工具  宏/函数
#define IS_POWER_OF_2(x) (!((x) & ((x) - 1)))//2的幂次方二进制表示只有1位
static unsigned roundup_pow2(unsigned x) {
    //特殊情况
    if (x <= 1) return 1;
    x--;
    //通过位运算将最高位的1向右填充所有低位然后加1
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}
static unsigned floor_pow2(unsigned x) {
    if (x == 0) return 0;
    unsigned p = roundup_pow2(x);
    return (p == x) ? x : (p >> 1);
}

static inline void *page_kva(struct Page *pg) {
    return (void *)(page2pa(pg) + va_pa_offset);
}

static void
buddy_init(void) {
    buddy_base_page = NULL;
    buddy_longest = NULL;
    buddy_size = 0;
    nr_free = 0;
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);

    // 先清理传入页的状态
    struct Page *p = base;
    for (; p != base + n; p++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }

    // 预估需要的元数据空间（unsigned[2*size-1]），再据此确定可管理的 2^k
    // 先假设全部可用，迭代出满足 bytes(meta) 的最大 2^k
    unsigned avail = (unsigned)n;
    unsigned best_size = 0;
    unsigned meta_pages_need = 0;

    //循环寻找最佳大小页的内存
    for (;;) {
        unsigned candid = floor_pow2(avail);
        if (candid < 1) break;

        //计算元数据的需求
        unsigned node_cnt = 2 * candid - 1;
        size_t meta_bytes = sizeof(unsigned) * node_cnt;
        size_t meta_pages = (meta_bytes + PGSIZE - 1) / PGSIZE; //向上取整的公式
        if (candid <= avail - meta_pages) {
            best_size = candid;
            meta_pages_need = meta_pages;
            break;
        }
        avail = candid - 1;
    }
    assert(best_size > 0);

    // carve 元数据页：使用 base..base+meta_pages_need 作为 longest 存储
    struct Page *meta_base = base;
    struct Page *data_base = base + meta_pages_need;

    // 标记元数据页为保留，避免被分配
    for (p = meta_base; p != data_base; p++) {
        p->flags = 0;
        SetPageReserved(p);
        set_page_ref(p, 0);
        p->property = 0;
    }

    // 伙伴数组起始虚拟地址
    buddy_longest = (unsigned *)page_kva(meta_base);
    // buddy_longest 需要 node_cnt 个 unsigned 空间，跨越多个页

    buddy_base_page = data_base;
    buddy_size = best_size;
    nr_free = buddy_size;

    // 初始化 longest 树
    unsigned node_size = buddy_size * 2;
    for (unsigned i = 0; i < 2 * buddy_size - 1; i++) {
        if (IS_POWER_OF_2(i + 1)) {
            node_size >>= 1;
        }
        buddy_longest[i] = node_size;
    }
}


static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (buddy_size == 0 || buddy_longest == NULL) return NULL;
    if (n > nr_free) return NULL;

    // 按 2 的幂分配
    unsigned need = (unsigned)n;
    unsigned alloc_sz = roundup_pow2(need);
    if (alloc_sz > buddy_size) return NULL;
    if (alloc_sz > nr_free) return NULL;

    // 分配：需要在分配过程中得到 index 与 node_size 以计算偏移
    if (buddy_longest[0] < alloc_sz) return NULL;

    //树的遍历  从根节点开始
    unsigned index = 0;
    unsigned node_size = buddy_size;
    while (node_size != alloc_sz) {
        unsigned left = 2 * index + 1;
        unsigned right = left + 1;
        if (buddy_longest[left] >= alloc_sz) {
            index = left;
        } else {
            index = right;
        }
        node_size >>= 1;
    }
    buddy_longest[index] = 0; // 标记节点为已占用

    // 更新祖先
    unsigned idx_up = index;
    while (idx_up) {
        idx_up = (idx_up - 1) >> 1; //父节点
        unsigned left = 2 * idx_up + 1, right = left + 1;
        unsigned l = buddy_longest[left];
        unsigned r = buddy_longest[right];

        buddy_longest[idx_up] = (l > r) ? l : r;
    }
    // 计算偏移
    unsigned offset = (index + 1) * node_size - buddy_size;

     struct Page *page = buddy_base_page + offset;
    // 记录已分配块大小在 property 字段，便于 free 时识别
    for (unsigned i = 0; i < alloc_sz; i++) {
        struct Page *pp = page + i;
        pp->flags = 0;
        set_page_ref(pp, 0);
        if (i == 0) {
            pp->property = alloc_sz;    //第一页记录块的大小
        } else {
            pp->property = 0;
        }
    }
    nr_free -= alloc_sz;
    ClearPageProperty(page);
    return page;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    assert(buddy_size > 0 && buddy_longest != NULL);
    assert(base >= buddy_base_page);
    
    // 获取实际分配的大小
    unsigned alloc_sz = (base->property != 0) ? base->property : (unsigned)n;
    assert(IS_POWER_OF_2(alloc_sz) && alloc_sz <= buddy_size);

    // 计算偏移
    unsigned offset = (unsigned)(base - buddy_base_page);
    assert(offset + alloc_sz <= buddy_size);

    // 清理页状态
    struct Page *p = base;
    for (unsigned i = 0; i < alloc_sz; i++, p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
        p->property = 0;
    }

    // 将块释放回 buddy 树： 找到该叶对应的 index。自顶向下
    unsigned index = 0;
    unsigned node_size = buddy_size;
    unsigned node_offset = 0;
    while (node_size != alloc_sz) {
        unsigned left = 2 * index + 1;
        unsigned right = left + 1;
        unsigned half = node_size >> 1;

        //左子树管理[node_offset, node_offset + half) 范围，右子树管理 [node_offset + half, node_offset + node_size)
        if (offset < node_offset + half) {
            index = left;
        } else {
            node_offset += half;
            index = right;
        }
        node_size = half;
    }

    // 设置该节点为空闲
    buddy_longest[index] = node_size;

    // 向上合并
    while (index) {
        unsigned parent = (index - 1) >> 1;
        unsigned left = 2 * parent + 1;
        unsigned right = left + 1;
        unsigned l = buddy_longest[left];
        unsigned r = buddy_longest[right];
        if (l == r) {
            buddy_longest[parent] = l << 1;
        } else {
            buddy_longest[parent] = (l > r) ? l : r;
        }
        index = parent;
    }

    nr_free += alloc_sz;
}

static size_t
buddy_nr_free_pages(void) {
    return nr_free;
}

static void
basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);
    
    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);
    
    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);
    
    free_page(p0);
    free_page(p1);
    free_page(p2);
}

static void
buddy_check(void) {
    basic_check();
    
    struct Page *a = alloc_pages(1);
    assert(a != NULL);
    struct Page *b = alloc_pages(2);
    assert(b != NULL);
    struct Page *c = alloc_pages(3); // 会按 4 分配
    assert(c != NULL);

    free_pages(b, 2);
    free_pages(a, 1);
    free_pages(c, 3);

    assert(nr_free_pages() == nr_free);
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};