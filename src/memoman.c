#define _GNU_SOURCE

#include "memoman.h"
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>

/* ===================== */
/* === Configuration === */
/* ===================== */

#define INITIAL_HEAP_SIZE (1024 * 1024)
#define MAX_HEAP_SIZE (1024 * 1024 * 1024)
#define HEAP_GROWTH_FACTOR 2

/* ======================== */
/* === Global Singleton === */
/* ======================== */

mm_allocator_t* sys_allocator = NULL;
char* sys_heap_base = NULL; 
size_t sys_heap_cap = 0;

/* ========================= */
/* === Utility Functions === */
/* ========================= */

static inline size_t align_size(size_t size) { return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1); }
static inline void* block_to_user(tlsf_block_t* block) { return (char*)block + BLOCK_HEADER_OVERHEAD; }
static inline tlsf_block_t* user_to_block(void* ptr) { return (tlsf_block_t*)((char*)ptr - BLOCK_HEADER_OVERHEAD); }

/* Bitwise Ops */
static inline int fls_generic(size_t word) { return (sizeof(size_t) * 8) - 1 - __builtin_clzl(word); }
static inline int ffs_generic(uint32_t word) {
  int result = __builtin_ffs(word);
  return result ? result - 1 : -1;
}

/* ============================== */
/* === TLSF Mapping Functions === */
/* ============================== */

static inline void mapping(size_t size, int* fl, int* sl) {
  if (size < TLSF_MIN_BLOCK_SIZE) { *fl = 0; *sl = 0; }
  else {
    int fli = fls_generic(size);
    *fl = fli - TLSF_FLI_OFFSET;
    *sl = (int)((size >> (fli - TLSF_SLI)) & (TLSF_SLI_COUNT - 1));

    if (*fl < 0) { *fl = 0; *sl = 0; }
    if (*fl >= TLSF_FLI_MAX) { *fl = TLSF_FLI_MAX - 1; *sl = TLSF_SLI_COUNT - 1; }
  }
}

/* Wrapper for testing mapping logic externally */
void mm_get_mapping_indices(size_t size, int* fl, int* sl) { mapping(size, fl, sl); }

/* ================================= */
/* === Instance-Based Bitmap Ops === */
/* ================================= */

static inline void set_fl_bit(mm_allocator_t* ctrl, int fl) { ctrl->fl_bitmap |= (1U << fl); }
static inline void set_sl_bit(mm_allocator_t* ctrl, int fl, int sl) { ctrl->sl_bitmap[fl] |= (1U << sl); }
static inline void clear_fl_bit(mm_allocator_t* ctrl, int fl) { ctrl->fl_bitmap &= ~(1U << fl); }
static inline void clear_sl_bit(mm_allocator_t* ctrl, int fl, int sl) { ctrl->sl_bitmap[fl] &= ~(1U << sl); }

static inline int find_suitable_fl(mm_allocator_t* ctrl, int fl) {
  uint32_t mask = ctrl->fl_bitmap & (~0U << fl);
  return mask ? ffs_generic(mask) : -1;
}

static inline int find_suitable_sl(mm_allocator_t* ctrl, int fl, int sl) {
  uint32_t mask = ctrl->sl_bitmap[fl] & (~0U << sl);
  return mask ? ffs_generic(mask) : -1;
}

/* =============================== */
/* === Block Utility Functions === */
/* =============================== */

static inline size_t block_size(tlsf_block_t* block) { return block->size & TLSF_SIZE_MASK; }
static inline int block_is_free(tlsf_block_t* block) { return (block->size & TLSF_BLOCK_FREE) != 0; }
static inline int block_is_prev_free(tlsf_block_t* block) { return (block->size & TLSF_PREV_FREE) != 0; }
static inline void block_set_size(tlsf_block_t* block, size_t size) {
  size_t flags = block->size & ~TLSF_SIZE_MASK;
  block->size = size | flags;
}
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

static inline tlsf_block_t* block_prev(tlsf_block_t* block) { return block->prev_phys; }

static inline tlsf_block_t* block_next_safe(mm_allocator_t* ctrl, tlsf_block_t* block) {
  tlsf_block_t* next = (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + block_size(block));
  if ((char*)next >= ctrl->heap_end) return NULL;
  return next;
}

static inline void block_mark_as_free(mm_allocator_t* ctrl, tlsf_block_t* block) {
  block_set_free(block);
  tlsf_block_t* next = block_next_safe(ctrl, block);
  if (next) block_set_prev_free(next);
}

/* ================================== */
/* === List Management (Instance) === */
/* ================================== */

static inline void insert_free_block(mm_allocator_t* ctrl, tlsf_block_t* block) {
  int fl, sl;
  mapping(block_size(block), &fl, &sl);

  tlsf_block_t* head = ctrl->blocks[fl][sl];
  block->next_free = head;
  block->prev_free = NULL;

  if (head) { head->prev_free = block; }
  ctrl->blocks[fl][sl] = block;

  set_fl_bit(ctrl, fl);
  set_sl_bit(ctrl, fl, sl);
}

static inline void remove_free_block_direct(mm_allocator_t* ctrl, tlsf_block_t* block, int fl, int sl) {
  tlsf_block_t* prev = block->prev_free;
  tlsf_block_t* next = block->next_free;

  if (prev) { prev->next_free = next ;}
  else {
    ctrl->blocks[fl][sl] = next;
    if (!next) {
      clear_sl_bit(ctrl, fl, sl);
      if (ctrl->sl_bitmap[fl] == 0) { clear_fl_bit(ctrl, fl); }
    }
  }
  if (next) { next->prev_free = prev; }
}

static inline void remove_free_block(mm_allocator_t* ctrl, tlsf_block_t* block) {
  int fl, sl;
  mapping(block_size(block), &fl, &sl);
  remove_free_block_direct(ctrl, block, fl, sl);
}

static inline tlsf_block_t* search_suitable_block(mm_allocator_t* ctrl, size_t size, int* out_fl, int* out_sl) {
  int fl, sl;
  mapping(size, &fl, &sl);

  int sl_found = find_suitable_sl(ctrl, fl, sl);
  if (sl_found >= 0) {
    *out_fl = fl; *out_sl = sl_found;
    return ctrl->blocks[fl][sl_found];
  }

  int fl_found = find_suitable_fl(ctrl, fl + 1);
  if (fl_found < 0) return NULL;

  sl_found = find_suitable_sl(ctrl, fl_found, 0);
  *out_fl = fl_found; *out_sl = sl_found;
  return ctrl->blocks[fl_found][sl_found];
}

/* =================================== */
/* === Coalescing/Splitting (Inst) === */
/* =================================== */

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
  remainder->prev_phys = block;

  block_set_prev_used(remainder);

  tlsf_block_t* next = block_next_safe(ctrl, remainder);
  if (next) {
    next->prev_phys = remainder;
    block_set_prev_free(next);
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
      if (next) next->prev_phys = prev;

      block = prev;
    }
  }

  tlsf_block_t* next = block_next_safe(ctrl, block);
  if (next && block_is_free(next)) {
    remove_free_block(ctrl, next);
    size_t combined = block_size(block) + BLOCK_HEADER_OVERHEAD + block_size(next);
    block_set_size(block, combined);

    tlsf_block_t* next_next = block_next_safe(ctrl, block);
    if (next_next) next_next->prev_phys = block;
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

/* =========================== */
/* === Public Instance API === */
/* =========================== */

int mm_validate_inst(mm_allocator_t* ctrl) {
  if (!ctrl) return 0;

  #define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "[Memoman] Integrity Failure: %s\n", msg); return 0; } } while(0)

  /* 1. Physical Heap Walk */
  tlsf_block_t* prev = NULL;
  tlsf_block_t* curr = (tlsf_block_t*)ctrl->heap_start;
  int found_epilogue = 0;

  while (curr && (char*)curr < ctrl->heap_end) {
    /* Check Alignment & Bounds */
    CHECK(((uintptr_t)curr % ALIGNMENT == 0), "Block not aligned");
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
        CHECK(curr->prev_phys == prev, "Ghost prev_phys pointer invalid");
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
  block->prev_phys = prologue;
  block->size = 0; /* Initialize flags */
  block_set_prev_used(block); /* Prologue is used */
  epilogue->prev_phys = block;
  
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

  size = align_size(size);
  if (size < TLSF_MIN_BLOCK_SIZE) size = TLSF_MIN_BLOCK_SIZE;

  int fl, sl;
  tlsf_block_t* block = search_suitable_block(ctrl, size, &fl, &sl);
  if (!block) return NULL;

  remove_free_block_direct(ctrl, block, fl, sl);
  tlsf_block_t* remainder = split_block(ctrl, block, size);
  if (remainder) insert_free_block(ctrl, remainder);

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

  if (block_is_free(block)) return;  /* Double free protection */

  block_mark_as_free(ctrl, block);
  block = coalesce(ctrl, block);
  insert_free_block(ctrl, block);
  mm_check_integrity(ctrl);
}

/* =========================== */
/* === Usable Size Helpers === */
/* =========================== */

size_t mm_get_usable_size(mm_allocator_t* allocator, void* ptr) {
  if (!ptr || !allocator) return 0;

  if ((char*)ptr < allocator->heap_start || (char*)ptr >= allocator->heap_end) {
    large_block_t* lb = find_large_block(allocator, ptr);
    if (lb) return lb->size - sizeof(large_block_t);
    return 0;
  }
  tlsf_block_t* block = user_to_block(ptr);
  return block_size(block);
}

size_t mm_malloc_usable_size(void* ptr) {
  if (!sys_allocator) return 0;
  return mm_get_usable_size(sys_allocator, ptr);
}

void mm_print_heap_stats(void) {
  if (!sys_allocator) { printf("Allocator not initialized.\n"); return; }
  printf("Heap: %p - %p (%zu bytes)\n", sys_allocator->heap_start, sys_allocator->heap_end, (size_t)(sys_allocator->heap_end - sys_allocator->heap_start));
}

size_t mm_get_free_space(void) {
  if (!sys_allocator) return 0;
  size_t free_space = 0;
  for (int fl = 0; fl < TLSF_FLI_MAX; fl++) {
    for (int sl = 0; sl < TLSF_SLI_COUNT; sl++) {
      tlsf_block_t* block = sys_allocator->blocks[fl][sl];
      while (block) {
        free_space += block_size(block);
        block = block->next_free;
      }
    }
  }
  return free_space;
}

size_t mm_get_total_allocated(void) {
  if (!sys_allocator) return 0;
  size_t total_heap = (size_t)(sys_allocator->heap_end - sys_allocator->heap_start);
  return total_heap - mm_get_free_space();
}

void mm_print_free_list(void) {
  if (!sys_allocator) {
    printf("Allocator not initialized.\n");
    return;
  }
  printf("\n=== Free List ===\n");
  for (int fl = 0; fl < TLSF_FLI_MAX; fl++) {
    for (int sl = 0; sl < TLSF_SLI_COUNT; sl++) {
      tlsf_block_t* block = sys_allocator->blocks[fl][sl];
      if (block) {
        printf("FL%d SL%d: ", fl, sl);
        while (block) {
          printf("[%zu] ", block_size(block));
          block = block->next_free;
        }
        printf("\n");
      }
    }
  }
  printf("=================\n");
}

void mm_reset_allocator(void) {
  mm_destroy();
  mm_init();
}

/* ========================== */
/* === Global Wrapper API === */
/* ========================== */

static int global_grow_heap(size_t min_additional) {
  if (!sys_allocator) return -1;

  size_t old_cap = sys_heap_cap;
  size_t new_cap = old_cap * HEAP_GROWTH_FACTOR;
  while (new_cap < old_cap + min_additional) new_cap *= HEAP_GROWTH_FACTOR;
  if (new_cap > MAX_HEAP_SIZE) new_cap = MAX_HEAP_SIZE;
  if (new_cap <= old_cap) return -1;

  if (mprotect(sys_heap_base, new_cap, PROT_READ | PROT_WRITE) != 0) return -1;

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
  new_epilogue->prev_phys = block;

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

void* mm_calloc(size_t nmemb, size_t size) {
  if (nmemb != 0 && size > SIZE_MAX / nmemb) return NULL;
  size_t total = nmemb * size;
  void* p = mm_malloc(total);
  if (p) memset(p, 0, total);
  return p;
}

void* mm_realloc(void*ptr, size_t size) {
  if (!ptr) return mm_malloc(size);
  if (size == 0) { mm_free(ptr); return NULL; }
  
  if (!sys_allocator) return NULL;
  mm_check_integrity(sys_allocator);

  /* Handle Main Heap Blocks */
  if ((char*)ptr >= sys_allocator->heap_start && (char*)ptr < sys_allocator->heap_end) {
    if ((uintptr_t)ptr % ALIGNMENT != 0) return NULL;

    tlsf_block_t* block = user_to_block(ptr);
    if (!block_check_magic(block)) return NULL;

    size_t current_size = block_size(block);
    size_t aligned_size = align_size(size);
    if (aligned_size < TLSF_MIN_BLOCK_SIZE) aligned_size = TLSF_MIN_BLOCK_SIZE;

    /* Case 1: Shrink or Same Size */
    if (aligned_size <= current_size) {
      tlsf_block_t* remainder = split_block(sys_allocator, block, aligned_size);
      if (remainder) insert_free_block(sys_allocator, remainder);
      mm_check_integrity(sys_allocator);
      return ptr;
    }

    /* Case 2: Grow (Try to coalesce with next block) */
    tlsf_block_t* next = block_next_safe(sys_allocator, block);
    if (next && block_is_free(next)) {
      size_t next_size = block_size(next);
      size_t combined = current_size + BLOCK_HEADER_OVERHEAD + next_size;
      
      if (combined >= aligned_size) {
        remove_free_block(sys_allocator, next);
        block_set_size(block, combined);
        
        tlsf_block_t* next_next = block_next_safe(sys_allocator, block);
        if (next_next) {
          next_next->prev_phys = block;
          block_set_prev_used(next_next);
        }

        tlsf_block_t* remainder = split_block(sys_allocator, block, aligned_size);
        if (remainder) insert_free_block(sys_allocator, remainder);
        mm_check_integrity(sys_allocator);
        return ptr;
      }
    }
  } 
  /* Handle Large Blocks */
  else {
    large_block_t* lb = find_large_block(sys_allocator, ptr);
    if (lb) {
      size_t current_size = lb->size - sizeof(large_block_t);
      if (size <= current_size) return ptr; /* Shrink in place (waste space but valid) */
      /* Fallthrough to malloc/copy/free for grow */
    } else {
      return NULL; /* Invalid pointer */
    }
  }

  /* Fallback: Allocate new, copy, free old */
  void* new_ptr = mm_malloc(size);
  if (new_ptr) {
    size_t old_usable = mm_malloc_usable_size(ptr);
    memcpy(new_ptr, ptr, (old_usable < size) ? old_usable : size);
    mm_free(ptr);
  }
  return new_ptr;
}

int mm_validate(void) {
  if (!sys_allocator) return 1;
  return mm_validate_inst(sys_allocator);
}