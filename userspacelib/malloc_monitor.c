#include "malloc_monitor.h"
#include <stdio.h>
#include <unistd.h>

struct malloc_state arenas[MAX_ARENAS];
void *arena_ptrs[MAX_ARENAS];
size_t arena_count = 0;
struct malloc_state *my_main_arena = &arenas[0];

heap_info heapinfos[MAX_HEAPINFOS];
void *heap_ptrs[MAX_HEAPINFOS];
size_t heapinfo_count = 0;


// === Helper ===
int read_arena(void *addr, struct malloc_state *out)
{
    return read_memory(addr, out, sizeof(struct malloc_state)) == sizeof(struct malloc_state);
}

int read_heapinfo(void *addr, heap_info *out)
{
    return read_memory(addr, out, sizeof(heap_info)) == sizeof(heap_info);
}

int read_chunk(void *addr, struct malloc_chunk *out)
{
    return read_memory(addr, out, sizeof(struct malloc_chunk)) == sizeof(struct malloc_chunk);
}

uint8_t get_flags(struct malloc_chunk *chunk)
{
    return (uint8_t)(chunk->mchunk_size & 0x7);
}

// === Arena/heapinfo discovery ===
void update_arena(struct malloc_state *start)
{
    // clear lists
    arena_count = 0;
    heapinfo_count = 0;

    // follow circular linked list of arenas
    struct malloc_state *cur = start;
    do
    {
        if (arena_count >= MAX_ARENAS) break;

        struct malloc_state st;
        if (!read_arena(cur, &st)) break;

        arena_ptrs[arena_count] = cur;
        arenas[arena_count] = st;
        arena_count++;

        // follow all heap_info linked to this arena
        heap_info hi;
        void *heap_ptr = NULL;

        // each arena has a "top chunk" pointer, use it to locate heap_info
        // assume heap_info lies at the beginning of region containing top
        // start at top->prev heap_info chain
        // (simplified, real implementation needs walking back through heap_info.prev)
        heap_ptr = (void *)((uintptr_t)st.top & ~(0xFFF)); // page-align guess
        while (cur != start && heap_ptr && heapinfo_count < MAX_HEAPINFOS)
        {
            if (!read_heapinfo(heap_ptr, &hi)) break;
            heap_ptrs[heapinfo_count] = heap_ptr;
            heapinfos[heapinfo_count] = hi;
            heapinfo_count++;

            if (hi.prev == NULL) break;
            heap_ptr = hi.prev;
        }
        cur = st.next;

    } while (cur && cur != start);
}


int init_module(void)
{
    FILE *file = fopen("/proc/memitor", "w");
    int ret = fprintf(file, "%d", getpid());
    if (ret < 0)
        return ret;
    fclose(file);

    file = fopen("/proc/memshot", "w");
    ret = fprintf(file, "%d", getpid());
    fclose(file);
    return ret;
}