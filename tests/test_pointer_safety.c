#include "test_framework.h"
#include "../src/memoman.h"
#include "memoman_test_internal.h"

#include <stdint.h>

/*
 * These tests exercise the non-MM_DEBUG behavior: invalid pointers should be
 * ignored/return-NULL instead of crashing or corrupting.
 *
 * In MM_DEBUG builds with abort-on-invalid-pointer enabled, these would assert,
 * so we compile them out in that configuration.
 */
#if defined(MM_DEBUG) && defined(MM_DEBUG_ABORT_ON_INVALID_POINTER) && MM_DEBUG_ABORT_ON_INVALID_POINTER
int main(void) { return 0; }
#else

static int test_free_ignores_non_owned_pointer(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  uint8_t not_owned[64] __attribute__((aligned(16)));
  (mm_free)(alloc, not_owned);
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_realloc_rejects_non_owned_pointer(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  uint8_t not_owned[64] __attribute__((aligned(16)));
  void* p = (mm_realloc)(alloc, not_owned, 128);
  ASSERT_NULL(p);
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_free_rejects_interior_pointer(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  void* p = (mm_malloc)(alloc, 128);
  ASSERT_NOT_NULL(p);

  void* interior = (void*)((char*)p + sizeof(size_t));
  (mm_free)(alloc, interior);
  ASSERT((mm_validate)(alloc));

  /* Original allocation must still be freeable. */
  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_realloc_rejects_interior_pointer(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  void* p = (mm_malloc)(alloc, 128);
  ASSERT_NOT_NULL(p);

  void* interior = (void*)((char*)p + sizeof(size_t));
  void* q = (mm_realloc)(alloc, interior, 256);
  ASSERT_NULL(q);
  ASSERT((mm_validate)(alloc));

  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_rejects_aligned_interior_pointer(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  void* p = (mm_malloc)(alloc, 256);
  ASSERT_NOT_NULL(p);

  void* interior = (void*)((uintptr_t)p + (2 * ALIGNMENT));
  ASSERT(((uintptr_t)interior % ALIGNMENT) == 0);

  (mm_free)(alloc, interior);
  ASSERT((mm_validate)(alloc));

  void* q = (mm_realloc)(alloc, interior, 128);
  ASSERT_NULL(q);
  ASSERT((mm_validate)(alloc));

  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_rejects_pointer_to_prev_footer(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  void* p = (mm_malloc)(alloc, 128);
  ASSERT_NOT_NULL(p);

  void* footer = (void*)((char*)p - sizeof(tlsf_block_t*));
  (mm_free)(alloc, footer);
  ASSERT((mm_validate)(alloc));

  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_free_rejects_forged_header_pointer(void) {
  size_t total_bytes = mm_size() + mm_pool_overhead() + mm_block_size_min();
  uint8_t* backing = malloc(total_bytes);
  ASSERT_NOT_NULL(backing);

  tlsf_t alloc = mm_create_with_pool(backing, total_bytes);
  ASSERT_NOT_NULL(alloc);

  size_t size = mm_block_size_min();
  void* p = (mm_malloc)(alloc, size);
  ASSERT_NOT_NULL(p);
  ASSERT_NULL((mm_malloc)(alloc, size));

  char* interior = (char*)p + (2 * ALIGNMENT);
  tlsf_block_t* fake_block = (tlsf_block_t*)(interior - BLOCK_HEADER_OVERHEAD);
  fake_block->size = TLSF_PREV_FREE | TLSF_MIN_BLOCK_SIZE;
  tlsf_block_t** prev_ptr = (tlsf_block_t**)((char*)fake_block - sizeof(tlsf_block_t*));
  *prev_ptr = NULL;

  (mm_free)(alloc, interior);
  ASSERT_NULL((mm_malloc)(alloc, size));

  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));
  free(backing);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("pointer_safety");
  RUN_TEST(test_free_ignores_non_owned_pointer);
  RUN_TEST(test_realloc_rejects_non_owned_pointer);
  RUN_TEST(test_free_rejects_interior_pointer);
  RUN_TEST(test_realloc_rejects_interior_pointer);
  RUN_TEST(test_rejects_aligned_interior_pointer);
  RUN_TEST(test_rejects_pointer_to_prev_footer);
  RUN_TEST(test_free_rejects_forged_header_pointer);
  TEST_SUITE_END();
  TEST_MAIN_END();
}

#endif
