#include "test_framework.h"
#include "../src/memoman.h"
#include <string.h>

/* === Edge Cases === */

static int null_is_malloc(void) {
  void* ptr = mm_realloc(NULL, 100);
  ASSERT_NOT_NULL(ptr);

  size_t usable = mm_get_usable_size(ptr);
  ASSERT_GE(usable, 100);

  mm_free(ptr);
  return 1;
}

static int zero_is_free(void) {
  void* ptr = mm_malloc(100);
  ASSERT_NOT_NULL(ptr);

  void* result = mm_realloc(ptr, 0);
  ASSERT_NULL(result);

  /* ptr is now freed - don't use it */
  return 1;
}

static int null_and_zero(void) {
  void* result = mm_realloc(NULL, 0);
  ASSERT_NULL(result);
  return 1;
}

static int invalid_ptr(void) {
  void* result = mm_realloc((void*)0xDEADBEEF, 100);
  ASSERT_NULL(result);
  return 1;
}

/* === Data Preservation === */

static int preserves_data_grow(void) {
  char* ptr = mm_malloc(50);
  ASSERT_NOT_NULL(ptr);

  /* Fill with pattern */
  for (int i = 0; i < 50; i++) { ptr[i] = (char)i; }

  /* Grow to 200 bytes */
  char* new_ptr = mm_realloc(ptr, 200);
  ASSERT_NOT_NULL(new_ptr);

  /* Verify old data preserved */
  for (int i = 0; i < 50; i++) { ASSERT_EQ(new_ptr[i], (char)i); }

  mm_free(new_ptr);
  return 1;
}

static int preserves_data_shrink(void) {
  char* ptr = mm_malloc(200);
  ASSERT_NOT_NULL(ptr);

  /* Fill with pattern */
  for (int i = 0; i < 200; i++) { ptr[i] = (char)i; }

  /* Shrink to 50 bytes */
  char* new_ptr = mm_realloc(ptr, 50);
  ASSERT_NOT_NULL(new_ptr);

  /* Verify first 50 bytes preserved */
  for (int i = 0; i < 50; i++) { ASSERT_EQ(new_ptr[i], (char)i); }

  mm_free(new_ptr);
  return 1;
}

static int preserves_data_same_size(void) {
  int* arr = mm_malloc(10 * sizeof(int));
  ASSERT_NOT_NULL(arr);

  for (int i = 0; i < 10; i++) { arr[i] = i * 100; }

  /* Realloc to same size */
  int* new_arr = mm_realloc(arr, 10 * sizeof(int));
  ASSERT_NOT_NULL(new_arr);

  /* Verify data intact */
  for (int i = 0; i < 10; i++) { ASSERT_EQ(new_arr[i], i * 100); }
  
  mm_free(new_arr);
  return 1;
} 

/* === Size Changes === */

static int grow_small_to_medium(void) {
  void* ptr = mm_malloc(64);
  ASSERT_NOT_NULL(ptr);

  void* new_ptr = mm_realloc(ptr, 1024);
  ASSERT_NOT_NULL(new_ptr);

  size_t usable = mm_get_usable_size(new_ptr);
  ASSERT_GE(usable, 1024);

  mm_free(new_ptr);
  return 1;
}

static int shrink_medium_to_small(void) {
  void* ptr = mm_malloc(1024);
  ASSERT_NOT_NULL(ptr);

  void* new_ptr = mm_realloc(ptr, 64);
  ASSERT_NOT_NULL(new_ptr);

  size_t usable = mm_get_usable_size(new_ptr);
  ASSERT_GE(usable, 64);

  mm_free(new_ptr);
  return 1;
}

static int grow_to_large_block(void) {
  /* Start small, grow to >1MB (mmap territory) */
  void* ptr = mm_malloc(100);
  ASSERT_NOT_NULL(ptr);

  void* new_ptr = mm_realloc(ptr, 2 * 1024 * 1024);
  ASSERT_NOT_NULL(new_ptr);

  size_t usable = mm_get_usable_size(new_ptr);
  ASSERT_GE(usable, 2 * 1024 * 1024);

  mm_free(new_ptr);
  return 1;
}

static int shrink_from_large_block(void) {
  /* Start large, shrink to TLSF size */
  void* ptr = mm_malloc(2 * 1024 * 1024);
  ASSERT_NOT_NULL(ptr);

  void* new_ptr = mm_realloc(ptr, 100);
  ASSERT_NOT_NULL(new_ptr);

  size_t usable = mm_get_usable_size(new_ptr);
  ASSERT_GE(usable, 100);

  mm_free(new_ptr);
  return 1;
}

/* === Multiple Reallocs === */

static int realloc_chain(void) {
  void* ptr = mm_malloc(10);
  ASSERT_NOT_NULL(ptr);

  /* Chain of reallocs */
  ptr = mm_realloc(ptr, 50);
  ASSERT_NOT_NULL(ptr);

  ptr = mm_realloc(ptr, 200);
  ASSERT_NOT_NULL(ptr);

  ptr = mm_realloc(ptr, 100);
  ASSERT_NOT_NULL(ptr);

  ptr = mm_realloc(ptr, 500);
  ASSERT_NOT_NULL(ptr);

  mm_free(ptr);
  return 1;
}

static int realloc_with_pattern(void) {
  char* ptr = mm_malloc(32);
  ASSERT_NOT_NULL(ptr);
  memset(ptr, 0xAA, 32);

  /* Grow and verify pattern preserved */
  ptr = mm_realloc(ptr, 128);
  ASSERT_NOT_NULL(ptr);
  for (int i = 0; i < 32; i++) { ASSERT_EQ((unsigned char)ptr[i], 0xAA); }

  /* Shrink and verify pattern still there */
  ptr = mm_realloc(ptr, 64);
  ASSERT_NOT_NULL(ptr);
  for (int i = 0; i < 32; i++) { ASSERT_EQ((unsigned char)ptr[i], 0xAA); }

  mm_free(ptr);
  return 1;
}

/* === Failure Cases === */

static int failure_leaves_original(void) {
  void* ptr = mm_malloc(100);
  ASSERT_NOT_NULL(ptr);

  /* Fill with data */
  memset(ptr, 0xBB, 100);

  /* Try to realloc invalid pointer (should fail) */
  void* bad = mm_realloc((void*)0x12345678, 200);
  ASSERT_NULL(bad);

  /* Original pointer should still be valid */
  ASSERT_EQ(mm_get_usable_size(ptr), mm_get_usable_size(ptr));

  mm_free(ptr);
  return 1;
}

/* === Parameterized Tests === */

static int grow(size_t size) {
  void* ptr = mm_malloc(size);
  ASSERT_NOT_NULL(ptr);

  void* new_ptr = mm_realloc(ptr, size * 2);
  ASSERT_NOT_NULL(new_ptr);

  size_t usable = mm_get_usable_size(new_ptr);
  ASSERT_GE(usable, size * 2);

  mm_free(new_ptr);
  return 1;
}

static int shrink(size_t size) {
  if (size < 2) return 1;  /* Skip too-small sizes */

  void* ptr = mm_malloc(size);
  ASSERT_NOT_NULL(ptr);

  void* new_ptr = mm_realloc(ptr, size / 2);
  ASSERT_NOT_NULL(new_ptr);

  size_t usable = mm_get_usable_size(new_ptr);
  ASSERT_GE(usable, size / 2);

  mm_free(new_ptr);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("mm_realloc");
  
  TEST_SECTION("Edge Cases");
  RUN_TEST(null_is_malloc);
  RUN_TEST(zero_is_free);
  RUN_TEST(null_and_zero);
  RUN_TEST(invalid_ptr);

  TEST_SECTION("Data Preservations");
  RUN_TEST(preserves_data_grow);
  RUN_TEST(preserves_data_shrink);
  RUN_TEST(preserves_data_same_size);

  TEST_SECTION("Size Changes");
  RUN_TEST(grow_small_to_medium);
  RUN_TEST(shrink_medium_to_small);
  RUN_TEST(grow_to_large_block);
  RUN_TEST(shrink_from_large_block);

  TEST_SECTION("Multiple Reallocs");
  RUN_TEST(realloc_chain);
  RUN_TEST(realloc_with_pattern);

  TEST_SECTION("Failure Handling");
  RUN_TEST(failure_leaves_original);

  TEST_SECTION("Parameterized: Grow");
  RUN_PARAMETERIZED(grow, TEST_SIZES, TEST_SIZES_COUNT);

  TEST_SECTION("Parameterized: Shrink");
  RUN_PARAMETERIZED(shrink, TEST_SIZES, TEST_SIZES_COUNT);

  TEST_SUITE_END();
  TEST_MAIN_END();
}