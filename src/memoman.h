#ifndef MEMOMAN_H
#define MEMOMAN_H

#include <stddef.h>
#include <stdint.h>

/* ========================== */
/* === Internal Constants === */
/* ========================== */

#define ALIGNMENT 16
#define LARGE_ALLOC_THRESHOLD (1024 * 1024)
#define INITIAL_HEAP_SIZE (1024 * 1024)
#define TLSF_MIN_BLOCK_SIZE 16
#define TLSF_FLI_MAX 30
#define TLSF_SLI 5
#define TLSF_SLI_COUNT (1 << TLSF_SLI)
#define TLSF_FLI_OFFSET 4

#define TLSF_BLOCK_FREE (1 << 0)
#define TLSF_PREV_FREE (1 << 1)
#define TLSF_SIZE_MASK (~(size_t)3)
#define LARGE_BLOCK_MAGIC 0xDEADB10C

#define BLOCK_HEADER_OVERHEAD offsetof(tlsf_block_t, next_free)

/* ======================= */
/* === Data Structures === */
/* ======================= */

typedef struct tlsf_block {
  struct tlsf_block* prev_phys;
  size_t size;
  struct tlsf_block* next_free;
  struct tlsf_block* prev_free;
} tlsf_block_t;

typedef struct large_block {
  uint32_t magic;
  size_t size;
  struct large_block* next;
  struct large_block* prev;
} large_block_t;

typedef struct mm_allocator {
  tlsf_block_t* blocks[TLSF_FLI_MAX][TLSF_SLI_COUNT];
  uint32_t fl_bitmap;
  uint32_t sl_bitmap[TLSF_FLI_MAX];
  char* heap_start;
  char* heap_end;
  large_block_t* large_blocks;
} mm_allocator_t;

/* ==================== */
/* === Global State === */
/* ==================== */

/* Exposed for testing */
extern mm_allocator_t* sys_allocator;
extern char* sys_heap_base;
extern size_t sys_heap_cap;

/* ===================== */
/* === Instsance API === */
/* ===================== */

mm_allocator_t* mm_create(void* mem, size_t bytes);
void mm_destroy_instance(mm_allocator_t* allocator);
void* mm_malloc_inst(mm_allocator_t* allocator, size_t size);
void mm_free_inst(mm_allocator_t* allocator, void* ptr);
size_t mm_get_usable_size(mm_allocator_t* allocator, void* ptr);
void mm_get_mapping_indices(size_t size, int* fl, int* sl);

/* ========================== */
/* === Global Wrapper API === */
/* ========================== */

int mm_init(void);                             // Initialize global default instance
void mm_destroy(void);                         // Destroy global default instance
void* mm_malloc(size_t size);                  // Allocate from global instance
void mm_free(void* ptr);                       // Free from global instance
void* mm_calloc(size_t nmemb, size_t size);
void* mm_realloc(void* ptr, size_t size);
size_t mm_malloc_usable_size(void* ptr);
void mm_print_heap_stats(void);
size_t mm_get_free_space(void);
size_t mm_get_total_allocated(void);
void mm_print_free_list(void);
void mm_reset_allocator(void);

#endif 