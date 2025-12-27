#include "test_framework.h"
#include "../src/memoman.h"

/* === Simple Tests === */

static int test_null_returns_zero(void) {
  ASSERT_EQ(mm_malloc_usable_size(NULL), 0);
  return 1;
}

static int test_invalid_ptr_returns_zero(void) {
  ASSERT_EQ(mm_malloc_usable_size((void*)0xDEADBEEF), 0);
  return 1;
}

/* === Parameterized Tests === */

static int test_usable_ge_requested(size_t size) {
  void* ptr = mm_malloc(size);
  ASSERT_NOT_NULL(ptr);

  size_t usable = mm_malloc_usable_size(ptr);
  ASSERT_GE(usable, size);

  mm_free(ptr);
  return 1;
}

static int test_usable_after_free_is_zero_or_free(size_t size) {
  void* ptr = mm_malloc(size);
  ASSERT_NOT_NULL(ptr);

  size_t before = mm_malloc_usable_size(ptr);
  ASSERT_GE(before, size);

  mm_free(ptr);
  /* After free: either 0 (invalid) or still returns size (block exists but free) */
  /* We just verify no crash - behavior is implementation-defined */
  (void)mm_malloc_usable_size(ptr);

  return 1;
}

static int test_large_block_usable(size_t size) {
  void* ptr = mm_malloc(size);
  ASSERT_NOT_NULL(ptr);

  size_t usable = mm_malloc_usable_size(ptr);
  ASSERT_GE(usable, size);

  mm_free(ptr);
  return 1;
}

/* === Multiple Allocation Tests === */

static int test_multiple_allocations(void) {
  void* ptrs[10];
  size_t sizes[10] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};

  /* Allocate all */
  for (int i = 0; i < 10; i++) {
    ptrs[i] = mm_malloc(sizes[i]);
    ASSERT_NOT_NULL(ptrs[i]);
  }

  /* Verify all usable sizes */
  for (int i = 0; i < 10; i++) { ASSERT_GE(mm_malloc_usable_size(ptrs[i]), sizes[i]); }

  /* Free all */
  for (int i = 0; i < 10; i++) { mm_free(ptrs[i]); }

  return 1;
}

static int test_usable_size_stable(void) {
  void* ptr = mm_malloc(100);
  ASSERT_NOT_NULL(ptr);

  size_t s1 = mm_malloc_usable_size(ptr);
  size_t s2 = mm_malloc_usable_size(ptr);
  size_t s3 = mm_malloc_usable_size(ptr);
  
  ASSERT_EQ(s1, s2);
  ASSERT_EQ(s2, s3);
  ASSERT_GE(s1, 100);

  mm_free(ptr);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("mm_get_usable_size");

  /* Simple tests */
  RUN_TEST(test_null_returns_zero);
  RUN_TEST(test_invalid_ptr_returns_zero);
  RUN_TEST(test_multiple_allocations);
  RUN_TEST(test_usable_size_stable);

  /* Parameterized: TLSF sizes */
  RUN_PARAMETERIZED(test_usable_ge_requested, TEST_SIZES, TEST_SIZES_COUNT);
  RUN_PARAMETERIZED(test_usable_after_free_is_zero_or_free, TEST_SIZES, TEST_SIZES_COUNT);

  /* ParameterizedL: Large block sizes (mmap'd) */
  RUN_PARAMETERIZED(test_large_block_usable, TEST_LARGE_SIZES, TEST_LARGE_SIZES_COUNT);
  
  TEST_SUITE_END();
  TEST_MAIN_END();
}
