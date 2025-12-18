#include "../src/memoman.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>

/* Access internal TLSF control */
extern tlsf_control_t* tlsf_ctrl;

int main(void) {
  printf("=== Heap Bounds Validation Tests ===\n");

  /* Initialize allocator */
  void* init = mm_malloc(1);
  assert(init != NULL);
  mm_free(init);

  /* Test 1: Free NULL pointer (should be safe) */
  printf("  Test 1: Free NULL pointer... ");
  mm_free(NULL);
  printf("PASSED\n");

  /* Test 2: Free stack pointer (outside heap) */
  printf("  Test 2: Free stack pointer... ");
  char stack_var = 'x';
  mm_free(&stack_var);
  printf("PASSED\n");

  /* Test 3: Free arbitrary invalid pointer */
  printf("  Test 3: Free arbitrary invalid pointer (0xDEADBEEF)... ");
  mm_free((void*)(uintptr_t)0xDEADBEEF);
  printf("PASSED\n");

  /* Test 4: Free pointer before heap start */
  printf("  Test 4: Free pointer before heap... ");
  void* before_heap = (void*)(tlsf_ctrl->heap_start - 100);
  mm_free(before_heap);
  printf("PASSED\n");

  /* Test 5: Free pointer after heap end */
  printf("  Test 5: Free pointer after heap... ");
  void* after_heap = (void*)(tlsf_ctrl->heap_end + 100);
  mm_free(after_heap);
  printf("PASSED\n");

  /* Test 6: Free pointer at exact heap_end (invalid - one past end) */
  printf("  Test 6: Free pointer at heap_end boundary... ");
  mm_free((void*)tlsf_ctrl->heap_end);
  printf("PASSED\n");

  /* Test 7: Double free handling */
  printf("  Test 7: Double free... ");
  void* p = mm_malloc(64);
  assert(p != NULL);
  mm_free(p);
  mm_free(p);  /* Second free - should not crash */
  printf("PASSED\n");

  /* Test 8: Free inside heap but not a valid block (misaligned) */
  printf("  Test 8: Free misaligned heap pointer... ");
  void* valid = mm_malloc(128);
  assert(valid != NULL);
  void* misaligned = (char*)valid + 3;  /* Intentionally misaligned */
  mm_free(misaligned); /* Should not crash, but undefined behavior */
  mm_free(valid);  /* Free the actual block */
  printf("PASSED\n");

  /* Test 9: Verify heap still functional after invalid frees */
  printf("  Test 9: Heap integrity check... ");
  void* a = mm_malloc(100);
  void* b = mm_malloc(200);
  void* c = mm_malloc(300);
  assert(a != NULL && b != NULL && c != NULL);

  /* Write to verify they're valid */
  ((char*)a)[0] = 'A';
  ((char*)b)[0] = 'B';
  ((char*)c)[0] = 'C';

  mm_free(b);
  void* d = mm_malloc(150);
  assert(d != NULL);
  
  mm_free(a);
  mm_free(b);
  mm_free(c);
  printf("PASSED\n");
  
  /* Test 10: Large block (mmap'd) with invalid free nearby */
  printf("  Test 10: Large block + invalid free... ");
  void* large = mm_malloc(2 * 1024 * 1024);  /* 2MB - triggers mmap */
  assert(large != NULL);

  /* Try freeing near but not at the large block */
  mm_free((char*)large + 100);  /* Invalid - inside large block data */

  mm_free(large);  /* Valid free */
  printf("PASSED\n");

  printf("\n=== All Heap Bounds Tests Passed ===\n");
  return 0;
}