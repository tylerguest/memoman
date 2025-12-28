#undef NDEBUG
#include "../src/memoman.h"
#include <stdio.h>
#include <assert.h>

/* Access internal TLSF control */
extern mm_allocator_t* sys_allocator;

/* Helper to access block fields since they are internal */
static inline size_t get_block_size(tlsf_block_t* block) { return block->size & TLSF_SIZE_MASK; }
static inline int is_block_used(tlsf_block_t* block) { return !(block->size & TLSF_BLOCK_FREE); }

int main() {
  printf("Testing Sentinels (Prologue/Epilogue)...\n");
  
  /* Initialize allocator */
  mm_reset_allocator();
  assert(sys_allocator != NULL);

  /* Test 1: Prologue exists */
  printf("  Test 1: Prologue sentinel... ");
  tlsf_block_t* prologue = (tlsf_block_t*)sys_allocator->heap_start;
  assert(get_block_size(prologue) == 0);
  assert(is_block_used(prologue));
  printf("PASSED\n");

  /* Test 2: Epilogue exists */
  printf("  Test 2: Epilogue sentinel... ");
  tlsf_block_t* epilogue = (tlsf_block_t*)(sys_allocator->heap_end - BLOCK_HEADER_OVERHEAD);
  assert(get_block_size(epilogue) == 0);
  assert(is_block_used(epilogue));
  printf("PASSED\n");

  /* Test 3: Heap Growth maintains epilogue */
  printf("  Test 3: Epilogue after growth... ");
  char* old_end = sys_allocator->heap_end;
  
  /* Allocate enough to trigger heap growth */
  for (int i = 0; i < 200; i++) {
    if (!mm_malloc(10000)) break; 
  }
  
  assert(sys_allocator->heap_end > old_end); // Ensure heap actually grew
  tlsf_block_t* new_epilogue = (tlsf_block_t*)(sys_allocator->heap_end - BLOCK_HEADER_OVERHEAD);
  assert(get_block_size(new_epilogue) == 0);
  assert(is_block_used(new_epilogue));
  printf("PASSED\n");

  printf("\nAll sentinel tests passed!\n");
  return 0;
}