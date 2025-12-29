#define _GNU_SOURCE

#include "memoman.h"
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>


mm_allocator_t* sys_allocator = NULL;
char* sys_heap_base = NULL; 
size_t sys_heap_cap = 0;

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

static inline void block_set_magic(tlsf_block_t* block) {
#ifdef DEBUG_OUTPUT
  block->magic = TLSF_BLOCK_MAGIC;
#else
  (void)block;
#endif
}

static inline int block_check_magic(tlsf_block_t* block) {
#ifdef DEBUG_OUTPUT
  return block->magic == TLSF_BLOCK_MAGIC;
#else
  (void)block;
  return 1;
#endif
}

static inline tlsf_block_t* block_prev(tlsf_block_t* block) { return *((tlsf_block_t**)((char*)block - sizeof(tlsf_block_t*))); }

static inline void block_set_prev(tlsf_block_t* block, tlsf_block_t* prev) { *((tlsf_block_t**)((char*)block - sizeof(tlsf_block_t*))) = prev; }

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
}

static inline void* block_to_user(tlsf_block_t* block) { return (void*)((char*)block + BLOCK_HEADER_OVERHEAD); }

static inline tlsf_block_t* user_to_block(void* ptr) { return (tlsf_block_t*)((char*)ptr - BLOCK_HEADER_OVERHEAD); }


void mm_get_mapping_indices(size_t size, int* fl, int* sl) { mapping(size, fl, sl); }


static inline tlsf_block_t* block_next_safe(mm_allocator_t* ctrl, tlsf_block_t* block) {
  tlsf_block_t* next = (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + block_size(block));
  if ((char*)next >= ctrl->heap_end) return NULL;
  return next;
}

static inline void block_mark_as_free(mm_allocator_t* ctrl, tlsf_block_t* block) {
  block_set_free(block);
  tlsf_block_t* next = block_next_safe(ctrl, block);
  if (next) {
    block_set_prev_free(next);
    block_set_prev(next, block);
  }
}


static inline tlsf_block_t* split_block(mm_allocator_t* ctrl, tlsf_block_t* block, size_t size) {
  size_t block_total_size = block_size(block);
  size_t min_split_size = TLSF_MIN_BLOCK_SIZE + BLOCK_HEADER_OVERHEAD;

  if (block_total_size < size + min_split_size) return NULL;

  size_t remainder_size = block_total_size - size - BLOCK_HEADER_OVERHEAD;
  block_set_size(block, size);

  tlsf_block_t* remainder = (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + size);
  block_set_magic(remainder);
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

static void create_free_block(mm_allocator_t* ctrl, void* start, size_t size) {
  if (size < BLOCK_HEADER_OVERHEAD + TLSF_MIN_BLOCK_SIZE) return;

  tlsf_block_t* block = (tlsf_block_t*)start;
  size_t block_data_size = size - BLOCK_HEADER_OVERHEAD;
  block_set_magic(block);
  block_set_size(block, block_data_size);
  block_set_free(block);

  block = coalesce(ctrl, block);
  insert_free_block(ctrl, block);
}


int mm_validate_inst(mm_allocator_t* ctrl) {
  if (!ctrl) return 0;

  #define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "[Memoman] Integrity Failure: %s\n", msg); return 0; } } while(0)

  /* 1. Physical Heap Walk */
  tlsf_block_t* prev = NULL;
  tlsf_block_t* curr = (tlsf_block_t*)ctrl->heap_start;
  int found_epilogue = 0;

  while (curr && (char*)curr < ctrl->heap_end) {
    /* Check Alignment & Bounds */
    CHECK(((uintptr_t)curr % ALIGNMENT == 0), "Block header not aligned");
    if ((char*)curr != ctrl->heap_start) {
        CHECK(((uintptr_t)block_to_user(curr) % ALIGNMENT == 0), "Block payload not aligned");
    }
    CHECK((char*)curr >= ctrl->heap_start && (char*)curr < ctrl->heap_end, "Block out of bounds");
    #ifdef DEBUG_OUTPUT
    CHECK(curr->magic == TLSF_BLOCK_MAGIC, "Block magic corrupted");
    #endif

    size_t size = block_size(curr);
    int is_free = block_is_free(curr);
    int is_prev_free = block_is_prev_free(curr);

    /* Check Ghost Pointer / Prev Free Flag Consistency */
    if (prev) {
      int prev_actual_free = block_is_free(prev);
      CHECK(is_prev_free == prev_actual_free, "PREV_FREE flag desync with actual prev block");

      if (is_prev_free) {
        /* Ghost pointer must point to prev */
        CHECK(block_prev(curr) == prev, "Ghost prev_phys pointer invalid");
        /* Coalescing Invariant: No two free blocks adjacent */
        CHECK(!is_free, "Adjacent free blocks detected (coalescing failed)");
      }
    }

    /* Sentinel Checks */
    if (size == 0) {
      if ((char*)curr == ctrl->heap_start) CHECK(!is_free, "Prologue must be used");
      else {
        CHECK(!is_free, "Epilogue must be used");
        CHECK((char*)curr + BLOCK_HEADER_OVERHEAD == ctrl->heap_end, "Epilogue not at heap end");
        found_epilogue = 1;
        break; 
      }
    }

    prev = curr;
    curr = block_next_safe(ctrl, curr);
  }
  CHECK(found_epilogue, "Heap walk finished without finding epilogue");

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

static large_block_t* find_large_block(mm_allocator_t* ctrl, void* ptr) {
  large_block_t* lb = ctrl->large_blocks;
  while (lb) {
    if ((char*)lb + sizeof(large_block_t) == (char*)ptr) return lb;
    lb = lb->next;
  }
  return NULL;
}

mm_allocator_t* mm_create(void* mem, size_t bytes) {
  /* Overhead: Allocator + Alignment Padding + Prologue + Min Block + Epilogue */
  size_t overhead = sizeof(mm_allocator_t) + ALIGNMENT + BLOCK_HEADER_OVERHEAD + BLOCK_HEADER_OVERHEAD;
  if (bytes < overhead + TLSF_MIN_BLOCK_SIZE) return NULL;

  mm_allocator_t* allocator = (mm_allocator_t*)mem;
  memset(allocator, 0, sizeof(mm_allocator_t));

  char* heap_start = (char*)mem + sizeof(mm_allocator_t);
  
  /* Align heap_start to ALIGNMENT */
  uintptr_t start_addr = (uintptr_t)heap_start;
  uintptr_t aligned_addr = (start_addr + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
  heap_start = (char*)aligned_addr;

  allocator->heap_start = heap_start;
  allocator->heap_end = (char*)mem + bytes;

  /* 1. Create Prologue (Sentinel) */
  tlsf_block_t* prologue = (tlsf_block_t*)heap_start;
  block_set_size(prologue, 0);
  block_set_used(prologue);
  block_set_prev_used(prologue);
  block_set_magic(prologue);

  /* 2. Create Epilogue (Sentinel) */
  tlsf_block_t* epilogue = (tlsf_block_t*)(allocator->heap_end - BLOCK_HEADER_OVERHEAD);
  block_set_size(epilogue, 0);
  block_set_used(epilogue);
  block_set_prev_free(epilogue);
  block_set_magic(epilogue);

  /* 3. Create Main Free Block */
  char* middle_start = heap_start + BLOCK_HEADER_OVERHEAD;
  size_t middle_size = (char*)epilogue - middle_start;
  
  tlsf_block_t* block = (tlsf_block_t*)middle_start;
  block->size = 0; /* Initialize flags */
  block_set_prev_used(block); /* Prologue is used */
  block_set_prev(epilogue, block);
  
  create_free_block(allocator, middle_start, middle_size);

  return allocator;
}

void mm_destroy_instance(mm_allocator_t* allocator) {
  if (!allocator) return;

  large_block_t* lb = allocator->large_blocks;
  while (lb) {
    large_block_t* next = lb->next;
    munmap(lb, lb->size);
    lb = next;
  }
}

void* mm_malloc_inst(mm_allocator_t* ctrl, size_t size) {
  if (!ctrl || size == 0) return NULL;
  mm_check_integrity(ctrl);

  if (size >= LARGE_ALLOC_THRESHOLD) {
    size_t total = sizeof(large_block_t) + align_size(size);
    void* ptr = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return NULL;

    large_block_t* block = (large_block_t*)ptr;
    block->magic = LARGE_BLOCK_MAGIC;
    block->size = total;
    block->prev = NULL;
    block->next = ctrl->large_blocks;
    if (ctrl->large_blocks) ctrl->large_blocks->prev = block;
    ctrl->large_blocks = block;

    return (char*)ptr + sizeof(large_block_t);
  }

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

  if ((char*)ptr < ctrl->heap_start || (char*)ptr >= ctrl->heap_end) {
    large_block_t* lb = find_large_block(ctrl, ptr);
    if (lb) {
      if (lb->prev) lb->prev->next = lb->next;
      else ctrl->large_blocks = lb->next;
      if (lb->next) lb->next->prev = lb->prev;
      munmap(lb, lb->size);
    } else {
      fprintf(stderr, "[Memoman] Error: Pointer %p outside heap bounds in free()\n", ptr);
    }
    return;
  }

  if ((uintptr_t)ptr % ALIGNMENT != 0) {
    fprintf(stderr, "[Memoman] Error: Invalid pointer alignment in free()\n");
    return;
  }

  tlsf_block_t* block = user_to_block(ptr);
  if (!block_check_magic(block)) {
    fprintf(stderr, "[Memoman] Error: Invalid block magic in free()\n");
    return;
  }

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
  if ((char*)ptr >= ctrl->heap_start && (char*)ptr < ctrl->heap_end) {
    if ((uintptr_t)ptr % ALIGNMENT != 0) return -1;

    tlsf_block_t* block = user_to_block(ptr);
    if (!block_check_magic(block)) return -1;

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
  /* Handle Large Blocks */
  else {
    large_block_t* lb = find_large_block(ctrl, ptr);
    if (!lb) return -1; /* Invalid pointer */
    size_t current_size = lb->size - sizeof(large_block_t);
    return (size <= current_size) ? 0 : 1;
  }
}

void* mm_realloc_inst(mm_allocator_t* ctrl, void* ptr, size_t size) {
  if (!ptr) return mm_malloc_inst(ctrl, size);
  if (size == 0) { mm_free_inst(ctrl, ptr); return NULL; }

  int status = try_realloc_inplace(ctrl, ptr, size);
  
  if (status == 0) return ptr; /* In-place success */
  if (status == -1) {
    fprintf(stderr, "[Memoman] Error: Invalid pointer in realloc()\n");
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
      block_set_magic(block);
      block_set_free(block);
      /* Aligned block follows prefix */
      aligned_block = (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + block_size(block));
      /* Setup aligned block header */
      block_set_magic(aligned_block);
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
    block_set_prev(next, aligned_block);
    block_set_prev_used(next);
  }

  mm_check_integrity(ctrl);
  return block_to_user(aligned_block);
}


static int global_grow_heap(size_t min_additional) {
  if (!sys_allocator) return -1;

  size_t old_cap = sys_heap_cap;
  size_t new_cap = old_cap * HEAP_GROWTH_FACTOR;
  while (new_cap < old_cap + min_additional) new_cap *= HEAP_GROWTH_FACTOR;
  if (new_cap > MAX_HEAP_SIZE) new_cap = MAX_HEAP_SIZE;
  if (new_cap <= old_cap) return -1;

  if (mprotect(sys_heap_base, new_cap, PROT_READ | PROT_WRITE) != 0) { perror("mprotect failed"); return -1; }

  /* Handle Epilogue Movement */
  tlsf_block_t* old_epilogue = (tlsf_block_t*)(sys_heap_base + old_cap - BLOCK_HEADER_OVERHEAD);
  size_t old_prev_flags = old_epilogue->size & TLSF_PREV_FREE;

  sys_heap_cap = new_cap;
  sys_allocator->heap_end = sys_heap_base + new_cap;
  
  /* Create new epilogue at the new end */
  tlsf_block_t* new_epilogue = (tlsf_block_t*)(sys_allocator->heap_end - BLOCK_HEADER_OVERHEAD);
  block_set_size(new_epilogue, 0);
  block_set_used(new_epilogue);
  block_set_prev_free(new_epilogue); // Will be adjusted by coalesce/create_free_block
  block_set_magic(new_epilogue);

  /* Convert old epilogue + new space into a free block */
  /* We start at old_epilogue because it is now part of the free space */
  tlsf_block_t* block = old_epilogue;
  size_t added_size = new_cap - old_cap;
  size_t total_new_size = added_size;
  
  /* Preserve the PREV_FREE state of the old epilogue! */
  block->size = old_prev_flags; 
  block_set_prev(new_epilogue, block);
  create_free_block(sys_allocator, block, total_new_size);
  return 0;
}

int mm_init(void) {
  if (sys_allocator) return 0;

  sys_heap_base = mmap(NULL, MAX_HEAP_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (sys_heap_base == MAP_FAILED) return -1;

  if (mprotect(sys_heap_base, INITIAL_HEAP_SIZE, PROT_READ | PROT_WRITE) != 0) {
    munmap(sys_heap_base, MAX_HEAP_SIZE);
    return -1;
  }

  sys_heap_cap = INITIAL_HEAP_SIZE;
  sys_allocator = mm_create(sys_heap_base, INITIAL_HEAP_SIZE);
  if (!sys_allocator) return -1;

  return 0;
}

void* mm_malloc(size_t size) {
  if (!sys_allocator) if (mm_init() != 0) return NULL;
  void* ptr = mm_malloc_inst(sys_allocator, size);
  if (!ptr && size < LARGE_ALLOC_THRESHOLD) {
    if (global_grow_heap(size + BLOCK_HEADER_OVERHEAD) == 0) { ptr = mm_malloc_inst(sys_allocator, size); }
  }
  return ptr;
}

void mm_free(void* ptr) { if (sys_allocator) mm_free_inst(sys_allocator, ptr); }

void mm_destroy(void) {
  if (sys_allocator) { 
    mm_destroy_instance(sys_allocator);
    munmap(sys_heap_base, MAX_HEAP_SIZE);
    sys_allocator = NULL;
    sys_heap_base = NULL;
  }
}

void mm_reset_allocator(void) {
  if (sys_allocator) mm_destroy();
  mm_init();
}

void* mm_calloc(size_t nmemb, size_t size) {
  if (!sys_allocator) if (mm_init() != 0) return NULL;
  return mm_calloc_inst(sys_allocator, nmemb, size);
}

void* mm_realloc(void*ptr, size_t size) {
  if (!ptr) return mm_malloc(size);
  if (size == 0) { mm_free(ptr); return NULL; }
  
  if (!sys_allocator) return NULL;

  int status = try_realloc_inplace(sys_allocator, ptr, size);
  
  if (status == 0) return ptr; /* In-place success */
  if (status == -1) {
    fprintf(stderr, "[Memoman] Error: Invalid pointer in realloc()\n");
    return NULL;
  }

  /* Fallback: Allocate new, copy, free old */
  /* We use mm_malloc (global) here to support heap growth if needed */
  void* new_ptr = mm_malloc(size);
  if (new_ptr) {
    size_t old_usable = mm_get_usable_size(sys_allocator, ptr);
    memcpy(new_ptr, ptr, (old_usable < size) ? old_usable : size);
    mm_free_inst(sys_allocator, ptr);
  }
  return new_ptr;
}

void* mm_memalign(size_t alignment, size_t size) {
  if (!sys_allocator) if (mm_init() != 0) return NULL;
  return mm_memalign_inst(sys_allocator, alignment, size);
}

int mm_validate(void) {
  if (!sys_allocator) return 1;
  return mm_validate_inst(sys_allocator);
}

size_t mm_get_usable_size(mm_allocator_t* allocator, void* ptr) {
  if (!allocator || !ptr) return 0;

  /* Large blocks (mmap) */
  if ((char*)ptr < allocator->heap_start || (char*)ptr >= allocator->heap_end) {
    large_block_t* lb = find_large_block(allocator, ptr);
    if (!lb) return 0;
    return lb->size - sizeof(large_block_t);
  }

  /* Main heap blocks */
  if ((uintptr_t)ptr % ALIGNMENT != 0) return 0;
  tlsf_block_t* block = user_to_block(ptr);
  if (!block_check_magic(block)) return 0;
  if (block_is_free(block)) return 0;
  return block_size(block);
}

size_t mm_malloc_usable_size(void* ptr) {
  if (!sys_allocator) return 0;
  return mm_get_usable_size(sys_allocator, ptr);
}

size_t mm_get_free_space_inst(mm_allocator_t* allocator) {
  if (!allocator) return 0;
  size_t total = 0;
  tlsf_block_t* curr = (tlsf_block_t*)allocator->heap_start;
  /* Skip Prologue */
  if (curr && block_size(curr) == 0) curr = block_next_safe(allocator, curr);
  while (curr && (char*)curr < allocator->heap_end) {
    size_t sz = block_size(curr);
    if (sz == 0) break;
    if (block_is_free(curr)) total += sz;
    curr = block_next_safe(allocator, curr);
  }
  /* include large blocks' free space as 0 (they are allocated outside) */
  return total;
}

size_t mm_get_total_allocated_inst(mm_allocator_t* allocator) {
  if (!allocator) return 0;
  size_t total = 0;
  tlsf_block_t* curr = (tlsf_block_t*)allocator->heap_start;
  /* Skip Prologue */
  if (curr && block_size(curr) == 0) curr = block_next_safe(allocator, curr);
  while (curr && (char*)curr < allocator->heap_end) {
    size_t sz = block_size(curr);
    if (sz == 0) break;
    if (!block_is_free(curr)) total += sz;
    curr = block_next_safe(allocator, curr);
  }
  /* Add mmap'd large blocks
     Note: large_blocks store total mmap size; user-usable is size - header */
  large_block_t* lb = allocator->large_blocks;
  while (lb) { total += (lb->size - sizeof(large_block_t)); lb = lb->next; }
  return total;
}

void mm_print_heap_stats_inst(mm_allocator_t* allocator) {
  if (!allocator) return;
  size_t free = mm_get_free_space_inst(allocator);
  size_t alloc = mm_get_total_allocated_inst(allocator);
  char buf1[32], buf2[32];
  if (free >= 1024*1024) snprintf(buf1, sizeof(buf1), "%zuMB", free / (1024*1024));
  else if (free >= 1024) snprintf(buf1, sizeof(buf1), "%zuKB", free / 1024);
  else snprintf(buf1, sizeof(buf1), "%zuB", free);

  if (alloc >= 1024*1024) snprintf(buf2, sizeof(buf2), "%zuMB", alloc / (1024*1024));
  else if (alloc >= 1024) snprintf(buf2, sizeof(buf2), "%zuKB", alloc / 1024);
  else snprintf(buf2, sizeof(buf2), "%zuB", alloc);

  printf("Heap stats: free=%s allocated=%s\n", buf1, buf2);
}

void mm_print_free_list_inst(mm_allocator_t* allocator) { (void)allocator; /* No-op simple implementation for tests */ }

size_t mm_get_free_space(void) { if (!sys_allocator) return 0; return mm_get_free_space_inst(sys_allocator); }
size_t mm_get_total_allocated(void) { if (!sys_allocator) return 0; return mm_get_total_allocated_inst(sys_allocator); }
void mm_print_heap_stats(void) { if (!sys_allocator) return; mm_print_heap_stats_inst(sys_allocator); }
void mm_print_free_list(void) { if (!sys_allocator) return; mm_print_free_list_inst(sys_allocator); }