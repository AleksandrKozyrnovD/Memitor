#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "arenamirror.h"

int main()
{

    for (size_t size = 1; size <= 4096; size+=1)
    {
        void *malloc1 = malloc(size);
        void *malloc2 = malloc(size);
        memset(malloc1, 'F', size);
        memset(malloc2, 'F', size);
        printf("%p - %p = %zu (%zu) (%zu %zu)\n",
            mem2chunk(malloc1), mem2chunk(malloc2), (size_t)(mem2chunk(malloc2)) - (size_t)(mem2chunk(malloc1)), size,
            chunksize(mem2chunk(malloc1)), chunksize(mem2chunk(malloc2)));
    }

    return 0;
}
