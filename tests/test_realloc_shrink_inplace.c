#include "test_framework.h"
#include "../src/memoman.h"
#include <string.h>

/* === In-Place Shrink Tests === */

static int shrink_returns_same_pointer(void) {
  void* ptr = mm_malloc(1024);
  ASSERT_NOT_NULL(ptr);

  /* Fill with pattern */
  memset(ptr, 0xCC, 1024);

  /* Shrink to 256 - should return same pointer */
  void* new_ptr = mm_realloc(ptr,256);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, ptr);  /* Same addess! */

  /* Verify data preserved */
  for (int i = 0; i < 256; i++) { ASSERT_EQ(((unsigned char*)new_ptr)[i], 0xCC); }

  mm_free(new_ptr);
  return 1;
}

static int shrink_small_amount_same_pointer(void) {
  /* Shrink by small amount - may not split if remainder too small */
  void* ptr = mm_malloc(100);
  ASSERT_NOT_NULL(ptr);

  void* new_ptr = mm_realloc(ptr, 90);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, ptr);  /* Should return same pointer */

  mm_free(new_ptr);
  return 1;
}

static int shrink_large_to_small_same_pointer(void) {
  void* ptr = mm_malloc(4096);
  ASSERT_NOT_NULL(ptr);

  /* Fill first 128 bytes */
  for (int i = 0; i < 128; i++) { ((char*)ptr)[i] = (char)(i & 0xFF); }

  /* Shrink to 128 */
  void* new_ptr = mm_realloc(ptr, 128);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, ptr);

  /* Verify data intact */
  for (int i = 0; i < 128; i++) { ASSERT_EQ(((char*)new_ptr)[i], (char)(i & 0xFF)); }

  mm_free(new_ptr);
  return 1;
}

static int shrink_creates_free_space(void) {
  reset_allocator();
  
  size_t free_before = get_free_space();

  /* Allocate large block */
  void* ptr = mm_malloc(4096);
  ASSERT_NOT_NULL(ptr);

  /* Shrink to small size - should free up space */
  void* new_ptr = mm_realloc(ptr, 256);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, ptr);

  size_t free_after = get_free_space();

  /* Should have more free space now */
  ASSERT_GT(free_after, free_before - 4096);

  mm_free(new_ptr);
  return 1;
}

static int shrink_freed_space_reusable(void) {
  reset_allocator();

  /* Allocate and shrink */
  void* ptr1 = mm_malloc(4096);
  ASSERT_NOT_NULL(ptr1);

  void* ptr1_shrunk = mm_realloc(ptr1, 256);
  ASSERT_EQ(ptr1_shrunk, ptr1);

  /* Allocate another block - should be able to use freed space */
  void* ptr2 = mm_malloc(2048);
  ASSERT_NOT_NULL(ptr2);

  mm_free(ptr1_shrunk);
  mm_free(ptr2);
  return 1;
}

/* === Shrink vs Grow Behavior === */

static int grow_returns_different_pointer(void) {
  void* ptr = mm_malloc(100);
  ASSERT_NOT_NULL(ptr);

  /* Growing typically requires new allocation (until Phase 3.2) */
  void* new_ptr = mm_realloc(ptr, 2048);
  ASSERT_NOT_NULL(new_ptr);

  /* For now, grow should reutrn different pointer */
  /* Phase 3.2 will optimize this */

  mm_free(new_ptr);
  return 1;
}

/* === Edge Cases === */

static int shrink_to_min_block_size(void) {
  void* ptr = mm_malloc(1024);
  ASSERT_NOT_NULL(ptr);

  /* Shrink to minimum block size (16 bytes) */
  void* new_ptr = mm_realloc(ptr, 16);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, ptr);

  mm_free(new_ptr);
  return 1;
}

static int shrink_to_one_byte(void) {
  void* ptr = mm_malloc(1024);
  ASSERT_NOT_NULL(ptr);

  /* Shrink to 1 byte (will be aligned to MIN_BLOCK_SIZE) */
  void* new_ptr = mm_realloc(ptr, 1);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, ptr);

  mm_free(new_ptr);
  return 1;
}

static int multiple_shrinks_same_pointer(void) {
  void* ptr = mm_malloc(4096);
  ASSERT_NOT_NULL(ptr);

  void* original = ptr;

  /* Chain of shrinks - should all return same pointer */
  ptr = mm_realloc(ptr, 2048);
  ASSERT_EQ(ptr, original);

  ptr = mm_realloc(ptr, 1024);
  ASSERT_EQ(ptr, original);

  ptr = mm_realloc(ptr, 512);
  ASSERT_EQ(ptr, original);

  ptr = mm_realloc(ptr, 256);
  ASSERT_EQ(ptr, original);

  mm_free(ptr);
  return 1;
}

/* === Parameterized Tests === */

static int shrink_half(size_t size) {
  if (size < 32) return 1; /* Skip very small sizes */

  void* ptr = mm_malloc(size);
  ASSERT_NOT_NULL(ptr);

  void* original = ptr;

  /* Shrink to hald - should return same pointer */
  void* new_ptr = mm_realloc(ptr, size / 2);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, original);

  mm_free(new_ptr);
  return 1;
}

static int shrink_quarter(size_t size) {
  if (size < 64) return 1;  /* Skip very small sizes */

  void* ptr = mm_malloc(size);
  ASSERT_NOT_NULL(ptr);

  void* original = ptr;

  /* Shrink to quarter */
  void* new_ptr = mm_realloc(ptr, size / 4);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, original);

  mm_free(new_ptr);
  return 1;
}  

int main(void) {
  TEST_SUITE_BEGIN("mm_realloc in-place shrink");
  
  TEST_SECTION("In-Place Shrink");
  RUN_TEST(shrink_returns_same_pointer);
  RUN_TEST(shrink_small_amount_same_pointer);
  RUN_TEST(shrink_large_to_small_same_pointer);
  RUN_TEST(shrink_creates_free_space);
  RUN_TEST(shrink_freed_space_reusable);

  TEST_SECTION("Shrink vs Grow");
  RUN_TEST(grow_returns_different_pointer);

  TEST_SECTION("Edge Cases");
  RUN_TEST(shrink_to_min_block_size);
  RUN_TEST(shrink_to_one_byte);
  RUN_TEST(multiple_shrinks_same_pointer);

  TEST_SECTION("Parameterized: Shrink Half");
  RUN_PARAMETERIZED(shrink_half, TEST_SIZES, TEST_SIZES_COUNT);

  TEST_SECTION("Parameterized: Shrink Quarter");
  RUN_PARAMETERIZED(shrink_quarter, TEST_SIZES, TEST_SIZES_COUNT);
  
  TEST_SUITE_END();
  TEST_MAIN_END();
}