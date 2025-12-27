#include "test_framework.h"
#include "../src/memoman.h"

static int test_mixed_allocation(void) {
  void* small = mm_malloc(64);                // size class
  void* medium = mm_malloc(4096);             // free list
  void* large = mm_malloc(2 * 1024 * 1024);   // direct mmap (2MB)

  ASSERT_NOT_NULL(small);
  ASSERT_NOT_NULL(medium);
  ASSERT_NOT_NULL(large);

  // write to each to verify they're valid
  memset(small, 0xAA, 64);
  memset(medium, 0xBB, 4096);
  memset(large, 0xCC, 2 * 1024 * 1024);

  mm_free(medium);
  mm_free(large);
  mm_free(small);
  return 1;
}

static int test_lazy_init(void) {
  mm_destroy();
  extern char* sys_heap_base;
  ASSERT_NOT_NULL(mm_malloc(100));
  ASSERT_NOT_NULL(sys_heap_base);
  return 1;
}

static int test_pointer_stability(void) {
  mm_reset_allocator();
  void* first = mm_malloc(100);
  ASSERT_NOT_NULL(first);
  memset(first, 0x42, 100);
  
  // allocate enough to trigger heap growth (1MB initial)
  for (int i = 0; i < 100; i++) {
    ASSERT_NOT_NULL(mm_malloc(10000));
  }
  
  // verify original pointer still valid
  for (int i = 0; i < 100; i++) { ASSERT_EQ(((unsigned char*)first)[i], 0x42); }
  return 1;
}

static int test_large_bypass(void) {
  mm_reset_allocator();
  void* large1 = mm_malloc(3 * 1024 * 1024);  // 3MB
  void* large2 = mm_malloc(5 * 1024 * 1024);  // 5MB
  ASSERT_NOT_NULL(large1);
  ASSERT_NOT_NULL(large2);

  // verify they're separate from main heap
  size_t free_space = mm_get_free_space();
  size_t total_heap = (size_t)(sys_allocator->heap_end - sys_allocator->heap_start);
  size_t heap_used = total_heap - free_space;
  ASSERT_LT(heap_used, 1024 * 1024);  // main heap should be mostly empty

  mm_free(large1);
  mm_free(large2);
  return 1;
}

static int test_growth_boundaries(void) {
  mm_reset_allocator();
  extern size_t sys_heap_cap;
  
  // allocate exactly to 1MB boundary
  size_t allocated = 0;
  while (allocated < 1024 * 1024 - 1024) {
    ASSERT_NOT_NULL(mm_malloc(1000));
    allocated += 1000 + sizeof(tlsf_block_t);
  }

  // this should trigger first mprotect growth
  ASSERT_NOT_NULL(mm_malloc(10000));
  ASSERT_GE(sys_heap_cap, 2 * 1024 * 1024);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("MMAP & Heap Growth");
  RUN_TEST(test_mixed_allocation);
  RUN_TEST(test_lazy_init);
  RUN_TEST(test_pointer_stability);
  RUN_TEST(test_large_bypass);
  RUN_TEST(test_growth_boundaries);
  TEST_SUITE_END();
  TEST_MAIN_END();
}