#include "test_framework.h"
#include "../src/memoman.h"

/* Access internal TLSF control for white-box testing */
extern mm_allocator_t* sys_allocator;

/* Helper to access block fields */
static inline tlsf_block_t* user_to_block_helper(void* ptr) {
  return (tlsf_block_t*)((char*)ptr - offsetof(tlsf_block_t, next_free));
}

static int test_sentinel_linkage(void) {
  TEST_RESET();

  void* ptr = mm_malloc(64);
  ASSERT_NOT_NULL(ptr);

  tlsf_block_t* block = user_to_block_helper(ptr);
  tlsf_block_t* prev = block->prev_phys;

  /* The previous physical block must be the Prologue */
  /* Prologue is at heap_start */
  ASSERT_EQ(prev, (tlsf_block_t*)sys_allocator->heap_start);

  /* Prologue must be used and size 0 */
  ASSERT_EQ(prev->size & TLSF_SIZE_MASK, 0);
  ASSERT_EQ(prev->size & TLSF_BLOCK_FREE, 0);

  mm_free(ptr);
  return 1;
}

static int test_large_block_alignment(void) {
  TEST_RESET();

  /* Allocate 2MB, which exceeds LARGE_ALLOC_THRESHOLD (1MB) */
  void* ptr = mm_malloc(2 * 1024 * 1024);
  ASSERT_NOT_NULL(ptr);

  /* Check alignment */
  ASSERT_EQ((uintptr_t)ptr % ALIGNMENT, 0);

  mm_free(ptr);
  return 1;
}

static int test_growth_coalescing(void) {
  TEST_RESET();

  /* Fill most of the initial 1MB heap */
  /* Leave some space at the end */
  size_t initial_fill = 900 * 1024;
  void* p1 = mm_malloc(initial_fill);
  ASSERT_NOT_NULL(p1);

  /* Allocate a block at the very end */
  void* p2 = mm_malloc(64 * 1024);
  ASSERT_NOT_NULL(p2);

  /* Free the last block */
  mm_free(p2);

  /* Force growth by allocating something that doesn't fit in the hole but fits in the grown heap */
  /* Current free: ~64KB + fragmentation, Request: 200KB */
  /* This forces heap to double (add 1MB) */
  void* p3 = mm_malloc(200 * 1024);
  ASSERT_NOT_NULL(p3);

  /* The new free space should be: (Old Free 64KB) + (New 1MB) - (Allocated 200KB) */
  size_t free_after = mm_get_free_space();

  /* Check if it's > 800KB to be safe against metadata overhead */
  ASSERT_GT(free_after, 800 * 1024);

  mm_free(p1);
  mm_free(p3);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Heap Structure & Growth");

  RUN_TEST(test_sentinel_linkage);
  RUN_TEST(test_large_block_alignment);
  RUN_TEST(test_growth_coalescing);

  TEST_SUITE_END();
  TEST_MAIN_END();
}