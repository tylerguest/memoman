#include "../src/memoman.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main() {
  printf("Testing mmap implementation...\n");

  /* Test 1: Mixed allocation patterns */
  printf("  Test 1: Mixed allocation patterns... ");
  void* small = mm_malloc(64);                // size class
  void* medium = mm_malloc(4096);             // free list
  void* large = mm_malloc(2 * 1024 * 1024);   // direct mmap (2MB)

  assert(small != NULL);
  assert(medium != NULL);
  assert(large != NULL);

  printf("\n    small (64B) at: %p\n", small);
  printf("    medium (4KB) at: %p\n", medium);
  printf("    large (2MB) at: %p\n", large);

  // write to each to verify they're valid
  memset(small, 0xAA, 64);
  memset(medium, 0xBB, 4096);
  memset(large, 0xCC, 2 * 1024 * 1024);

  mm_free(medium);
  mm_free(large);
  mm_free(small);
  printf("    PASSED\n");

  /* Test 2: Lazy initialization */
  printf("  Test 2: Lazy initialization... ");
  reset_allocator();
  extern char* heap;
  heap = NULL;  // force re-init
  void* p1 = mm_malloc(100);
  assert(p1 != NULL);
  assert(heap != NULL);
  printf("\n    heap initialized at: %p\n", heap);
  printf("    PASSED\n");

  /* Test 3: Pointer stability after heap growth */
  printf("  Test 3: Pointer stability after growth... ");
  reset_allocator();
  void* first = mm_malloc(100);
  assert(first != NULL);
  memset(first, 0x42, 100);
  
  printf("\n    initial pointer: %p\n", first);
  extern size_t heap_capacity;
  printf("    initial capacity: %zu bytes\n", heap_capacity);

  // allocate enough to trigger heap growth (1MB initial)
  for (int i = 0; i < 100; i++) {
    void* p = mm_malloc(10000);
    assert(p != NULL);
  }
  
  printf("    capacity after growth: %zu bytes\n", heap_capacity);
  printf("    pointer still at: %p (unchanged)\n", first);

  // verify original pointer still valid
  unsigned char* check = (unsigned char*)first;
  for (int i = 0; i < 100; i++) { assert(check[i] == 0x42); }
  printf("    data integrity verified\n");
  printf("    PASSED\n");

  /* Test 4: Large allocation bypass */
  printf("  Test 4: Large allocation bypass... ");
  reset_allocator();
  void* large1 = mm_malloc(3 * 1024 * 1024);  // 3MB
  void* large2 = mm_malloc(5 * 1024 * 1024);  // 5MB
  assert(large1 != NULL);
  assert(large2 != NULL);

  printf("\n    large1 (3MB) at: %p\n", large1);
  printf("    large2 (5MB) at: %p\n", large2);

  // verify they're separate from main heap
  extern char* current;
  size_t heap_used = current - heap;
  printf("    main heap used: %zu bytes (should be minimal)\n", heap_used);
  printf("    heap base: %p, large1: %p (diff: %td bytes)\n", 
         heap, large1, (char*)large1 - heap);
  assert(heap_used < 1024 * 1024);  // main heap should be mostly empty

  mm_free(large1);
  mm_free(large2);
  printf("    PASSED\n");

  /* Test 5: Double initialization guard */
  printf("  Test 5: Double initialization guard... ");
  int result = mm_init();
  printf("\n    mm_init() returned: %d (0 = already initialized)\n", result);
  assert(result == 0);  // should return 0 (already initialized)
  printf("    PASSED\n");

  /* Test 6: Heap growth boundaries */
  printf("  Test 6: Heap growth at boundaries... ");
  reset_allocator();
  printf("\n    initial capacity: %zu bytes\n", heap_capacity);
  
  // allocate exactly to 1MB boundary
  size_t allocated = 0;
  while (allocated < 1024 * 1024 - 1024) {
    void* p = mm_malloc(1000);
    assert(p != NULL);
    allocated += 1000 + sizeof(block_header_t);
  }
  printf("    allocated up to boundary: %zu bytes\n", allocated);

  // this should trigger first mprotect growth
  void* trigger_growth = mm_malloc(10000);
  assert(trigger_growth != NULL);
  
  printf("    capacity after growth: %zu bytes\n", heap_capacity);
  printf("    growth factor: %.1fx\n", (double)heap_capacity / (1024.0 * 1024.0));
  assert(heap_capacity >= 2 * 1024 * 1024);  // should have doubled
  printf("    PASSED\n");

  printf("\n✓ ALL mmap implementation tests passed!\n");

  return 0;
}