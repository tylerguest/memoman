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
** - `mm_add_pool()` requires `mem` and `bytes` aligned to `sizeof(size_t)`; misaligned pools are rejected.
*/

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* tlsf_t: a TLSF structure. Can contain 1 to N pools. */
/* pool_t: base address of a managed pool (TLSF-style). */
typedef void* tlsf_t;
typedef void* pool_t;

/*
** Create/destroy an allocator instance (in-place).
**
** - `mm_create` is TLSF-style control-only creation: it does not implicitly add a pool.
** - `mm_create_with_pool` is the convenience API for a single backing buffer: it creates control + adds the remaining bytes as the first pool.
*/
tlsf_t mm_create(void* mem);
tlsf_t mm_create_with_pool(void* mem, size_t bytes);
void mm_destroy(tlsf_t alloc);
pool_t mm_get_pool(tlsf_t alloc);

/* Add/remove memory pools. */
pool_t mm_add_pool(tlsf_t alloc, void* mem, size_t bytes);
void mm_remove_pool(tlsf_t alloc, pool_t pool);

/* malloc/memalign/realloc/free replacements. */
void* mm_malloc(tlsf_t alloc, size_t bytes);
void* mm_memalign(tlsf_t alloc, size_t align, size_t bytes);
void* mm_realloc(tlsf_t alloc, void* ptr, size_t size);
void  mm_free(tlsf_t alloc, void* ptr);

/* Returns internal block size, not original request size. */
size_t mm_block_size(void* ptr);

/* Overheads/limits of internal structures. */
size_t mm_size(void);
size_t mm_align_size(void);
size_t mm_block_size_min(void);
size_t mm_block_size_max(void);
size_t mm_pool_overhead(void);
size_t mm_alloc_overhead(void);

/* Debugging. */
typedef void (*mm_walker)(void* ptr, size_t size, int used, void* user);
void mm_walk_pool(pool_t pool, mm_walker walker, void* user);
int mm_validate(tlsf_t alloc);
int mm_validate_pool(pool_t pool);
int mm_check(tlsf_t alloc);
int mm_check_pool(pool_t pool);

/* Memoman extensions (TLSF does not define these). */
tlsf_t mm_init_in_place(void* mem, size_t bytes);
pool_t mm_get_pool_for_ptr(tlsf_t alloc, const void* ptr);
int mm_reset(tlsf_t alloc);

#if defined(__cplusplus)
};
#endif

#endif
