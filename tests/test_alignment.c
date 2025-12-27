#include "test_framework.h"
#include "../src/memoman.h"

static int test_basic_alignment(void) {
  void* ptr1 = mm_malloc(1);   
  void* ptr2 = mm_malloc(7);   
  void* ptr3 = mm_malloc(8);   
  void* ptr4 = mm_malloc(9);   
  void* ptr5 = mm_malloc(13);  
  void* ptr6 = mm_malloc(16);  
  void* ptr7 = mm_malloc(17);  

  ASSERT_EQ((uintptr_t)ptr1 % ALIGNMENT, 0);
  ASSERT_EQ((uintptr_t)ptr2 % ALIGNMENT, 0);
  ASSERT_EQ((uintptr_t)ptr3 % ALIGNMENT, 0);
  ASSERT_EQ((uintptr_t)ptr4 % ALIGNMENT, 0);
  ASSERT_EQ((uintptr_t)ptr5 % ALIGNMENT, 0);
  ASSERT_EQ((uintptr_t)ptr6 % ALIGNMENT, 0);
  ASSERT_EQ((uintptr_t)ptr7 % ALIGNMENT, 0);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Alignment");
  RUN_TEST(test_basic_alignment);
  TEST_SUITE_END();
  TEST_MAIN_END();
}