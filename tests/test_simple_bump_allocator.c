#include "test_framework.h"

static int test_bump_basic(void) {
  char* ptr1 = (char*)mm_malloc(10);
  char* ptr2 = (char*)mm_malloc(20);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);
  mm_free(ptr1);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Simple Allocator");
  RUN_TEST(test_bump_basic);
  TEST_SUITE_END();
  TEST_MAIN_END();
}

