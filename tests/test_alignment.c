#include "test_framework.h"
#include "memoman_test_internal.h"

static inline size_t block_size(const tlsf_block_t* block) { return block->size & TLSF_SIZE_MASK; }
static inline int block_is_free(const tlsf_block_t* block) { return (block->size & TLSF_BLOCK_FREE) != 0; }
static inline int block_is_prev_free(const tlsf_block_t* block) { return (block->size & TLSF_PREV_FREE) != 0; }
static inline tlsf_block_t* block_prev(const tlsf_block_t* block) {
  return *((tlsf_block_t**)((char*)block - sizeof(tlsf_block_t*)));
}

static int test_basic_alignment(void) {
  void* ptr1 = mm_malloc(1);
  void* ptr2 = mm_malloc(7);
  void* ptr3 = mm_malloc(8);
  void* ptr4 = mm_malloc(9);
  void* ptr5 = mm_malloc(13);
  void* ptr6 = mm_malloc(16);
  void* ptr7 = mm_malloc(17);

  ASSERT_EQ((uintptr_t)ptr1 % ALIGNMENT, 0);
  ASSERT_EQ((uintptr_t)ptr2 % ALIGNMENT, 0);
  ASSERT_EQ((uintptr_t)ptr3 % ALIGNMENT, 0);
  ASSERT_EQ((uintptr_t)ptr4 % ALIGNMENT, 0);
  ASSERT_EQ((uintptr_t)ptr5 % ALIGNMENT, 0);
  ASSERT_EQ((uintptr_t)ptr6 % ALIGNMENT, 0);
  ASSERT_EQ((uintptr_t)ptr7 % ALIGNMENT, 0);
  return 1;
}

static int test_memalign_basic(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  void* p = (mm_memalign)(alloc, 64, 256);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ((uintptr_t)p % 64, 0);
  ASSERT_GE((mm_block_size)(p), 256u);

  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_memalign_gap_adjusts_to_minimum(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  void* p = (mm_memalign)(alloc, 64, 128);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ((uintptr_t)p % 64, 0);

  tlsf_block_t* block = (tlsf_block_t*)((char*)p - BLOCK_HEADER_OVERHEAD);
  ASSERT(block_is_prev_free(block));

  tlsf_block_t* prev = block_prev(block);
  ASSERT_NOT_NULL(prev);
  ASSERT(block_is_free(prev));
  ASSERT_GE(block_size(prev), (size_t)TLSF_MIN_BLOCK_SIZE);

  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_memalign_no_prefix_when_aligned(void) {
  uint8_t backing[16 * 1024 + ALIGNMENT] __attribute__((aligned(16)));
  void* mem = (void*)(backing + ALIGNMENT);
  tlsf_t alloc = mm_create_with_pool(mem, sizeof(backing) - ALIGNMENT);
  ASSERT_NOT_NULL(alloc);

  void* p = (mm_memalign)(alloc, 16, 128);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ((uintptr_t)p % 16, 0);

  tlsf_block_t* block = (tlsf_block_t*)((char*)p - BLOCK_HEADER_OVERHEAD);
  ASSERT(!block_is_prev_free(block));

  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("Alignment");
  RUN_TEST(test_basic_alignment);
  RUN_TEST(test_memalign_basic);
  RUN_TEST(test_memalign_gap_adjusts_to_minimum);
  RUN_TEST(test_memalign_no_prefix_when_aligned);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
