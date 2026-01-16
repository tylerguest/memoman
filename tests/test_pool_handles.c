#include "test_framework.h"
#include "../src/memoman.h"
#include <stdint.h>

static int in_range(const void* p, const void* base, size_t bytes) {
  uintptr_t a = (uintptr_t)p;
  uintptr_t b = (uintptr_t)base;
  return a >= b && a < (b + bytes);
}

static void pool_layout(void* mem, size_t bytes, void** out_start, size_t* out_bytes) {
  size_t align = mm_align_size();
  uintptr_t start = (uintptr_t)mem;
  uintptr_t aligned = (start + align - 1) & ~(align - 1);
  size_t aligned_bytes = bytes - (aligned - start);
  aligned_bytes &= ~(align - 1);
  if (out_start) *out_start = (void*)aligned;
  if (out_bytes) *out_bytes = aligned_bytes;
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

  void* pool2_start = NULL;
  size_t pool2_bytes = 0;
  pool_layout(pool2, sizeof(pool2), &pool2_start, &pool2_bytes);

  void* big = (mm_malloc)(alloc, 64 * 1024);
  ASSERT_NOT_NULL(big);
  ASSERT(in_range(big, pool2_start, pool2_bytes));

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

  void* pool2_start = NULL;
  size_t pool2_bytes = 0;
  pool_layout(pool2, sizeof(pool2), &pool2_start, &pool2_bytes);

  void* big = (mm_malloc)(alloc, 64 * 1024);
  ASSERT_NOT_NULL(big);
  ASSERT(in_range(big, pool2_start, pool2_bytes));

  /* Not allowed: pool has a live allocation. Should be a no-op. */
  mm_remove_pool(alloc, p2);

  (mm_free)(alloc, big);
  ASSERT((mm_validate)(alloc));

  big = (mm_malloc)(alloc, 64 * 1024);
  ASSERT_NOT_NULL(big);
  ASSERT(in_range(big, pool2_start, pool2_bytes));
  (mm_free)(alloc, big);
  ASSERT((mm_validate)(alloc));

  /* Now empty: removal should succeed. */
  mm_remove_pool(alloc, p2);
  ASSERT((mm_validate)(alloc));

  big = (mm_malloc)(alloc, 64 * 1024);
  ASSERT_NULL(big);

  return 1;
}

static int test_remove_pool_rejects_pointer(void) {
  uint8_t backing[32 * 1024] __attribute__((aligned(16)));
  uint8_t pool2[32 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  pool_t p2 = mm_add_pool(alloc, pool2, sizeof(pool2));
  ASSERT_NOT_NULL(p2);

  void* bogus = (void*)((uintptr_t)p2 + 64);
  mm_remove_pool(alloc, bogus);
  ASSERT((mm_validate)(alloc));

  void* p = (mm_malloc)(alloc, 1024);
  ASSERT_NOT_NULL(p);
  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));

  mm_remove_pool(alloc, p2);
  ASSERT((mm_validate)(alloc));

  return 1;
}

static int test_remove_pool_rejects_overlap_handle(void) {
  uint8_t backing[32 * 1024] __attribute__((aligned(16)));
  uint8_t pool2[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  pool_t p2 = mm_add_pool(alloc, pool2, sizeof(pool2));
  ASSERT_NOT_NULL(p2);

  void* interior = (void*)((uintptr_t)p2 + 128);
  mm_remove_pool(alloc, interior);
  ASSERT((mm_validate)(alloc));

  void* big = (mm_malloc)(alloc, 48 * 1024);
  ASSERT_NOT_NULL(big);
  (mm_free)(alloc, big);
  ASSERT((mm_validate)(alloc));

  mm_remove_pool(alloc, p2);
  ASSERT((mm_validate)(alloc));

  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("pool_handles");
  RUN_TEST(test_get_pool_nonnull);
  RUN_TEST(test_add_pool_returns_handle);
  RUN_TEST(test_remove_pool_empty_disables_allocation);
  RUN_TEST(test_remove_pool_with_live_alloc_is_noop);
  RUN_TEST(test_remove_pool_rejects_pointer);
  RUN_TEST(test_remove_pool_rejects_overlap_handle);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
