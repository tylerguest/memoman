#define _GNU_SOURCE

#include "memoman.h"
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

/* Configuration */
#define ALIGNMENT 8
#define LARGE_ALLOC_THRESHOLD (1024 * 1024)

/* Heap management */
#define INITIAL_HEAP_SIZE (1024 * 1024)
#define MAX_HEAP_SIZE (1024 * 1024 * 1024)
#define HEAP_GROWTH_FACTOR 2

/* TLSF Control */
tlsf_control_t* tlsf_ctrl = NULL;

/* Large block tracking */
static large_block_t* large_blocks = NULL;

/* Heap globals */
char* heap = NULL;
char* current = NULL;
size_t heap_capacity = 0;

/* Utilities */
static inline size_t align_size(size_t size) { return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1); }
static inline void* header_to_user(block_header_t* header) { return (char*)header + sizeof(block_header_t); }
static inline block_header_t* user_to_header(void* ptr) { return (block_header_t*)((char*)ptr - sizeof(block_header_t)); }

/* TLSF Bit Manipulation Functions*/

/* 
 * Find Last Set (FLS) - position of most significant bit
 * Uses GCC/Clang builtin. For other compilers, implement manual bit scan
 */
static inline int fls_generic(size_t word) { return (sizeof(size_t) * 8) - 1- __builtin_clzl(word); }

/*
 * Find First Set (FFS) - position of least significant bit
 * Returns 0-based index, or -1 if not bits set
 */
static inline int ffs_generic(uint32_t word) {
  int result = __builtin_ffs(word);
  return result ? result - 1 :  -1;
}

/*
 * Maps allocation size to first-level index (FLI)
 * FLI represents the power-of-2 class: log2(size)
 */
static inline int mapping_fli(size_t size) { return fls_generic(size); }

/*
 * Maps allocation size to second-level index (SLI)
 * SLI subdividese the FLI range into TLSF_SLI_COUNT bins
 * Extracts the next TLSF_SLI bits after the MSB
 */
static inline int mapping_sli(size_t size, int fl) { 
  if (fl < TLSF_SLI) return 0; /* Prevent negative shift */
  return (int)((size >> (fl - TLSF_SLI)) & (TLSF_SLI_COUNT -1 )); 
}

/*
 * Combined mapping function
 * Maps size to first-level and second-level indices
 * Handles mininum block size by adjusting FLI
 */
static inline void mapping(size_t size, int* fl, int* sl) {
  if (size < TLSF_MIN_BLOCK_SIZE) {
    *fl = 0;
    *sl = 0;
  } else {
    int fli = mapping_fli(size);
    *sl = mapping_sli(size, fli);
    *fl = fli - TLSF_FLI_OFFSET;

    /* Clamp to valid range */
    if (*fl < 0) {
      *fl = 0;
      *sl = 0;
    } else if (*fl >= TLSF_FLI_MAX) {
      *fl = TLSF_FLI_MAX - 1;
      *sl = TLSF_SLI_COUNT - 1;
    }
  }
}

/* TLSF Bitmap Operations */

/*
 * Set bit in first-level bitmap when list becomes non-empty
 */
static inline void set_fl_bit(tlsf_control_t* ctrl, int fl) { ctrl->fl_bitmap |= (1U << fl); }

/*
 * Set bit in second-level bitmap when list becomes empty
 */
static inline void set_sl_bit(tlsf_control_t* ctrl, int fl, int sl) { ctrl->sl_bitmap[fl] |= (1U << sl); }

/*
 * Clear bit in first-level bitmap when list becomes empty
 */
static inline void clear_fl_bit(tlsf_control_t* ctrl, int fl) { ctrl->fl_bitmap &= ~(1U << fl); }


/*
 * Clear bit in second-level bitmap when list becomes empty
 */
static inline void clear_sl_bit(tlsf_control_t* ctrl, int fl, int sl) { ctrl->sl_bitmap[fl] &= ~(1U << sl); }

/*
 * Find next non-empty first-level list at or after fl
 * Uses bitmap mask and FFS for 0(1) search
 * Returns -1 if no suitable list found
 */
static inline int find_suitable_fl(tlsf_control_t* ctrl, int fl) {
  uint32_t mask = ctrl->fl_bitmap & (~0U << fl);
  return mask ? ffs_generic(mask) : -1;
}

/*
 * Find next non-empty second-level list at or after sl
 * Uses bitmap mask and FFS for 0(1) search
 * Returns -1 if no suitable list found
 */
static inline int find_suitable_sl(tlsf_control_t* ctrl, int fl, int sl) {
  uint32_t mask = ctrl->sl_bitmap[fl] & (~0U << sl);
  return mask ? ffs_generic(mask) : -1;
}

/* TLSF Block Utility Functions */

/*
 * Extract block size without flags
 * Size field stores size in upper bits, flags in lower 2 bits
 */
static inline size_t block_size(tlsf_block_t* block) { return block->size & TLSF_SIZE_MASK; }

/*
 * Check if block is free
 */
static inline int block_is_free(tlsf_block_t* block) { return block->size & TLSF_BLOCK_FREE; }

/*
 * Check if previous physical block is free
 */
static inline int block_is_prev_free(tlsf_block_t* block) { return block->size & TLSF_PREV_FREE; }

/*
 * Mark block as free (set flag)
 */
static inline void block_set_free(tlsf_block_t* block) { block->size |= TLSF_BLOCK_FREE; }

/*
 * Mark block as used (clear free flag)
 */
static inline void block_set_used(tlsf_block_t* block) { block->size &= ~TLSF_BLOCK_FREE; }

/*
 * Mark previous physical block as free
 */
static inline void block_set_prev_free(tlsf_block_t* block) { block->size |= TLSF_PREV_FREE; }

/*
 * Mark previous physical block as used
 */
static inline void block_set_prev_used(tlsf_block_t* block) { block->size &= ~TLSF_PREV_FREE; }

/* TLSF Block Navigation */

/*
 * Get next physical block
 * Advances point by block header size plus blocks's data size
 */
static inline tlsf_block_t* block_next(tlsf_block_t* block) { 
  tlsf_block_t* next = (tlsf_block_t*)((char*)block + sizeof(tlsf_block_t) + block_size(block)); 

  /* Boundary check - don't return invalid pointer */
  if (tlsf_ctrl && (char*)next >= tlsf_ctrl->heap_end) { return NULL; }

  return next;
}

/*
 * Get previous physical block using boundary tag
 */
static inline tlsf_block_t* block_prev(tlsf_block_t* block) { return block->prev_phys; }

/*
 * Convert block header to user pointer
 * User data starts immediately after block header
 */
static inline void* block_to_user(tlsf_block_t* block) { return (char*)block + sizeof(tlsf_block_t); }

/*
 * Convert user pointer to block header
 */
static inline void* user_to_block(void* ptr) { return (tlsf_block_t*)((char*)ptr - sizeof(tlsf_block_t)); }

/* TLSF Block Metadata Helpers */

/*
 * Set block size while preserving flags
 * Clears old size bits, keeps flag bits, set new size
 */
static inline void block_set_size(tlsf_block_t* block, size_t size) {
  size_t flags = block->size & ~TLSF_SIZE_MASK;
  block->size = size | flags;
}

/*
 * Mark block as free and update next block's prev_free flag
 * This is the proper way to free a block in TLSF
 */
static inline void block_mark_as_free(tlsf_block_t* block) {
  block_set_free(block);

  /* Update next block's prev_free flag */
  tlsf_block_t* next = block_next(block);
  if (next) { block_set_prev_free(next); }
}

/* TLSF Free List Management */

/*
 * Insert block into appropriate segregated free list
 * 0(1) operation - inserts at head of list, updates bitmaps
 */
static inline void insert_free_block(tlsf_control_t* ctrl, tlsf_block_t* block) {
  int fl, sl;
  mapping(block_size(block), &fl, &sl);

  /* Insert at head of list */
  tlsf_block_t* current_head = ctrl->blocks[fl][sl];
  block->next_free = current_head;
  block->prev_free = NULL;

  if (current_head) { current_head->prev_free = block; }

  ctrl->blocks[fl][sl] = block;

  /* Update bitmaps to mark list as non-empty */
  set_fl_bit(ctrl, fl);
  set_sl_bit(ctrl, fl, sl);
}

/*
 * Remove block from its segregated free list
 * O(1) operation - uses doubly-linbked list pointers
 * Updates bitmaps when list becomes empty
 */
static inline void remove_free_block(tlsf_control_t* ctrl, tlsf_block_t* block) {
  int fl, sl;
  mapping(block_size(block), &fl, &sl);

  tlsf_block_t* prev = block->prev_free;
  tlsf_block_t* next = block->next_free;

  /* Update previous block's next pointer (or list head) */
  if (prev) { prev->next_free = next ;}
  else {
    /* Removing head of list */
    ctrl->blocks[fl][sl] = next;

    /* Update bitmaps if list becomes empty */
    if (next == NULL) {
      clear_sl_bit(ctrl, fl, sl);

      /* Check if entire FL is now empty */
      if (ctrl->sl_bitmap[fl] == 0) { clear_fl_bit(ctrl, fl); }
    }
  }

  /* Update next block's prev pointer */
  if (next) { next->prev_free = prev; }

  /* Clear block's free list pointers */
  block->next_free = NULL;
  block->prev_free = NULL;
}

/*
 * Search for suitable free block using TLSF two-level bitmap search
 * O(1) operation - uses bitmaps to find smallest sufficient block
 * 
 * Strategy:
 * 1. Map requested size to FL/SL
 * 2. Search same FL for block >= requested SL
 * 3. If not found, search next non-empty FL
 * 4. Return first block from found list (guaranteed to fit)
 */
static inline tlsf_block_t* search_suitable_block(tlsf_control_t* ctrl, size_t size) {
  int fl, sl;
  mapping(size, &fl, &sl);

  /* Search for block in same FL, starting at SL */
  int sl_found = find_suitable_sl(ctrl, fl, sl);

  if (sl_found >= 0) {
    /* Found in same FL */
    return ctrl->blocks[fl][sl_found];
  }

  /* No suitable block in same FL - search next FL */
  int fl_found = find_suitable_fl(ctrl, fl + 1);

  if (fl_found < 0) {
    /* No suitable block found in entire allocator */
    return NULL;
  }

  /* Get first non-empty SL in the found FL */
  sl_found = find_suitable_sl(ctrl, fl_found, 0);

  if (sl_found < 0) {
    /* Bitmap inconsistency - should not happen */
    return NULL;
  }

  return ctrl->blocks[fl_found][sl_found];
}

/* TLSF Block Splititng & Coalescing */

/*
 * Split block if remainder is usable
 * Returns pointer to remainder block (not inserted into free list)
 * Returns NULL if block cannot be split (too small)
 *
 * Requirements for splitting:
 * - Remainder must fit TLSF_MIN_BLOCK_SIZE + block_header
 * - Original block is updated to requested size
 * - New block is created in remainder space
 */
static inline tlsf_block_t* split_block(tlsf_block_t* block, size_t size) {
  size_t block_total_size = block_size(block);
  size_t min_split_size = TLSF_MIN_BLOCK_SIZE + sizeof(tlsf_block_t);

  /* Check if remainder is usable */
  if (block_total_size < size + min_split_size) {
    /* Can't split - remainder too small */
    return NULL;
  }

  size_t remainder_size = block_total_size - size - sizeof(tlsf_block_t);

  /* Update original block size */
  block_set_size(block, size);

  /* Create new block in remainder */
  tlsf_block_t* remainder = (tlsf_block_t*)((char*)block + sizeof(tlsf_block_t) + size);
  block_set_size(remainder, remainder_size);
  block_set_free(remainder);

  /* Initialize free list pointers */
  remainder->next_free = NULL;
  remainder->prev_free = NULL;

  /* Update physical block linkage */
  remainder->prev_phys = block;

  /* Mark remainder's previous block (the allocated block) as used */
  block_set_prev_used(remainder);

  /* Upadate next block's prev_phys pointer */
  tlsf_block_t* next = block_next(remainder);
  if (next) { 
    next->prev_phys = remainder; 
    /* Update next block's prev_phys pointer */
    block_set_prev_free(next);
  } else {
    /* Remainder is now the last physical block */
    if (tlsf_ctrl) { tlsf_ctrl->last_block = remainder; }
  }
  
  return remainder;
}

/*
 * Coalesce with previous physical block
 * Both blocks must be free and prev must be physically adjacent
 * Removes prev from free list, combines size, returns merged block
 * Caller must insert returned blockinto free list
 */
static inline tlsf_block_t* coalesce_prev(tlsf_control_t* ctrl, tlsf_block_t* block) {
  if(!block_is_prev_free(block)) { return block; }

  tlsf_block_t* prev = block_prev(block);
  if (!prev || !block_is_free(prev)) { return block; }
  
  /* Remove prev from free list */
  remove_free_block(ctrl, prev);

  /* Merge: prev absorbs current block */
  size_t combined_size = block_size(prev) + sizeof(tlsf_block_t) + block_size(block);
  block_set_size(prev, combined_size);

  /* Update next block's prev_phys to point to merged block */
  tlsf_block_t* next = block_next(prev);
  if (next) { next->prev_phys = prev; }

  return prev;
}

/*
 * Coalesce with next physical block
 * Both blocks must be free and physically adjacent
 * Removes next from free list, combines sizes, returns merged block
 * Caler must insert returned block into free list
 */
static inline tlsf_block_t* coalesce_next(tlsf_control_t* ctrl, tlsf_block_t* block) {
  tlsf_block_t* next = block_next(block);

  if (!next || !block_is_free(next)) { return block; }

  /* Check if we're absorbing the last block */
  int absorbing_last = (ctrl && ctrl->last_block == next);

  /* Remove next from free list */
  remove_free_block(ctrl, next);

  /* Merge: current block absrobs next */
  size_t combined_size = block_size(block) + sizeof(tlsf_block_t) + block_size(next);
  block_set_size(block, combined_size);

  /* Update next-next block's prev_phys to point to merged block */
  tlsf_block_t* next_next = block_next(block);
  if(next_next) { next_next->prev_phys = block; }

  /* Update last_block if we absorbed it */
  if (absorbing_last) { ctrl->last_block = block; }

  return block;
}

/*
 * Coalesce block with adjacent free blocks (immediate coalescing)
 * Tries to merge with both previous and next blocks
 * Removes merged blocks from free lists
 * Returns final coalesced block (caller must insert into free list)
 *
 * Strategy:
 * 1. Try coalescing with previous block
 * 2. Try coalescing with next block
 * 3. Return final merged block
 */
static inline tlsf_block_t* coalesce(tlsf_control_t* ctrl, tlsf_block_t* block) {
  /* Coalesce with previous block first */
  block = coalesce_prev(ctrl, block);

  /* Then coalesce with next block */
  block = coalesce_next(ctrl, block);

  return block;
}

/* TLSF Wilderness Management */

/*
 * Create free block from uncommitted/newly committed heap space
 * Searches for previous physical block to maintain boundary tags
 * Coalesces with adjacent free blocks automatically
 * Inserts final block intro TLSF free lists
 *
 * @param ctrl TLSF control structure
 * @param start Start address of new heap region
 * @param siuze Size of new heap region
 */
static void create_free_block(tlsf_control_t* ctrl, void* start, size_t size) {
  if (!ctrl || !start || size < sizeof(tlsf_block_t) + TLSF_MIN_BLOCK_SIZE) { return; }

  /* Create new block header */
  tlsf_block_t* block = (tlsf_block_t*)start;
  size_t block_data_size = size - sizeof(tlsf_block_t);
  block_set_size(block, block_data_size);
  block_set_free(block);
  
  /* Initialize free list pointers */
  block->next_free = NULL;
  block->prev_free = NULL;

  /* Find previous physical block by searching backward from start */
  tlsf_block_t* prev_block = ctrl->last_block;

  /* Verify it's actually adjacent (should always be true for heap growth) */
  if (prev_block) {
    char* prev_end = (char*)prev_block + sizeof(tlsf_block_t) + block_size(prev_block);
    if (prev_end != (char*)block) { prev_block = NULL;  /* Not adjacent - shouldn't happen in normal use */ }
  }

  /* Set up physical linkage */
  block->prev_phys = prev_block;
  
  if (prev_block) {
    /* Update prev_free flag based on previous block state */
    if (block_is_free(prev_block)) {
      block_set_prev_free(block);
    } else {
      block_set_prev_used(block);
    }
  } else {
    /* First block in heap - no previous blocks */
    block_set_prev_used(block);
  }

  /* This new block is now the last physical block */
  ctrl->last_block = block;

  /* Try to coalesce with adjacent blocks */
  block = coalesce(ctrl, block);

  /* After coalescing, the merged block is still the last block */
  ctrl->last_block = block;

  /* Insert into TLSF free lists */
  insert_free_block(ctrl, block);
}

/* Allocation */

/*
 * Reserve-then-commit strategy: reserves 1GB address space but only commits
 * initial 1MB as PROT_READ | PROT_WRITE. Heap grows via mprotect (not mremap)
 * to prevent address changes that would invalidate user pointers.
 *
 * TLSF initialization:
 * - Allocates control structure at start of heap
 * - Creates initial free block from remaining space
 */
int mm_init(void) {
  if (heap != NULL) return 0;  

  heap = mmap(NULL, MAX_HEAP_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

  if (heap == MAP_FAILED) {
    heap = NULL;
    return -1;
  }
  
  /* Commit initial 1MB */
  if (mprotect(heap, INITIAL_HEAP_SIZE, PROT_READ | PROT_WRITE) != 0) {
    munmap(heap, MAX_HEAP_SIZE);
    heap = NULL;
    return -1;
  }

  heap_capacity = INITIAL_HEAP_SIZE;
  current = heap;

  /* Allocate TLSF control structure at start of heap */
  tlsf_ctrl = (tlsf_control_t*)heap;
  current = heap + sizeof(tlsf_control_t);

  /* Zero out control structure (bitmaps and block matrix) */
  memset(tlsf_ctrl, 0, sizeof(tlsf_control_t));

  /* Set heap bounds */
  tlsf_ctrl->heap_start = current;
  tlsf_ctrl->heap_end = heap + heap_capacity;
  tlsf_ctrl->heap_capacity = heap_capacity;
  tlsf_ctrl->last_block = NULL;  /* Will be set by create_free_block */

  /* Create initial free block spanning usable heap */
  size_t initial_free_size = heap + heap_capacity - current;
  if (initial_free_size >= sizeof(tlsf_block_t) + TLSF_MIN_BLOCK_SIZE) { create_free_block(tlsf_ctrl, current, initial_free_size); }

  return 0;
}

void mm_destroy(void) {
  /* Cleanup large blocks */
  while (large_blocks) {
    large_block_t* next = large_blocks->next;
    large_blocks->magic = 0;  // Clear magic before unmap
    munmap(large_blocks, large_blocks->size);
    large_blocks = next;
  }

  /* Munmap main heap */
  if (heap != NULL) {
    munmap(heap, MAX_HEAP_SIZE); /* Unmap entire reserved region */
    heap = NULL;
    heap_capacity = 0;
    current = NULL;
  }

  /* Clear TLSF control pointer */
  tlsf_ctrl = NULL;
}

/*
 * Expands heap capacity by doubling (or more if needed).
 * Uses mprotect on pre-reserved address space to avoid heap relocation.
 */
static int grow_heap(size_t min_additional) {
  size_t old_capacity = heap_capacity;
  size_t new_capacity = heap_capacity * HEAP_GROWTH_FACTOR;
  
  while (new_capacity < heap_capacity + min_additional) { new_capacity *= HEAP_GROWTH_FACTOR; }
  
  if (new_capacity > MAX_HEAP_SIZE) { new_capacity = MAX_HEAP_SIZE; }
  
  if (new_capacity <= heap_capacity) { return -1; } // can't grow further
  
  /* Commit new region using mprotect (preserves base address) */
  if (mprotect(heap, new_capacity, PROT_READ | PROT_WRITE) != 0) { return -1; }
  
  heap_capacity = new_capacity;

  /* If TLSF is active, create free block in newly committed region */
  if (tlsf_ctrl) {
    size_t growth_size = new_capacity - old_capacity;
    void* new_region = heap + old_capacity;

    /* Update heap_end in control structure */
    tlsf_ctrl->heap_end = heap + new_capacity;

    /* Create free block spanning the newly committed region */
    create_free_block(tlsf_ctrl, new_region, growth_size);
  }

  return 0;
}

/*
 * TLSF allocation with dynamic heap growth
 *
 * Strategy:
 * 1. Large blocks (>=1MB): direct mmap bypass
 * 2. TLSF search: O(1) bitmap-based good-fit
 * 3. Block splitting: minimize waste
 * 4. Heap growth: expand when no suitable block found
 */
void* mm_malloc(size_t size) {
  /* Lazy initialization */
  if (heap == NULL) { if (mm_init() != 0) return NULL; }

  /* Edge case: zero size */
  if (size == 0) return NULL;

  /* Large block bypass - direct mmap to avoid fragmentation */
  if (size >= LARGE_ALLOC_THRESHOLD) {
    size_t total_size = sizeof(large_block_t) + align_size(size);
    void* ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return NULL;

    large_block_t* block = (large_block_t*)ptr;
    block->magic = LARGE_BLOCK_MAGIC; 
    block->size = total_size;
    block->prev = NULL;
    block->next = large_blocks;
    if (large_blocks) { large_blocks->prev = block; }
    large_blocks = block;

    return (char*)ptr + sizeof(large_block_t);
  }
  
  /* Align size and enforce minimum block size */
  size = align_size(size);
  if (size < TLSF_MIN_BLOCK_SIZE) { size = TLSF_MIN_BLOCK_SIZE; }

  /* Search for suitable block using TLSF bitmap search */
  tlsf_block_t* block = search_suitable_block(tlsf_ctrl, size);

  /* If no block found, grow heap and create new free block */
  if (block == NULL) {
    size_t needed = sizeof(tlsf_block_t) + size;
    if (grow_heap(needed) != 0) { return NULL; /* Heap growth failed */ }
    
    /* Try searching again after heap growth */
    block = search_suitable_block(tlsf_ctrl, size);
    if (block == NULL) { return NULL; /* Should not happen after successful growth */ } 
  }

  /* Remove block from free list */
  remove_free_block(tlsf_ctrl, block);

  /* Split block if it's significantly larger than needed */
  tlsf_block_t* remainder = split_block(block, size);
  if (remainder) { insert_free_block(tlsf_ctrl, remainder); /* Insert remainder back into free lists */ }
  
  /* Mark block as used */
  block_set_used(block);
  
  /* Update next block's prev_free flag */
  tlsf_block_t* next = block_next(block);
  if (next) { block_set_prev_used(next); }

  /* Return user pointer (skip past block header) */
  return block_to_user(block);
}

/*
 * TLSF deallocation with immediate coalescing
 * 
 * Strategy:
 * 1. Large blocks (>=1MB): direct mmap
 * 2. Mark block as free
 * 3. Coalesce with adjacent free blocks
 * 4. Insert into TLSF free lists
 */
void mm_free(void* ptr) {
  /* Edge case: NULL pointer */
  if (ptr == NULL) return;

  /* Skip if TLSF not initialized (shouldn't happen in normal use) */
  if (tlsf_ctrl == NULL) return;

  /* Check if pointer is within heap bounds */
  if ((char*)ptr >= tlsf_ctrl->heap_start && (char*)ptr < tlsf_ctrl->heap_end) {
    /* Within heap - handle as TLSF block */
    tlsf_block_t* block = (tlsf_block_t*)user_to_block(ptr);

    /* Additional validation: block header must also be within bounds */
    if ((char*)block < tlsf_ctrl->heap_start) {
      return; /* Block header would be before heap start */
    }

    /* Double-free detection: check if block is already free */
    if (block_is_free(block)) {
#ifdef DEBUG_OUTPUT
      fprintf(stderr, "mm_free: double-free detected at %p\n", ptr);
#endif
      return;  /* Already free - silently ignore to prevent corruption */
    }

    /* Mark block as free */
    block_set_free(block);

    /* Update next block's prev_free flag */
    tlsf_block_t* next = block_next(block);
    if (next && (char*)next < tlsf_ctrl->heap_end) { block_set_prev_free(next); }

    /* Coalesce with adjacent free blocks (immediate coalescing) */
    block = coalesce(tlsf_ctrl, block);

    /* Insert coalesced block into TLSF free lists */
    insert_free_block(tlsf_ctrl, block);
    return;
  }

  /* Pointer is outside heap - check if it's a large block */
  large_block_t* potential_large = (large_block_t*)((char*)ptr - sizeof(large_block_t));

  /* Walk large_blocks list to verify this is one of ours */
  large_block_t* lb = large_blocks;
  while (lb) {
    if (lb == potential_large) {
      /* Found it - safe to dereference now */
      if (lb->magic == LARGE_BLOCK_MAGIC) {
        /* O(1) doubly-linked list removal */
        if (lb->prev) { lb->prev->next = lb->next; }
        else { large_blocks = lb->next; }

        if (lb->next) { lb->next->prev = lb->prev; }

        lb->magic = 0;
        munmap(lb, lb->size);
        return;
      }
    }
    lb = lb->next;
  }

  /* Invalid pointer - not in heap and not a known large block */
#ifdef DEBUG_OUTPUT
  fprintf(stderr, "mm_free: invalid pointer %p (heap: %p-%p)\n", ptr, (void*)tlsf_ctrl->heap_start, (void*)tlsf_ctrl->heap_end);
#endif
}

/* Management */
size_t get_free_space(void) { 
  if (!tlsf_ctrl) return heap_capacity;
  
  size_t total_free = 0;

  for (int fl = 0; fl < TLSF_FLI_MAX; fl++) {
    if (!((tlsf_ctrl->fl_bitmap & (1U << fl)))) continue;

    for (int sl = 0; sl < TLSF_SLI_COUNT; sl++) {
      tlsf_block_t* block = tlsf_ctrl->blocks[fl][sl];

      while (block != NULL) {
        size_t size = block->size & TLSF_SIZE_MASK;
        total_free += size + sizeof(tlsf_block_t); /* Include header overhead */
        block = block->next_free;
      }
    }
  }

  return total_free; 
}

size_t mm_get_usable_size(void* ptr) {
  /* NULL pointer has no size */
  if (ptr == NULL) return 0;

  /* Check if TLSF is initialized */
  if (tlsf_ctrl == NULL) return 0;

  /* Check if pointer is within heap bounds (TLSF block) */
  if ((char*)ptr >= tlsf_ctrl->heap_start && (char*)ptr < tlsf_ctrl->heap_end) {
    tlsf_block_t* block = (tlsf_block_t*)user_to_block(ptr);

    /* Validate block header is within bounds */
    if ((char*)block < tlsf_ctrl->heap_start) return 0;

    /* Return the block's data size */
    return block_size(block);
  }

  /* Check if it's a large block (mmap'd) */
  large_block_t* potential_large = (large_block_t*)((char*)ptr - sizeof(large_block_t));

  /* Walk large_blocks list to verify */
  large_block_t* lb = large_blocks;
  while(lb) {
    if (lb == potential_large && lb->magic == LARGE_BLOCK_MAGIC) {
      /* Return usable size (total - header) */
      return lb->size - sizeof(large_block_t);
    }
    lb = lb->next;
  }

  /* Invalid pointer */
  return 0;
}

void* mm_calloc(size_t nmemb, size_t size) {
  /* check for multiplication overflow */
  if (nmemb != 0 && size > SIZE_MAX / nmemb) {
    return NULL; /* Overflow would occur */
  }

  size_t total = nmemb * size;

  /* Handle zero-size allocation */
  if (total == 0) { return NULL; }

  void* ptr = mm_malloc(total);
  if (ptr == NULL) { return NULL; }

  /* Zero-initialize the memory */
  memset(ptr, 0, total);
  
  return ptr;
}

void* mm_realloc(void* ptr, size_t size) {
  /* Edge case: NULL pointer - equivalent to malloc */
  if (ptr == NULL) { return mm_malloc(size); }

  /* Edge case: size == 0 - equivalent to free */
  if (size == 0) {
    mm_free(ptr);
    return NULL;
  }

  /* Get old block size */
  size_t old_size = mm_get_usable_size(ptr);
  if (old_size == 0) {
    /* Invalid pointer */
    return NULL;
  }

  /* Align requested size */
  size_t aligned_size = align_size(size);
  if (aligned_size < TLSF_MIN_BLOCK_SIZE) { aligned_size = TLSF_MIN_BLOCK_SIZE; }

  /* Check if this is a TLSF block or large block */
  int is_tlsf_block = ((char*)ptr >= tlsf_ctrl->heap_start && (char*)ptr < tlsf_ctrl->heap_end);

  /* In-place shrink for TLSF blocks */
  if (aligned_size <= old_size  && is_tlsf_block) {
    tlsf_block_t* block = user_to_block(ptr);
    size_t block_total_size = block_size(block);

    /* Check if remainder would be usable after split */
    size_t min_split_size = TLSF_MIN_BLOCK_SIZE + sizeof(tlsf_block_t);

    if (block_total_size >= aligned_size + min_split_size) {
      /* Split block in place - return same pointer */
      tlsf_block_t* remainder = split_block(block, aligned_size);

      if (remainder) {
        /* Mark remainder as free and insert into free lists */
        block_mark_as_free(remainder);

        /* Coalesce remainder with adjacent free blocks */
        remainder = coalesce(tlsf_ctrl, remainder);

        /* Insert into TLSF free lists */
        insert_free_block(tlsf_ctrl, remainder);
      }

      /* Return same pointer - no data movement needed */
      return ptr;
    }

    /* Remainder too small to split - keep existing block as-is */
    return ptr;
  }

  /* Phase 3.2: In-place grow for TLSF blocks */
  if (is_tlsf_block && aligned_size > old_size) {
    tlsf_block_t* block = user_to_block(ptr);
    tlsf_block_t* next = block_next(block);

    /* Check if next block exists, is free, and provides enough space */
    if (next && block_is_free(next)) {
      size_t current_size = block_size(block);
      size_t next_size = block_size(next);
      size_t combined_size = current_size + sizeof(tlsf_block_t) + next_size;

      /* Check if combined size is sufficient */
      if (combined_size >= aligned_size) {
        /* Remove next block from free list */
        remove_free_block(tlsf_ctrl, next);

        /* Absorb next block into current block */
        block_set_size(block, combined_size);

        /* Update physical linkage */
        tlsf_block_t* next_next = block_next(block);
        if (next_next) {
          next_next->prev_phys = block;
          block_set_prev_used(next_next);
        }

        /* Update last_block tracking if we absorbed the last block */
        if (tlsf_ctrl->last_block == next) { tlsf_ctrl->last_block = block; }

        /* Now try to split if we have excess space */
        size_t min_split_size = TLSF_MIN_BLOCK_SIZE + sizeof(tlsf_block_t);
        if (combined_size >= aligned_size + min_split_size) {
          tlsf_block_t* remainder = split_block(block, aligned_size);
          if (remainder) {
            block_mark_as_free(remainder);
            remainder = coalesce(tlsf_ctrl, remainder);
            insert_free_block(tlsf_ctrl, remainder);
          }
        }

        /* Return same pointer - grown in place! */
        return ptr;
      }
    }
  }

  /* Can't shrink in place (large block or growing) - fallback to malloc+copy+free */
  void* new_ptr = mm_malloc(size);
  if (new_ptr == NULL) {
    /* Allocation failed - original block unchanged */
    return NULL;
  }

  /* Copy data from old to new (up to mininum of old/new sizes) */
  size_t copy_size = (old_size < size) ? old_size : size;
  memcpy(new_ptr, ptr, copy_size);

  /* Free old block */
  mm_free(ptr);

  return new_ptr;
}

size_t get_total_allocated(void) { 
  if (!tlsf_ctrl) return 0;
  
  size_t free_space = get_free_space();
  size_t usable_heap = heap_capacity - sizeof(tlsf_control_t);

  if (free_space > usable_heap) return 0;
  return usable_heap - free_space;
}

void reset_allocator(void) {
  /* Cleanup large blocks */
  while (large_blocks) {
    large_block_t* next = large_blocks->next;
    large_blocks->magic = 0;  // Clear magic before unmap
    munmap(large_blocks, large_blocks->size);
    large_blocks = next;
  }

  if (heap && tlsf_ctrl) {
    /* Reset heap pointer to start of allcoatable space */
    current = tlsf_ctrl->heap_start;

    /* Zero out TLSF bitmaps */
    tlsf_ctrl->fl_bitmap = 0;
    memset(tlsf_ctrl->sl_bitmap, 0, sizeof(tlsf_ctrl->sl_bitmap));

    /* Zero out TLSF block matrix */
    memset(tlsf_ctrl->blocks, 0, sizeof(tlsf_ctrl->blocks));

    /* Reset last block tracking */
    tlsf_ctrl->last_block = NULL;

    /* Recreate initial free block */
    size_t initial_free_size = heap + heap_capacity - current;
    if (initial_free_size >= sizeof(tlsf_block_t) + TLSF_MIN_BLOCK_SIZE) { create_free_block(tlsf_ctrl, current, initial_free_size); }
  }

}