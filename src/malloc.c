#include "malloc.h"
#include <stdio.h>

#define ALIGNMENT 8 // for performance

// bump allocator
static char heap[1024 * 1024]; // 1MB heap
static char* current = heap;
static size_t total_allocated = 0;

static block_header_t* free_list = NULL;

static size_t align_size(size_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

static void* header_to_user(block_header_t* header) {
    return (char*)header + sizeof(block_header_t);
}

static block_header_t* user_to_header(void* ptr) {
    return (block_header_t*)((char*)ptr - sizeof(block_header_t));
}

void* my_malloc(size_t size) {
    if (size == 0) return NULL;

    size = align_size(size);

    size_t total_size = sizeof(block_header_t) + size;

    // check for space
    if (current + total_size > heap + sizeof(heap)) {
        printf("Out of memory!\n");
        return NULL;
    }

    block_header_t* header = (block_header_t*)current;
    header->size = size;
    header->is_free = 0;
    header->next = NULL;

    current += total_size;
    total_allocated += total_size;

    void* user_ptr = header_to_user(header);
    printf("Allocated %zu bytes (+ %zu header) at %p\n",
           size, sizeof(block_header_t), user_ptr);

    return user_ptr;
}

void my_free(void* ptr) {
    if (ptr == NULL) return;

    block_header_t* header = user_to_header(ptr);
    header->is_free = 1;

    header->next = free_list;
    free_list = header;

    printf("Freed block of %zu bytes at %p\n", header->size, ptr);
}

void print_heap_stats(void) {
    printf("\n=== Heap Statistics ===\n");
    printf("Total heap size: %zu bytes (%.2f MB)\n",
           sizeof(heap), sizeof(heap) / (1024.0 * 1024.0));
    printf("Total allocated: %zu bytes\n", total_allocated);
    printf("Free space: %zu bytes\n", sizeof(heap) - total_allocated);
    printf("Usage: %.1f%%\n",
           (double)total_allocated / sizeof(heap) * 100);
    printf("====================\n\n");
}

size_t get_total_allocated(void) {
    return total_allocated;
}

size_t get_free_space(void) {
    return sizeof(heap) - total_allocated;
}

void reset_allocator(void) {
    current = heap;
    total_allocated = 0;
    
    printf("Allocator reset - all memory freed\n");
}