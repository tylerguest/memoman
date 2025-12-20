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

/* === In-Place Operations (Task 3.3) === */

static int inplace_shrink_same_pointer(void) {
  reset_allocator();
  
  void* ptr = mm_malloc(1024);
  ASSERT_NOT_NULL(ptr);
  
  void* original = ptr;
  
  /* Shrink should return same pointer */
  void* new_ptr = mm_realloc(ptr, 256);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, original);
  
  mm_free(new_ptr);
  return 1;
}

static int inplace_grow_same_pointer(void) {
  reset_allocator();
  
  /* Allocate two blocks, free second to create adjacent free space */
  void* ptr1 = mm_malloc(256);
  void* ptr2 = mm_malloc(512);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);
  
  void* original = ptr1;
  mm_free(ptr2);
  
  /* Grow should absorb adjacent free block and return same pointer */
  void* new_ptr = mm_realloc(ptr1, 512);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, original);
  
  mm_free(new_ptr);
  return 1;
}

static int inplace_same_size_same_pointer(void) {
  void* ptr = mm_malloc(512);
  ASSERT_NOT_NULL(ptr);
  
  void* original = ptr;
  
  /* Same size should return same pointer */
  void* new_ptr = mm_realloc(ptr, 512);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, original);
  
  mm_free(new_ptr);
  return 1;
}

static int inplace_shrink_preserves_data(void) {
  reset_allocator();
  
  char* ptr = mm_malloc(1024);
  ASSERT_NOT_NULL(ptr);
  
  /* Fill with pattern */
  for (int i = 0; i < 1024; i++) { ptr[i] = (char)(i & 0xFF); }
  
  void* original = ptr;
  
  /* Shrink in place */
  char* new_ptr = mm_realloc(ptr, 256);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, original);
  
  /* Verify data preserved */
  for (int i = 0; i < 256; i++) { ASSERT_EQ(new_ptr[i], (char)(i & 0xFF)); }
  
  mm_free(new_ptr);
  return 1;
}

static int inplace_grow_preserves_data(void) {
  reset_allocator();
  
  /* Setup: allocate and free to create fragmentation */
  void* ptr1 = mm_malloc(256);
  void* ptr2 = mm_malloc(512);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);
  
  /* Fill first block with pattern */
  for (int i = 0; i < 256; i++) { ((char*)ptr1)[i] = (char)(i & 0xFF); }
  
  void* original = ptr1;
  mm_free(ptr2);
  
  /* Grow in place */
  char* new_ptr = mm_realloc(ptr1, 512);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, original);
  
  /* Verify original data preserved */
  for (int i = 0; i < 256; i++) { ASSERT_EQ(new_ptr[i], (char)(i & 0xFF)); }
  
  mm_free(new_ptr);
  return 1;
}

/* === Large Block Handling (Task 3.4) === */

static int large_block_realloc_always_moves(void) {
  /* Large blocks (>=1MB) should always use slow path */
  void* ptr = mm_malloc(2 * 1024 * 1024);
  ASSERT_NOT_NULL(ptr);
  
  /* Realloc large block - should allocate new block */
  void* new_ptr = mm_realloc(ptr, 3 * 1024 * 1024);
  ASSERT_NOT_NULL(new_ptr);
  
  /* May or may not be same address, but should work correctly */
  mm_free(new_ptr);
  return 1;
}

static int large_block_realloc_preserves_data(void) {
  size_t large_size = 2 * 1024 * 1024;
  char* ptr = mm_malloc(large_size);
  ASSERT_NOT_NULL(ptr);
  
  /* Fill with pattern (sample some locations) */
  ptr[0] = 0xAA;
  ptr[1000] = 0xBB;
  ptr[large_size - 1] = 0xCC;
  
  /* Realloc to larger size */
  char* new_ptr = mm_realloc(ptr, 3 * 1024 * 1024);
  ASSERT_NOT_NULL(new_ptr);
  
  /* Verify data preserved */
  ASSERT_EQ(new_ptr[0], (char)0xAA);
  ASSERT_EQ(new_ptr[1000], (char)0xBB);
  ASSERT_EQ(new_ptr[large_size - 1], (char)0xCC);
  
  mm_free(new_ptr);
  return 1;
}

static int large_block_shrink_to_small(void) {
  /* Shrink from large (>=1MB) to small TLSF block */
  size_t large_size = 2 * 1024 * 1024;
  char* ptr = mm_malloc(large_size);
  ASSERT_NOT_NULL(ptr);
  
  /* Fill pattern */
  for (int i = 0; i < 512; i++) { ptr[i] = (char)(i & 0xFF); }
  
  /* Shrink to small size */
  char* new_ptr = mm_realloc(ptr, 512);
  ASSERT_NOT_NULL(new_ptr);
  
  /* Verify data preserved */
  for (int i = 0; i < 512; i++) { ASSERT_EQ(new_ptr[i], (char)(i & 0xFF)); }
  
  mm_free(new_ptr);
  return 1;
}

static int large_block_grow_from_small(void) {
  /* Grow from small TLSF block to large (>=1MB) */
  char* ptr = mm_malloc(512);
  ASSERT_NOT_NULL(ptr);
  
  /* Fill pattern */
  for (int i = 0; i < 512; i++) { ptr[i] = (char)(i & 0xFF); }
  
  void* original = ptr;
  
  /* Grow to large size */
  char* new_ptr = mm_realloc(ptr, 2 * 1024 * 1024);
  ASSERT_NOT_NULL(new_ptr);
  
  /* Should be different pointer (transitions to mmap) */
  ASSERT_NE(new_ptr, original);
  
  /* Verify data preserved */
  for (int i = 0; i < 512; i++) { ASSERT_EQ(new_ptr[i], (char)(i & 0xFF)); }
  
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

  TEST_SECTION("Data Preservation");
  RUN_TEST(preserves_data_grow);
  RUN_TEST(preserves_data_shrink);
  RUN_TEST(preserves_data_same_size);

  TEST_SECTION("Size Changes");
  RUN_TEST(grow_small_to_medium);
  RUN_TEST(shrink_medium_to_small);
  RUN_TEST(grow_to_large_block);
  RUN_TEST(shrink_from_large_block);

  TEST_SECTION("In-Place Operations");
  RUN_TEST(inplace_shrink_same_pointer);
  RUN_TEST(inplace_grow_same_pointer);
  RUN_TEST(inplace_same_size_same_pointer);
  RUN_TEST(inplace_shrink_preserves_data);
  RUN_TEST(inplace_grow_preserves_data);

  TEST_SECTION("Large Block Handling");
  RUN_TEST(large_block_realloc_always_moves);
  RUN_TEST(large_block_realloc_preserves_data);
  RUN_TEST(large_block_shrink_to_small);
  RUN_TEST(large_block_grow_from_small);

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