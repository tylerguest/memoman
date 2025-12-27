#include "../src/memoman.h"
#include <stdio.h>
#include <string.h>

int main() {
  printf("=== Heap Statistics Test ===");  
  
  printf("1. Initial heap state:\n");
  mm_print_heap_stats();  
  
  printf("2. After allocations:\n");
  char* ptr1 = (char*)mm_malloc(100);
  char* ptr2 = (char*)mm_malloc(100);
  int*  ptr3 = (int*)mm_malloc(sizeof(int) * 50);  
  mm_print_heap_stats();  
  
  printf("3. Individual function tests:\n");
  printf("   mm_mm_get_total_allocated(): %zu bytes\n", mm_get_total_allocated());
  printf("   mm_mm_get_free_space(): %zu bytes\n", mm_get_free_space());  
  
  printf("\n4. Memory usage test:\n");
  strcpy(ptr1, "Hello Statistics");
  strcpy(ptr2, "This is a longer string to test memory usage");  
  for (int i = 0; i < 10; i++) { ptr3[i] = i * i; }  
  printf("   String 1: %s\n", ptr1);
  printf("   String 2: %s\n", ptr2);
  printf("   Numbers: ");
  for (int i = 0; i < 10; i++) { printf("%d ", ptr3[i]); }
  printf("\n");  
  
  printf("\n5. After more allocations:\n");
  mm_malloc(1000);   // 1KB
  mm_malloc(5000);   // 5KB  
  mm_print_heap_stats();  
  
  printf("=== Test Complete ===\n");
  
  return 0;
}