#ifndef __MEMORY_READ_H__
#define __MEMORY_READ_H__

#include <stddef.h>

/*
    Read nbytes from memory at offset_ptr into buf.
    Memory is located inside own address space.
    Return the number of bytes read. 0 if failed.
*/
size_t read_memory(void *offset_ptr, void *buf, size_t nbytes);

#endif
