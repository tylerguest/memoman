#include "test_framework.h"
#include <stdint.h>

static int test_destroy_null_noop(void) {
  (mm_destroy)(NULL);
  return 1;
}

static int test_create_in_place(void) {
  uint8_t pool[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create(pool);
  ASSERT_NOT_NULL(alloc);
  ASSERT_EQ((void*)alloc, (void*)pool);

  /* TLSF-style create: control-only (no implicit pool). */
  ASSERT_NULL(mm_get_pool(alloc));
  ASSERT_NULL((mm_malloc)(alloc, 16));
  ASSERT((mm_validate)(alloc));

  /* Caller can add a pool explicitly. */
  pool_t p0 = mm_add_pool(alloc, (uint8_t*)pool + mm_size(), sizeof(pool) - mm_size());
  ASSERT_NOT_NULL(p0);
  void* p = (mm_malloc)(alloc, 128);
  ASSERT_NOT_NULL(p);
  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));

  (mm_destroy)(alloc);
  return 1;
}

static int test_create_with_pool_smoke(void) {
  uint8_t pool[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = (mm_create_with_pool)(pool, sizeof(pool));
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

static int test_init_in_place_alias(void) {
  uint8_t pool[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = (mm_init_in_place)(pool, sizeof(pool));
  ASSERT_NOT_NULL(alloc);
  ASSERT_EQ((void*)alloc, (void*)pool);
  ASSERT((mm_validate)(alloc));

  ASSERT_NOT_NULL(mm_get_pool(alloc));
  void* p = (mm_malloc)(alloc, 128);
  ASSERT_NOT_NULL(p);
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
  tlsf_t alloc = (mm_create_with_pool)(unaligned, bytes);
  ASSERT_NULL(alloc);

  free(raw);
  return 1;
}

static int test_create_requires_minimum_size(void) {
  uint8_t pool[128] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(pool, sizeof(pool));
  ASSERT_NULL(alloc);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("lifecycle_parity");
  RUN_TEST(test_destroy_null_noop);
  RUN_TEST(test_create_in_place);
  RUN_TEST(test_create_with_pool_smoke);
  RUN_TEST(test_init_in_place_alias);
  RUN_TEST(test_create_requires_alignment);
  RUN_TEST(test_create_requires_minimum_size);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
