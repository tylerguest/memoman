#include "test_framework.h"
#include "../src/memoman.h"

/* Access internal TLSF control */

static int test_free_null(void) {
  mm_free(NULL);
  return 1;
}

static int test_free_stack(void) {
  /* Skipped: Bounds checking removed */
  return 1;
}

static int test_free_invalid(void) {
  /* Skipped: Bounds checking removed */
  return 1;
}

static int test_free_before_heap(void) {
  /* Skipped: Bounds checking removed */
  return 1;
}

static int test_free_after_heap(void) {
  /* Skipped: Bounds checking removed */
  return 1;
}

static int test_free_heap_end(void) {
  /* Skipped: Bounds checking removed */
  return 1;
}

static int test_misaligned_free(void) {
#if defined(MM_DEBUG) && defined(MM_DEBUG_ABORT_ON_INVALID_POINTER) && MM_DEBUG_ABORT_ON_INVALID_POINTER
  /* In strict debug pointer-safety mode, invalid frees abort by design. */
  return 1;
#else
  void* valid = mm_malloc(128);
  ASSERT_NOT_NULL(valid);
  void* misaligned = (char*)valid + 3;
  mm_free(misaligned); /* Should ignore */
  mm_free(valid);
  return 1;
#endif
}

static int test_large_block_invalid_free(void) {
  /* Skipped: Bounds checking removed */
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
