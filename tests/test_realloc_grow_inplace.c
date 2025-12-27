#include "test_framework.h"
#include "../src/memoman.h"
#include <string.h>

/* === In-Place Grow Tests === */

static int grow_returns_same_pointer(void) {
  mm_reset_allocator();
  
  /* Allocate two blocks */
  void* ptr1 = mm_malloc(256);
  void* ptr2 = mm_malloc(256);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);

  /* Free second block to create free space after first */
  mm_free(ptr2);

  void* original = ptr1;

  /* Grow first block - should absorb the free block */
  void* new_ptr = mm_realloc(ptr1, 400);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, original);  /* Same pointer! */

  mm_free(new_ptr);
  return 1;
}

static int grow_with_data_preservation(void) {
  mm_reset_allocator();
  
  void* ptr1 = mm_malloc(128);
  void* ptr2 = mm_malloc(512);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);

  /* Fill first block with pattern */
  for (int i = 0; i < 128; i++) {
    ((char*)ptr1)[i] = (char)(i & 0xFF);
  }

  void* original = ptr1;

  /* Free second block */
  mm_free(ptr2);

  /* Grow first block */
  void* new_ptr = mm_realloc(ptr1, 512);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, original);

  /* Verify data preserved */
  for (int i = 0; i < 128; i++) {
    ASSERT_EQ(((char*)new_ptr)[i], (char)(i & 0xFF));
  }

  mm_free(new_ptr);
  return 1;
}

static int grow_exact_fit(void) {
  mm_reset_allocator();
  
  /* Allocate blocks that will create exact fit scenario */
  void* ptr1 = mm_malloc(100);
  void* ptr2 = mm_malloc(100);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);

  void* original = ptr1;

  /* Free second block */
  mm_free(ptr2);

  /* Grow to exactly use the freed space */
  size_t usable1 = mm_malloc_usable_size(ptr1);
  void* new_ptr = mm_realloc(ptr1, usable1 + 100);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, original);

  mm_free(new_ptr);
  return 1;
}

static int grow_multiple_times(void) {
  mm_reset_allocator();
  
  void* ptr1 = mm_malloc(64);
  void* ptr2 = mm_malloc(512);
  void* ptr3 = mm_malloc(512);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);
  ASSERT_NOT_NULL(ptr3);

  void* original = ptr1;

  /* Free adjacent blocks */
  mm_free(ptr2);

  /* First grow */
  ptr1 = mm_realloc(ptr1, 256);
  ASSERT_EQ(ptr1, original);

  /* Free next block */
  mm_free(ptr3);

  /* Second grow */
  ptr1 = mm_realloc(ptr1, 512);
  ASSERT_EQ(ptr1, original);

  mm_free(ptr1);
  return 1;
}

static int grow_splits_excess(void) {
  mm_reset_allocator();
  
  /* Allocate small block followed by large block */
  void* ptr1 = mm_malloc(128);
  void* ptr2 = mm_malloc(2048);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);

  size_t free_before = mm_get_free_space();

  /* Free large block */
  mm_free(ptr2);

  /* Grow small block by a little - should split remainder */
  void* original = ptr1;
  void* new_ptr = mm_realloc(ptr1, 256);
  ASSERT_EQ(new_ptr, original);

  size_t free_after = mm_get_free_space();

  /* Should still have significant free space from split */
  ASSERT_GT(free_after, free_before - 2048);

  mm_free(new_ptr);
  return 1;
}

/* === Cannot Grow In-Place === */

static int grow_no_next_block(void) {
  mm_reset_allocator();
  
  /* Allocate single block at end */
  void* ptr = mm_malloc(256);
  ASSERT_NOT_NULL(ptr);

  /* Try to grow - no next block available */
  void* new_ptr = mm_realloc(ptr, 512);
  ASSERT_NOT_NULL(new_ptr);
  
  /* Should allocate new block since can't grow in place */
  /* (May or may not be same pointer depending on heap state) */

  mm_free(new_ptr);
  return 1;
}

static int grow_next_block_used(void) {
  mm_reset_allocator();
  
  void* ptr1 = mm_malloc(256);
  void* ptr2 = mm_malloc(256);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);

  void* original = ptr1;

  /* Next block is allocated - can't grow in place */
  void* new_ptr = mm_realloc(ptr1, 512);
  ASSERT_NOT_NULL(new_ptr);

  /* Will be different pointer since can't grow in place */
  ASSERT_NE(new_ptr, original);

  mm_free(new_ptr);
  mm_free(ptr2);
  return 1;
}

static int grow_next_block_too_small(void) {
  mm_reset_allocator();
  
  void* ptr1 = mm_malloc(256);
  void* ptr2 = mm_malloc(64);
  void* ptr3 = mm_malloc(256);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);
  ASSERT_NOT_NULL(ptr3);

  void* original = ptr1;

  /* Free tiny adjacent block */
  mm_free(ptr2);

  /* Try to grow by more than tiny block provides */
  void* new_ptr = mm_realloc(ptr1, 512);
  ASSERT_NOT_NULL(new_ptr);

  /* Should allocate new block */
  ASSERT_NE(new_ptr, original);

  mm_free(new_ptr);
  mm_free(ptr3);
  return 1;
}

/* === Edge Cases === */

static int grow_coalesces_multiple_free_blocks(void) {
  mm_reset_allocator();
  
  void* ptr1 = mm_malloc(128);
  void* ptr2 = mm_malloc(256);
  void* ptr3 = mm_malloc(256);
  void* ptr4 = mm_malloc(128);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);
  ASSERT_NOT_NULL(ptr3);
  ASSERT_NOT_NULL(ptr4);

  void* original = ptr1;

  /* Free adjacent blocks - they should coalesce */
  mm_free(ptr2);
  mm_free(ptr3);

  /* Grow should use coalesced free space */
  void* new_ptr = mm_realloc(ptr1, 512);
  ASSERT_EQ(new_ptr, original);

  mm_free(new_ptr);
  mm_free(ptr4);
  return 1;
}

static int grow_same_size_returns_same_pointer(void) {
  void* ptr = mm_malloc(256);
  ASSERT_NOT_NULL(ptr);

  void* original = ptr;

  /* Realloc to same size */
  void* new_ptr = mm_realloc(ptr, 256);
  ASSERT_EQ(new_ptr, original);

  mm_free(new_ptr);
  return 1;
}

/* === Shrink vs Grow Behavior === */

static int shrink_then_grow_same_pointer(void) {
  mm_reset_allocator();
  
  void* ptr = mm_malloc(1024);
  ASSERT_NOT_NULL(ptr);

  void* original = ptr;

  /* Shrink in place */
  ptr = mm_realloc(ptr, 256);
  ASSERT_EQ(ptr, original);

  /* Grow back using freed space */
  ptr = mm_realloc(ptr, 512);
  ASSERT_EQ(ptr, original);

  /* Grow more */
  ptr = mm_realloc(ptr, 1024);
  ASSERT_EQ(ptr, original);

  mm_free(ptr);
  return 1;
}

/* === Parameterized Tests === */

static int grow_double(size_t size) {
  if (size < 32 || size > 4096) return 1;  /* Reasonable range */

  mm_reset_allocator();
  
  void* ptr1 = mm_malloc(size);
  void* ptr2 = mm_malloc(size * 3);  /* Ensure enough space for doubling */
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);

  void* original = ptr1;
  mm_free(ptr2);

  /* Grow to double size */
  void* new_ptr = mm_realloc(ptr1, size * 2);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, original);

  mm_free(new_ptr);
  return 1;
}

static int grow_small_increment(size_t size) {
  if (size < 64) return 1;

  mm_reset_allocator();
  
  void* ptr1 = mm_malloc(size);
  void* ptr2 = mm_malloc(512);
  ASSERT_NOT_NULL(ptr1);
  ASSERT_NOT_NULL(ptr2);

  void* original = ptr1;
  mm_free(ptr2);

  /* Grow by small amount */
  void* new_ptr = mm_realloc(ptr1, size + 64);
  ASSERT_NOT_NULL(new_ptr);
  ASSERT_EQ(new_ptr, original);

  mm_free(new_ptr);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("mm_realloc in-place grow");
  
  TEST_SECTION("In-Place Grow");
  RUN_TEST(grow_returns_same_pointer);
  RUN_TEST(grow_with_data_preservation);
  RUN_TEST(grow_exact_fit);
  RUN_TEST(grow_multiple_times);
  RUN_TEST(grow_splits_excess);

  TEST_SECTION("Cannot Grow In-Place");
  RUN_TEST(grow_no_next_block);
  RUN_TEST(grow_next_block_used);
  RUN_TEST(grow_next_block_too_small);

  TEST_SECTION("Edge Cases");
  RUN_TEST(grow_coalesces_multiple_free_blocks);
  RUN_TEST(grow_same_size_returns_same_pointer);

  TEST_SECTION("Shrink Then Grow");
  RUN_TEST(shrink_then_grow_same_pointer);

  TEST_SECTION("Parameterized: Grow Double");
  RUN_PARAMETERIZED(grow_double, TEST_SIZES, TEST_SIZES_COUNT);

  TEST_SECTION("Parameterized: Grow Small Increment");
  RUN_PARAMETERIZED(grow_small_increment, TEST_SIZES, TEST_SIZES_COUNT);

  TEST_SUITE_END();
  TEST_MAIN_END();
}