#ifndef MEMOMAN_H
#define MEMOMAN_H

#include <stddef.h>
#include <stdint.h>

/* ========================== */
/* === Internal Constants === */
/* ========================== */

#define ALIGNMENT 8
#define TLSF_MIN_BLOCK_SIZE 24 /* Constraint: 32B physical min - 8B overhead */
#define TLSF_FLI_MAX 30
#define TLSF_SLI 5
#define TLSF_SLI_COUNT (1 << TLSF_SLI)
#define TLSF_FLI_OFFSET 8 /* TLSF_SLI (5) + LOG2_ALIGN (3) */

#define TLSF_BLOCK_FREE (1 << 0)
#define TLSF_PREV_FREE (1 << 1)
#define TLSF_SIZE_MASK (~(size_t)3)
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

typedef struct mm_allocator {
  uint32_t fl_bitmap;
  uint32_t sl_bitmap[TLSF_FLI_MAX];
  tlsf_block_t* blocks[TLSF_FLI_MAX][TLSF_SLI_COUNT];
  size_t total_pool_size;
  size_t current_free_size;
} mm_allocator_t;

/* ==================== */
/* === Global State === */
/* ==================== */

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

/* Add a new memory pool to an existing allocator instance.
 * Returns 1 on success, 0 on failure (e.g. alignment issues, too small). */
int mm_add_pool(mm_allocator_t* allocator, void* mem, size_t bytes);

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

/* Internal helpers */
void mm_get_mapping_indices(size_t size, int* fl, int* sl);
int mm_validate_inst(mm_allocator_t* allocator);

#endif 