#include "test_framework.h"
#include "../src/memoman.h"
#include <stdint.h>

static int test_reset_fails_with_live_allocations(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  void* p = (mm_malloc)(alloc, 1024);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ((mm_reset)(alloc), 0);
  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_reset_succeeds_when_all_free(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  uint8_t pool2[64 * 1024] __attribute__((aligned(16)));

  tlsf_t alloc = mm_create(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);
  ASSERT_NOT_NULL(mm_add_pool(alloc, pool2, sizeof(pool2)));

  void* a = (mm_malloc)(alloc, 1024);
  void* b = (mm_malloc)(alloc, 2048);
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);

  (mm_free)(alloc, a);
  (mm_free)(alloc, b);
  ASSERT((mm_validate)(alloc));

  ASSERT_EQ((mm_reset)(alloc), 1);
  ASSERT((mm_validate)(alloc));

  void* c = (mm_malloc)(alloc, 4096);
  ASSERT_NOT_NULL(c);
  (mm_free)(alloc, c);
  ASSERT((mm_validate)(alloc));
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Reset");
  RUN_TEST(test_reset_fails_with_live_allocations);
  RUN_TEST(test_reset_succeeds_when_all_free);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
