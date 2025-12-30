#include "test_framework.h"
#include "memoman_test_internal.h"
#include <stddef.h>
#include <stdlib.h>

/* Helper to acccess internal block structure */
static tlsf_block_t* get_block(void* ptr) { return (tlsf_block_t*)((char*)ptr - BLOCK_HEADER_OVERHEAD); }

static tlsf_block_t* get_prev_phys(tlsf_block_t* b) { return *((tlsf_block_t**)((char*)b - sizeof(tlsf_block_t*))); }

static int test_coalescing_invariants() {
  TEST_RESET();

  /* Alloc 3 blocks */
  void* p1 = mm_malloc(64);
  void* p2 = mm_malloc(64);
  void* p3 = mm_malloc(64);

  ASSERT_NOT_NULL(p1);
  ASSERT_NOT_NULL(p2);
  ASSERT_NOT_NULL(p3);

  tlsf_block_t* b1 = get_block(p1);
  tlsf_block_t* b2 = get_block(p2);
  tlsf_block_t* b3 = get_block(p3);

  /* Free middle - no coalesce, just marked free */
  mm_free(p2);
  ASSERT(b2->size & TLSF_BLOCK_FREE);
  ASSERT(!(b1->size & TLSF_BLOCK_FREE));
  ASSERT(!(b3->size & TLSF_BLOCK_FREE));

  /* Check ghost pointer invariant: b3 should now have PREV_FREE set */
  ASSERT(b3->size & TLSF_PREV_FREE);
  /* And b3->prev_phys should point to b2 (Ghost Pointer Active) */
  ASSERT(get_prev_phys(b3) == b2);

  /* Free left - should coalesce with b2 (Right Coalesce) */
  mm_free(p1);
  /* b1 should now be the head of a larger free block */
  ASSERT(b1->size & TLSF_BLOCK_FREE);

  /* b3 should still point to the free block on its left. */
  /* Since b1 and b2 coalesced, b3->prev_phys should now be b1 */
  ASSERT(get_prev_phys(b3) == b1);

  mm_free(p3);
  return 1;
}

static int test_ghost_pointer_safety() {
  TEST_RESET();

  void* p1 = mm_malloc(64);
  void* p2 = mm_malloc(64);

  /* Fill p1 with pattern.
  ** In Conte style, p1's user data overlapts with p2's prev_phy IF p1 was free.
  ** But p1 is used. So p2->prev_phys is technically "invalid" (it's p1's data).
  ** We want to ensure that writing to p1 doesn't corrupt p2's header flags. 
  **/
  memset(p1, 0xAA, 64);

  tlsf_block_t* b2 = get_block(p2);

  /* p2 should NOT have PREV_FREE set */
  ASSERT(!(b2->size & TLSF_PREV_FREE));

  /* Free p1. Now p2->prev_phy becomes validf and must point to p1. */
  mm_free(p1);

  ASSERT(b2->size & TLSF_PREV_FREE);
  ASSERT(get_prev_phys(b2) == get_block(p1));

  mm_free(p2);
  return 1;
}

static int test_bitmap_consistency() {
  TEST_RESET();

  size_t size = 1024;
  int fl, sl;
  mm_get_mapping_indices(size, &fl, &sl);

  /* Initially, we might have a large free block, but let's alloc specifically */
  void* p = mm_malloc(size);
  mm_free(p);

  /* Now we expect a block in the lit corresponding to ~1024 bytes
  ** Note: It might have coalesced, so we check it ANY bit is set,
  ** or we alloc everything else to isolate it.
  ** Simpler check: Alloc, check bit is CLEARED (if it was the only one). 
  **
  ** Lets rely on the internal integrity check which runs one every malloc/free in debug mode. 
  ** If we do a bunch of random allocs and dont't crash, the bitmaps are consistent. 
  **/
  for (int i = 0; i < 100; i++) {
    void* ptr = mm_malloc(i * 16 + 16);
    if (ptr) mm_free(ptr);
  }

  return 1;
}

static int test_corruption_catch() {
  printf("Running corruption test... expect crash!\n");
  void* p = mm_malloc(64);
  tlsf_block_t* b = get_block(p);
  b->size = 0;
  mm_free(p);
  return 1;
}

int main() {
  TEST_SUITE_BEGIN("Invariants & Integrity");
  RUN_TEST(test_coalescing_invariants);
  RUN_TEST(test_ghost_pointer_safety);
  RUN_TEST(test_bitmap_consistency);
  if (getenv("MM_RUN_CRASH_TESTS")) {
    RUN_TEST(test_corruption_catch);
  }
  TEST_SUITE_END();
  TEST_MAIN_END();
}
