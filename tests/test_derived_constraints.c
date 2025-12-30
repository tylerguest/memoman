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

  uintptr_t pool_base = (uintptr_t)alloc + sizeof(mm_allocator_t);
  pool_base = (pool_base + (ALIGNMENT - 1)) & ~(uintptr_t)(ALIGNMENT - 1);
  tlsf_block_t* first = (tlsf_block_t*)pool_base;
  ASSERT(first->size & TLSF_BLOCK_FREE);
  size_t total_free = first->size & TLSF_SIZE_MASK;
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

  tlsf_block_t* used = (tlsf_block_t*)((char*)p - BLOCK_START_OFFSET);
  size_t used_size = used->size & TLSF_SIZE_MASK;
  tlsf_block_t* remainder = (tlsf_block_t*)((char*)used + BLOCK_HEADER_OVERHEAD + used_size);
  ASSERT(remainder->size & TLSF_BLOCK_FREE);
  ASSERT_GE((remainder->size & TLSF_SIZE_MASK), TLSF_MIN_BLOCK_SIZE);

  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));

  /* Allocate from a small free block (< SMALL_BLOCK_SIZE) leaving a remainder smaller than TLSF_MIN_BLOCK_SIZE:
   * should not split, and should reuse the entire free block.
   */
  uint8_t pool2[32768] __attribute__((aligned(ALIGNMENT)));
  mm_allocator_t* alloc2 = mm_create(pool2, sizeof(pool2));
  ASSERT_NOT_NULL(alloc2);

  void* g1 = (mm_malloc)(alloc2, 64);
  void* t = (mm_malloc)(alloc2, 240);
  void* g2 = (mm_malloc)(alloc2, 64);
  ASSERT_NOT_NULL(g1);
  ASSERT_NOT_NULL(t);
  ASSERT_NOT_NULL(g2);

  tlsf_block_t* t_block = (tlsf_block_t*)((char*)t - BLOCK_START_OFFSET);
  size_t t_size = t_block->size & TLSF_SIZE_MASK;
  ASSERT_LT(t_size, small_block_size);

  (mm_free)(alloc2, t);
  ASSERT((mm_validate)(alloc2));

  size_t req_nosplit = t_size - BLOCK_HEADER_OVERHEAD - (TLSF_MIN_BLOCK_SIZE - ALIGNMENT);
  req_nosplit &= ~(ALIGNMENT - 1);
  ASSERT_GT(req_nosplit, 0);

  void* q = (mm_malloc)(alloc2, req_nosplit);
  ASSERT_EQ(q, t);

  tlsf_block_t* q_block = (tlsf_block_t*)((char*)q - BLOCK_START_OFFSET);
  size_t q_size = q_block->size & TLSF_SIZE_MASK;
  tlsf_block_t* q_next = (tlsf_block_t*)((char*)q_block + BLOCK_HEADER_OVERHEAD + q_size);
  tlsf_block_t* g2_block = (tlsf_block_t*)((char*)g2 - BLOCK_START_OFFSET);
  ASSERT_EQ(q_next, g2_block);

  (mm_free)(alloc2, q);
  (mm_free)(alloc2, g1);
  (mm_free)(alloc2, g2);
  ASSERT((mm_validate)(alloc2));

  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("derived_constraints");
  RUN_TEST(test_derived_constants_sane);
  RUN_TEST(test_split_respects_min_block_size);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
