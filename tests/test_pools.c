#include "test_framework.h"
#include "../src/memoman.h"
#include "memoman_test_internal.h"
#include <stdint.h>

static inline size_t block_size(const tlsf_block_t* block) { return block->size & TLSF_SIZE_MASK; }
static inline int block_is_prev_free(const tlsf_block_t* block) { return (block->size & TLSF_PREV_FREE) != 0; }

static int test_allocation_across_pools(void) {
  /* Pool 1: ~12KB. 
   * mm_size() is ~8KB, leaving ~4KB for allocation. */
  uint8_t pool1[12288] __attribute__((aligned(8)));
  tlsf_t alloc = mm_create_with_pool(pool1, sizeof(pool1));
  
  /* Fill Pool 1 */
  void* p1 = (mm_malloc)(alloc, 3000);
  ASSERT_NOT_NULL(p1);

  /* Try to alloc another 3000 - should fail */
  void* p2 = (mm_malloc)(alloc, 3000);
  ASSERT_NULL(p2);

  /* Add Pool 2: 8KB */
  uint8_t pool2[8192] __attribute__((aligned(8)));
  ASSERT_NOT_NULL(mm_add_pool(alloc, pool2, sizeof(pool2)));

  /* Now alloc should succeed (from Pool 2) */
  p2 = (mm_malloc)(alloc, 3000);
  ASSERT_NOT_NULL(p2);

  /* Verify pointers are in different regions */
  uintptr_t addr1 = (uintptr_t)p1;
  uintptr_t addr2 = (uintptr_t)p2;
  uintptr_t diff = (addr1 > addr2) ? (addr1 - addr2) : (addr2 - addr1);
  ASSERT_GT(diff, 4096);

  return 1;
}

static int test_add_pool_rejects_misaligned_start(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  uint8_t pool2[8192] __attribute__((aligned(16)));
  void* misaligned = (void*)((uintptr_t)pool2 + 1);
  ASSERT_NULL(mm_add_pool(alloc, misaligned, sizeof(pool2) - 1));
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_add_pool_rejects_misaligned_size(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  uint8_t pool2[8193] __attribute__((aligned(16)));
  ASSERT_NULL(mm_add_pool(alloc, pool2, sizeof(pool2)));
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_add_pool_rejects_overlap(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  size_t offset = mm_size() + 1024;
  size_t align = mm_align_size();
  offset = (offset + (align - 1)) & ~(align - 1);

  pool_t overlap = mm_add_pool(alloc, backing + offset, 4096);
  ASSERT_NULL(overlap);
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_pool_layout_prev_phys_outside_pool(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  pool_t pool = mm_get_pool(alloc);
  ASSERT_NOT_NULL(pool);

  tlsf_block_t* first = (tlsf_block_t*)pool;
  ASSERT_EQ(block_is_prev_free(first), 0);

  mm_pool_desc_t* desc = NULL;
  struct mm_allocator_t* ctrl = (struct mm_allocator_t*)alloc;
  for (size_t i = 0; i < MM_MAX_POOLS; i++) {
    if (!ctrl->pools[i].active) continue;
    if ((pool_t)ctrl->pools[i].start == pool) {
      desc = &ctrl->pools[i];
      break;
    }
  }
  ASSERT_NOT_NULL(desc);

  tlsf_block_t* epilogue = (tlsf_block_t*)((char*)desc->end - BLOCK_HEADER_OVERHEAD);
  ASSERT(block_size(epilogue) == 0);
  ASSERT(block_is_prev_free(epilogue));
  return 1;
}

int main(void) {
  /* 
   * Note: These tests use stack-allocated pools and do not use the 
   * global test instance from test_framework.h
   */
  printf("\n" COLOR_BOLD "=== Discontiguous Pools ===" COLOR_RESET "\n");
  
  RUN_TEST(test_allocation_across_pools);
  RUN_TEST(test_add_pool_rejects_misaligned_start);
  RUN_TEST(test_add_pool_rejects_misaligned_size);
  RUN_TEST(test_add_pool_rejects_overlap);
  RUN_TEST(test_pool_layout_prev_phys_outside_pool);
  
  TEST_MAIN_END();
}
