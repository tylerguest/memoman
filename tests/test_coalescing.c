#include "test_framework.h"
#include "../src/memoman.h"

static int test_basic_coalesce_right(void) {
  TEST_RESET();
  void* a = mm_malloc(64);
  void* b = mm_malloc(64);
  void* guard = mm_malloc(64);
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);
  ASSERT_NOT_NULL(guard);

  mm_free(b);
  mm_free(a);

  void* c = mm_malloc(100);
  ASSERT_EQ(c, a);

  mm_free(c);
  mm_free(guard);
  return 1;
}

static int test_basic_coalesce_left(void) {
  TEST_RESET();
  void* a = mm_malloc(64);
  void* b = mm_malloc(64);
  void* guard = mm_malloc(64);

  mm_free(a);
  mm_free(b);

  void* c = mm_malloc(100);
  ASSERT_EQ(c, a);

  mm_free(c);
  mm_free(guard);
  return 1;
}

static int test_sandwich_coalesce(void) {
  TEST_RESET();
  void* a = mm_malloc(64);
  void* b = mm_malloc(64);
  void* c = mm_malloc(64);
  void* guard = mm_malloc(64);

  mm_free(a);
  mm_free(c);
  mm_free(b);

  void* d = mm_malloc(150);
  ASSERT_EQ(d, a);

  mm_free(d);
  mm_free(guard);
  return 1;
}

static int test_fragmentation_survival(void) {
  TEST_RESET();
  void* ptrs[100];
  for(int i=0; i<100; i++) ptrs[i] = mm_malloc(64);

  for(int i=0; i<100; i+=2) mm_free(ptrs[i]);
  for(int i=1; i<100; i+=2) mm_free(ptrs[i]);

  size_t total_size = 100 * 64;
  void* huge = mm_malloc(total_size);
  ASSERT_NOT_NULL(huge);

  if (huge) mm_free(huge);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("TLSF Coalescing");
  RUN_TEST(test_basic_coalesce_right);
  RUN_TEST(test_basic_coalesce_left);
  RUN_TEST(test_sandwich_coalesce);
  RUN_TEST(test_fragmentation_survival);
  TEST_SUITE_END();
  TEST_MAIN_END();
}