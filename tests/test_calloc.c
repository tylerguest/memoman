#include "test_framework.h"
#include "../src/memoman.h"
#include <limits.h>
#include <stdint.h>

/* === Edge Cases === */

static int zero_nmemb(void) {
  void* ptr = mm_calloc(0, 100);
  ASSERT_NULL(ptr);
  return 1;
}

static int zero_size(void) {
  void* ptr = mm_calloc(100, 0);
  ASSERT_NULL(ptr);
  return 1;
}

static int both_zero(void) {
  void* ptr = mm_calloc(0, 0);
  ASSERT_NULL(ptr);
  return 1;
}

static int overflow_returns_null(void) {
  /* SIZE_MAX / 2 + 1 elements of size 2 would overflow */
  void* ptr = mm_calloc(SIZE_MAX / 2 + 1, 2);
  ASSERT_NULL(ptr);
  return 1;
}

static int overflow_large_nmemb(void) {
  void* ptr = mm_calloc(SIZE_MAX, 2);
  ASSERT_NULL(ptr);
  return 1;
}

static int overflow_large_size(void) {
  void* ptr = mm_calloc(2, SIZE_MAX);
  ASSERT_NULL(ptr);
  return 1;
}

/* === Zero Initialization === */

static int memory_is_zeroed(size_t size) {
  unsigned char* ptr = mm_calloc(1, size);
  ASSERT_NOT_NULL(ptr);
  
  /* Verify all bytes are zero */
  for (size_t i = 0; i < size; i++) { ASSERT_EQ(ptr[i], 0); }

  mm_free(ptr);
  return 1;
}

static int array_is_zeroed(void) {
  int* arr = mm_calloc(100, sizeof(int));
  ASSERT_NOT_NULL(arr);

  for (int i = 0; i < 100; i++) { ASSERT_EQ(arr[i], 0); }

  mm_free(arr);
  return 1;
}

static int large_array_zeroed(void) {
  /* test with large block (triggers mmap path) */
  size_t count = 1024 * 1024 / sizeof(int);  /* 1MB worth of ints */
  int* arr = mm_calloc(count, sizeof(int));
  ASSERT_NOT_NULL(arr);

  /* Check first, middle, and last elements */
  ASSERT_EQ(arr[0], 0);
  ASSERT_EQ(arr[count / 2], 0);
  ASSERT_EQ(arr[count - 1], 0);

  mm_free(arr);
  return 1;
}

/* === Usable Size === */

static int usable_size_correct(void) {
  void* ptr = mm_calloc(10, 16);  /* 160 bytes */
  ASSERT_NOT_NULL(ptr);

  /* FIX: Call global wrapper */
  size_t usable = mm_malloc_usable_size(ptr);
  ASSERT_GE(usable, 160);

  mm_free(ptr);
  return 1;
}

/* === Parameterized Tests === */

static int calloc_basic(size_t size) {
  void* ptr = mm_calloc(1, size);
  ASSERT_NOT_NULL(ptr);

  /* FIX: Call global wrapper */
  size_t usable = mm_malloc_usable_size(ptr);
  ASSERT_GE(usable, size);

  mm_free(ptr);
  return 1;
}

static int calloc_array(size_t size) {
  /* Allocate 10 elements of given size */
  unsigned char* ptr = mm_calloc(10, size);
  ASSERT_NOT_NULL(ptr);

  /* Verify all zeroed */
  for (size_t i = 0; i < 10 * size; i++) { ASSERT_EQ(ptr[i], 0); }

  mm_free(ptr);
  return 1;
}

int main(void) {
  /* Explicit init for test environment consistency */
  mm_init();

  TEST_SUITE_BEGIN("mm_calloc");

  TEST_SECTION("Edge Cases");
  RUN_TEST(zero_nmemb);
  RUN_TEST(zero_size);
  RUN_TEST(both_zero);
  RUN_TEST(overflow_returns_null);
  RUN_TEST(overflow_large_nmemb);
  RUN_TEST(overflow_large_size);  

  TEST_SECTION("Zero Initialization");
  RUN_TEST(array_is_zeroed);
  RUN_TEST(large_array_zeroed);
  RUN_TEST(usable_size_correct);

  TEST_SECTION("Parameterized: Single Element");
  RUN_PARAMETERIZED(memory_is_zeroed, TEST_SIZES, TEST_SIZES_COUNT);

  TEST_SECTION("Parameterized: Array Allocation");
  RUN_PARAMETERIZED(calloc_basic, TEST_SIZES, TEST_SIZES_COUNT);
  RUN_PARAMETERIZED(calloc_array, TEST_SIZES, TEST_SIZES_COUNT);

  TEST_SUITE_END();
  TEST_MAIN_END();
  
  mm_destroy();
}
