#include "test_framework.h"
#include "memoman_test_internal.h"

#include <stdint.h>

static int test_sizing_constants(void) {
  ASSERT_EQ(mm_align_size(), ALIGNMENT);
  ASSERT_EQ(mm_alloc_overhead(), BLOCK_START_OFFSET);
  ASSERT_EQ(mm_pool_overhead(), ALIGNMENT + (2 * BLOCK_HEADER_OVERHEAD));

  ASSERT_EQ(mm_block_size_min(), TLSF_MIN_BLOCK_SIZE);
  ASSERT_EQ(mm_block_size_max() % ALIGNMENT, 0);
  ASSERT(mm_block_size_max() < ((size_t)1 << FL_INDEX_MAX));
  ASSERT(mm_block_size_max() >= mm_block_size_min());

  ASSERT_EQ(mm_size(), sizeof(struct mm_allocator_t));
  return 1;
}

static int test_pool_overhead_minimum(void) {
  uint8_t buf[256] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create(buf, sizeof(buf));
  ASSERT_NULL(alloc);

  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  alloc = mm_create(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  uint8_t pool[1024] __attribute__((aligned(16)));
  size_t too_small = mm_pool_overhead() + mm_block_size_min() - 1;
  ASSERT(too_small < sizeof(pool));
  ASSERT_NULL(mm_add_pool(alloc, pool, too_small));

  size_t just_enough = mm_pool_overhead() + mm_block_size_min();
  ASSERT(just_enough <= sizeof(pool));
  ASSERT_NOT_NULL(mm_add_pool(alloc, pool, just_enough));

  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_block_size_max_behavior(void) {
  uint8_t backing[256 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  /*
   * `mm_block_size_max()` is a global limit (size-class limit), not a promise
   * that any given pool can satisfy the request. Instead, verify the allocator
   * rejects sizes above the advertised max independent of pool size.
   */
  void* p = (mm_malloc)(alloc, mm_block_size_max() + mm_align_size());
  ASSERT_NULL(p);

  /* Sanity: smaller allocations still work. */
  p = (mm_malloc)(alloc, 1024);
  ASSERT_NOT_NULL(p);
  ASSERT_GE((mm_block_size)(p), 1024u);
  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));

  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("overheads_limits");
  RUN_TEST(test_sizing_constants);
  RUN_TEST(test_pool_overhead_minimum);
  RUN_TEST(test_block_size_max_behavior);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
