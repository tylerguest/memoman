#ifndef MEMOMAN_H
#define MEMOMAN_H

#include <stddef.h>
#include <stdint.h>

/* TLSF Configuration */
#define TLSF_MIN_BLOCK_SIZE 16             // Mininum allocatable block
#define TLSF_FLI_MAX 30                    // log2(1GB) for max pool size
#define TLSF_SLI 5                         // Second level index (2^5 = 32 bins)
#define TLSF_SLI_COUNT (1 << TLSF_SLI)     // 32 second-level bins
#define TLSF_FLI_OFFSET 4                  // Offset for minimum block size

/* Block flag macros - stored in LSBs of size field */
#define TLSF_BLOCK_FREE (1 << 0)           // Is block free?
#define TLSF_PREV_FREE (1 << 1)            // Is previous physical block free?
#define TLSF_SIZE_MASK (~(size_t)3)        // Mask to extract actual size

/* Global variables */

/*
 * TLSF block header with boundary tags and free list pointers
 * Size field uses LSBs for flags (free/prev)
 * Free blocks use next_free/prev_free for doubly-linked segregated lists
 * All blocks use prev_phys for backward physical traversal (boundary tag)
 */
typedef struct tlsf_block {
  size_t size;
  struct tlsf_block* prev_phys;

  /* Free blocks only - these oberlap user data in allocated blocks */
  struct tlsf_block* next_free;
  struct tlsf_block* prev_free;
} tlsf_block_t;

/*
 * TLSF control structure - manages the two-level segeregated fit allocator
 * Placed at the start of the heap for cache locality
 */
typedef struct {
  /* Two-level segregated free lists: FLI x SLI matrix */
  tlsf_block_t* blocks[TLSF_FLI_MAX][TLSF_SLI_COUNT];

  /* Bitmaps for 0(1) search */
  uint32_t fl_bitmap;                       // First-level: which FLI have free blocks
  uint32_t sl_bitmap[TLSF_FLI_MAX];         // Second-level: which SLI per FLI

  /* Heap bounds */
  char* heap_start;                         // Start of allocatable heap
  char* heap_end;                           // End of committed heap
  size_t heap_capacity;                     // Total committed capacity
} tlsf_control_t;

/*
 * Large block structure - for allocations >= 1MB.
 * These bypass TLSF and use direct mmap.
 */
typedef struct large_block {
  size_t size;
  struct large_block* next;
} large_block_t;

/* Legacy type - to be removed */
typedef tlsf_block_t block_header_t;

/*
 * Initialize the allocator (optional - called automatically)
 * @return 0 on success, -1 on failure
 */
int mm_init(void);

/*
 * Cleanup and free all memory
 */
void mm_destroy(void);

/*
 * Allocate memory from the custom heap
 * 
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if allocation fails
 * 
 * Uses a segregated free list with size classes for fast allocation.
 * Falls back to first-fit search if no size class match is found.
 */
void* mm_malloc(size_t size);

/*
 * Free previously allocated memory
 * 
 * @param ptr Pointer to memory to free (returned by mm_malloc)
 * 
 * Adds the block back to the appropriate free list.
 * Periodically coalesces adjacent free blocks to reduce fragmentation.
 */
void mm_free(void* ptr);

/*
 * Reset the allocator to initial state
 * 
 * Clears all allocations and free lists. Use for testing or cleanup.
 */
void reset_allocator(void);

/*
 * Display current heap usage statistics
 * 
 * Shows total heap size, used space, free space, and usage percentage.
 */
void print_heap_stats(void);

/*
 * Display all blocks in the free list
 * 
 * Debugging utility that shows each free block's size and address.
 */
void print_free_list(void);

/*
 * Get total bytes allocated from heap
 * 
 * @return Total number of bytes currently in use (includes headers)
 */
size_t get_total_allocated(void);

/*
 * Get remaining free space in heap
 * 
 * @return Number of bytes available for allocation
 */
size_t get_free_space(void);

block_header_t* get_free_list(void);

#endif