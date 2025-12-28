#include "test_framework.h"
#include "../src/memoman.h"
#include <string.h>

/* Helper to acess internal block structure */
static tlsf_block_t* get_block(void* ptr) { return (tlsf_block_t*)((char*)ptr - BLOCK_HEADER_OVERHEAD); }

static int test_valid_heap() {
  TEST_RESET();
  ASSERT(mm_validate());

  void* p1 = mm_malloc(64);
  void* p2 = mm_malloc(128);
  void* p3 = mm_malloc(256);

  ASSERT(mm_validate());

  mm_free(p2);
  ASSERT(mm_validate());

  mm_free(p1);
  ASSERT(mm_validate());

  mm_free(p3);
  ASSERT(mm_validate());

  return 1;
}

static int test_corrupt_alignment() {
  TEST_RESET();
  void* p = mm_malloc(64);
  ASSERT(mm_validate());

  tlsf_block_t* b = get_block(p);
  size_t original_size = b->size;

  /* Corrupt size to be unaligned (add 4 bytes) */
  b->size += 4;

  printf("  (Expect error message below) -> ");
  int result = mm_validate();

  b->size = original_size;
  ASSERT(result == 0);

  mm_free(p);
  return 1;
}

static int test_corrupt_overflow() {
  TEST_RESET();
  void* p = mm_malloc(64);
  tlsf_block_t* b = get_block(p);
  size_t original_size = b->size;

  /* Set size to extend beyond heap end */
  b->size = (1024 * 1024 * 2) | (original_size & 3);

  printf("  (Expect error message below) -> ");
  int result = mm_validate();

  b->size = original_size;
  ASSERT(result == 0);

  mm_free(p);
  return 1;
}

static int test_corrupt_free_list() {
  TEST_RESET();

  /* Create a free block */
  void* p1 = mm_malloc(64);
  void* p2 = mm_malloc(64); /* Barrier */
  mm_free(p1);

  ASSERT(mm_validate());

  /* Manually clear the bitmap bit for this block size */
  int fl, sl;
  mm_get_mapping_indices(64, &fl, &sl);

  sys_allocator->sl_bitmap[fl] &= ~(1U << sl);
  
  printf("  (Expect error message below) -> ");
  int result = mm_validate();

  /* Restore bit */
  sys_allocator->sl_bitmap[fl] |= (1u << sl);

  ASSERT(result == 0);

  mm_free(p2);
  return 1;
}

static int test_corrupt_coalescing() {
  TEST_RESET();

  void* p1 = mm_malloc(64);
  void* p2 = mm_malloc(64);

  /* Free p1. p2;s header should have PREV_FREE set. */
  mm_free(p1);

  tlsf_block_t* b2 = get_block(p2);

  /* Manually clear PREV_FREE flag on b2 */
  b2->size &= ~TLSF_PREV_FREE;

  printf("  (Expect error message below) -> ");
  int result = mm_validate();

  /* Restore flag */
  b2->size |= TLSF_PREV_FREE;

  ASSERT(result == 0);

  mm_free(p2);
  return 1;
}

int main() {
  TEST_SUITE_BEGIN("Validation API");
  RUN_TEST(test_valid_heap);
  RUN_TEST(test_corrupt_alignment);
  RUN_TEST(test_corrupt_overflow);
  RUN_TEST(test_corrupt_free_list);
  RUN_TEST(test_corrupt_coalescing);
  TEST_SUITE_END();
  TEST_MAIN_END();
}