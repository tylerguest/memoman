#ifndef INCLUDED_memoman
#define INCLUDED_memoman

/*
** memoman: deterministic, pool-based TLSF allocator (TLSF 3.1 semantics).
**
** Ownership model (TLSF-style):
** - The caller provides all memory (one or more pools).
** - The allocator never allocates or frees OS memory.
** - `mm_destroy()` never frees memory; the caller frees the backing buffers.
**
** Alignment rules:
** - `mm_create()` / `mm_create_with_pool()` require `mem` to be aligned to `sizeof(size_t)`.
** - `mm_add_pool()` accepts any `mem` pointer; it internally aligns the pool start up to `sizeof(size_t)` (this may reduce usable bytes).
 */

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct mm_allocator_t mm_allocator_t;

/*
** Create/destroy an allocator instance (in-place).
**
** `mm_create()` initializes an allocator control structure at the start of `mem`
** and uses the remaining bytes as the initial pool.
**
** Returns NULL on failure (insufficient space, misalignment).
*/
mm_allocator_t* mm_create(void* mem, size_t bytes);

/* TLSF-style symmetry alias of `mm_create()`. */
mm_allocator_t* mm_create_with_pool(void* mem, size_t bytes);

/* No-op by design (caller owns memory). Safe to call with NULL. */
void mm_destroy(mm_allocator_t* alloc);

/*
** Add a discontiguous pool to an existing allocator.
** Returns nonzero on success, 0 on failure (insufficient size, etc.).
*/
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
