#define _GNU_SOURCE

#include "memoman.h"
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>

static inline size_t align_size(size_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

static inline int fls_generic(size_t word) { return (sizeof(size_t) * 8) - 1 - __builtin_clzl(word); }
static inline int ffs_generic(uint32_t word) {
  int result = __builtin_ffs(word);
  return result ? result - 1 : -1;
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

static inline void mapping(size_t size, int* fli, int* sli) {
  int fl, sl;
  if (size < (1 << TLSF_FLI_OFFSET)) {
    fl = 0;
    sl = size / (1 << (TLSF_FLI_OFFSET - TLSF_SLI));
  } else {
    fl = fls_generic(size);
    sl = (size >> (fl - TLSF_SLI)) ^ (1 << TLSF_SLI);
    fl -= (TLSF_FLI_OFFSET - 1);
  }
  *fli = fl;
  *sli = sl;
}

static inline tlsf_block_t* search_suitable_block(mm_allocator_t* ctrl, size_t size, int* fli, int* sli) {
  mapping(size, fli, sli);
  
  // Check for a block in the ideal list
  if ((ctrl->sl_bitmap[*fli] & (1U << *sli)) == 0) {
    // No block in the ideal list, find the next biggest
    int fl = *fli;
    int sl = *sli + 1;
    
    // Find the next available SL slot in the current FL
    uint32_t sl_map = ctrl->sl_bitmap[fl] & (~0U << sl);
    if (sl_map == 0) {
      // No available SL slots in the current FL, find the next FL
      int fl_map = ctrl->fl_bitmap & (~0U << (fl + 1));
      if (fl_map == 0) {
        return NULL; // No suitable block found
      }
      *fli = ffs_generic(fl_map);
      *sli = ffs_generic(ctrl->sl_bitmap[*fli]);
    } else {
      *sli = ffs_generic(sl_map);
    }
  }
  return ctrl->blocks[*fli][*sli];
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
  mapping(block_size(block), &fl, &sl);
  remove_free_block_direct(ctrl, block, fl, sl);
}

static void insert_free_block(mm_allocator_t* ctrl, tlsf_block_t* block) {
  int fl, sl;
  mapping(block_size(block), &fl, &sl);
    
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


void mm_get_mapping_indices(size_t size, int* fl, int* sl) { mapping(size, fl, sl); }


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


int mm_validate_inst(mm_allocator_t* ctrl) {
  if (!ctrl) return 0;

  #define CHECK(cond, msg) do { if (!(cond)) { return 0; } } while(0)

  /* Physical walk removed as we now support discontiguous pools and don't track them all */

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
        mapping(block_size(walk), &mapped_fl, &mapped_sl);
        CHECK(mapped_fl == fl && mapped_sl == sl, "Block in wrong free list bucket");

        list_prev = walk;
        walk = walk->next_free;
      }
    }
  }
  #undef CHECK
  return 1;
}

#ifdef MM_DEBUG
static void mm_check_integrity(mm_allocator_t* ctrl) {
  assert(mm_validate_inst(ctrl) && "Heap integrity check failed");
}
#else
#define mm_check_integrity(ctrl) ((void)0)
#endif

mm_allocator_t* mm_create(void* mem, size_t bytes) {
  /* Overhead: Allocator + Alignment Padding + Prologue + Min Block + Epilogue */
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

int mm_add_pool(mm_allocator_t* allocator, void* mem, size_t bytes) {
  if (!allocator || !mem) return 0;

  size_t overhead = ALIGNMENT + BLOCK_HEADER_OVERHEAD + BLOCK_HEADER_OVERHEAD;
  if (bytes < overhead + TLSF_MIN_BLOCK_SIZE) return 0;

  uintptr_t start_addr = (uintptr_t)mem;
  uintptr_t aligned_addr = (start_addr + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
  char* pool_start = (char*)aligned_addr;
  size_t aligned_bytes = bytes - (aligned_addr - start_addr);
  
  /* Check if alignment ate too much */
  if (aligned_bytes < overhead + TLSF_MIN_BLOCK_SIZE) return 0;

  char* pool_end = pool_start + aligned_bytes;

  /* 1. Create Prologue */
  tlsf_block_t* prologue = (tlsf_block_t*)pool_start;
  block_set_size(prologue, 0); /* Minimal prologue: only the size word exists */
  block_set_used(prologue);
  block_set_prev_used(prologue);

  /* 2. Create Epilogue */
  tlsf_block_t* epilogue = (tlsf_block_t*)(pool_end - BLOCK_START_OFFSET);
  block_set_size(epilogue, 0);
  block_set_used(epilogue);
  block_set_prev_free(epilogue);

  /* 3. Create Main Free Block */
  tlsf_block_t* block = (tlsf_block_t*)(pool_start + BLOCK_START_OFFSET);
  size_t size = (char*)epilogue - (char*)block - BLOCK_HEADER_OVERHEAD;
  
  block_set_size(block, size);
  block_set_free(block);
  block_set_prev_used(block);
  block_set_prev(epilogue, block);
  
  insert_free_block(allocator, block);
  allocator->total_pool_size += aligned_bytes;
  
  return 1;
}

void* mm_malloc_inst(mm_allocator_t* ctrl, size_t size) {
  if (!ctrl || size == 0) return NULL;
  mm_check_integrity(ctrl);

  if (size < TLSF_MIN_BLOCK_SIZE) size = TLSF_MIN_BLOCK_SIZE;
  size = align_size(size);

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

  mm_check_integrity(ctrl);
  return block_to_user(block);
}

void mm_free_inst(mm_allocator_t* ctrl, void* ptr) {
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

  block_mark_as_free(ctrl, block);
  block = coalesce(ctrl, block);
  /* Always insert the coalesced block into the free list */
  insert_free_block(ctrl, block);
  mm_check_integrity(ctrl);
}

void* mm_calloc_inst(mm_allocator_t* ctrl, size_t nmemb, size_t size) {
  if (nmemb != 0 && size > SIZE_MAX / nmemb) return NULL;
  size_t total = nmemb * size;
  void* p = mm_malloc_inst(ctrl, total);
  if (p) memset(p, 0, total);
  return p;
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

void* mm_realloc_inst(mm_allocator_t* ctrl, void* ptr, size_t size) {
  if (!ptr) return mm_malloc_inst(ctrl, size);
  if (size == 0) { mm_free_inst(ctrl, ptr); return NULL; }

  int status = try_realloc_inplace(ctrl, ptr, size);
  
  if (status == 0) return ptr; /* In-place success */
  if (status == -1) {
    return NULL;
  }

  /* Status 1: Needs move */
  void* new_ptr = mm_malloc_inst(ctrl, size);
  if (new_ptr) {
    size_t old_usable = mm_get_usable_size(ctrl, ptr);
    memcpy(new_ptr, ptr, (old_usable < size) ? old_usable : size);
    mm_free_inst(ctrl, ptr);
  }
  return new_ptr;
}

void* mm_memalign_inst(mm_allocator_t* ctrl, size_t alignment, size_t size) {
  if (!ctrl) return NULL;
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) return NULL; /* must be power of two */
  if (size == 0) return NULL;

  /* If alignment is <= default alignment, regular malloc suffices */
  if (alignment <= ALIGNMENT) return mm_malloc_inst(ctrl, size);

  mm_check_integrity(ctrl);

  /* Normalize requested size */
  size_t requested_size = (size < TLSF_MIN_BLOCK_SIZE) ? TLSF_MIN_BLOCK_SIZE : size;
  requested_size = align_size(requested_size);

  /* We need extra space for alignment and header */
  size_t search_size = requested_size + alignment + BLOCK_HEADER_OVERHEAD;

  int fl = 0, sl = 0;
  tlsf_block_t* block = search_suitable_block(ctrl, search_size, &fl, &sl);
  if (!block) return NULL;

  /* Remove the chosen free block from free lists */
  remove_free_block_direct(ctrl, block, fl, sl);

  size_t orig_size = block_size(block);

  uintptr_t user_addr = (uintptr_t)block_to_user(block);
  uintptr_t aligned_user = (user_addr + (alignment - 1)) & ~(alignment - 1);
  tlsf_block_t* aligned_block = (tlsf_block_t*)(aligned_user - BLOCK_HEADER_OVERHEAD);

  /* Compute prefix gap (bytes) between original header and aligned header */
  ptrdiff_t prefix_bytes = (char*)aligned_block - (char*)block;

  if (prefix_bytes > 0) {
    /* If the prefix is big enough to hold a free block, split it off */
    if ((size_t)prefix_bytes >= BLOCK_HEADER_OVERHEAD + TLSF_MIN_BLOCK_SIZE) {
      /* Create prefix free block (reuse original header) */
      size_t prefix_data = (size_t)prefix_bytes - BLOCK_HEADER_OVERHEAD;
      block_set_size(block, prefix_data);
      block_set_free(block);
      /* Aligned block follows prefix */
      aligned_block = (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + block_size(block));
      /* Setup aligned block header */
      size_t aligned_data = orig_size - ((size_t)prefix_bytes);
      block_set_size(aligned_block, aligned_data);
      /* The aligned block now has previous free */
      block_set_prev_free(aligned_block);
      block_set_prev(aligned_block, block);
      /* Insert the prefix into free lists */
      insert_free_block(ctrl, block);
    } else {
      /* Prefix too small to split: leave it as part of aligned_block */
      aligned_block = block;
      aligned_user = (uintptr_t)block_to_user(aligned_block);
      prefix_bytes = 0;
    }
  } else {
    /* No prefix; aligned_block is original block */
    aligned_block = block;
    aligned_user = (uintptr_t)block_to_user(aligned_block);
    prefix_bytes = 0;
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

  mm_check_integrity(ctrl);
  return block_to_user(aligned_block);
}

size_t mm_get_usable_size(mm_allocator_t* allocator, void* ptr) {
  if (!allocator || !ptr) return 0;

  /* Main heap blocks */
  if ((uintptr_t)ptr % ALIGNMENT != 0) return 0;
  tlsf_block_t* block = user_to_block(ptr);
  if (block_is_free(block)) return 0;
  return block_size(block);
}

size_t mm_get_free_space_inst(mm_allocator_t* allocator) {
  if (!allocator) return 0;
  return allocator->current_free_size;
}

size_t mm_get_total_allocated_inst(mm_allocator_t* allocator) {
  if (!allocator) return 0;
  return allocator->total_pool_size - allocator->current_free_size;
}
