#include "test_framework.h"
#include "../src/memoman.h"
#include <stdlib.h>
#include <stdint.h>

static int test_stack_pool() {
  /* 1. Create a pool on the stack (Explicit Ownership) 
   * Note: sizeof(mm_allocator_t) is ~8KB, so we need a buffer larger than that.
   */
  uint8_t buffer[16384] __attribute__((aligned(16)));
  
  mm_allocator_t* alloc = mm_create(buffer, sizeof(buffer));
  ASSERT_NOT_NULL(alloc);
  
  /* 2. Allocate from it */
  void* p1 = mm_malloc_inst(alloc, 64);
  ASSERT_NOT_NULL(p1);
  
  /* Verify p1 is inside buffer */
  uintptr_t p1_addr = (uintptr_t)p1;
  uintptr_t buf_addr = (uintptr_t)buffer;
  ASSERT_GE(p1_addr, buf_addr);
  ASSERT_LT(p1_addr, buf_addr + sizeof(buffer));
  
  /* 3. Free and Destroy */
  mm_free_inst(alloc, p1);
  mm_destroy_instance(alloc);
  
  /* Stack buffer is automatically reclaimed, no leaks */
  return 1;
}

static int test_multiple_pools() {
  /* 1. Create two independent pools */
  size_t pool_size = 1024 * 1024;
  void* mem1 = malloc(pool_size);
  void* mem2 = malloc(pool_size);
  
  mm_allocator_t* a1 = mm_create(mem1, pool_size);
  mm_allocator_t* a2 = mm_create(mem2, pool_size);
  
  ASSERT_NOT_NULL(a1);
  ASSERT_NOT_NULL(a2);
  ASSERT_NE(a1, a2);

  /* 2. Alloc from both */
  void* p1 = mm_malloc_inst(a1, 128);
  void* p2 = mm_malloc_inst(a2, 128);
  
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
  mm_destroy_instance(a1);
  mm_destroy_instance(a2);
  
  free(mem1);
  free(mem2);
  return 1;
}

static int test_large_block_cleanup() {
  /* Create a small pool */
  uint8_t buffer[16384] __attribute__((aligned(16)));
  mm_allocator_t* alloc = mm_create(buffer, sizeof(buffer));
  ASSERT_NOT_NULL(alloc);

  /* Alloc something larger than the pool (triggers mmap) */
  size_t large_sz = 2 * 1024 * 1024;
  void* p = mm_malloc_inst(alloc, large_sz);
  ASSERT_NOT_NULL(p);
  
  /* Verify it's NOT in the buffer */
  uintptr_t p_addr = (uintptr_t)p;
  uintptr_t buf_addr = (uintptr_t)buffer;
  int in_buffer = (p_addr >= buf_addr) && (p_addr < buf_addr + sizeof(buffer));
  ASSERT(!in_buffer);
  
  /* Destroy instance should munmap the large block */
  mm_destroy_instance(alloc);
  
  return 1;
}

int main() {
  TEST_SUITE_BEGIN("Allocator Lifecycle");
  RUN_TEST(test_stack_pool);
  RUN_TEST(test_multiple_pools);
  RUN_TEST(test_large_block_cleanup);
  TEST_SUITE_END();
  TEST_MAIN_END();
}