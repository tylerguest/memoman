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

  /* Allocate leaving at least TLSF_MIN_BLOCK_SIZE for the remainder.
   * Note: TLSF mapping_search rounds requests up to the next size class.
   * To avoid requesting a class larger than the available free block, pick a
   * request at a size-class boundary (so mapping_search doesn't bump it).
   */
  size_t req_split = total_free - BLOCK_HEADER_OVERHEAD - TLSF_MIN_BLOCK_SIZE;
  req_split &= ~(ALIGNMENT - 1);
  const size_t small_block_size = (size_t)1 << FL_INDEX_SHIFT;
  if (req_split >= small_block_size) {
    int fl = 0;
    size_t tmp = req_split;
    while (tmp >>= 1) fl++;
    const size_t step = (size_t)1 << (fl - SL_INDEX_COUNT_LOG2);
    req_split &= ~(step - 1);
  }
  ASSERT_GT(req_split, 0);

  void* p = (mm_malloc)(alloc, req_split);
  ASSERT_NOT_NULL(p);

  size_t free_after_split = mm_free_space(alloc);
  ASSERT_GE(free_after_split, TLSF_MIN_BLOCK_SIZE);

  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));

  /* For any successful allocation from a single free block, the post-allocation
   * free space is either 0 (no split) or at least TLSF_MIN_BLOCK_SIZE (valid split).
   */
  total_free = mm_free_space(alloc);
  ASSERT_GT(total_free, 0);
  int checked = 0;
  for (size_t backoff = 0; backoff <= 4096; backoff += ALIGNMENT) {
    size_t req = total_free > backoff ? (total_free - backoff) : 0;
    if (!req) break;
    void* q = (mm_malloc)(alloc, req);
    if (!q) continue;
    checked++;
    size_t free_after = mm_free_space(alloc);
    ASSERT(free_after == 0 || free_after >= TLSF_MIN_BLOCK_SIZE);
    (mm_free)(alloc, q);
    ASSERT((mm_validate)(alloc));
  }
  ASSERT(checked > 0);

  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("derived_constraints");
  RUN_TEST(test_derived_constants_sane);
  RUN_TEST(test_split_respects_min_block_size);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
