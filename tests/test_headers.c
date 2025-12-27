#include "test_framework.h"
#include "../src/memoman.h"

static int test_headers_basic(void) {
  char* ptr1 = (char*)mm_malloc(100);
  char* ptr2 = (char*)mm_malloc(200);
  char* ptr3 = (char*)mm_malloc(50);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);
  ASSERT_NOT_NULL(ptr3);
  mm_free(ptr2);
  mm_free(ptr1);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Headers");
  RUN_TEST(test_headers_basic);
  TEST_SUITE_END();
  TEST_MAIN_END();
}