#include "test_framework.h"
#include "../src/memoman.h"
#include <stdlib.h>
#include <stdint.h>

static int test_stack_pool() {
  /* 1. Create a pool on the stack (Explicit Ownership) 
   * Note: mm_size() is ~8KB, so we need a buffer larger than that.
   */
  uint8_t buffer[16384] __attribute__((aligned(16)));
  
  tlsf_t alloc = mm_create_with_pool(buffer, sizeof(buffer));
  ASSERT_NOT_NULL(alloc);
  
  /* 2. Allocate from it */
  void* p1 = (mm_malloc)(alloc, 64);
  ASSERT_NOT_NULL(p1);
  
  /* Verify p1 is inside buffer */
  uintptr_t p1_addr = (uintptr_t)p1;
  uintptr_t buf_addr = (uintptr_t)buffer;
  ASSERT_GE(p1_addr, buf_addr);
  ASSERT_LT(p1_addr, buf_addr + sizeof(buffer));
  
  /* 3. Free and Destroy */
  (mm_free)(alloc, p1);
  
  /* Stack buffer is automatically reclaimed, no leaks */
  return 1;
}

static int test_multiple_pools() {
  /* 1. Create two independent pools */
  size_t pool_size = 1024 * 1024;
  void* mem1 = malloc(pool_size);
  void* mem2 = malloc(pool_size);
  
  tlsf_t a1 = mm_create_with_pool(mem1, pool_size);
  tlsf_t a2 = mm_create_with_pool(mem2, pool_size);
  
  ASSERT_NOT_NULL(a1);
  ASSERT_NOT_NULL(a2);
  ASSERT_NE(a1, a2);

  /* 2. Alloc from both */
  void* p1 = (mm_malloc)(a1, 128);
  void* p2 = (mm_malloc)(a2, 128);
  
  ASSERT_NOT_NULL(p1);
  ASSERT_NOT_NULL(p2);
  
  /* Verify isolation */
  uintptr_t p1_addr = (uintptr_t)p1;
  uintptr_t mem1_addr = (uintptr_t)mem1;
  ASSERT_GE(p1_addr, mem1_addr);
  ASSERT_LT(p1_addr, mem1_addr + pool_size);
  
  uintptr_t p2_addr = (uintptr_t)p2;
  uintptr_t mem2_addr = (uintptr_t)mem2;
  ASSERT_GE(p2_addr, mem2_addr);
  ASSERT_LT(p2_addr, mem2_addr + pool_size);
  
  /* 3. Cleanup */
  free(mem1);
  free(mem2);
  return 1;
}

int main() {
  TEST_SUITE_BEGIN("Allocator Lifecycle");
  RUN_TEST(test_stack_pool);
  RUN_TEST(test_multiple_pools);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
