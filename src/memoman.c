#define _GNU_SOURCE

#include "memoman.h"
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>

/* =====================================================================================
 * Internal TLSF definitions
 * ===================================================================================== */

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

/* Pool tracking (bounded, O(1) scans). */
#define MM_MAX_POOLS 32

typedef struct mm_pool_desc_t {
  char* start;
  char* end;
  size_t bytes;
  size_t live_allocations;
  int active;
} mm_pool_desc_t;

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
#if UINTPTR_MAX > 0xffffffffu
#define FL_INDEX_MAX 32
#else
#define FL_INDEX_MAX 30
#endif
#define MM_ALIGN_SHIFT \
  ((ALIGNMENT == 1) ? 0 : (ALIGNMENT == 2) ? 1 : (ALIGNMENT == 4) ? 2 : \
   (ALIGNMENT == 8) ? 3 : (ALIGNMENT == 16) ? 4 : (ALIGNMENT == 32) ? 5 : -1)
#define FL_INDEX_SHIFT (SL_INDEX_COUNT_LOG2 + MM_ALIGN_SHIFT)
#define FL_INDEX_COUNT (FL_INDEX_MAX - FL_INDEX_SHIFT + 1)
static const size_t SMALL_BLOCK_SIZE = (size_t)1 << FL_INDEX_SHIFT;
static const size_t BLOCK_SIZE_MAX = (size_t)1 << FL_INDEX_MAX;

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
  mm_pool_desc_t pools[MM_MAX_POOLS];
};

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

static inline size_t align_size(size_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

static inline int fls_u32(unsigned int word) {
  if (!word) return -1;
  return 31 - __builtin_clz(word);
}

static inline int ffs_u32(unsigned int word) {
  if (!word) return -1;
  return __builtin_ctz(word);
}

static inline int fls_sizet(size_t word) {
  if (!word) return -1;
#if SIZE_MAX > 0xffffffffu
  return 63 - __builtin_clzll((unsigned long long)word);
#else
  return fls_u32((unsigned int)word);
#endif
}

static inline size_t block_size(tlsf_block_t* block) { return block->size & TLSF_SIZE_MASK; }
static inline int block_is_free(tlsf_block_t* block) { return (block->size & TLSF_BLOCK_FREE) != 0; }
static inline int block_is_prev_free(tlsf_block_t* block) { return (block->size & TLSF_PREV_FREE) != 0; }
static inline void block_set_size(tlsf_block_t* block, size_t size) { size_t flags = block->size & ~TLSF_SIZE_MASK; block->size = size | flags; }
static inline void block_set_free(tlsf_block_t* block) { block->size |= TLSF_BLOCK_FREE; }
static inline void block_set_used(tlsf_block_t* block) { block->size &= ~TLSF_BLOCK_FREE; }
static inline void block_set_prev_free(tlsf_block_t* block) { block->size |= TLSF_PREV_FREE; }
static inline void block_set_prev_used(tlsf_block_t* block) { block->size &= ~TLSF_PREV_FREE; }

static inline tlsf_block_t* block_prev(tlsf_block_t* block) {
  return *((tlsf_block_t**)((char*)block - sizeof(tlsf_block_t*)));
}
static inline void block_set_prev(tlsf_block_t* block, tlsf_block_t* prev) {
  *((tlsf_block_t**)((char*)block - sizeof(tlsf_block_t*))) = prev;
}

static inline void mapping_insert(size_t size, int* fli, int* sli) {
  int fl, sl;
  if (size < SMALL_BLOCK_SIZE) {
    fl = 0;
    sl = (int)size / (int)(SMALL_BLOCK_SIZE / SL_INDEX_COUNT);
  } else {
    fl = fls_sizet(size);
    sl = (int)(size >> (fl - SL_INDEX_COUNT_LOG2)) ^ (1 << SL_INDEX_COUNT_LOG2);
    fl -= (FL_INDEX_SHIFT - 1);
  }
  *fli = fl;
  *sli = sl;
}

static inline void mapping_search(size_t size, int* fli, int* sli) {
  if (size >= SMALL_BLOCK_SIZE) {
    const int fl = fls_sizet(size);
    const size_t round = ((size_t)1 << (fl - SL_INDEX_COUNT_LOG2)) - 1;
    if (size <= SIZE_MAX - round) size += round;
  }
  mapping_insert(size, fli, sli);
}

static inline tlsf_block_t* search_suitable_block(mm_allocator_t* ctrl, size_t size, int* fli, int* sli) {
  mapping_search(size, fli, sli);

  int fl = *fli;
  int sl = *sli;

  unsigned int sl_map = ctrl->sl_bitmap[fl] & (~0U << sl);
  if (!sl_map) {
    const unsigned int fl_map = ctrl->fl_bitmap & (~0U << (fl + 1));
    if (!fl_map) return NULL;
    fl = ffs_u32(fl_map);
    *fli = fl;
    sl_map = ctrl->sl_bitmap[fl];
  }

  sl = ffs_u32(sl_map);
  *sli = sl;
  return ctrl->blocks[fl][sl];
}

static void remove_free_block_direct(mm_allocator_t* ctrl, tlsf_block_t* block, int fl, int sl) {
  tlsf_block_t* prev = block->prev_free;
  tlsf_block_t* next = block->next_free;

  if (prev) {
    prev->next_free = next;
  } else {
    ctrl->blocks[fl][sl] = next;
  }

  if (next) {
    next->prev_free = prev;
  }

  // If the list is now empty, update the bitmaps
  if (!ctrl->blocks[fl][sl]) {
    ctrl->sl_bitmap[fl] &= ~(1U << sl);
    if (ctrl->sl_bitmap[fl] == 0) {
      ctrl->fl_bitmap &= ~(1U << fl);
    }
  }
  ctrl->current_free_size -= block_size(block);
}

static void remove_free_block(mm_allocator_t* ctrl, tlsf_block_t* block) {
  int fl, sl;
  mapping_insert(block_size(block), &fl, &sl);
  remove_free_block_direct(ctrl, block, fl, sl);
}

static void insert_free_block(mm_allocator_t* ctrl, tlsf_block_t* block) {
  int fl, sl;
  mapping_insert(block_size(block), &fl, &sl);
    
  tlsf_block_t* head = ctrl->blocks[fl][sl];
  block->next_free = head;
  block->prev_free = NULL;

  if (head) { head->prev_free = block; }
    
  ctrl->blocks[fl][sl] = block;

  // Update bitmaps
  ctrl->sl_bitmap[fl] |= (1U << sl);
  ctrl->fl_bitmap |= (1U << fl);
  ctrl->current_free_size += block_size(block);
}

static inline void* block_to_user(tlsf_block_t* block) { return (void*)((char*)block + BLOCK_START_OFFSET); }
static inline tlsf_block_t* user_to_block(void* ptr) { return (tlsf_block_t*)((char*)ptr - BLOCK_START_OFFSET); }

static mm_pool_desc_t* pool_desc_from_handle(mm_allocator_t* ctrl, mm_pool_t pool) {
  if (!ctrl || !pool) return NULL;
  uintptr_t base = (uintptr_t)&ctrl->pools[0];
  uintptr_t end = (uintptr_t)(&ctrl->pools[MM_MAX_POOLS]);
  uintptr_t p = (uintptr_t)pool;
  if (p < base || p >= end) return NULL;
  size_t off = (size_t)(p - base);
  if ((off % sizeof(mm_pool_desc_t)) != 0) return NULL;
  size_t idx = off / sizeof(mm_pool_desc_t);
  if (idx >= MM_MAX_POOLS) return NULL;
  mm_pool_desc_t* desc = &ctrl->pools[idx];
  if (!desc->active) return NULL;
  return desc;
}

static mm_pool_desc_t* pool_desc_for_block(mm_allocator_t* ctrl, const tlsf_block_t* block) {
  if (!ctrl || !block) return NULL;
  uintptr_t addr = (uintptr_t)block;
  for (size_t i = 0; i < MM_MAX_POOLS; i++) {
    mm_pool_desc_t* p = &ctrl->pools[i];
    if (!p->active) continue;
    if (addr >= (uintptr_t)p->start && addr < (uintptr_t)p->end) return p;
  }
  return NULL;
}


void mm_get_mapping_indices(size_t size, int* fl, int* sl) { mapping_insert(size, fl, sl); }
void mm_get_mapping_search_indices(size_t size, int* fl, int* sl) { mapping_search(size, fl, sl); }


static inline tlsf_block_t* block_next_safe(mm_allocator_t* ctrl, tlsf_block_t* block) {
  size_t sz = block_size(block);
  if (sz == 0) return NULL; /* epilogue */
  tlsf_block_t* next = (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + sz);
  /* With discontiguous pools, we rely on sentinel blocks (size 0) to stop iteration. */
  (void)ctrl;
  return next;
}

static inline void block_mark_as_free(mm_allocator_t* ctrl, tlsf_block_t* block) {
  block_set_free(block);
#ifdef MM_DEBUG
  size_t sz = block_size(block);
  assert(sz >= TLSF_MIN_BLOCK_SIZE);
  assert(sz <= ctrl->total_pool_size);
#endif
  tlsf_block_t* next = block_next_safe(ctrl, block);
  if (next) {
    block_set_prev_free(next);
    block_set_prev(next, block);
  }
}


static inline tlsf_block_t* split_block(mm_allocator_t* ctrl, tlsf_block_t* block, size_t size) {
  size_t block_total_size = block_size(block);
  size_t min_split_size = size + BLOCK_HEADER_OVERHEAD + TLSF_MIN_BLOCK_SIZE;

  if (block_total_size < min_split_size) return NULL;

  size_t remainder_size = block_total_size - size - BLOCK_HEADER_OVERHEAD;
  block_set_size(block, size);

  tlsf_block_t* remainder = (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + size);
  block_set_size(remainder, remainder_size);
  block_set_free(remainder);
  block_set_prev_used(remainder); // The block before remainder is now used

  tlsf_block_t* next = block_next_safe(ctrl, remainder);
  if (next) {
    block_set_prev_free(next); // The block before next is now free (remainder)
    block_set_prev(next, remainder);
  }
  return remainder;
}

static inline tlsf_block_t* coalesce(mm_allocator_t* ctrl, tlsf_block_t* block) {
  if (block_is_prev_free(block)) {
    tlsf_block_t* prev = block_prev(block);
    if (prev && block_is_free(prev)) {
      remove_free_block(ctrl, prev);
      size_t combined = block_size(prev) + BLOCK_HEADER_OVERHEAD + block_size(block);
      block_set_size(prev, combined);

      tlsf_block_t* next = block_next_safe(ctrl, prev);
      if (next) {
        block_set_prev_free(next);
        block_set_prev(next, prev);
      }
      block = prev;
    }
  }

  tlsf_block_t* next = block_next_safe(ctrl, block);
  if (next && block_is_free(next)) {
    remove_free_block(ctrl, next);
    size_t combined = block_size(block) + BLOCK_HEADER_OVERHEAD + block_size(next);
    block_set_size(block, combined);

    tlsf_block_t* next_next = block_next_safe(ctrl, block);
    if (next_next) {
      block_set_prev_free(next_next);
      block_set_prev(next_next, block);
    }
  }

  return block;
}


int mm_validate(mm_allocator_t* ctrl) {
  if (!ctrl) return 0;

  #define CHECK(cond, msg) do { if (!(cond)) { return 0; } } while(0)

  /* Physical walk removed as we now support discontiguous pools and don't track them all */

  /* 1. Per-pool physical validation. */
  for (size_t i = 0; i < MM_MAX_POOLS; i++) {
    if (!ctrl->pools[i].active) continue;
    CHECK(mm_validate_pool(ctrl, (mm_pool_t)&ctrl->pools[i]), "Pool validation failed");
  }

  /* 2. Logical Free List Walk */
  for (int fl = 0; fl < TLSF_FLI_MAX; fl++) {
    for (int sl = 0; sl < TLSF_SLI_COUNT; sl++) {
      tlsf_block_t* block = ctrl->blocks[fl][sl];
      
      /* Bitmap Consistency */
      int has_bit = (ctrl->sl_bitmap[fl] & (1U << sl)) != 0;
      if (block) CHECK(has_bit, "Bitmap cleared but list not empty");
      else {
        CHECK(!has_bit, "Bitmap set but list empty");
        continue;
      }

      tlsf_block_t* walk = block;
      tlsf_block_t* list_prev = NULL;
      int count = 0;
      
      while (walk) {
        CHECK(count++ < 10000, "Infinite loop detected in free list");
        CHECK(block_is_free(walk), "Used block found in free list");
        CHECK(walk->prev_free == list_prev, "Free list prev pointer broken");
        CHECK((block_size(walk) % ALIGNMENT) == 0, "Free block size unaligned");
        CHECK(block_size(walk) >= TLSF_MIN_BLOCK_SIZE, "Free block too small");

        /* Prev-physical linkage: the next block must mark prev as free and point back to us. */
        tlsf_block_t* phys_next = block_next_safe(ctrl, walk);
        CHECK(phys_next != NULL, "Free block missing next physical");
        CHECK(block_is_prev_free(phys_next), "Next block missing PREV_FREE");
        CHECK(block_prev(phys_next) == walk, "Next block prev pointer mismatch");
        
        /* Size Mapping Check */
        int mapped_fl, mapped_sl;
        mapping_insert(block_size(walk), &mapped_fl, &mapped_sl);
        CHECK(mapped_fl == fl && mapped_sl == sl, "Block in wrong free list bucket");

        list_prev = walk;
        walk = walk->next_free;
      }
    }
  }
  #undef CHECK
  return 1;
}

int mm_validate_pool(mm_allocator_t* alloc, mm_pool_t pool) {
  if (!alloc || !pool) return 0;

  mm_pool_desc_t* desc = pool_desc_from_handle(alloc, pool);
  if (!desc) return 0;

  tlsf_block_t* block = (tlsf_block_t*)desc->start;
  tlsf_block_t* epilogue = (tlsf_block_t*)(desc->end - BLOCK_HEADER_OVERHEAD);

  if ((uintptr_t)desc->start % ALIGNMENT != 0) return 0;
  if ((uintptr_t)desc->end % ALIGNMENT != 0) return 0;

  if (block_is_free(epilogue)) return 0;
  if (block_size(epilogue) != 0) return 0;

  size_t max_steps = (desc->bytes / ALIGNMENT) + 2;
  tlsf_block_t* prev = NULL;
  for (size_t i = 0; i < max_steps; i++) {
    size_t sz = block_size(block);
    if (sz == 0) break;

    if ((sz % ALIGNMENT) != 0) return 0;
    if (sz < TLSF_MIN_BLOCK_SIZE) return 0;

    tlsf_block_t* next = (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + sz);
    if ((uintptr_t)next > (uintptr_t)epilogue) return 0;

    if (block_is_prev_free(block)) {
      if (!prev) return 0;
      if (!block_is_free(prev)) return 0;
      if (block_prev(block) != prev) return 0;
    } else {
      if (prev && block_is_free(prev)) return 0;
    }

    if (next) {
      if (block_is_free(block)) {
        if (!block_is_prev_free(next)) return 0;
        if (block_prev(next) != block) return 0;
      } else {
        if (block_is_prev_free(next)) return 0;
      }
    }

    prev = block;
    block = next;
    if ((uintptr_t)block == (uintptr_t)epilogue) break;
  }

  if ((uintptr_t)block != (uintptr_t)epilogue) return 0;
  return 1;
}

#ifdef MM_DEBUG
static void mm_check_integrity(mm_allocator_t* ctrl) {
  assert(mm_validate(ctrl) && "Heap integrity check failed");
}
#else
#define mm_check_integrity(ctrl) ((void)0)
#endif

mm_allocator_t* mm_create(void* mem, size_t bytes) {
  /* Overhead: Allocator + Alignment Padding + Min Block + Epilogue */
  size_t overhead = sizeof(mm_allocator_t) + ALIGNMENT + BLOCK_HEADER_OVERHEAD + BLOCK_HEADER_OVERHEAD;
  if (bytes < overhead + TLSF_MIN_BLOCK_SIZE) return NULL;

  /* Ensure provided memory is aligned */
  if ((uintptr_t)mem % ALIGNMENT != 0) return NULL;

  mm_allocator_t* allocator = (mm_allocator_t*)mem;
  memset(allocator, 0, sizeof(mm_allocator_t));

  /* Add the rest of the memory as the first pool */
  void* pool_mem = (char*)mem + sizeof(mm_allocator_t);
  size_t pool_bytes = bytes - sizeof(mm_allocator_t);
  
  if (!mm_add_pool(allocator, pool_mem, pool_bytes)) return NULL;

  return allocator;
}

mm_allocator_t* mm_create_with_pool(void* mem, size_t bytes) {
  return mm_create(mem, bytes);
}

mm_allocator_t* mm_init_in_place(void* mem, size_t bytes) {
  return mm_create(mem, bytes);
}

void mm_destroy(mm_allocator_t* alloc) {
  /* No-op by design: caller owns all memory and core never calls OS APIs. */
  (void)alloc;
}

int mm_reset(mm_allocator_t* allocator) {
  if (!allocator) return 0;

  /* Refuse to reset if the heap is already inconsistent. */
  if (!mm_validate(allocator)) return 0;

  /* Refuse to reset if any live allocation exists in any pool. */
  for (size_t i = 0; i < MM_MAX_POOLS; i++) {
    if (!allocator->pools[i].active) continue;
    if (allocator->pools[i].live_allocations != 0) return 0;
  }

  allocator->fl_bitmap = 0;
  memset(allocator->sl_bitmap, 0, sizeof(allocator->sl_bitmap));
  memset(allocator->blocks, 0, sizeof(allocator->blocks));
  allocator->current_free_size = 0;

  for (size_t i = 0; i < MM_MAX_POOLS; i++) {
    mm_pool_desc_t* desc = &allocator->pools[i];
    if (!desc->active) continue;

    tlsf_block_t* epilogue = (tlsf_block_t*)(desc->end - BLOCK_HEADER_OVERHEAD);
    block_set_size(epilogue, 0);
    block_set_used(epilogue);
    block_set_prev_free(epilogue);

    tlsf_block_t* block = (tlsf_block_t*)desc->start;
    size_t size = (size_t)((char*)epilogue - (char*)block - BLOCK_HEADER_OVERHEAD);
    if (size < TLSF_MIN_BLOCK_SIZE) return 0;

    block->size = 0;
    block_set_size(block, size);
    block_set_free(block);
    block_set_prev_used(block);
    block_set_prev(epilogue, block);

    insert_free_block(allocator, block);
    desc->live_allocations = 0;
  }

  return mm_validate(allocator);
}

mm_pool_t mm_get_pool(mm_allocator_t* allocator) {
  if (!allocator) return NULL;
  for (size_t i = 0; i < MM_MAX_POOLS; i++) {
    if (allocator->pools[i].active) return (mm_pool_t)&allocator->pools[i];
  }
  return NULL;
}

mm_pool_t mm_get_pool_for_ptr(mm_allocator_t* allocator, const void* ptr) {
  if (!allocator || !ptr) return NULL;

  uintptr_t user_addr = (uintptr_t)ptr;
  if (user_addr < (uintptr_t)BLOCK_START_OFFSET) return NULL;

  uintptr_t block_addr = user_addr - (uintptr_t)BLOCK_START_OFFSET;
  if ((block_addr % (uintptr_t)ALIGNMENT) != 0) return NULL;

  for (size_t i = 0; i < MM_MAX_POOLS; i++) {
    mm_pool_desc_t* p = &allocator->pools[i];
    if (!p->active) continue;
    if (block_addr >= (uintptr_t)p->start && block_addr < (uintptr_t)p->end) return (mm_pool_t)p;
  }

  return NULL;
}

mm_pool_t mm_add_pool(mm_allocator_t* allocator, void* mem, size_t bytes) {
  if (!allocator || !mem) return NULL;

  size_t overhead = ALIGNMENT + BLOCK_HEADER_OVERHEAD + BLOCK_HEADER_OVERHEAD;
  if (bytes < overhead + TLSF_MIN_BLOCK_SIZE) return NULL;

  uintptr_t start_addr = (uintptr_t)mem;
  uintptr_t aligned_addr = (start_addr + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
  char* pool_start = (char*)aligned_addr;
  size_t aligned_bytes = bytes - (aligned_addr - start_addr);
  
  /* Check if alignment ate too much */
  if (aligned_bytes < overhead + TLSF_MIN_BLOCK_SIZE) return NULL;

  char* pool_end = pool_start + aligned_bytes;

  mm_pool_desc_t* desc = NULL;
  for (size_t i = 0; i < MM_MAX_POOLS; i++) {
    if (!allocator->pools[i].active) {
      desc = &allocator->pools[i];
      break;
    }
  }
  if (!desc) return NULL;

  desc->start = pool_start;
  desc->end = pool_end;
  desc->bytes = aligned_bytes;
  desc->live_allocations = 0;
  desc->active = 1;

  /* 1. Create Epilogue sentinel. */
  tlsf_block_t* epilogue = (tlsf_block_t*)(pool_end - BLOCK_HEADER_OVERHEAD);
  block_set_size(epilogue, 0);
  block_set_used(epilogue);
  block_set_prev_free(epilogue);

  /*
  ** 2. Create main free block.
  ** Conte-style: the first block's prev-phys pointer lives immediately before
  ** its size word, and falls outside the pool. We never dereference it because
  ** the first block is always marked prev-used.
  */
  tlsf_block_t* block = (tlsf_block_t*)pool_start;
  size_t size = (size_t)((char*)epilogue - (char*)block - BLOCK_HEADER_OVERHEAD);
  
  block_set_size(block, size);
  block_set_free(block);
  block_set_prev_used(block);
  block_set_prev(epilogue, block);
  
  insert_free_block(allocator, block);
  allocator->total_pool_size += aligned_bytes;
  
  return (mm_pool_t)desc;
}

void mm_remove_pool(mm_allocator_t* allocator, mm_pool_t pool) {
  if (!allocator || !pool) return;

  mm_pool_desc_t* desc = pool_desc_from_handle(allocator, pool);
  if (!desc) return;

#ifdef MM_DEBUG
  assert(desc->live_allocations == 0 && "mm_remove_pool requires the pool to have no live allocations");
#endif
  if (desc->live_allocations != 0) return;

  tlsf_block_t* block = (tlsf_block_t*)desc->start;
  tlsf_block_t* epilogue = (tlsf_block_t*)(desc->end - BLOCK_HEADER_OVERHEAD);

  size_t max_steps = (desc->bytes / ALIGNMENT) + 2;
  /* Preflight: refuse to remove if any used block exists (don't mutate state). */
  for (size_t i = 0; i < max_steps; i++) {
    size_t sz = block_size(block);
    if (sz == 0) break;

    if (!block_is_free(block)) return;

    block = (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + sz);
    if ((uintptr_t)block > (uintptr_t)epilogue) return;
  }

  if ((uintptr_t)block != (uintptr_t)epilogue) return;

  /* Removal: every block in the pool is free, so remove free-list nodes. */
  block = (tlsf_block_t*)desc->start;
  for (size_t i = 0; i < max_steps; i++) {
    size_t sz = block_size(block);
    if (sz == 0) break;

    if (!block_is_free(block)) return;
    remove_free_block(allocator, block);

    block = (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + sz);
    if ((uintptr_t)block > (uintptr_t)epilogue) return;
  }

  allocator->total_pool_size -= desc->bytes;
  desc->active = 0;
  desc->start = NULL;
  desc->end = NULL;
  desc->bytes = 0;
  desc->live_allocations = 0;
}

void* mm_malloc(mm_allocator_t* ctrl, size_t size) {
  if (!ctrl || size == 0) return NULL;
  mm_check_integrity(ctrl);

  if (size < TLSF_MIN_BLOCK_SIZE) size = TLSF_MIN_BLOCK_SIZE;
  if (size >= BLOCK_SIZE_MAX) return NULL;
  if (size > SIZE_MAX - (ALIGNMENT - 1)) return NULL;
  size = align_size(size);
  if (size >= BLOCK_SIZE_MAX) return NULL;

  int fl, sl;
  tlsf_block_t* block = search_suitable_block(ctrl, size, &fl, &sl);
  if (!block) return NULL;

  remove_free_block_direct(ctrl, block, fl, sl);
  tlsf_block_t* remainder = split_block(ctrl, block, size);
  if (remainder) {
    /* Coalesce remainder with next block if it is free */
    remainder = coalesce(ctrl, remainder);
    insert_free_block(ctrl, remainder);
  }

  block_set_used(block);
  tlsf_block_t* next = block_next_safe(ctrl, block);
  if (next) block_set_prev_used(next);

  mm_pool_desc_t* pool_desc = pool_desc_for_block(ctrl, block);
#ifdef MM_DEBUG
  assert(pool_desc && "allocation returned a block outside any pool");
#endif
  if (pool_desc) pool_desc->live_allocations++;

  mm_check_integrity(ctrl);
  return block_to_user(block);
}

void mm_free(mm_allocator_t* ctrl, void* ptr) {
  if (!ptr || !ctrl) return;
  mm_check_integrity(ctrl);

  if ((uintptr_t)ptr % ALIGNMENT != 0) {
    return;
  }

  tlsf_block_t* block = user_to_block(ptr);

  if (block_is_free(block)) {
    /* Double free detected: do nothing, but optionally log for debug */
    // fprintf(stderr, "[Memoman] Warning: Double free detected for %p\n", ptr);
    return;
  }

  mm_pool_desc_t* pool_desc = pool_desc_for_block(ctrl, block);
#ifdef MM_DEBUG
  assert(pool_desc && pool_desc->live_allocations > 0);
#endif
  if (pool_desc && pool_desc->live_allocations > 0) pool_desc->live_allocations--;

  block_mark_as_free(ctrl, block);
  block = coalesce(ctrl, block);
  /* Always insert the coalesced block into the free list */
  insert_free_block(ctrl, block);
  mm_check_integrity(ctrl);
}

static int try_realloc_inplace(mm_allocator_t* ctrl, void* ptr, size_t size) {
  if (!ctrl) return -1;
  mm_check_integrity(ctrl);

  /* Handle Main Heap Blocks */
  {
    if ((uintptr_t)ptr % ALIGNMENT != 0) return -1;

    tlsf_block_t* block = user_to_block(ptr);

    size_t current_size = block_size(block);
    if (size < TLSF_MIN_BLOCK_SIZE) size = TLSF_MIN_BLOCK_SIZE;
    size_t aligned_size = align_size(size);

    /* Case 1: Shrink or Same Size */
    if (aligned_size <= current_size) {
      tlsf_block_t* remainder = split_block(ctrl, block, aligned_size);
      if (remainder) {
        block_mark_as_free(ctrl, remainder);
        remainder = coalesce(ctrl, remainder);
        insert_free_block(ctrl, remainder);
      }
      mm_check_integrity(ctrl);
      return 0;
    }

    /* Case 2: Grow (Try to coalesce with next block) */
    tlsf_block_t* next = block_next_safe(ctrl, block);
    if (next && block_is_free(next)) {
      size_t next_size = block_size(next);
      size_t combined = current_size + BLOCK_HEADER_OVERHEAD + next_size;
      
      if (combined >= aligned_size) {
        remove_free_block(ctrl, next);
        block_set_size(block, combined);
        
        tlsf_block_t* next_next = block_next_safe(ctrl, block);
        if (next_next) {
          block_set_prev_used(next_next);
        }

        tlsf_block_t* remainder = split_block(ctrl, block, aligned_size);
        if (remainder) {
          block_mark_as_free(ctrl, remainder);
          remainder = coalesce(ctrl, remainder);
          insert_free_block(ctrl, remainder);
        }
        mm_check_integrity(ctrl);
        return 0;
      }
    }
    return 1; /* Valid, but needs move */
  } 
  return -1;
}

void* mm_realloc(mm_allocator_t* ctrl, void* ptr, size_t size) {
  if (!ptr) return mm_malloc(ctrl, size);
  if (size == 0) { mm_free(ctrl, ptr); return NULL; }

  int status = try_realloc_inplace(ctrl, ptr, size);
  
  if (status == 0) return ptr; /* In-place success */
  if (status == -1) {
    return NULL;
  }

  /* Status 1: Needs move */
  void* new_ptr = mm_malloc(ctrl, size);
  if (new_ptr) {
    size_t old_usable = block_size(user_to_block(ptr));
    memcpy(new_ptr, ptr, (old_usable < size) ? old_usable : size);
    mm_free(ctrl, ptr);
  }
  return new_ptr;
}

void* mm_memalign(mm_allocator_t* ctrl, size_t alignment, size_t size) {
  if (!ctrl) return NULL;
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) return NULL; /* must be power of two */
  if (size == 0) return NULL;

  /* If alignment is <= default alignment, regular malloc suffices */
  if (alignment <= ALIGNMENT) return mm_malloc(ctrl, size);

  mm_check_integrity(ctrl);

  /* Normalize requested size */
  size_t requested_size = (size < TLSF_MIN_BLOCK_SIZE) ? TLSF_MIN_BLOCK_SIZE : size;
  requested_size = align_size(requested_size);

  /*
  ** Conte TLSF requires an extra minimum free block worth of space so that if
  ** the alignment gap would be too small to split, we can advance to the next
  ** aligned boundary and still trim a valid leading free block.
  */
  const size_t gap_minimum = BLOCK_HEADER_OVERHEAD + TLSF_MIN_BLOCK_SIZE;
  if (requested_size >= BLOCK_SIZE_MAX) return NULL;
  if (alignment > SIZE_MAX - requested_size) return NULL;
  size_t size_with_gap = requested_size + alignment;
  if (gap_minimum > SIZE_MAX - size_with_gap) return NULL;
  size_with_gap += gap_minimum;
  if (size_with_gap > SIZE_MAX - (alignment - 1)) return NULL;
  size_t search_size = (size_with_gap + (alignment - 1)) & ~(alignment - 1);
  if (search_size >= BLOCK_SIZE_MAX) return NULL;

  int fl = 0, sl = 0;
  tlsf_block_t* block = search_suitable_block(ctrl, search_size, &fl, &sl);
  if (!block) return NULL;

  /* Remove the chosen free block from free lists */
  remove_free_block_direct(ctrl, block, fl, sl);

  size_t orig_size = block_size(block);

  uintptr_t user_addr = (uintptr_t)block_to_user(block);
  uintptr_t aligned_user = (user_addr + (alignment - 1)) & ~(alignment - 1);
  size_t gap = (size_t)(aligned_user - user_addr);

  if (gap && gap < gap_minimum) {
    const size_t gap_remain = gap_minimum - gap;
    const size_t offset = (gap_remain > alignment) ? gap_remain : alignment;
    /* Conte semantics: advance from the *first* aligned boundary, not from the raw pointer. */
    aligned_user = ((aligned_user + offset) + (alignment - 1)) & ~(alignment - 1);
    gap = (size_t)(aligned_user - user_addr);
  }

  tlsf_block_t* aligned_block = block;

  if (gap) {
    /* Trim a leading free block (gap must be large enough to be a valid free block). */
    if (gap < gap_minimum) {
      insert_free_block(ctrl, block);
      mm_check_integrity(ctrl);
      return NULL;
    }

    /* Leading prefix becomes a free block using the original header. */
    const size_t prefix_payload = gap - BLOCK_HEADER_OVERHEAD;
    const size_t aligned_payload = orig_size - gap;

    block_set_size(block, prefix_payload);
    block_set_free(block);

    aligned_block = (tlsf_block_t*)((char*)block + gap);
    aligned_block->size = 0;
    block_set_size(aligned_block, aligned_payload);
    block_set_free(aligned_block);
    block_set_prev_free(aligned_block);
    block_set_prev(aligned_block, block);

    insert_free_block(ctrl, block);
  }

  /* At this point aligned_block points to a free block with sufficient space */
  if (block_size(aligned_block) < requested_size) {
    /* Should not happen, but guard */
    insert_free_block(ctrl, aligned_block);
    mm_check_integrity(ctrl);
    return NULL;
  }

  tlsf_block_t* remainder = split_block(ctrl, aligned_block, requested_size);
  if (remainder) insert_free_block(ctrl, remainder);

  block_set_used(aligned_block);
  tlsf_block_t* next = block_next_safe(ctrl, aligned_block);
  if (next) {
    block_set_prev_used(next);
  }

  mm_pool_desc_t* pool_desc = pool_desc_for_block(ctrl, aligned_block);
#ifdef MM_DEBUG
  assert(pool_desc && "memalign returned a block outside any pool");
#endif
  if (pool_desc) pool_desc->live_allocations++;

  mm_check_integrity(ctrl);
  return block_to_user(aligned_block);
}

size_t mm_block_size(void* ptr) {
  if (!ptr) return 0;
  tlsf_block_t* block = user_to_block(ptr);
  return block_size(block);
}

size_t mm_size(void) { return sizeof(mm_allocator_t); }
size_t mm_align_size(void) { return ALIGNMENT; }
size_t mm_block_size_min(void) { return TLSF_MIN_BLOCK_SIZE; }

size_t mm_block_size_max(void) {
  /* Must be < BLOCK_SIZE_MAX and remain aligned after rounding. */
  return BLOCK_SIZE_MAX - ALIGNMENT;
}

size_t mm_pool_overhead(void) {
  /* Worst-case internal overhead of adding a pool (includes alignment slop). */
  return ALIGNMENT + (2 * BLOCK_HEADER_OVERHEAD);
}

size_t mm_alloc_overhead(void) {
  /* Returned pointer is immediately after the size word. */
  return BLOCK_START_OFFSET;
}

void mm_walk_pool(mm_pool_t pool, mm_walker walker, void* user) {
  if (!pool || !walker) return;
  mm_pool_desc_t* desc = (mm_pool_desc_t*)pool;
  if (!desc->active) return;

  tlsf_block_t* block = (tlsf_block_t*)desc->start;
  tlsf_block_t* epilogue = (tlsf_block_t*)(desc->end - BLOCK_HEADER_OVERHEAD);

  size_t max_steps = (desc->bytes / ALIGNMENT) + 2;
  for (size_t i = 0; i < max_steps; i++) {
    size_t sz = block_size(block);
    if (sz == 0) break;

    int used = block_is_free(block) ? 0 : 1;
    walker(block_to_user(block), sz, used, user);

    tlsf_block_t* next = (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + sz);
    if ((uintptr_t)next > (uintptr_t)epilogue) break;
    block = next;
  }
}
