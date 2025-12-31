#include "test_framework.h"
#include "memoman_test_internal.h"

#include <stdint.h>

static tlsf_t make_two_pool_allocator(uint8_t* pool1, size_t bytes1, uint8_t* pool2, size_t bytes2, pool_t* out_pool2) {
  tlsf_t alloc = mm_create(pool1, bytes1);
  ASSERT_NOT_NULL(alloc);
  pool_t p2 = mm_add_pool(alloc, pool2, bytes2);
  ASSERT_NOT_NULL(p2);
  if (out_pool2) *out_pool2 = p2;
  ASSERT((mm_validate)(alloc));
  return alloc;
}

static int test_detects_fl_sl_bitmap_mismatch(void) {
  uint8_t pool1[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create(pool1, sizeof(pool1));
  ASSERT_NOT_NULL(alloc);
  struct mm_allocator_t* ctrl = (struct mm_allocator_t*)alloc;

  /* Force an inconsistent bitmap state. */
  ctrl->sl_bitmap[0] |= 1u;
  ctrl->fl_bitmap &= ~(1u << 0);
  ASSERT(!(mm_validate)(alloc));
  return 1;
}

static int test_detects_free_block_missing_from_list(void) {
  uint8_t pool1[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create(pool1, sizeof(pool1));
  ASSERT_NOT_NULL(alloc);
  struct mm_allocator_t* ctrl = (struct mm_allocator_t*)alloc;

  void* p = (mm_malloc)(alloc, 1024);
  ASSERT_NOT_NULL(p);
  (mm_free)(alloc, p);
  ASSERT((mm_validate)(alloc));

  tlsf_block_t* b = (tlsf_block_t*)((char*)p - BLOCK_START_OFFSET);
  int fl = 0, sl = 0;
  mm_get_mapping_indices(b->size & TLSF_SIZE_MASK, &fl, &sl);
  ASSERT(ctrl->blocks[fl][sl] != NULL);

  /* Corrupt: drop the freed block from its bucket list (but keep it physically free). */
  tlsf_block_t* head = ctrl->blocks[fl][sl];
  if (head == b) {
    ctrl->blocks[fl][sl] = b->next_free;
    if (ctrl->blocks[fl][sl]) ctrl->blocks[fl][sl]->prev_free = NULL;
  } else {
    /* Find and unlink b from the list. */
    tlsf_block_t* prev = head;
    for (size_t i = 0; i < 1024 && prev; i++) {
      if (prev->next_free == b) break;
      prev = prev->next_free;
    }
    ASSERT(prev && prev->next_free == b);
    prev->next_free = b->next_free;
    if (b->next_free) b->next_free->prev_free = prev;
  }

  ASSERT(!(mm_validate)(alloc));
  return 1;
}

static int test_detects_prev_free_inconsistency(void) {
  uint8_t pool1[64 * 1024] __attribute__((aligned(16)));
  tlsf_t alloc = mm_create(pool1, sizeof(pool1));
  ASSERT_NOT_NULL(alloc);

  void* a = (mm_malloc)(alloc, 256);
  void* b = (mm_malloc)(alloc, 256);
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);

  (mm_free)(alloc, a);
  ASSERT((mm_validate)(alloc));

  tlsf_block_t* bb = (tlsf_block_t*)((char*)b - BLOCK_START_OFFSET);
  bb->size &= ~TLSF_PREV_FREE;
  ASSERT(!(mm_validate)(alloc));
  return 1;
}

static int test_detects_epilogue_corruption(void) {
  uint8_t pool1[64 * 1024] __attribute__((aligned(16)));
  uint8_t pool2[64 * 1024] __attribute__((aligned(16)));
  pool_t p2 = NULL;
  tlsf_t alloc = make_two_pool_allocator(pool1, sizeof(pool1), pool2, sizeof(pool2), &p2);

  mm_pool_desc_t* desc = (mm_pool_desc_t*)p2;
  tlsf_block_t* epilogue = (tlsf_block_t*)(desc->end - BLOCK_HEADER_OVERHEAD);

  /* Force a wrong prev_free state. */
  epilogue->size &= ~TLSF_PREV_FREE;
  ASSERT(!(mm_validate)(alloc));
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("validate_full");
  RUN_TEST(test_detects_fl_sl_bitmap_mismatch);
  RUN_TEST(test_detects_free_block_missing_from_list);
  RUN_TEST(test_detects_prev_free_inconsistency);
  RUN_TEST(test_detects_epilogue_corruption);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
