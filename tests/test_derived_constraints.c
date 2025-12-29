#include "test_framework.h"
#include "memoman_test_internal.h"

static int test_derived_constants_sane(void) {
  ASSERT((ALIGNMENT & (ALIGNMENT - 1)) == 0);
  ASSERT_GE(ALIGNMENT, sizeof(void*));

  ASSERT_EQ(BLOCK_HEADER_OVERHEAD, sizeof(size_t));
  ASSERT_EQ(BLOCK_START_OFFSET, BLOCK_HEADER_OVERHEAD);
  ASSERT_EQ(offsetof(tlsf_block_t, next_free), BLOCK_START_OFFSET);
  ASSERT_EQ(offsetof(tlsf_block_t, prev_free), BLOCK_START_OFFSET + sizeof(void*));

  ASSERT_EQ(TLSF_MIN_BLOCK_SIZE % ALIGNMENT, 0);
  ASSERT_GE(TLSF_MIN_BLOCK_SIZE, 3 * sizeof(void*));

  ASSERT_EQ(TLSF_FLI_MAX, FL_INDEX_COUNT);
  ASSERT_LE(FL_INDEX_COUNT, (int)(sizeof(unsigned int) * 8));
  ASSERT_LE(SL_INDEX_COUNT, (int)(sizeof(unsigned int) * 8));

  return 1;
}

static int test_split_respects_min_block_size(void) {
  uint8_t pool[32768] __attribute__((aligned(ALIGNMENT)));
  mm_allocator_t* alloc = mm_create(pool, sizeof(pool));
  ASSERT_NOT_NULL(alloc);

  size_t total_free = mm_free_space(alloc);
  ASSERT_GT(total_free, TLSF_MIN_BLOCK_SIZE + BLOCK_HEADER_OVERHEAD);

  /* Allocate leaving exactly (or more than) TLSF_MIN_BLOCK_SIZE for the remainder. */
  size_t req_split = total_free - BLOCK_HEADER_OVERHEAD - TLSF_MIN_BLOCK_SIZE;
  req_split &= ~(ALIGNMENT - 1);
  ASSERT_GT(req_split, 0);

  void* p = (mm_malloc)(alloc, req_split);
  ASSERT_NOT_NULL(p);

  size_t free_after_split = mm_free_space(alloc);
  ASSERT_GE(free_after_split, TLSF_MIN_BLOCK_SIZE);

  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));

  /* Allocate leaving a remainder smaller than TLSF_MIN_BLOCK_SIZE: should not split. */
  total_free = mm_free_space(alloc);
  size_t req_nosplit = total_free - BLOCK_HEADER_OVERHEAD - (TLSF_MIN_BLOCK_SIZE - ALIGNMENT);
  req_nosplit &= ~(ALIGNMENT - 1);
  ASSERT_GT(req_nosplit, 0);
  ASSERT_LE(req_nosplit, total_free);

  p = (mm_malloc)(alloc, req_nosplit);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ(mm_free_space(alloc), 0);

  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));

  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("derived_constraints");
  RUN_TEST(test_derived_constants_sane);
  RUN_TEST(test_split_respects_min_block_size);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
