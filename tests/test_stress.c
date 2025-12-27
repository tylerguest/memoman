#include "test_framework.h"
#include "../src/memoman.h"

static int test_stress_basic(void) {
  void* ptrs[10];  
  for (int i = 0; i < 10; i++) {
      ptrs[i] = mm_malloc(50 + i * 20);
      ASSERT_NOT_NULL(ptrs[i]);
  }  
  for (int i = 1; i < 10; i += 2) {
      mm_free(ptrs[i]);
  }  
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Stress");
  RUN_TEST(test_stress_basic);
  TEST_SUITE_END();
  TEST_MAIN_END();
}