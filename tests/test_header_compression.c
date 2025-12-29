#include "test_framework.h"
#include "../src/memoman_internal.h"
#include <stddef.h>

/* Access internal TLSF control */
extern mm_allocator_t* sys_allocator;

/* Helper to access block fields */
static inline tlsf_block_t* user_to_block_helper(void* ptr) {
  return (tlsf_block_t*)((char*)ptr - BLOCK_HEADER_OVERHEAD);
}

static int test_overhead_reduction(void) {
  TEST_RESET();
  
  /* Allocate a small block */
  void* ptr = mm_malloc(32);
  ASSERT_NOT_NULL(ptr);
  
  tlsf_block_t* block = user_to_block_helper(ptr);
  
  /* 
   * Verify the overhead is reduced.
   * It should be sizeof(size_t) = 8 bytes on 64-bit.
   */
  size_t overhead = (char*)ptr - (char*)block;
  size_t expected = BLOCK_HEADER_OVERHEAD;
  
  ASSERT_EQ(overhead, expected);
  
#ifndef DEBUG_OUTPUT
  /* Sanity check specific value for 64-bit to ensure we actually changed the layout */
  if (sizeof(void*) == 8) {
      /* 8 bytes size */
      ASSERT_EQ(overhead, 8);
  }
#endif

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
  
  /* Verify we wrote to the space occupied by next_free */
  ASSERT_EQ(block->next_free, (tlsf_block_t*)pattern);
  
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
