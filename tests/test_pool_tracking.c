#include "test_framework.h"
#include "../src/memoman.h"

#include <stdint.h>

static int test_get_pool_for_ptr_basic(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  uint8_t pool2[128 * 1024] __attribute__((aligned(16)));

  mm_allocator_t* alloc = mm_create(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  mm_pool_t p0 = mm_get_pool(alloc);
  ASSERT_NOT_NULL(p0);

  mm_pool_t p2 = mm_add_pool(alloc, pool2, sizeof(pool2));
  ASSERT_NOT_NULL(p2);

  void* a = (mm_malloc)(alloc, 1024);
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(mm_get_pool_for_ptr(alloc, a));

  void* b = (mm_malloc)(alloc, 64 * 1024);
  ASSERT_NOT_NULL(b);

  mm_pool_t pb = mm_get_pool_for_ptr(alloc, b);
  ASSERT_NOT_NULL(pb);
  ASSERT(pb == p2);

  ASSERT_NULL(mm_get_pool_for_ptr(alloc, NULL));
  ASSERT_NULL(mm_get_pool_for_ptr(NULL, a));

  (mm_free)(alloc, a);
  (mm_free)(alloc, b);
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_get_pool_for_ptr_rejects_non_mm_ptrs(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  mm_allocator_t* alloc = mm_create(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  uint8_t not_from_mm[64] __attribute__((aligned(16)));
  ASSERT_NULL(mm_get_pool_for_ptr(alloc, not_from_mm));

  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("pool_tracking");
  RUN_TEST(test_get_pool_for_ptr_basic);
  RUN_TEST(test_get_pool_for_ptr_rejects_non_mm_ptrs);
  TEST_SUITE_END();
  TEST_MAIN_END();
}

