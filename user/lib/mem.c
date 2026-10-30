/* mem.c —— 用户态堆的唯一定义点。*/
#include "../include/mem.h"

malloc_block_t *malloc_head = 0;
mutex_t malloc_lock = MUTEX_INIT;
void *slab_freelist[SLAB_COUNT] = {0};
