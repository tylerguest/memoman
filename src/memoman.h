#ifndef MEMOMAN_H
#define MEMOMAN_H

#include <stddef.h>
#include <stdint.h>

/*
** Block Layout (TLSF 3.1 semantics)
**
** A block pointer addresses the size word of the current block. The previous
** block pointer (prev_phys) lives immediately *before* the size word as part
** of the previous block's footer.
**
** Used block (user data starts immediately after size):
**   [prev_phys (footer of previous)] [ size|flags ] [ user payload ... ]
**                     (block - 8)          ^block   ^returned pointer (block+8)
**
** Free block (free-list links stored in user payload):
**   [prev_phys] [ size|flags ] [ next_free ] [ prev_free ] [ payload slack ]
**        ^          +0             +8           +16
**
** Constants:
**   BLOCK_HEADER_OVERHEAD = sizeof(size_t)                (size word only)
**   BLOCK_START_OFFSET    = BLOCK_HEADER_OVERHEAD         (payload begins here)
**
** Consequences:
**   - block_to_user(block) == (char*)block + BLOCK_START_OFFSET
**   - prev_phys pointer for a block is stored at ((char*)block - sizeof(void*))
**   - Free-list pointers always reside in the user payload when a block is free.
*/

/* 
 * We use the exact TLSF 3.1 block structure.
 * Note: prev_phys_block is only valid if the previous block is free.
 */
typedef struct tlsf_block_t {
    size_t size; /* LSBs used for flags */
    struct tlsf_block_t* next_free;
    struct tlsf_block_t* prev_free;
} tlsf_block_t;

/* 
 * Overhead exposed for tests.
 * BLOCK_START_OFFSET is the distance from the block struct pointer 
 * to the user payload.
 */
#define BLOCK_HEADER_OVERHEAD sizeof(size_t)
#define BLOCK_START_OFFSET BLOCK_HEADER_OVERHEAD

/* Flags for the size field */
#define TLSF_BLOCK_FREE   (size_t)1
#define TLSF_PREV_FREE    (size_t)2
#define TLSF_SIZE_MASK    (~(TLSF_BLOCK_FREE | TLSF_PREV_FREE))

/* Alignment */
#define ALIGNMENT         sizeof(size_t)

/* Block size constants */
/* Minimum payload required for a free block:
 *   - next_free pointer (stored at payload start)
 *   - prev_free pointer (stored at payload start)
 *   - footer word holding next block's prev pointer (stored at payload end)
 */
#define TLSF_MIN_BLOCK_SIZE (3 * sizeof(void*))

/* Configuration constants matching TLSF 3.1 defaults for 64-bit */
#define SL_INDEX_COUNT_LOG2 5
#define SL_INDEX_COUNT (1 << SL_INDEX_COUNT_LOG2)
#define FL_INDEX_MAX 32
#define FL_INDEX_SHIFT (SL_INDEX_COUNT_LOG2 + 3) /* 3 for 8-byte alignment */
#define FL_INDEX_COUNT (FL_INDEX_MAX - FL_INDEX_SHIFT + 1)

/* Aliases for compatibility */
#define TLSF_SLI          SL_INDEX_COUNT_LOG2
#define TLSF_SLI_COUNT    SL_INDEX_COUNT
#define TLSF_FLI_OFFSET   FL_INDEX_SHIFT
#define TLSF_FLI_MAX      FL_INDEX_COUNT

typedef struct mm_allocator_t {
    tlsf_block_t block_null;
    unsigned int fl_bitmap;
    unsigned int sl_bitmap[FL_INDEX_COUNT];
    tlsf_block_t* blocks[FL_INDEX_COUNT][SL_INDEX_COUNT];
    size_t current_free_size;
    size_t total_pool_size;
} mm_allocator_t;

/* Global allocator instance exposed for tests */
extern mm_allocator_t* sys_allocator;

/* Global API */
void* mm_malloc(size_t size);
void mm_free(void* ptr);
void* mm_calloc(size_t nmemb, size_t size);
void* mm_realloc(void* ptr, size_t size);
size_t mm_malloc_usable_size(void* ptr);
int mm_validate(void);
size_t mm_get_free_space(void);
void mm_reset_allocator(void);
void mm_get_mapping_indices(size_t size, int* fl, int* sl);

/* Instance API */
mm_allocator_t* mm_create(void* mem, size_t bytes);
void mm_destroy(mm_allocator_t* alloc);
int mm_add_pool(mm_allocator_t* alloc, void* mem, size_t bytes);
void* mm_malloc_inst(mm_allocator_t* alloc, size_t size);
void mm_free_inst(mm_allocator_t* alloc, void* ptr);
void* mm_calloc_inst(mm_allocator_t* alloc, size_t nmemb, size_t size);
void* mm_realloc_inst(mm_allocator_t* alloc, void* ptr, size_t size);
int mm_validate_inst(mm_allocator_t* alloc);
size_t mm_get_free_space_inst(mm_allocator_t* alloc);
size_t mm_get_usable_size(mm_allocator_t* alloc, void* ptr);
size_t mm_get_total_allocated_inst(mm_allocator_t* alloc);

#endif
