#include "test_framework.h"
#include "../src/memoman.h"
#include <stdint.h>

static int in_range(const void* p, const void* base, size_t bytes) {
  uintptr_t a = (uintptr_t)p;
  uintptr_t b = (uintptr_t)base;
  return a >= b && a < (b + bytes);
}

static int test_get_pool_nonnull(void) {
  uint8_t pool[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(pool, sizeof(pool));
  ASSERT_NOT_NULL(alloc);
  ASSERT_NOT_NULL(mm_get_pool(alloc));
  return 1;
}

static int test_add_pool_returns_handle(void) {
  uint8_t pool1[64 * 1024] __attribute__((aligned(16)));
  uint8_t pool2[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(pool1, sizeof(pool1));
  ASSERT_NOT_NULL(alloc);

  pool_t p0 = mm_get_pool(alloc);
  ASSERT_NOT_NULL(p0);

  pool_t p1 = mm_add_pool(alloc, pool2, sizeof(pool2));
  ASSERT_NOT_NULL(p1);
  ASSERT(p1 != p0);

  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_remove_pool_empty_disables_allocation(void) {
  uint8_t backing[32 * 1024] __attribute__((aligned(16)));
  uint8_t pool2[128 * 1024] __attribute__((aligned(16)));

  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  pool_t p2 = mm_add_pool(alloc, pool2, sizeof(pool2));
  ASSERT_NOT_NULL(p2);

  void* big = (mm_malloc)(alloc, 64 * 1024);
  ASSERT_NOT_NULL(big);
  ASSERT(in_range(big, pool2, sizeof(pool2)));

  (mm_free)(alloc, big);
  ASSERT((mm_validate)(alloc));

  mm_remove_pool(alloc, p2);
  ASSERT((mm_validate)(alloc));

  big = (mm_malloc)(alloc, 64 * 1024);
  ASSERT_NULL(big);

  return 1;
}

static int test_remove_pool_with_live_alloc_is_noop(void) {
  uint8_t backing[32 * 1024] __attribute__((aligned(16)));
  uint8_t pool2[128 * 1024] __attribute__((aligned(16)));

  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  pool_t p2 = mm_add_pool(alloc, pool2, sizeof(pool2));
  ASSERT_NOT_NULL(p2);

  void* big = (mm_malloc)(alloc, 64 * 1024);
  ASSERT_NOT_NULL(big);
  ASSERT(in_range(big, pool2, sizeof(pool2)));

  /* Not allowed: pool has a live allocation. Should be a no-op. */
  mm_remove_pool(alloc, p2);

  (mm_free)(alloc, big);
  ASSERT((mm_validate)(alloc));

  big = (mm_malloc)(alloc, 64 * 1024);
  ASSERT_NOT_NULL(big);
  ASSERT(in_range(big, pool2, sizeof(pool2)));
  (mm_free)(alloc, big);
  ASSERT((mm_validate)(alloc));

  /* Now empty: removal should succeed. */
  mm_remove_pool(alloc, p2);
  ASSERT((mm_validate)(alloc));

  big = (mm_malloc)(alloc, 64 * 1024);
  ASSERT_NULL(big);

  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("pool_handles");
  RUN_TEST(test_get_pool_nonnull);
  RUN_TEST(test_add_pool_returns_handle);
  RUN_TEST(test_remove_pool_empty_disables_allocation);
  RUN_TEST(test_remove_pool_with_live_alloc_is_noop);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
