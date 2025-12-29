#ifndef MEMOMAN_H
#define MEMOMAN_H

/*
** Memoman allocator public API (pool-based, deterministic, O(1)).
**
** Usage model:
** - Caller provides memory (one or more pools).
** - Core never calls OS allocation APIs.
** - All allocations are relative to a specific allocator instance.
**
** TLSF 3.1 layout note:
** - The returned user pointer is immediately after the size word.
** - When a block is free, free-list pointers are stored in the user payload.
** - The prev-physical pointer of a block is stored in the previous block's payload.
*/

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct mm_allocator_t mm_allocator_t;

/* Lifecycle / pools */
mm_allocator_t* mm_create(void* mem, size_t bytes);
int mm_add_pool(mm_allocator_t* alloc, void* mem, size_t bytes);

/* Allocation */
void* mm_malloc(mm_allocator_t* alloc, size_t size);
void  mm_free(mm_allocator_t* alloc, void* ptr);
void* mm_calloc(mm_allocator_t* alloc, size_t nmemb, size_t size);
void* mm_realloc(mm_allocator_t* alloc, void* ptr, size_t size);
void* mm_memalign(mm_allocator_t* alloc, size_t alignment, size_t size);

/* Introspection */
size_t mm_usable_size(mm_allocator_t* alloc, void* ptr);
size_t mm_free_space(mm_allocator_t* alloc);
size_t mm_total_allocated(mm_allocator_t* alloc);
int mm_validate(mm_allocator_t* alloc);

#if defined(__cplusplus)
}
#endif

#endif
