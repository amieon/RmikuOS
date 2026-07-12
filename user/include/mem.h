#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "syscall.h"
#include "lock.h"

/* ---- 内存保护标志(唯一定义点) ---- */
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

/* ---- mmap / munmap ---- */
static inline void *mmap(usize len, usize prot) {
    isize ret = syscall3(SYS_MMAP, len, prot, 0);
    if (ret < 0) return (void *)-1;
    return (void *)ret;
}

static inline int munmap(void *addr, usize len) {
    return syscall3(SYS_MUNMAP, (usize)addr, len, 0);
}


void *memset(void *s, int c, unsigned long n);
void *memcpy(void *dst, const void *src, unsigned long n);



/* ---- 用户态堆分配器 ---- */
#define MALLOC_ALIGNMENT 16
#define MALLOC_PAGE_SIZE 4096
#define MALLOC_CHUNK_SIZE (64 * 1024)

typedef struct malloc_block {
    usize size;
    usize free;
    struct malloc_block *next;
} malloc_block_t;

static malloc_block_t *malloc_head = 0;
static mutex_t malloc_lock = MUTEX_INIT;

static inline usize malloc_align_up(usize x, usize align) {
    return (x + align - 1) & ~(align - 1);
}

static inline usize malloc_header_size(void) {
    return malloc_align_up(sizeof(malloc_block_t), MALLOC_ALIGNMENT);
}

static inline void *malloc_payload(malloc_block_t *block) {
    return (void *)((char *)block + malloc_header_size());
}

static inline malloc_block_t *malloc_block_from_payload(void *ptr) {
    return (malloc_block_t *)((char *)ptr - malloc_header_size());
}

/* ========== SLAB ALLOCATOR (小对象快速路径, O(1)) ========== */
#define SLAB_COUNT 12
#define SLAB_MAGIC (1ULL << 63)

static const usize slab_sizes[SLAB_COUNT] = {
    16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024
};

static void *slab_freelist[SLAB_COUNT] = {0};

static inline int size_to_sc(usize size) {
    for (int i = 0; i < SLAB_COUNT; i++) {
        if (size <= slab_sizes[i]) return i;
    }
    return -1;
}

static inline void slab_refill(int sc) {
    usize obj_size = slab_sizes[sc];
    usize header = malloc_header_size();
    usize unit = header + obj_size;
    usize chunk_size = MALLOC_CHUNK_SIZE;
    if (chunk_size < unit * 8) chunk_size = unit * 8;
    chunk_size = malloc_align_up(chunk_size, MALLOC_PAGE_SIZE);

    void *chunk = mmap(chunk_size, PROT_READ | PROT_WRITE);
    if ((isize)chunk < 0) return;

    char *p = (char *)chunk;
    usize count = chunk_size / unit;
    for (usize i = 0; i < count; i++) {
        void *payload = p + header;
        *(void **)payload = slab_freelist[sc];
        slab_freelist[sc] = payload;
        p += unit;
    }
}

static inline void *slab_alloc(int sc) {
    if (!slab_freelist[sc]) {
        slab_refill(sc);
        if (!slab_freelist[sc]) return 0;
    }
    void *p = slab_freelist[sc];
    slab_freelist[sc] = *(void **)p;

    usize header = malloc_header_size();
    malloc_block_t *b = (malloc_block_t *)((char *)p - header);
    b->size = SLAB_MAGIC | (usize)sc;
    b->free = 0;
    return p;
}

static inline void slab_free(void *ptr, int sc) {
    *(void **)ptr = slab_freelist[sc];
    slab_freelist[sc] = ptr;
}

static inline int is_slab(malloc_block_t *b) {
    return (b->size & SLAB_MAGIC) != 0;
}

static inline int slab_sc(malloc_block_t *b) {
    return (int)(b->size & ~SLAB_MAGIC);
}

/* ========== 首次适应分配器 (大对象, 保持原逻辑) ========== */
static inline malloc_block_t *malloc_find_free(usize size) {
    malloc_block_t *cur = malloc_head;
    while (cur) {
        if (cur->free && cur->size >= size) return cur;
        cur = cur->next;
    }
    return 0;
}

static inline void malloc_split_block(malloc_block_t *block, usize size) {
    usize header = malloc_header_size();
    if (block->size < size + header + MALLOC_ALIGNMENT) return;
    malloc_block_t *new_block = (malloc_block_t *)((char *)malloc_payload(block) + size);
    new_block->size = block->size - size - header;
    new_block->free = 1;
    new_block->next = block->next;
    block->size = size;
    block->next = new_block;
}

static inline malloc_block_t *malloc_request_chunk(usize size) {
    usize header = malloc_header_size();
    usize total = size + header;
    if (total < MALLOC_CHUNK_SIZE) total = MALLOC_CHUNK_SIZE;
    total = malloc_align_up(total, MALLOC_PAGE_SIZE);
    void *mem = mmap(total, PROT_READ | PROT_WRITE);
    if ((isize)mem < 0) return 0;
    malloc_block_t *block = (malloc_block_t *)mem;
    block->size = total - header;
    block->free = 1;
    block->next = 0;
    if (!malloc_head) {
        malloc_head = block;
    } else {
        malloc_block_t *cur = malloc_head;
        while (cur->next) cur = cur->next;
        cur->next = block;
    }
    return block;
}

static inline void malloc_coalesce(void) {
    malloc_block_t *cur = malloc_head;
    usize header = malloc_header_size();
    while (cur && cur->next) {
        char *cur_end = (char *)malloc_payload(cur) + cur->size;
        if (cur->free && cur->next->free && cur_end == (char *)cur->next) {
            cur->size += header + cur->next->size;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

static inline void *__malloc_unlocked(usize size) {
    if (size == 0) return 0;
    size = malloc_align_up(size, MALLOC_ALIGNMENT);
    
    /* 小对象走 slab, O(1) */
    int sc = size_to_sc(size);
    if (sc >= 0) return slab_alloc(sc);
    
    /* 大对象走首次适应 */
    malloc_block_t *block = malloc_find_free(size);
    if (!block) {
        block = malloc_request_chunk(size);
        if (!block) return 0;
    }
    malloc_split_block(block, size);
    block->free = 0;
    return malloc_payload(block);
}

static inline void *malloc(usize size) {
    mutex_lock(&malloc_lock);
    void *p = __malloc_unlocked(size);
    mutex_unlock(&malloc_lock);
    return p;
}

static inline void __free_unlocked(void *ptr) {
    if (!ptr) return;
    malloc_block_t *block = malloc_block_from_payload(ptr);
    if (is_slab(block)) {
        slab_free(ptr, slab_sc(block));
        return;
    }
    block->free = 1;
    malloc_coalesce();
}

static inline void free(void *ptr) {
    mutex_lock(&malloc_lock);
    __free_unlocked(ptr);
    mutex_unlock(&malloc_lock);
}

static inline void *calloc(usize n, usize size) {
    usize total = n * size;
    if (n != 0 && total / n != size) return 0;
    char *p = (char *)malloc(total);
    if (!p) return 0;
    for (usize i = 0; i < total; i++) p[i] = 0;
    return p;
}

#ifdef __cplusplus
}
#endif