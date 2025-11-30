#include "../src/memoman.h"
#include <stdio.h>
#include <string.h>

int main() {
  printf("=== Block Splitting Test ===\n");  
  
  printf("1. Allocate a large block:\n");
  char* ptr1 = (char*)memomall(400);
  strcpy(ptr1, "Large block");  
  
  printf("2. Free the large block:\n");
  memofree(ptr1);  
  
  printf("3. Allocate smaller block (should split):\n");
  char* ptr2 = (char*)memomall(100);
  strcpy(ptr2, "Small reused block");  
  
  printf("4. Allocate another small block (should use split remainder):\n");
  char* ptr3 = (char*)memomall(200);
  strcpy(ptr3, "Using remainder");  
  printf("   ptr2: %s\n", ptr2);
  printf("   ptr3: %s\n", ptr3);  
  
  printf("5. Show final state:\n");
  print_heap_stats();  
  
  printf("=== Block Splitting Test Complete ===\n");
  
  return 0;
}