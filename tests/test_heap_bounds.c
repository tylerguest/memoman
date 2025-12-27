#include "test_framework.h"
#include "../src/memoman.h"

/* Access internal TLSF control */
extern mm_allocator_t* sys_allocator;

static int test_free_null(void) {
  mm_free(NULL);
  return 1;
}

static int test_free_stack(void) {
  char stack_var = 'x';
  mm_free(&stack_var);
  return 1;
}

static int test_free_invalid(void) {
  mm_free((void*)(uintptr_t)0xDEADBEEF);
  return 1;
}

static int test_free_before_heap(void) {
  void* before_heap = (void*)(sys_allocator->heap_start - 100);
  mm_free(before_heap);
  return 1;
}

static int test_free_after_heap(void) {
  void* after_heap = (void*)(sys_allocator->heap_end + 100);
  mm_free(after_heap);
  return 1;
}

static int test_free_heap_end(void) {
  mm_free((void*)sys_allocator->heap_end);
  return 1;
}

static int test_misaligned_free(void) {
  void* valid = mm_malloc(128);
  ASSERT_NOT_NULL(valid);
  void* misaligned = (char*)valid + 3;
  mm_free(misaligned); /* Should ignore */
  mm_free(valid);
  return 1;
}

static int test_large_block_invalid_free(void) {
  void* large = mm_malloc(2 * 1024 * 1024);
  ASSERT_NOT_NULL(large);
  mm_free((char*)large + 100); /* Invalid */
  mm_free(large);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Heap Bounds Validation");
  RUN_TEST(test_free_null);
  RUN_TEST(test_free_stack);
  RUN_TEST(test_free_invalid);
  RUN_TEST(test_free_before_heap);
  RUN_TEST(test_free_after_heap);
  RUN_TEST(test_free_heap_end);
  RUN_TEST(test_misaligned_free);
  RUN_TEST(test_large_block_invalid_free);
  TEST_SUITE_END();
  TEST_MAIN_END();
}