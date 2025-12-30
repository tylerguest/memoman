#ifndef INCLUDED_memoman
#define INCLUDED_memoman

/*
** Memoman: deterministic, pool-based TLSF allocator (TLSF 3.1 semantics).
** Core never calls OS allocation APIs; caller owns all memory.
*/

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct mm_allocator_t mm_allocator_t;

/* Create/destroy an allocator instance (in-place). */
mm_allocator_t* mm_create(void* mem, size_t bytes);
mm_allocator_t* mm_create_with_pool(void* mem, size_t bytes);
void mm_destroy(mm_allocator_t* alloc);

/* Add pools. */
int mm_add_pool(mm_allocator_t* alloc, void* mem, size_t bytes);

/* malloc/memalign/realloc/free replacements. */
void* mm_malloc(mm_allocator_t* alloc, size_t size);
void* mm_memalign(mm_allocator_t* alloc, size_t alignment, size_t size);
void* mm_realloc(mm_allocator_t* alloc, void* ptr, size_t size);
void  mm_free(mm_allocator_t* alloc, void* ptr);

/* Returns internal block size, not original request size. */
size_t mm_block_size(void* ptr);

/* Debugging. Returns nonzero if internal consistency checks pass. */
int mm_validate(mm_allocator_t* alloc);

#if defined(__cplusplus)
}
#endif

#endif
