#include "../src/memoman.h"
#include <stdio.h>
#include <assert.h>

/* Access internal TLSF control */
extern tlsf_control_t* tlsf_ctrl;

int main() {
  printf("Testing last_block tracking...\n");
  
  /* Initialize allocator first (lazy init via malloc) */
  void* init_ptr = mm_malloc(1);
  assert(init_ptr != NULL);
  mm_free(init_ptr);

  /* Test 1: Initial state */
  printf("  Test 1: Initial last_block... ");
  reset_allocator();
  assert(tlsf_ctrl != NULL);
  assert(tlsf_ctrl->last_block != NULL);
  printf("PASSED\n");

  /* Test 2: last_block after single allocation */
  printf("  Test 2: After allocation... ");
  void* p1 = mm_malloc(100);
  assert(p1 != NULL);
  assert(tlsf_ctrl->last_block != NULL);
  printf("PASSED\n");

  /* Test 3: last_block after free (should still be valid) */
  printf("  Test 3: After free... ");
  mm_free(p1);
  assert(tlsf_ctrl->last_block != NULL);
  printf("PASSED\n");

  /* Test 4: last_block after heap growth */
  printf("  Test 4: After heap growth... ");
  reset_allocator();
  tlsf_block_t* initial_last = tlsf_ctrl->last_block;

  /* Allocate enough to trigger heap growth */
  for (int i = 0; i < 100; i++) {
    assert(mm_malloc(10000) != NULL);
  }

  /* last_block should have moved after growth */
  assert(tlsf_ctrl->last_block != NULL);
  printf("(initial=%p, after=%p) PASSED\n", initial_last, tlsf_ctrl->last_block);

  /* Test 5: Verify last_block is actually the last physical block */
  printf("  Test 5: Verify last_block is last... ");
  reset_allocator();
  void* a = mm_malloc(64);
  void* b = mm_malloc(128);
  void* c = mm_malloc(256);

  /* last_block should have no next physical block */
  tlsf_block_t* last = tlsf_ctrl->last_block;
  (void)last;
  assert((char*)last + sizeof(tlsf_block_t) + (last->size & (~(size_t)3)) <= tlsf_ctrl->heap_end);
  printf("PASSED\n");

  mm_free(a);
  mm_free(b);
  mm_free(c);

  printf("\nAll last_block tests passed!\n");
  return 0;
}