#include "test_framework.h"
#include "memoman_test_internal.h"

#include <stdint.h>

typedef struct {
  tlsf_t alloc;
  pool_t pool;
  size_t blocks;
  size_t used_blocks;
  size_t free_blocks;
  int ok;
} walk_stats_t;

static void count_blocks(void* ptr, size_t size, int used, void* user) {
  walk_stats_t* st = (walk_stats_t*)user;
  if (!st->ok) return;

  (void)size;
  st->blocks++;
  if (used) st->used_blocks++;
  else st->free_blocks++;

  if (!ptr) { st->ok = 0; return; }
  if (((uintptr_t)ptr % ALIGNMENT) != 0) { st->ok = 0; return; }
  if (mm_get_pool_for_ptr(st->alloc, ptr) != st->pool) { st->ok = 0; return; }
}

static int test_validate_pool_smoke(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  uint8_t pool2[128 * 1024] __attribute__((aligned(16)));

  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  pool_t p0 = mm_get_pool(alloc);
  ASSERT_NOT_NULL(p0);
  ASSERT((mm_validate_pool)(p0));

  void* a = (mm_malloc)(alloc, 1024);
  ASSERT_NOT_NULL(a);

  pool_t p2 = mm_add_pool(alloc, pool2, sizeof(pool2));
  ASSERT_NOT_NULL(p2);
  ASSERT((mm_validate_pool)(p2));

  void* b = (mm_malloc)(alloc, 64 * 1024);
  ASSERT_NOT_NULL(b);

  ASSERT((mm_validate)(alloc));
  ASSERT((mm_validate_pool)(p0));
  ASSERT((mm_validate_pool)(p2));

  (mm_free)(alloc, a);
  (mm_free)(alloc, b);
  ASSERT((mm_validate)(alloc));

  return 1;
}

static int test_walk_pool_counts(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  uint8_t pool2[128 * 1024] __attribute__((aligned(16)));

  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  void* a = (mm_malloc)(alloc, 1024);
  ASSERT_NOT_NULL(a);

  pool_t p0 = mm_get_pool(alloc);
  ASSERT_NOT_NULL(p0);

  pool_t p2 = mm_add_pool(alloc, pool2, sizeof(pool2));
  ASSERT_NOT_NULL(p2);

  void* b = (mm_malloc)(alloc, 64 * 1024);
  ASSERT_NOT_NULL(b);

  walk_stats_t st0 = {0};
  st0.alloc = alloc;
  st0.pool = p0;
  st0.ok = 1;
  mm_walk_pool(p0, count_blocks, &st0);
  ASSERT(st0.ok);
  ASSERT_GT(st0.blocks, 0);
  ASSERT_GT(st0.used_blocks, 0);

  walk_stats_t st2 = {0};
  st2.alloc = alloc;
  st2.pool = p2;
  st2.ok = 1;
  mm_walk_pool(p2, count_blocks, &st2);
  ASSERT(st2.ok);
  ASSERT_GT(st2.blocks, 0);
  ASSERT_GT(st2.used_blocks, 0);

  (mm_free)(alloc, a);
  (mm_free)(alloc, b);
  ASSERT((mm_validate)(alloc));
  return 1;
}

static int test_validate_pool_detects_corruption(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(alloc);

  pool_t p0 = mm_get_pool(alloc);
  ASSERT_NOT_NULL(p0);

  void* a = (mm_malloc)(alloc, 1024);
  ASSERT_NOT_NULL(a);

  tlsf_block_t* block = (tlsf_block_t*)((char*)a - BLOCK_START_OFFSET);
  block->size = ((size_t)1 << 20) | (block->size & ~TLSF_SIZE_MASK);

  ASSERT(!(mm_validate_pool)(p0));
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("walk_validate_pool");
  RUN_TEST(test_validate_pool_smoke);
  RUN_TEST(test_walk_pool_counts);
  RUN_TEST(test_validate_pool_detects_corruption);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
