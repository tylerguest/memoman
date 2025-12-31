#include "test_framework.h"
#include "memoman_test_internal.h"

static inline tlsf_block_t* user_to_block_local(void* ptr) { return (tlsf_block_t*)((char*)ptr - BLOCK_START_OFFSET); }
static inline size_t payload_size(const tlsf_block_t* block) { return block->size & TLSF_SIZE_MASK; }
static inline tlsf_block_t* block_next_local(const tlsf_block_t* block) {
  return (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + payload_size(block));
}
static inline tlsf_block_t* block_prev_link_local(const tlsf_block_t* block) {
  return *((tlsf_block_t**)((char*)block - sizeof(void*)));
}

static tlsf_t make_allocator(uint8_t* backing, size_t bytes) {
  tlsf_t alloc = mm_create(backing, bytes);
  ASSERT_NOT_NULL(alloc);
  return alloc;
}

static int test_add_pool_initial_links(void) {
  uint8_t pool[16384] __attribute__((aligned(ALIGNMENT)));
  tlsf_t alloc = make_allocator(pool, sizeof(pool));
  (void)alloc;

  char* pool_mem = (char*)pool + mm_size();
  size_t pool_bytes = sizeof(pool) - mm_size();

  uintptr_t start_addr = (uintptr_t)pool_mem;
  uintptr_t aligned_addr = (start_addr + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
  char* pool_start = (char*)aligned_addr;
  size_t aligned_bytes = pool_bytes - (aligned_addr - start_addr);
  char* pool_end = pool_start + aligned_bytes;

  tlsf_block_t* first = (tlsf_block_t*)pool_start;
  ASSERT(first->size & TLSF_BLOCK_FREE);
  ASSERT(!(first->size & TLSF_PREV_FREE));

  tlsf_block_t* epilogue = (tlsf_block_t*)(pool_end - BLOCK_HEADER_OVERHEAD);
  ASSERT_EQ((epilogue->size & TLSF_SIZE_MASK), 0);
  ASSERT_EQ((epilogue->size & TLSF_BLOCK_FREE), 0);
  ASSERT(epilogue->size & TLSF_PREV_FREE);
  ASSERT_EQ(block_prev_link_local(epilogue), first);

  return 1;
}

static int test_free_sets_next_prev_link(void) {
  uint8_t pool[16384] __attribute__((aligned(ALIGNMENT)));
  tlsf_t alloc = make_allocator(pool, sizeof(pool));

  void* p1 = (mm_malloc)(alloc, 64);
  void* p2 = (mm_malloc)(alloc, 64);
  ASSERT_NOT_NULL(p1);
  ASSERT_NOT_NULL(p2);

  tlsf_block_t* b1 = user_to_block_local(p1);
  tlsf_block_t* b2 = user_to_block_local(p2);
  ASSERT(!(b2->size & TLSF_PREV_FREE));

  (mm_free)(alloc, p1);
  ASSERT(b2->size & TLSF_PREV_FREE);
  ASSERT_EQ(block_prev_link_local(b2), b1);

  (mm_free)(alloc, p2);
  return 1;
}

static int test_split_updates_next_links(void) {
  uint8_t pool[16384] __attribute__((aligned(ALIGNMENT)));
  tlsf_t alloc = make_allocator(pool, sizeof(pool));

  void* big = (mm_malloc)(alloc, 256);
  ASSERT_NOT_NULL(big);
  (mm_free)(alloc, big);

  void* small = (mm_malloc)(alloc, 64);
  ASSERT_NOT_NULL(small);

  tlsf_block_t* used = user_to_block_local(small);
  tlsf_block_t* remainder = block_next_local(used);

  ASSERT(remainder->size & TLSF_BLOCK_FREE);
  ASSERT(!(remainder->size & TLSF_PREV_FREE));

  tlsf_block_t* next = block_next_local(remainder);
  ASSERT_EQ((next->size & TLSF_SIZE_MASK), 0); /* epilogue */
  ASSERT(next->size & TLSF_PREV_FREE);
  ASSERT_EQ(block_prev_link_local(next), remainder);

  (mm_free)(alloc, small);
  return 1;
}

static int test_realloc_grow_clears_next_prev_free(void) {
  uint8_t pool[16384] __attribute__((aligned(ALIGNMENT)));
  tlsf_t alloc = make_allocator(pool, sizeof(pool));

  void* p1 = (mm_malloc)(alloc, 128);
  void* p2 = (mm_malloc)(alloc, 256);
  ASSERT_NOT_NULL(p1);
  ASSERT_NOT_NULL(p2);

  (mm_free)(alloc, p2);

  void* original = p1;
  void* grown = (mm_realloc)(alloc, p1, 320);
  ASSERT_NOT_NULL(grown);
  ASSERT_EQ(grown, original);

  tlsf_block_t* b = user_to_block_local(grown);
  tlsf_block_t* next = block_next_local(b);
  ASSERT(!(next->size & TLSF_PREV_FREE));
  if ((next->size & TLSF_SIZE_MASK) != 0) {
    ASSERT(next->size & TLSF_BLOCK_FREE);
    tlsf_block_t* next2 = block_next_local(next);
    ASSERT_EQ((next2->size & TLSF_SIZE_MASK), 0); /* epilogue */
    ASSERT(next2->size & TLSF_PREV_FREE);
    ASSERT_EQ(block_prev_link_local(next2), next);
  }

  (mm_free)(alloc, grown);
  return 1;
}

static int test_realloc_shrink_sets_next_prev_link(void) {
  uint8_t pool[16384] __attribute__((aligned(ALIGNMENT)));
  tlsf_t alloc = make_allocator(pool, sizeof(pool));

  void* p = (mm_malloc)(alloc, 512);
  ASSERT_NOT_NULL(p);

  void* shrunk = (mm_realloc)(alloc, p, 128);
  ASSERT_NOT_NULL(shrunk);
  ASSERT_EQ(shrunk, p);

  tlsf_block_t* used = user_to_block_local(shrunk);
  tlsf_block_t* remainder = block_next_local(used);
  ASSERT(remainder->size & TLSF_BLOCK_FREE);
  ASSERT(!(remainder->size & TLSF_PREV_FREE));

  tlsf_block_t* next = block_next_local(remainder);
  ASSERT_EQ((next->size & TLSF_SIZE_MASK), 0); /* epilogue */
  ASSERT(next->size & TLSF_PREV_FREE);
  ASSERT_EQ(block_prev_link_local(next), remainder);

  (mm_free)(alloc, shrunk);
  return 1;
}

int main(void) {
  printf("\n" COLOR_BOLD "=== Prev-Physical Linkage ===" COLOR_RESET "\n");
  RUN_TEST(test_add_pool_initial_links);
  RUN_TEST(test_free_sets_next_prev_link);
  RUN_TEST(test_split_updates_next_links);
  RUN_TEST(test_realloc_grow_clears_next_prev_free);
  RUN_TEST(test_realloc_shrink_sets_next_prev_link);
  TEST_MAIN_END();
}
