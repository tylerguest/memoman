#ifndef MEMOMAN_H
#define MEMOMAN_H

#include <stddef.h>
#include <stdint.h>

/* ========================== */
/* === Internal Constants === */
/* ========================== */

#define ALIGNMENT 8
#define LARGE_ALLOC_THRESHOLD (1024 * 1024)
#define INITIAL_HEAP_SIZE (1024 * 1024)
#define HEAP_GROWTH_FACTOR 2
#define MAX_HEAP_SIZE (1UL << 30) /* 1 GiB reserved virtual space */
#define TLSF_MIN_BLOCK_SIZE 24 /* Constraint: 32B physical min - 8B overhead */
#define TLSF_FLI_MAX 30
#define TLSF_SLI 5
#define TLSF_SLI_COUNT (1 << TLSF_SLI)
#define TLSF_FLI_OFFSET 8 /* TLSF_SLI (5) + LOG2_ALIGN (3) */

#define TLSF_BLOCK_FREE (1 << 0)
#define TLSF_PREV_FREE (1 << 1)
#define TLSF_SIZE_MASK (~(size_t)3)
#define LARGE_BLOCK_MAGIC 0xDEADB10C
#define TLSF_BLOCK_MAGIC 0xCAFEBABE

#ifdef DEBUG_OUTPUT
#define BLOCK_HEADER_OVERHEAD (sizeof(size_t) + sizeof(uint32_t) + 12)
#else
#define BLOCK_HEADER_OVERHEAD sizeof(size_t)
#endif

/* ======================= */
/* === Data Structures === */
/* ======================= */

typedef struct tlsf_block {
  size_t size;
#ifdef DEBUG_OUTPUT
  uint32_t magic;
  char pad[12];
#endif
  struct tlsf_block* next_free;
  struct tlsf_block* prev_free;
} tlsf_block_t;

typedef struct large_block {
  uint32_t magic;
  size_t size;
  struct large_block* next;
  struct large_block* prev;
} large_block_t;

typedef struct mm_allocator {
  uint32_t fl_bitmap;
  uint32_t sl_bitmap[TLSF_FLI_MAX];
  tlsf_block_t* blocks[TLSF_FLI_MAX][TLSF_SLI_COUNT];
  char* heap_start;
  char* heap_end;
  large_block_t* large_blocks;
} mm_allocator_t;

/* ==================== */
/* === Global State === */
/* ==================== */

/* Exposed for testing */
extern mm_allocator_t* sys_allocator;
extern char* sys_heap_base;
extern size_t sys_heap_cap;

/* ====================================================================================
 *                                 INSTANCE API (Pool-based)
 * ====================================================================================
 * Explicit Ownership Model:
 * - The caller provides the memory block (pool).
 * - The allocator manages the internal structure of that pool.
 * - The caller is responsible for freeing the pool memory after destroying the instance.
 */

/* Initialize a new allocator instance inside the provided memory block.
 * Returns NULL if the memory block is too small (< ~8KB). */
mm_allocator_t* mm_create(void* mem, size_t bytes);

/* Destroy an allocator instance.
 * - Releases any external resources (like mmap'd large blocks).
 * - Does NOT free the initial memory pool (caller owns it). */
void mm_destroy_instance(mm_allocator_t* allocator);

/* Allocate/Free from a specific instance */
void* mm_malloc_inst(mm_allocator_t* allocator, size_t size);
void mm_free_inst(mm_allocator_t* allocator, void* ptr);
void* mm_calloc_inst(mm_allocator_t* allocator, size_t nmemb, size_t size);
void* mm_realloc_inst(mm_allocator_t* allocator, void* ptr, size_t size);
void* mm_memalign_inst(mm_allocator_t* allocator, size_t alignment, size_t size);

/* Get usable size for a pointer within a specific instance */
size_t mm_get_usable_size(mm_allocator_t* allocator, void* ptr);

/* Instance Statistics */
size_t mm_get_free_space_inst(mm_allocator_t* allocator);
size_t mm_get_total_allocated_inst(mm_allocator_t* allocator);
void mm_print_heap_stats_inst(mm_allocator_t* allocator);
void mm_print_free_list_inst(mm_allocator_t* allocator);

/* Internal helpers */
void mm_get_mapping_indices(size_t size, int* fl, int* sl);
int mm_validate_inst(mm_allocator_t* allocator);

/* ========================== */
/* === Global Wrapper API === */
/* ========================== */

int mm_init(void);                             // Initialize global default instance
void mm_destroy(void);                         // Destroy global default instance
void* mm_malloc(size_t size);                  // Allocate from global instance
void mm_free(void* ptr);                       // Free from global instance
void* mm_calloc(size_t nmemb, size_t size);
void* mm_realloc(void* ptr, size_t size);
void* mm_memalign(size_t alignment, size_t size);
size_t mm_malloc_usable_size(void* ptr);
void mm_print_heap_stats(void);
size_t mm_get_free_space(void);
size_t mm_get_total_allocated(void);
void mm_print_free_list(void);
void mm_reset_allocator(void);
int mm_validate(void);

#endif 