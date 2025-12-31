#include "test_framework.h"
#include "memoman_test_internal.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * Calculate fragmentation percentage
 *
 * Fragmentation = (Total Free Space - Largest Free Block) / Total Free Space * 100
 *
 * This measures how "scattered" free memory is
 * - 0% = All free space is in one contiguous block (ideal)
 * - 100% = Free space is maximally fragmented into small pieces
 */
static double calculate_fragmentation(void) {
  if (!sys_allocator) return 0.0;
  struct mm_allocator_t* ctrl = (struct mm_allocator_t*)sys_allocator;

  size_t total_free = 0;
  size_t largest_block = 0;
  int free_block_count = 0;

  /* Iterate through all free lists */
  for (int fl = 0; fl < TLSF_FLI_MAX; fl++) {
    if (!((ctrl->fl_bitmap & (1U << fl)))) continue;

    for (int sl = 0; sl < TLSF_SLI_COUNT; sl++) {
      tlsf_block_t* block = ctrl->blocks[fl][sl];

      while (block != NULL) {
        size_t block_size = block->size & TLSF_SIZE_MASK;
        total_free += block_size;
        free_block_count++;

        if (block_size > largest_block) { largest_block = block_size; }

        block = block->next_free;
      }
    }
  }

  if (total_free == 0) return 0.0;

  /* Fragmentation = unusable free space / total free space */
  double fragmentation = ((double)(total_free - largest_block) / total_free) * 100;

  return fragmentation;
}

/* Helper: count number of free blocks */
static int count_free_blocks(void) {
  if (!sys_allocator) return 0;
  struct mm_allocator_t* ctrl = (struct mm_allocator_t*)sys_allocator;

  int count = 0;
  for (int fl = 0; fl < TLSF_FLI_MAX; fl++) {
    if (!((ctrl->fl_bitmap & (1U << fl)))) continue;
    for (int sl = 0; sl < TLSF_SLI_COUNT; sl++) {
      tlsf_block_t* block = ctrl->blocks[fl][sl];
      while (block != NULL) {
        count++;
        block = block->next_free;
      }
    }
  }
  return count;
}


/*
 * Test 1: No fragmentation - single allocation and free
 */
static int test_no_fragmentation(void) {
  mm_reset_allocator();

  void* ptr = mm_malloc(1024);
  ASSERT_NOT_NULL(ptr);

  mm_free(ptr);

  double frag = calculate_fragmentation();
  int blocks = count_free_blocks();
  printf("   Fragmentation: %.2f%%, %d\n", frag, blocks);
  ASSERT_LT(frag, 1.0);  /* Should be near 0% */
  ASSERT_EQ(blocks, 1);  /* Should have exactly 1 free block */

  return 1;
}

/*
 * Test 2: Checkerboard fragmentation pattern
 * Allocate many blocks, free every other one, but keep some allocated
 * to prevent coalescing
 */
static int test_checkerboard_fragmentation(void) {
  mm_reset_allocator();

  #define NUM_BLOCKS 100
  void* ptrs[NUM_BLOCKS];

  /* Allocate 100 small blocks */
  for (int i = 0; i < NUM_BLOCKS; i++) {
    ptrs[i] = mm_malloc(64);
    ASSERT_NOT_NULL(ptrs[i]);
  }

  /* Free every other block (checkerboard pattern) */
  /* This creates 50 free blocks separated by 50 allocated blocks */
  for (int i = 0; i < NUM_BLOCKS; i += 2) { mm_free(ptrs[i]); }

  double frag = calculate_fragmentation();
  int free_blocks = count_free_blocks();
  printf("   Fragmentation after checkerboard free: %.2f%% fragmentation, %d free blocks\n", frag, free_blocks);

  /* With allocated blocks still in place, we should multiple free blocks */
  /* The fragmentation depends on whether adjacent freed blocks coalesced */
  ASSERT_GT(free_blocks, 1);

  /* Free remaining blocks */
  for (int i = 1; i < NUM_BLOCKS; i += 2) { mm_free(ptrs[i]); }

  /* After freeing everything, coalescing should reduce fragmentation */
  frag = calculate_fragmentation();
  free_blocks = count_free_blocks();
  printf("   After freeing all: %.2f%% fragmentation, %d free blocks\n", frag, free_blocks);
  ASSERT_LT(frag, 1.0);  /* Should be near 0% due to coalescing */

  return 1;
  #undef NUM_BLOCKS
}

/* Test 3: Extreme fragmentation with guar blocks 
 * Allocate alternating blokcs to prevent any coalescing
 */
static int test_extreme_fragmentation(void) {
  mm_reset_allocator();

  #define NUM_EXTREME 100000
  void* small_ptrs[NUM_EXTREME];
  void* guard_ptrs[NUM_EXTREME];

  /* Allocate alternating small and guard blocks */
  for (int i = 0; i < NUM_EXTREME; i++) { 
    small_ptrs[i] = mm_malloc(32);
    guard_ptrs[i] = mm_malloc(32);
    ASSERT_NOT_NULL(small_ptrs[i]);
    ASSERT_NOT_NULL(guard_ptrs[i]); 
  }
  
  /* Free only the small blocks, guards prevent coalescing */
  for (int i = 0; i < NUM_EXTREME; i++) { mm_free(small_ptrs[i]); }

  double frag = calculate_fragmentation();
  int free_blocks = count_free_blocks();
  printf("   Extreme fragmentation: %.2f%%,%d free blocks\n", frag, free_blocks);

  /* Should have many separate blocks */
  ASSERT_GT(free_blocks, 50);

  /* Cleanup guards to allow coalescing */
  for (int i = 0; i < NUM_EXTREME; i++) { mm_free(guard_ptrs[i]); }

  frag = calculate_fragmentation();
  free_blocks = count_free_blocks();
  printf("   After cleanup: %.2f%%, %d free blocks\n", frag, free_blocks);

  return 1;
  #undef NUM_EXTREME
}

int main(void) {
  TEST_SUITE_BEGIN("Memory Fragementation Tests");

  RUN_TEST(test_no_fragmentation);
  RUN_TEST(test_checkerboard_fragmentation);
  RUN_TEST(test_extreme_fragmentation);

  TEST_SUITE_END();
  TEST_MAIN_END();
}
