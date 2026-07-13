#ifndef __ARENAMIRROR_H__
#define __ARENAMIRROR_H__

/* Minimal malloc implementation structures */

#include <stddef.h>   /* for size_t */
#include <stdint.h>   /* for uintptr_t */

/* Basic type definitions */
#ifndef INTERNAL_SIZE_T
#define INTERNAL_SIZE_T size_t
#endif

/* Alignment */
#ifndef MALLOC_ALIGNMENT
#define MALLOC_ALIGNMENT (2 * sizeof(size_t))
#endif
#define MALLOC_ALIGN_MASK (MALLOC_ALIGNMENT - 1)

/* Size and alignment conversions */
#define SIZE_SZ (sizeof(INTERNAL_SIZE_T))
#define aligned_OK(m) (((unsigned long)(m) & MALLOC_ALIGN_MASK) == 0)

/* Chunk representations */
struct malloc_chunk {
    INTERNAL_SIZE_T      mchunk_prev_size;  /* Size of previous chunk (if free).  */
    INTERNAL_SIZE_T      mchunk_size;       /* Size in bytes, including overhead. */
    struct malloc_chunk* fd;         /* double links -- used only if free. */
    struct malloc_chunk* bk;         /* Only used for large blocks: pointer to next larger size.  */
    struct malloc_chunk* fd_nextsize; /* double links -- used only if free. */
    struct malloc_chunk* bk_nextsize;
};

/* Chunk utility macros */
#define chunk2mem(p)   ((void*)((char*)(p) + 2*SIZE_SZ))
#define mem2chunk(mem) ((struct malloc_chunk*)((char*)(mem) - 2*SIZE_SZ))

/* The smallest possible chunk */
#define MIN_CHUNK_SIZE        (offsetof(struct malloc_chunk, fd_nextsize))

/* The smallest size we can malloc is an aligned minimal chunk */
#define MINSIZE  \
  (unsigned long)(((MIN_CHUNK_SIZE+MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK))

/* Size field bit flags */
#define PREV_INUSE 0x1
#define IS_MMAPPED 0x2
#define NON_MAIN_ARENA 0x4
#define SIZE_BITS (PREV_INUSE | IS_MMAPPED | NON_MAIN_ARENA)

/* Get size, ignoring use bits */
#define chunksize(p) ((p)->mchunk_size & ~(SIZE_BITS))

/* Ptr to next physical malloc_chunk. */
#define next_chunk(p) ((struct malloc_chunk*) (((char *) (p)) + chunksize (p)))

/* Bins */
typedef struct malloc_chunk *mbinptr;
typedef struct malloc_chunk *mfastbinptr;

/* Bin counts */
#define NBINS             128
#define NSMALLBINS        64
#define NFASTBINS         10  /* Typical value for 64-bit systems */

/* Bin indexing */
#define bin_index(sz) \
  ((in_smallbin_range(sz)) ? smallbin_index(sz) : largebin_index(sz))

#define in_smallbin_range(sz)  \
  ((unsigned long)(sz) < (unsigned long)MIN_LARGE_SIZE)

#define SMALLBIN_WIDTH    MALLOC_ALIGNMENT
#define SMALLBIN_CORRECTION (MALLOC_ALIGNMENT > 2 * SIZE_SZ)
#define MIN_LARGE_SIZE    ((NSMALLBINS - SMALLBIN_CORRECTION) * SMALLBIN_WIDTH)

#define smallbin_index(sz) \
  ((SMALLBIN_WIDTH == 16 ? (((unsigned)(sz)) >> 4) : (((unsigned)(sz)) >> 3))\
   + SMALLBIN_CORRECTION)

/* Simplified largebin index for 64-bit */
#define largebin_index_64(sz)                                                \
  (((((unsigned long)(sz)) >> 6) <= 48) ?  48 + (((unsigned long)(sz)) >> 6) :\
   ((((unsigned long)(sz)) >> 9) <= 20) ?  91 + (((unsigned long)(sz)) >> 9) :\
   ((((unsigned long)(sz)) >> 12) <= 10) ? 110 + (((unsigned long)(sz)) >> 12) :\
   ((((unsigned long)(sz)) >> 15) <= 4) ? 119 + (((unsigned long)(sz)) >> 15) :\
   ((((unsigned long)(sz)) >> 18) <= 2) ? 124 + (((unsigned long)(sz)) >> 18) :\
   126)

#define largebin_index(sz) largebin_index_64(sz)

/* Bin addressing */
#define bin_at(m, i) \
  (mbinptr)(((char*)&((m)->bins[((i)-1)*2])) - offsetof(struct malloc_chunk, fd))

/* Binmap */
#define BINMAPSHIFT      5
#define BITSPERMAP       (1U << BINMAPSHIFT)
#define BINMAPSIZE       (NBINS / BITSPERMAP)

/* Main malloc state structure */
struct malloc_state {
    int mutex;

    /* Flags */
    int flags;

    int have_fastchunks;

    /* Fastbins */
    mfastbinptr fastbinsY[NFASTBINS];

    /* Base of the topmost chunk */
    struct malloc_chunk* top;

    /* The remainder from the most recent split of a small request */
    struct malloc_chunk* last_remainder;

    /* Normal bins packed as described above */
    struct malloc_chunk* bins[NBINS * 2 - 2];

    /* Bitmap of bins */
    unsigned int binmap[BINMAPSIZE];

    /* Linked list */
    struct malloc_state *next;

    /* Linked list for free arenas */
    struct malloc_state *next_free;

    /* Number of threads attached to this arena */
    INTERNAL_SIZE_T attached_threads;

    /* Memory allocated from the system in this arena */
    INTERNAL_SIZE_T system_mem;
    INTERNAL_SIZE_T max_system_mem;
};

/* Additional utility macros for chunk operations */
#define prev_inuse(p)       ((p)->mchunk_size & PREV_INUSE)
#define chunk_is_mmapped(p) ((p)->mchunk_size & IS_MMAPPED)
#define chunk_main_arena(p) (((p)->mchunk_size & NON_MAIN_ARENA) == 0)

#define set_head(p, s)       ((p)->mchunk_size = (s))
#define set_foot(p, s)       (((struct malloc_chunk*)((char*)(p) + (s)))->mchunk_prev_size = (s))

/* Fastbin convenience macros */
#define fastbin(ar_ptr, idx) ((ar_ptr)->fastbinsY[idx])
#define fastbin_index(sz) \
  ((((unsigned int)(sz)) >> (SIZE_SZ == 8 ? 4 : 3)) - 2)

/* Unsorted chunks */
#define unsorted_chunks(M)          (bin_at(M, 1))


typedef struct malloc_state *mstate;

typedef struct _heap_info
{
  mstate ar_ptr; /* Arena for this heap. */
  struct _heap_info *prev; /* Previous heap. */
  size_t size;   /* Current size in bytes. */
  size_t mprotect_size; /* Size in bytes that has been mprotected
                           PROT_READ|PROT_WRITE.  */
  /* Make sure the following data is properly aligned, particularly
     that sizeof (heap_info) + 2 * SIZE_SZ is a multiple of
     MALLOC_ALIGNMENT. */
  char pad[-6 * SIZE_SZ & MALLOC_ALIGN_MASK];
} heap_info;

#if __WORDSIZE == 32
#define MALLOC_STATE_SIZE 1108
#define HEAP_INFO_SIZE 16
#define ATTACHED_THREAD_SIZE sizeof(int)
#else
#define MALLOC_STATE_SIZE 2192
#define HEAP_INFO_SIZE 32
#define ATTACHED_THREAD_SIZE sizeof(long)
#endif

#ifndef DEFAULT_MMAP_THRESHOLD
#if __WORDSIZE == 32
#define DEFAULT_MMAP_THRESHOLD (512 * 1024)
#else
#define DEFAULT_MMAP_THRESHOLD (4 * 1024 * 1024 * sizeof(long))
#endif
#endif

#ifndef HEAP_MAX_SIZE
#ifdef DEFAULT_MMAP_THRESHOLD
#define HEAP_MAX_SIZE (2 * DEFAULT_MMAP_THRESHOLD)
#else
#define HEAP_MAX_SIZE (1024 * 1024)
#endif
#endif


extern struct malloc_state *my_main_arena;

#endif
