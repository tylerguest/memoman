#include "test_framework.h"
#include "../src/memoman.h"
#include <stdint.h>

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

static int test_add_pool_aligns_end(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  uint8_t pool2[8193] __attribute__((aligned(16)));
  ASSERT_NOT_NULL(mm_add_pool(alloc, pool2, sizeof(pool2)));
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

int main(void) {
  /* 
   * Note: These tests use stack-allocated pools and do not use the 
   * global test instance from test_framework.h
   */
  printf("\n" COLOR_BOLD "=== Discontiguous Pools ===" COLOR_RESET "\n");
  
  RUN_TEST(test_allocation_across_pools);
  RUN_TEST(test_add_pool_aligns_end);
  RUN_TEST(test_add_pool_rejects_overlap);
  
  TEST_MAIN_END();
}
