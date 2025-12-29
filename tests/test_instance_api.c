#include "test_framework.h"
#include "../src/memoman.h"
#include <string.h>
#include <limits.h>
#include <stdint.h>

static int test_calloc_inst_basic() {
  /* Create a pool on the stack */
  uint8_t buffer[16384] __attribute__((aligned(16)));
  mm_allocator_t* alloc = mm_create(buffer, sizeof(buffer));
  ASSERT_NOT_NULL(alloc);

  /* Allocate array of 10 ints */
  int* arr = (mm_calloc)(alloc, 10, sizeof(int));
  ASSERT_NOT_NULL(arr);

  /* Verify bounds (must be within stack buffer) */
  uintptr_t p_addr = (uintptr_t)arr;
  uintptr_t buf_addr = (uintptr_t)buffer;
  ASSERT_GE(p_addr, buf_addr);
  ASSERT_LT(p_addr, buf_addr + sizeof(buffer));

  /* Verify zero initialization */
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(arr[i], 0);
  }

  return 1;
}

static int test_calloc_inst_overflow() {
  uint8_t buffer[16384] __attribute__((aligned(16)));
  mm_allocator_t* alloc = mm_create(buffer, sizeof(buffer));
  
  /* Attempt allocation that overflows size_t calculation */
  void* p = (mm_calloc)(alloc, SIZE_MAX, 2);
  ASSERT_NULL(p);

  return 1;
}

static int test_realloc_inst_growth() {
  uint8_t buffer[16384] __attribute__((aligned(16)));
  mm_allocator_t* alloc = mm_create(buffer, sizeof(buffer));
  
  void* p = (mm_malloc)(alloc, 64);
  ASSERT_NOT_NULL(p);
  memset(p, 0xAA, 64);

  /* Grow block */
  void* p2 = (mm_realloc)(alloc, p, 128);
  ASSERT_NOT_NULL(p2);
  
  /* Verify data preservation */
  for (int i = 0; i < 64; i++) {
    ASSERT_EQ(((unsigned char*)p2)[i], 0xAA);
  }

  /* Verify it is still inside the instance */
  uintptr_t p_addr = (uintptr_t)p2;
  uintptr_t buf_addr = (uintptr_t)buffer;
  ASSERT_GE(p_addr, buf_addr);
  ASSERT_LT(p_addr, buf_addr + sizeof(buffer));

  return 1;
}

static int test_realloc_inst_oom() {
  /* Create a small pool (16KB) - enough for struct (~8KB) + heap */
  uint8_t buffer[16384] __attribute__((aligned(16)));
  mm_allocator_t* alloc = mm_create(buffer, sizeof(buffer));
  ASSERT_NOT_NULL(alloc);
  
  /* Use up some space */
  void* p = (mm_malloc)(alloc, 2000);
  ASSERT_NOT_NULL(p);

  /* Try to grow beyond capacity (2000 -> 20000) */
  /* This should FAIL because instances cannot grow automatically */
  void* p2 = (mm_realloc)(alloc, p, 20000);
  ASSERT_NULL(p2);

  /* Original pointer should still be valid */
  (mm_free)(alloc, p);
  
  return 1;
}

static int test_realloc_inst_inplace() {
  uint8_t buffer[16384] __attribute__((aligned(16)));
  mm_allocator_t* alloc = mm_create(buffer, sizeof(buffer));

  void* p1 = (mm_malloc)(alloc, 64);
  void* p2 = (mm_malloc)(alloc, 64);
  ASSERT_NOT_NULL(p1);
  ASSERT_NOT_NULL(p2);

  /* Free p2 to create a hole immediately after p1 */
  (mm_free)(alloc, p2);

  /* Grow p1 into p2's space */
  void* p3 = (mm_realloc)(alloc, p1, 100);
  
  /* Should have coalesced and returned same pointer */
  ASSERT_EQ(p3, p1);

  return 1;
}

int main() {
  /* 
   * Note: We use TEST_SUITE_BEGIN/END which init/destroy the global allocator,
   * but these tests use their own stack-based instances to verify isolation.
   */
  TEST_SUITE_BEGIN("Instance API (Calloc/Realloc)");
  RUN_TEST(test_calloc_inst_basic);
  RUN_TEST(test_calloc_inst_overflow);
  RUN_TEST(test_realloc_inst_growth);
  RUN_TEST(test_realloc_inst_oom);
  RUN_TEST(test_realloc_inst_inplace);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
