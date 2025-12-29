#include "test_framework.h"

static int test_stats_print(void) {
  char* ptr1 = (char*)mm_malloc(100);
  ASSERT_NOT_NULL(ptr1);
  mm_get_total_allocated();
  mm_get_free_space();
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Stats");
  RUN_TEST(test_stats_print);
  TEST_SUITE_END();
  TEST_MAIN_END();
}