#include "test_framework.h"
#include <stdint.h>

static int test_destroy_null_noop(void) {
  (mm_destroy)(NULL);
  return 1;
}

static int test_create_with_pool_smoke(void) {
  uint8_t pool[64 * 1024] __attribute__((aligned(16)));
  mm_allocator_t* alloc = (mm_create_with_pool)(pool, sizeof(pool));
  ASSERT_NOT_NULL(alloc);
  ASSERT((mm_validate)(alloc));

  void* p = (mm_malloc)(alloc, 128);
  ASSERT_NOT_NULL(p);
  ASSERT_GE((mm_block_size)(p), 128u);

  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));

  (mm_destroy)(alloc);
  return 1;
}

static int test_create_requires_alignment(void) {
  const size_t bytes = 64 * 1024;
  uint8_t* raw = (uint8_t*)malloc(bytes + 16);
  ASSERT_NOT_NULL(raw);

  void* unaligned = raw + 1;
  mm_allocator_t* alloc = (mm_create_with_pool)(unaligned, bytes);
  ASSERT_NULL(alloc);

  free(raw);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("lifecycle_parity");
  RUN_TEST(test_destroy_null_noop);
  RUN_TEST(test_create_with_pool_smoke);
  RUN_TEST(test_create_requires_alignment);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
