#ifndef USER_MALLOC_H
#define USER_MALLOC_H

#include "sys.h"

#define MALLOC_ALIGNMENT 16
#define MALLOC_CHUNK_SIZE (64 * 1024)

typedef struct malloc_block {
    usize size;
    int free;
    struct malloc_block *next;
} malloc_block_t;

static malloc_block_t *malloc_head = 0;

static inline usize malloc_align_up(usize x) {
    return (x + MALLOC_ALIGNMENT - 1) & ~(MALLOC_ALIGNMENT - 1);
}

static inline malloc_block_t *find_free_block(usize size) {
    malloc_block_t *cur = malloc_head;

    while (cur) {
        if (cur->free && cur->size >= size) {
            return cur;
        }

        cur = cur->next;
    }

    return 0;
}

static inline void split_block(malloc_block_t *block, usize size) {
    usize header_size = sizeof(malloc_block_t);

    if (block->size < size + header_size + MALLOC_ALIGNMENT) {
        return;
    }

    malloc_block_t *new_block =
        (malloc_block_t *)((char *)(block + 1) + size);

    new_block->size = block->size - size - header_size;
    new_block->free = 1;
    new_block->next = block->next;

    block->size = size;
    block->next = new_block;
}

static inline malloc_block_t *request_chunk(usize size) {
    usize total = size + sizeof(malloc_block_t);

    if (total < MALLOC_CHUNK_SIZE) {
        total = MALLOC_CHUNK_SIZE;
    }

    total = malloc_align_up(total);

    void *mem = mmap(total, PROT_READ | PROT_WRITE);

    if ((isize)mem < 0) {
        return 0;
    }

    malloc_block_t *block = (malloc_block_t *)mem;

    block->size = total - sizeof(malloc_block_t);
    block->free = 1;
    block->next = 0;

    if (!malloc_head) {
        malloc_head = block;
    } else {
        malloc_block_t *cur = malloc_head;

        while (cur->next) {
            cur = cur->next;
        }

        cur->next = block;
    }

    return block;
}

static inline void *malloc(usize size) {
    if (size == 0) {
        return 0;
    }

    size = malloc_align_up(size);

    malloc_block_t *block = find_free_block(size);

    if (!block) {
        block = request_chunk(size);

        if (!block) {
            return 0;
        }
    }

    split_block(block, size);

    block->free = 0;

    return (void *)(block + 1);
}

static inline void free(void *ptr) {
    if (!ptr) {
        return;
    }

    malloc_block_t *block = ((malloc_block_t *)ptr) - 1;
    block->free = 1;
}

static inline void *calloc(usize n, usize size) {
    usize total = n * size;

    if (n != 0 && total / n != size) {
        return 0;
    }

    char *p = (char *)malloc(total);

    if (!p) {
        return 0;
    }

    for (usize i = 0; i < total; i++) {
        p[i] = 0;
    }

    return p;
}

#endif