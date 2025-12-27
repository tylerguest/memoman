#include "test_framework.h"
#include "../src/memoman.h"
#include <stddef.h>

/* Access internal TLSF control */
extern mm_allocator_t* sys_allocator;

/* Helper to access block fields */
static inline tlsf_block_t* user_to_block_helper(void* ptr) {
  /* We know BLOCK_HEADER_OVERHEAD is offsetof(tlsf_block_t, next_free) */
  return (tlsf_block_t*)((char*)ptr - offsetof(tlsf_block_t, next_free));
}

static int test_overhead_reduction(void) {
  TEST_RESET();
  
  /* Allocate a small block */
  void* ptr = mm_malloc(32);
  ASSERT_NOT_NULL(ptr);
  
  tlsf_block_t* block = user_to_block_helper(ptr);
  
  /* 
   * Verify the overhead is reduced.
   * It should be offsetof(tlsf_block_t, next_free).
   * On 64-bit: prev_phys(8) + size(8) = 16 bytes.
   * (Old overhead was 32 bytes).
   */
  size_t overhead = (char*)ptr - (char*)block;
  size_t expected = offsetof(tlsf_block_t, next_free);
  
  ASSERT_EQ(overhead, expected);
  
  /* Sanity check specific value for 64-bit to ensure we actually changed the layout */
  if (sizeof(void*) == 8) {
      ASSERT_EQ(overhead, 16);
  }

  /* Verify we are actually looking at a block header by checking the size field */
  size_t size = block->size & TLSF_SIZE_MASK;
  ASSERT_GE(size, 32);
  ASSERT_EQ(block->size & TLSF_BLOCK_FREE, 0);
  
  mm_free(ptr);
  return 1;
}

static int test_data_overlap(void) {
  TEST_RESET();
  
  /* Allocate block */
  void* ptr = mm_malloc(64);
  ASSERT_NOT_NULL(ptr);
  
  tlsf_block_t* block = user_to_block_helper(ptr);
  
  /* 
   * The user pointer 'ptr' points exactly where 'next_free' would be 
   * if the block were free.
   */
  void** overlap_ptr = (void**)ptr;
  
  /* Write a pattern */
  void* pattern = (void*)0xDEADBEEF;
  *overlap_ptr = pattern;
  
  /* Verify we wrote to the space occupied by next_free in the struct definition */
  ASSERT_EQ(block->next_free, pattern);
  
  /* Verify we didn't corrupt the header fields (size/prev_phys) */
  size_t size = block->size & TLSF_SIZE_MASK;
  ASSERT_GE(size, 64);
  ASSERT_EQ(block->size & TLSF_BLOCK_FREE, 0);
  
  mm_free(ptr);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Header Compression");
  RUN_TEST(test_overhead_reduction);
  RUN_TEST(test_data_overlap);
  TEST_SUITE_END();
  TEST_MAIN_END();
}