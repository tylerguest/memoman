#include "../src/memoman.h"
#include <stdio.h>

int main() {
  printf("=== Comprehensive Allocator Test ===\n");  
  
  void* ptrs[10];  
  for (int i = 0; i < 10; i++) {
      ptrs[i] = mm_malloc(50 + i * 20);
      printf("Allocated ptr[%d]\n", i);
  }  
  
  mm_print_free_list();  
  
  for (int i = 1; i < 10; i += 2) {
      mm_free(ptrs[i]);
      printf("Freed ptr[%d]\n", i);
  }  
  
  mm_print_free_list();  
  
  return 0;
}