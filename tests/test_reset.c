#include "../src/memoman.h"
#include <stdio.h>
#include <string.h>

int main() {
  printf("=== Reset Allocator Test ===\n");  
  
  printf("1. Initial state:\n");
  print_heap_stats();  
  
  printf("2. Make some allocations:\n");
  char* ptr1 = (char*)memomall(1000);
  char* ptr2 = (char*)memomall(2000);
  char* ptr3 = (char*)memomall(3000);  
  strcpy(ptr1, "First allocation");
  strcpy(ptr2, "Second allocation");
  strcpy(ptr3, "Third allocation");  
  printf("   ptr1: %s\n", ptr1);
  printf("   ptr2: %s\n", ptr2);
  printf("   ptr3: %s\n", ptr3);  
  print_heap_stats();  
  
  printf("3. Reset the allocator:\n");
  reset_allocator();
  print_heap_stats();  
  
  printf("4. Allocate again after reset:\n");
  char* new_ptr = (char*)memomall(500);
  strcpy(new_ptr, "After reset!");
  printf("   new_ptr: %s at %p\n", new_ptr, (void*)new_ptr);  
  print_heap_stats();  
  
  printf("=== Reset Test Complete ===\n");
  
  return 0;
}