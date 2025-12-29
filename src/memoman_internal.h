#ifndef MEMOMAN_INTERNAL_H
#define MEMOMAN_INTERNAL_H

#include "memoman.h"
#include <stddef.h>
#include <stdint.h>

/* C99-compatible compile-time assertions. */
#define MM_STATIC_ASSERT(cond, name) typedef char mm_static_assert_##name[(cond) ? 1 : -1]

/*
** Block Layout (TLSF 3.1 semantics)
**
** A block pointer addresses the size word of the current block. The previous
** block pointer (prev_phys) is stored immediately before the size word, inside
** the previous block's payload.
**
** Used block:
**   [prev_phys (footer of previous)] [ size|flags ] [ user payload ... ]
**
** Free block:
**   [prev_phys] [ size|flags ] [ next_free ] [ prev_free ] [ payload slack ]
*/

typedef struct tlsf_block_t {
  size_t size; /* LSBs used for flags (TLSF_BLOCK_FREE, TLSF_PREV_FREE) */
  struct tlsf_block_t* next_free;
  struct tlsf_block_t* prev_free;
} tlsf_block_t;

/* The block header exposed to used blocks is a single size word. */
#define BLOCK_HEADER_OVERHEAD sizeof(size_t)
#define BLOCK_START_OFFSET BLOCK_HEADER_OVERHEAD

/* Flags stored in the size word. */
#define TLSF_BLOCK_FREE   (size_t)1
#define TLSF_PREV_FREE    (size_t)2
#define TLSF_SIZE_MASK    (~(TLSF_BLOCK_FREE | TLSF_PREV_FREE))

/* Default alignment (TLSF uses ALIGN_SIZE; we key off size_t). */
#define ALIGNMENT         sizeof(size_t)

/* Derived minimum payload required for a free block (TLSF 3.1 semantics):
** - next_free/prev_free stored at payload start (2 pointers)
** - next block's prev_phys stored in this payload (1 pointer)
*/
#define MM_FREELIST_LINKS_BYTES (2 * sizeof(void*))
#define MM_PREV_PHYS_FOOTER_BYTES (sizeof(void*))
#define MM_MIN_FREE_PAYLOAD_BYTES (MM_FREELIST_LINKS_BYTES + MM_PREV_PHYS_FOOTER_BYTES)
#define TLSF_MIN_BLOCK_SIZE ((MM_MIN_FREE_PAYLOAD_BYTES + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

/* TLSF-style mapping configuration (defaults match TLSF 3.1). */
#define SL_INDEX_COUNT_LOG2 5
#define SL_INDEX_COUNT (1 << SL_INDEX_COUNT_LOG2)
#define FL_INDEX_MAX 32
#define MM_ALIGN_SHIFT \
  ((ALIGNMENT == 1) ? 0 : (ALIGNMENT == 2) ? 1 : (ALIGNMENT == 4) ? 2 : \
   (ALIGNMENT == 8) ? 3 : (ALIGNMENT == 16) ? 4 : (ALIGNMENT == 32) ? 5 : -1)
#define FL_INDEX_SHIFT (SL_INDEX_COUNT_LOG2 + MM_ALIGN_SHIFT)
#define FL_INDEX_COUNT (FL_INDEX_MAX - FL_INDEX_SHIFT + 1)

/* Aliases for compatibility with TLSF naming. */
#define TLSF_SLI          SL_INDEX_COUNT_LOG2
#define TLSF_SLI_COUNT    SL_INDEX_COUNT
#define TLSF_FLI_OFFSET   FL_INDEX_SHIFT
#define TLSF_FLI_MAX      FL_INDEX_COUNT

struct mm_allocator_t {
  tlsf_block_t block_null;
  unsigned int fl_bitmap;
  unsigned int sl_bitmap[FL_INDEX_COUNT];
  tlsf_block_t* blocks[FL_INDEX_COUNT][SL_INDEX_COUNT];
  size_t current_free_size;
  size_t total_pool_size;
};

/* Internal/test-only helpers. */
void mm_get_mapping_indices(size_t size, int* fl, int* sl);

/* === Compile-time invariants (Conte-style) === */
MM_STATIC_ASSERT((ALIGNMENT & (ALIGNMENT - 1)) == 0, alignment_power_of_two);
MM_STATIC_ASSERT(ALIGNMENT >= sizeof(void*), alignment_ge_pointer);
MM_STATIC_ASSERT(MM_ALIGN_SHIFT >= 0, alignment_shift_supported);
MM_STATIC_ASSERT(BLOCK_HEADER_OVERHEAD == sizeof(size_t), header_overhead_is_size);
MM_STATIC_ASSERT(BLOCK_START_OFFSET == BLOCK_HEADER_OVERHEAD, payload_starts_after_size);
MM_STATIC_ASSERT(offsetof(tlsf_block_t, next_free) == BLOCK_START_OFFSET, freelist_links_in_payload);
MM_STATIC_ASSERT(offsetof(tlsf_block_t, prev_free) == (BLOCK_START_OFFSET + sizeof(void*)), freelist_prev_in_payload);
MM_STATIC_ASSERT((TLSF_MIN_BLOCK_SIZE % ALIGNMENT) == 0, min_block_aligned);
MM_STATIC_ASSERT(TLSF_MIN_BLOCK_SIZE >= (2 * sizeof(void*)), min_block_has_freelist_links);
MM_STATIC_ASSERT(TLSF_MIN_BLOCK_SIZE >= (3 * sizeof(void*)), min_block_has_prev_footer);
MM_STATIC_ASSERT(SL_INDEX_COUNT <= (sizeof(unsigned int) * 8), sl_bitmap_fits_uint);
MM_STATIC_ASSERT(FL_INDEX_COUNT <= (sizeof(unsigned int) * 8), fl_bitmap_fits_uint);

#endif

