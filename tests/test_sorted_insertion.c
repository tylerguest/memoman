#include "../src/memoman.h"
#include <stdio.h>
#include <assert.h>

static int verify_sorted(void) {
  block_header_t* free_list = get_free_list();  
  
  if (!free_list || !free_list->next) return 1;  
  
  block_header_t* curr = free_list;
  
  while (curr->next) {
    if (curr >= curr->next) {
      printf("FAIL: %p >= %p\n", (void*)curr, (void*)curr->next);
      return 0;
    }
    curr = curr->next;
  }
  return 1;
}

int main(void) {
  printf("=== Sorted Insertion Tests ===\n\n");
  printf("Test 1: Random order frees maintain sorting\n");
  reset_allocator();
  void* p[5];
  for (int i = 0; i < 5; i++) p[i] = memomall(3000);

  memofree(p[2]);
  assert(verify_sorted());
  memofree(p[4]);
  assert(verify_sorted());
  memofree(p[0]);
  assert(verify_sorted());
  memofree(p[3]);
  assert(verify_sorted());
  memofree(p[1]);
  assert(verify_sorted());
  printf("Passed\n\n");

  printf("Test 2: Insert at beginning\n");
  reset_allocator();
  void* p1 = memomall(3000);
  void* p2 = memomall(3000);
  memofree(p2);
  memofree(p1);
  assert(verify_sorted());
  printf("Passed\n");

  printf("Test 3: Insert at end\n");
  reset_allocator();
  p1 = memomall(3000);
  p2 = memomall(3000);
  memofree(p1);
  memofree(p2);
  assert(verify_sorted());
  printf("Passed\n\n");

  printf("Test 4: Insert in middle\n");
  reset_allocator();
  p1 = memomall(3000);
  p2 = memomall(3000);
  void* p3 = memomall(3000);
  memofree(p1);
  memofree(p3);
  memofree(p2);
  assert(verify_sorted());
  printf("Passed\n\n");

  printf("Test 5: Coalescing maintains sort order\n");
  reset_allocator();
  void* ptrs[60];
  for (int i = 0; i < 60; i++) ptrs[i] = memomall(3000);
  for (int i = 0; i < 60; i++) memofree(ptrs[i]);
  assert(verify_sorted());
  printf("Passed\n\n");

  printf("All sorted insertion tests passed!\n");

  return 0;
}