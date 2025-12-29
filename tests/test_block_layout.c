#include "test_framework.h"
#include "memoman_test_internal.h"

static int test_constants_match_tlsf(void) {
  ASSERT_EQ(BLOCK_HEADER_OVERHEAD, sizeof(size_t));
  ASSERT_EQ(BLOCK_START_OFFSET, offsetof(tlsf_block_t, size) + sizeof(size_t));
  return 1;
}

static int test_offset_placement(void) {
  ASSERT_EQ(offsetof(tlsf_block_t, next_free), BLOCK_START_OFFSET);
  ASSERT_EQ(offsetof(tlsf_block_t, prev_free), BLOCK_START_OFFSET + sizeof(void*));
  return 1;
}

static int test_user_pointer_matches_offset(void) {
  void* ptr = mm_malloc(32);
  ASSERT_NOT_NULL(ptr);

  tlsf_block_t* block = (tlsf_block_t*)((char*)ptr - BLOCK_START_OFFSET);
  ASSERT_EQ((char*)ptr, (char*)block + BLOCK_START_OFFSET);

  mm_free(ptr);
  return 1;
}

static int test_free_links_live_in_payload(void) {
  void* ptr = mm_malloc(64);
  ASSERT_NOT_NULL(ptr);

  tlsf_block_t* block = (tlsf_block_t*)((char*)ptr - BLOCK_START_OFFSET);
  size_t usable_before = mm_malloc_usable_size(ptr);

  mm_free(ptr);

  size_t free_size = block->size & TLSF_SIZE_MASK;
  char* payload = (char*)block + BLOCK_START_OFFSET;

  ASSERT_GE(free_size, usable_before);
  ASSERT_GE((char*)&block->next_free, payload);
  ASSERT_LT((char*)&block->next_free, payload + free_size);
  ASSERT_GE((char*)&block->prev_free, payload);
  ASSERT_LT((char*)&block->prev_free, payload + free_size);

  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("block_layout_tlsf_3_1");

  RUN_TEST(test_constants_match_tlsf);
  RUN_TEST(test_offset_placement);
  RUN_TEST(test_user_pointer_matches_offset);
  RUN_TEST(test_free_links_live_in_payload);

  TEST_SUITE_END();
  TEST_MAIN_END();
}
