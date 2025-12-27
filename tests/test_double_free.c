#include "../src/memoman.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* Access internal TLSF control */
extern mm_allocator_t* sys_allocator;

int main(void) {
  printf("=== Double-Free Detection Tests ===\n");

  /* Initialize allocator */
  void* init = mm_malloc(1);
  assert(init != NULL);
  mm_free(init);
  mm_reset_allocator();

  /* Re-init after reset */
  init = mm_malloc(1);
  mm_free(init);

  /* Test 1: Simple double-free */
  printf("  Test 1: Simple double-free... ");
  void* p1 = mm_malloc(64);
  assert(p1 != NULL);
  size_t free_before = mm_get_free_space();
  (void)free_before;

  mm_free(p1);
  size_t free_after_first = mm_get_free_space();
  (void)free_after_first;
  assert(free_after_first > free_before);  /* Space increased after free */

  mm_free(p1);  /* Double free - should be detected and ignored */
  assert(mm_get_free_space() == free_after_first);  /* No change - double free ignored */
  printf("PASSED\n");

  /* Test 2: Triple free */
  printf("  Test 2: Triple free... ");
  void* p2 = mm_malloc(128);
  assert(p2 != NULL);
  mm_free(p2);
  mm_free(p2);
  mm_free(p2);
  printf("PASSED\n");

  /* Test 3: Allocate-free-allocate-double-free pattern */
  printf("  Test 3: Reuse after free, then double-free original... ");
  void* p3 = mm_malloc(64);
  assert(p3 != NULL);
  mm_free(p3);

  void* p4 = mm_malloc(64);  /* May reuse p3's block */
  assert(p4 != NULL);

  /* If p4 == p3, freeing p3 again would corrupt p4 wihtout detection */
  if (p4 == p3) {
    /* Block was reused - double-free of p3 would now free p4! */
    /* With detection, this should NOT happen since p4 is allocated (not free) */
    mm_free(p3);  /* Should work - p4 is actually allocated */
  } else {
    /* Different blocks - p3's block is still free */
    mm_free(p3);  /* Should be caught as double-free */
    mm_free(p4);  /* Normal free */
  }
  printf("PASSED\n");

  /* Test 4: Multiple allocations, free in reverse, double-free middle */
  printf("  Test 4: Multiple blocks, double-free middle... ");
  void* blocks[5];
  for (int i = 0; i < 5; i++) {
    blocks[i] = mm_malloc(32 + i * 16);
    assert(blocks[i] != NULL);
  }

  /* Free all */
  for (int i = 4; i >= 0; i--) { mm_free(blocks[i]); }

  /* Double-free middle block */
  mm_free(blocks[2]);
  printf("PASSED\n");

  /* Test 5: Verify heap integrity after double-free attempts */
  printf("  Test 5: Heap integrity after double-frees... ");
  mm_reset_allocator();
  init = mm_malloc(1);
  mm_free(init);

  void* a = mm_malloc(100);
  void* b = mm_malloc(200);
  void* c = mm_malloc(300);
  assert(a && b && c);

  mm_free(b);
  mm_free(b);  /* Double free */

  /* Allocate again - should work normally */
  void* d = mm_malloc(150);
  assert(d != NULL);

  /* Write to verify memory is valid */
  memset(a, 'A', 100);
  memset(c, 'C', 300);
  memset(d, 'D', 150);

  mm_free(a);
  mm_free(c);
  mm_free(d);
  printf("PASSED\n");

  /* Test 6: Large block double-free (mmap'd) */
  printf("  Test 6: Large block double-free... ");
  void* large = mm_malloc(2 * 1024 * 1024); /* 2MB */
  assert(large != NULL);
  mm_free(large);
  mm_free(large);  /* Double free of large block - magive cleared, not in list */
  printf("PASSED\n");

  /* Test 7: Interleaved alloc/free/double-free */
  printf("  Test 7: Interleaved operations... ");
  void* x = mm_malloc(50);
  void* y = mm_malloc(50);
  mm_free(x);
  mm_free(x);  /* Double */
  void* z = mm_malloc(50);
  mm_free(y);
  mm_free(y);  /* Double */
  mm_free(z);
  mm_free(z);  /* Double */
  printf("PASSED\n");

  /* Test 8: Free, allocate same size, original should not be "free" */
  printf("  Test 8: Block reuse detection... ");
  void* orig = mm_malloc(64);
  assert(orig != NULL);
  mm_free(orig);

  void* reused = mm_malloc(64);
  /* If same address was reused, it's now allocated - not free */
  if (reused == orig) {
    /* Verify the block is NOT marked as free */
    /* Attempting to "double-free" orig should work since it's actually reused/allocated */
    size_t before = mm_get_free_space();
    (void)before;
    mm_free(orig);  /* this is actually freeing 'reused' - valid */
    assert(mm_get_free_space() > before);  /* Free space increased - block was freed */
  } else { mm_free(reused); }
  printf("PASSED\n");

  printf("\n=== All Double-Free Detection Tests Passed ===\n");
  return 0;
}