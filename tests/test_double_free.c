#include "test_framework.h"
#include "../src/memoman.h"

static int test_simple_double_free(void) {
  TEST_RESET();
  void* p1 = mm_malloc(64);
  ASSERT_NOT_NULL(p1);
  mm_free(p1);
  ASSERT(mm_validate());
  mm_free(p1); /* Double free */
  ASSERT(mm_validate());
  void* p2 = mm_malloc(64);
  ASSERT_NOT_NULL(p2);
  mm_free(p2);
  return 1;
}

static int test_triple_free(void) {
  TEST_RESET();
  void* p2 = mm_malloc(128);
  ASSERT_NOT_NULL(p2);
  mm_free(p2);
  mm_free(p2);
  mm_free(p2);
  return 1;
}

static int test_reuse_after_free(void) {
  TEST_RESET();
  void* p3 = mm_malloc(64);
  ASSERT_NOT_NULL(p3);
  mm_free(p3);
  void* p4 = mm_malloc(64);
  ASSERT_NOT_NULL(p4);
  if (p4 == p3) {
    mm_free(p3); /* Should work as p3 is reused */
  } else {
    mm_free(p3); /* Double free */
    mm_free(p4);
  }
  return 1;
}

static int test_middle_double_free(void) {
  TEST_RESET();
  void* blocks[5];
  for(int i=0; i<5; i++) blocks[i] = mm_malloc(32 + i*16);
  for(int i=4; i>=0; i--) mm_free(blocks[i]);
  mm_free(blocks[2]);
  return 1;
}

static int test_heap_integrity(void) {
  TEST_RESET();
  void* a = mm_malloc(100);
  void* b = mm_malloc(200);
  void* c = mm_malloc(300);
  mm_free(b);
  mm_free(b);
  void* d = mm_malloc(150);
  ASSERT_NOT_NULL(d);
  mm_free(a);
  mm_free(c);
  mm_free(d);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Double-Free Detection");
  RUN_TEST(test_simple_double_free);
  RUN_TEST(test_triple_free);
  RUN_TEST(test_reuse_after_free);
  RUN_TEST(test_middle_double_free);
  RUN_TEST(test_heap_integrity);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
