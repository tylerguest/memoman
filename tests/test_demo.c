#include "test_framework.h"
#include "../src/memoman.h"

#include <stdint.h>

static int test_demo_flow(void) {
  uint8_t pool1[128 * 1024] __attribute__((aligned(16)));
  uint8_t pool2[128 * 1024] __attribute__((aligned(16)));

  tlsf_t mm = mm_create_with_pool(pool1, sizeof(pool1));
  ASSERT_NOT_NULL(mm);

  void* a = (mm_malloc)(mm, 24);
  void* b = (mm_malloc)(mm, 256);
  void* c = (mm_memalign)(mm, 4096, 128);
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);
  ASSERT_NOT_NULL(c);
  ASSERT((((uintptr_t)c) & (4096 - 1)) == 0);
  ASSERT_GE((mm_block_size)(a), 24u);
  ASSERT_GE((mm_block_size)(b), 256u);
  ASSERT_GE((mm_block_size)(c), 128u);
  ASSERT((mm_validate)(mm));

  (mm_free)(mm, b);
  a = (mm_realloc)(mm, a, 1024);
  ASSERT_NOT_NULL(a);
  ASSERT_GE((mm_block_size)(a), 1024u);
  ASSERT((mm_validate)(mm));

  ASSERT_NOT_NULL(mm_add_pool(mm, pool2, sizeof(pool2)));
  void* d = (mm_malloc)(mm, 64 * 1024);
  ASSERT_NOT_NULL(d);
  ASSERT_GE((mm_block_size)(d), 64u * 1024u);
  ASSERT((mm_validate)(mm));

  (mm_free)(mm, a);
  (mm_free)(mm, c);
  (mm_free)(mm, d);
  ASSERT((mm_validate)(mm));

  (mm_destroy)(mm);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("demo");
  RUN_TEST(test_demo_flow);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
