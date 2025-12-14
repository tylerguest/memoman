#ifndef MEMOMAN_H
#define MEMOMAN_H

#include <stddef.h>

/**
 * Block metadata structure. User data follows immediately after.
 * size: usable bytes (excludes header), is_free: allocation status, next: free list linkage
 */
typedef struct block_header {
    size_t size;
    int is_free;
    struct block_header* next;
} block_header_t;

typedef struct large_block {
  size_t size;
  struct large_block* next;
} large_block_t;

/**
 * Initialize the allocator (optional - called automatically)
 * @return 0 on success, -1 on failure
 */
int mm_init(void);

/**
 * Cleanup and free all memory
 */
void mm_destroy(void);

/**
 * Allocate memory from the custom heap
 * 
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if allocation fails
 * 
 * Uses a segregated free list with size classes for fast allocation.
 * Falls back to first-fit search if no size class match is found.
 */
void* mm_malloc(size_t size);

/**
 * Free previously allocated memory
 * 
 * @param ptr Pointer to memory to free (returned by mm_malloc)
 * 
 * Adds the block back to the appropriate free list.
 * Periodically coalesces adjacent free blocks to reduce fragmentation.
 */
void mm_free(void* ptr);

/**
 * Reset the allocator to initial state
 * 
 * Clears all allocations and free lists. Use for testing or cleanup.
 */
void reset_allocator(void);

/**
 * Display current heap usage statistics
 * 
 * Shows total heap size, used space, free space, and usage percentage.
 */
void print_heap_stats(void);

/**
 * Display all blocks in the free list
 * 
 * Debugging utility that shows each free block's size and address.
 */
void print_free_list(void);

/**
 * Get total bytes allocated from heap
 * 
 * @return Total number of bytes currently in use (includes headers)
 */
size_t get_total_allocated(void);

/**
 * Get remaining free space in heap
 * 
 * @return Number of bytes available for allocation
 */
size_t get_free_space(void);

block_header_t* get_free_list(void);

#endif