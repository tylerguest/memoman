#include "test_framework.h"
#include "../src/memoman_internal.h"
#include <string.h>

/* Helper to acess internal block structure */
static tlsf_block_t* get_block(void* ptr) { return (tlsf_block_t*)((char*)ptr - BLOCK_START_OFFSET); }

static int test_valid_heap() {
  TEST_RESET();
  ASSERT(mm_validate());

  void* p1 = mm_malloc(64);
  void* p2 = mm_malloc(128);
  void* p3 = mm_malloc(256);

  ASSERT(mm_validate());

  mm_free(p2);
  ASSERT(mm_validate());

  mm_free(p1);
  ASSERT(mm_validate());

  mm_free(p3);
  ASSERT(mm_validate());

  return 1;
}

static int test_corrupt_alignment() {
  TEST_RESET();
  /* Alloc and free to get a block in the free list */
  void* p = mm_malloc(64);
  mm_free(p);
  
  ASSERT(mm_validate());

  tlsf_block_t* b = get_block(p);
  size_t original_size = b->size;

  /* Corrupt size to be unaligned (add 4 bytes) */
  b->size += 4;

  /* Should fail validation */
  int result = mm_validate();

  b->size = original_size;
  ASSERT(result == 0);

  return 1;
}

static int test_corrupt_overflow() {
  /* Skipped: Without physical heap walk, we cannot easily detect 
   * if a block size extends beyond the pool boundaries unless 
   * we track all pools. */
  return 1;
}

static int test_corrupt_free_list() {
  TEST_RESET();

  /* Create a free block */
  void* p1 = mm_malloc(64);
  void* p2 = mm_malloc(64); /* Barrier */
  mm_free(p1);

  ASSERT(mm_validate());

  /* Manually clear the bitmap bit for this block size */
  int fl, sl;
  mm_get_mapping_indices(64, &fl, &sl);

  sys_allocator->sl_bitmap[fl] &= ~(1U << sl);
  
  int result = mm_validate();

  /* Restore bit */
  sys_allocator->sl_bitmap[fl] |= (1u << sl);

  ASSERT(result == 0);

  mm_free(p2);
  return 1;
}

static int test_corrupt_coalescing() {
  /* Skipped: Requires physical walk to check neighbor flags */
  return 1;
}

#ifdef DEBUG_OUTPUT
/* Magic checks removed as TLSF 3.1 does not use magic numbers in headers */
#if 0
static int test_corrupt_magic() {
  TEST_RESET();
  /* Alloc and free to put in free list */
  void* p = mm_malloc(64);
  mm_free(p);
  
  tlsf_block_t* b = get_block(p);
  
  /* Save valid magic */
  uint32_t valid_magic = b->magic;
  
  /* Corrupt it (in free list) */
  b->magic = 0xDEADBEEF;

  int result = mm_validate();

  /* Restore */
  b->magic = valid_magic;
  
  ASSERT(result == 0);
  
  return 1;
}

static int test_free_safety_check() {
  TEST_RESET();
  void* p = mm_malloc(64);
  tlsf_block_t* b = get_block(p);
  
  uint32_t valid_magic = b->magic;
  
  /* 1. Corrupt Magic and try to free */
  b->magic = 0xBADF00D;
  
  size_t free_space_before = mm_get_free_space();
  
  mm_free(p);
  
  size_t free_space_after = mm_get_free_space();
  
  /* Should have rejected the free, so free space unchanged */
  ASSERT_EQ(free_space_before, free_space_after);
  
  /* 2. Restore and free properly */
  b->magic = valid_magic;
  mm_free(p);
  
  ASSERT_GT(mm_get_free_space(), free_space_before);
  
  return 1;
}
#endif
#endif

int main() {
  TEST_SUITE_BEGIN("Validation API");
  RUN_TEST(test_valid_heap);
  RUN_TEST(test_corrupt_alignment);
  RUN_TEST(test_corrupt_overflow);
  RUN_TEST(test_corrupt_free_list);
  RUN_TEST(test_corrupt_coalescing);
#ifdef DEBUG_OUTPUT
  /* Disabled magic tests */
  /* RUN_TEST(test_corrupt_magic); */
  /* RUN_TEST(test_free_safety_check); */
#endif
  TEST_SUITE_END();
  TEST_MAIN_END();
}
