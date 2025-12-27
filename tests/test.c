#include "test_framework.h"
#include "../src/memoman.h"

static int test_sanity(void) {
  char* str = mm_malloc(50);
  ASSERT_NOT_NULL(str);
  strcpy(str, "Hello World!");
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Sanity");
  RUN_TEST(test_sanity);
  TEST_SUITE_END();
  TEST_MAIN_END();
}