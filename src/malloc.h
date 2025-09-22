#ifndef MALLOC_H
#define MALLOC_H

#include <stddef.h>

typedef struct block_header {
    size_t size;
    int is_free;
    struct block_header* next;
} block_header_t;

void* my_malloc(size_t size);
void my_free(void* ptr);
void print_heap_stats(void);
void reset_allocator(void);
void print_free_list(void);

size_t get_total_allocated(void);
size_t get_free_space(void);

#endif