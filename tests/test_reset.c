#include "test_framework.h"
#include "../src/memoman.h"

static int test_reset_basic(void) {
  char* ptr1 = (char*)mm_malloc(1000);
  char* ptr2 = (char*)mm_malloc(2000);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);
  mm_reset_allocator();
  char* new_ptr = (char*)mm_malloc(500);
  ASSERT_NOT_NULL(new_ptr);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Reset");
  RUN_TEST(test_reset_basic);
  TEST_SUITE_END();
  TEST_MAIN_END();
}