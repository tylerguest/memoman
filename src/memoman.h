#ifndef INCLUDED_memoman
#define INCLUDED_memoman

/*
** memoman: deterministic, pool-based TLSF allocator (TLSF 3.1 semantics).
**
** Ownership model:
** - The caller provides all memory (one or more pools).
** - The allocator never allocates or frees OS memory.
** - `mm_destroy()` never frees memory; the caller frees the backing buffers.
**
** Alignment rules:
** - `mm_create()`/`mm_create_with_pool()` require `mem` aligned to `sizeof(size_t)`.
** - `mm_add_pool()` accepts any `mem`; it aligns the pool start up to `sizeof(size_t)` (reducing usable bytes).
*/

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct mm_allocator_t mm_allocator_t;
typedef void* mm_pool_t;

/* Create/destroy an allocator instance (in-place). */
mm_allocator_t* mm_create(void* mem, size_t bytes);
mm_allocator_t* mm_create_with_pool(void* mem, size_t bytes);
mm_allocator_t* mm_init_in_place(void* mem, size_t bytes);
void mm_destroy(mm_allocator_t* alloc);

/* Add/remove memory pools. */
mm_pool_t mm_get_pool(mm_allocator_t* alloc);
mm_pool_t mm_add_pool(mm_allocator_t* alloc, void* mem, size_t bytes);
void mm_remove_pool(mm_allocator_t* alloc, mm_pool_t pool);

/* malloc/memalign/realloc/free replacements. */
void* mm_malloc(mm_allocator_t* alloc, size_t size);
void* mm_memalign(mm_allocator_t* alloc, size_t alignment, size_t size);
void* mm_realloc(mm_allocator_t* alloc, void* ptr, size_t size);
void  mm_free(mm_allocator_t* alloc, void* ptr);

/* Returns internal block size, not original request size. */
size_t mm_block_size(void* ptr);

/* Sizing helpers (TLSF-style). */
size_t mm_size(void);
size_t mm_align_size(void);
size_t mm_block_size_min(void);
size_t mm_block_size_max(void);
size_t mm_pool_overhead(void);
size_t mm_alloc_overhead(void);

/* Debug tooling. */
typedef void (*mm_walker)(void* ptr, size_t size, int used, void* user);
void mm_walk_pool(mm_pool_t pool, mm_walker walker, void* user);

/* Returns nonzero if internal consistency checks pass. */
int mm_validate(mm_allocator_t* alloc);
int mm_validate_pool(mm_allocator_t* alloc, mm_pool_t pool);

/* memoman extensions. */
mm_pool_t mm_get_pool_for_ptr(mm_allocator_t* alloc, const void* ptr);
int mm_reset(mm_allocator_t* alloc);

/*
** Pointer safety policy (debug mode):
** - In normal builds, `mm_free`/`mm_realloc` perform best-effort checks and ignore/reject pointers that are
**   clearly invalid (not within any pool, misaligned, or not a plausible block header).
** - In `MM_DEBUG` builds, invalid pointers may trigger an assertion depending on:
**   - `MM_DEBUG_ABORT_ON_INVALID_POINTER` (default 1)
**   - `MM_DEBUG_ABORT_ON_DOUBLE_FREE` (default 0)
**
** Passing invalid pointers remains undefined behavior in C; this policy exists to prevent silent corruption and
** to fail fast in debug builds.
*/

#if defined(__cplusplus)
}
#endif

#endif
