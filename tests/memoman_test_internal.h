#ifndef MEMOMAN_TEST_INTERNAL_H
#define MEMOMAN_TEST_INTERNAL_H

/*
 * Test-only visibility into memoman's internal TLSF layout.
 *
 * This file intentionally duplicates internal definitions so the public
 * header `src/memoman.h` can remain a small, user-facing API.
 */

#include "../src/memoman.h"
#include <stddef.h>
#include <stdint.h>

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
 * - next_free/prev_free stored at payload start (2 pointers)
 * - next block's prev_phys stored in this payload (1 pointer)
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

/* Complete the opaque type for tests. */
struct mm_allocator_t {
  tlsf_block_t block_null;
  unsigned int fl_bitmap;
  unsigned int sl_bitmap[FL_INDEX_COUNT];
  tlsf_block_t* blocks[FL_INDEX_COUNT][SL_INDEX_COUNT];
  size_t current_free_size;
  size_t total_pool_size;
};

/* Test-only helper exposed by the implementation. */
void mm_get_mapping_indices(size_t size, int* fl, int* sl);

#endif

