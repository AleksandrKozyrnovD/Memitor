#ifndef __ARENA_MONITOR_H__
#define __ARENA_MONITOR_H__

#include "arenamirror.h"
#include "memory_read.h"

// === Global storage ===
#define MAX_ARENAS 64
#define MAX_HEAPINFOS 256

extern struct malloc_state arenas[MAX_ARENAS];
extern void *arena_ptrs[MAX_ARENAS];   // addresses of arenas in target memory
extern size_t arena_count;

extern heap_info heapinfos[MAX_HEAPINFOS];
extern void *heap_ptrs[MAX_HEAPINFOS];
extern size_t heapinfo_count;


enum chunk_flags
{
    prev_inuse = 0x1,
    is_mmaped = 0x2,
    non_main_arena = 0x4
};


uint8_t get_chunk_flags(struct malloc_chunk *chunk);
int read_chunk(void *addr, struct malloc_chunk *out);

// updates arenas and heapinfos arrays.
void update_arena(struct malloc_state *main_arena_addr);

#define CHUNK_FROM_PTR(ptr, chunk) (read_chunk((void *)((uintptr_t)ptr-2*sizeof(size_t)), chunk))

int init_module(void);


#endif
