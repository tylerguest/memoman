#define _GNU_SOURCE

#include "memoman.h"
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

/* Configuration */
#define ALIGNMENT 8
#define LARGE_ALLOC_THRESHOLD (1024 * 1024)

/* Legacy - to be removed */
#define COALESCE_THRESHOLD 10
#define NUM_SIZE_CLASSES 21
#define NUM_FREE_LISTS 8

/* Heap management */
#define INITIAL_HEAP_SIZE (1024 * 1024)
#define MAX_HEAP_SIZE (1024 * 1024 * 1024)
#define HEAP_GROWTH_FACTOR 2

/* TLSF Control */
static tlsf_control_t* tlsf_ctrl = NULL;

/* Large block tracking */
static large_block_t* large_blocks = NULL;

/* Legacy globals - to be removed*/
char* heap = NULL;
char* current = NULL;
static size_t heap_size = 0;
size_t heap_capacity = 0;
static size_t total_allocated = 0;
block_header_t* free_list[NUM_FREE_LISTS] = { NULL };
static block_header_t* size_classes[NUM_SIZE_CLASSES] = { NULL };


/* Utilities */
static inline size_t align_size(size_t size) { 
  return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1); 
}

static inline void* header_to_user(block_header_t* header) { 
  return (char*)header + sizeof(block_header_t); 
}
static inline block_header_t* user_to_header(void* ptr) { 
  return (block_header_t*)((char*)ptr - sizeof(block_header_t)); 
}

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
static inline int mapping_sli(size_t size, int fl) { return (int)((size >> (fl - TLSF_SLI)) & (TLSF_SLI_COUNT -1 )); }

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
    *fl = mapping_fli(size);
    *sl = mapping_sli(size, *fl);
    *fl -= TLSF_FLI_OFFSET;  // adjust for some mininum block size
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
static inline tlsf_block_t* block_next(tlsf_block_t* block) { return (tlsf_block_t*)((char*)block + sizeof(tlsf_block_t) + block_size(block)); }

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
  if (next) { block_set_prev_used(next); }
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

/* Allocation */

/*
 * Reserve-then-commit strategy: reserves 1GB address space but only commits
 * initial 1MB as PROT_READ | PROT_WRITE. Heap grows via mprotect (not mremap)
 * to prevent address changes that would invalidate user pointers.
 */
int mm_init(void) {
  if (heap != NULL) return 0;  

  heap = mmap(NULL, MAX_HEAP_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

  if (heap == MAP_FAILED) {
    heap = NULL;
    return -1;
  }

  if (mprotect(heap, INITIAL_HEAP_SIZE, PROT_READ | PROT_WRITE) != 0) {
    munmap(heap, MAX_HEAP_SIZE);
    heap = NULL;
    return -1;
  }

  heap_capacity = INITIAL_HEAP_SIZE;
  current = heap;
  heap_size = 0;
  return 0;
}

void mm_destroy(void) {
  if (heap != NULL) {
    munmap(heap, heap_capacity);
    heap = NULL;
    heap_capacity = 0;
    heap_size = 0;
    current = NULL;
  }

  for (int i = 0; i < NUM_FREE_LISTS; i++) free_list[i] = NULL;
  for (int i = 0; i < NUM_SIZE_CLASSES; i++) size_classes[i] = NULL;
}

/*
 * Expands heap capacity by doubling (or more if needed).
 * Uses mprotect on pre-reserved address space to avoid heap relocation.
 */
static int grow_heap(size_t min_additional) {
  size_t new_capacity = heap_capacity * HEAP_GROWTH_FACTOR;
  while (new_capacity < heap_capacity + min_additional) { new_capacity *= HEAP_GROWTH_FACTOR; }
  if (new_capacity > MAX_HEAP_SIZE) { new_capacity = MAX_HEAP_SIZE; }
  if (new_capacity <= heap_capacity) { return -1; } // can't grow further
  
  if (mprotect(heap, new_capacity, PROT_READ | PROT_WRITE) != 0) { return -1; }
  
  heap_capacity = new_capacity;
  return 0;
}

/*
 * Four-tier allocation strategy:
 * 1. Large blocks (>=1MB): direct mmap to avoid fragmenting main heap
 * 2. Size classes (<=2KB): 0(1) exact-fit from segregated lists
 * 3. Free lists (>2KB): best-fit with splitting to minimize waste
 * 4. Bump allocation: linear allocation from heap top
 */
void* mm_malloc(size_t size) {
  if (heap == NULL) { if (mm_init() != 0) return NULL; }
  if (size == 0) return NULL;  

  // bypass allocator for large blocks to prevent fragmentation
  if (size >= LARGE_ALLOC_THRESHOLD) {
    size_t total_size = sizeof(large_block_t) + align_size(size);
    void* ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return NULL;

    large_block_t* block = (large_block_t*)ptr;
    block->size = total_size;
    block->next = large_blocks;
    large_blocks = block;

    return (char*)ptr + sizeof(large_block_t);
  }

  size = align_size(size);  

  int class = get_size_class(size);
  if (class >= 0) {
    void* ptr = pop_from_class(class);
    if (ptr) return ptr;
  } 

  int list_index = get_free_list_index(size);
  if (list_index >= 0) {
    block_header_t** prev_ptr = &free_list[list_index];
    block_header_t* current_block = free_list[list_index];
    block_header_t* best_fit = NULL;
    block_header_t** best_prev = NULL;  
    while (current_block != NULL) {
      if (current_block->size >= size) {
        if (best_fit == NULL || current_block->size < best_fit->size) {
          best_fit = current_block;
          best_prev = prev_ptr;
        }
      }
      prev_ptr = &current_block->next;
      current_block = current_block->next;
    }

    if (best_fit) {
      *best_prev = best_fit->next;  

      size_t remaining_size = best_fit->size - size;
      size_t min_split_size = sizeof(block_header_t) + ALIGNMENT;  
      
      // split block only if remainder is usable
      if (remaining_size >= min_split_size) {
        char* split_point = (char*)header_to_user(best_fit) + size;
        block_header_t* new_block = (block_header_t*)split_point;
        new_block->size = remaining_size - sizeof(block_header_t);
        new_block->is_free = 1;
        int new_list_index = get_free_list_index(new_block->size);
        new_block->next = free_list[new_list_index];
        free_list[new_list_index] = new_block;
        best_fit->size = size;
      }  
      
      best_fit->is_free = 0;
      best_fit->next = NULL;
      
      return header_to_user(best_fit);
    }
  }  

  size_t total_size = sizeof(block_header_t) + size;  
  
  if (current + total_size > heap + heap_capacity) {  if (grow_heap(total_size) != 0) { return NULL; } }  
  
  block_header_t* header = (block_header_t*)current;
  header->size = size;
  header->is_free = 0;
  header->next = NULL;  
  current += total_size;
  total_allocated += total_size;  
  
  return header_to_user(header);
}

/*
 * Returns blocks to appropriate free list (or size class for small blocks).
 * Performs forward and backward coalescing to reduce fragmentation.
 * Large blocks (>=1MB) are unmapped immediately.
 */
void mm_free(void* ptr) {
  if (ptr == NULL) return;
  
  // check if this is a large allocation that should be unmapped
  large_block_t** large_prev = &large_blocks;
  large_block_t* large_curr = large_blocks;

  while(large_curr) {
    if ((char*)large_curr + sizeof(large_block_t) == ptr) {
      *large_prev = large_curr->next;
      munmap(large_curr, large_curr->size);
      return;
    }
    large_prev = &large_curr->next;
    large_curr = large_curr->next;
  }

  block_header_t* header = user_to_header(ptr);
  header->is_free = 1;  
  int class = get_size_class(header->size);

  if (class >= 0) {
    header->next = size_classes[class];
    size_classes[class] = header;
    return;
  } 

  // insert into free list sorted by address for efficient coalescing
  int list_index = get_free_list_index(header->size);
  block_header_t* curr = free_list[list_index];
  block_header_t* prev = NULL;
  
  while (curr && (char*)curr < (char*)header) {
    prev = curr;
    curr = curr->next;
  }

  header->next = curr;
  if (prev) prev->next = header;
  else free_list[list_index] = header;  
  
  // forward coalescing: merge with physically adjacent next block
  char* block_end = (char*)header + sizeof(block_header_t) + header->size;
  block_header_t* next = (block_header_t*)block_end;
  
  // bounds check required because block_end could be at heap boundary
  if (heap != NULL &&
      (char*)next >= heap &&
      (char*)next < heap + heap_capacity &&
      (char*)next + sizeof(block_header_t) <= heap + heap_capacity &&
      next->is_free) {
    int next_list_index = get_free_list_index(next->size);

    block_header_t* next_curr = free_list[next_list_index];
    block_header_t* next_prev = NULL;

    while (next_curr) {
      if (next_curr == next) {
        if (next_prev) next_prev->next = next_curr->next;
        else free_list[next_list_index] = next_curr->next;
        break;
      }
      next_prev = next_curr;
      next_curr = next_curr->next;
    }
    header->size += sizeof(block_header_t) + next->size;
    header->next = next->next;
  }
  
  // backward coalescing: merge with physically adjacent previous block
  curr = free_list[list_index];
  prev = NULL;
  
  while (curr) {
    char* curr_end = (char*)curr + sizeof(block_header_t) + curr->size;
    
    if (curr_end == (char*)header && curr->is_free) {
      curr->size += sizeof(block_header_t) + header->size;
      curr->next = header->next;
      if (prev) prev->next = curr;
      else free_list[list_index] = curr;
      break;
    }

    prev = curr;
    curr = curr->next;
  }
}

/* Management */
size_t get_total_allocated(void) { return total_allocated; }
size_t get_free_space(void) { return heap_capacity - (current - heap); }
block_header_t* get_free_list(void) { return free_list[0]; }

void reset_allocator(void) {
  while (large_blocks) {
    large_block_t* next = large_blocks->next;
    munmap(large_blocks, large_blocks->size);
    large_blocks = next;
  }

  if (heap) {
    current = heap;
    heap_size = 0;

    for (int i = 0; i < NUM_FREE_LISTS; i++) free_list[i] = NULL;
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) size_classes[i] = NULL;
  }
}