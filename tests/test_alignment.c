#include "../src/memoman.h"
#include <stdio.h>
#include <string.h>

void test_alignment() {
  printf("=== Testing Memory Alignment ===\n\n");
  
  printf("1. Testing different allocation sizes:\n");
  void* ptr1 = mm_malloc(1);   
  void* ptr2 = mm_malloc(7);   
  void* ptr3 = mm_malloc(8);   
  void* ptr4 = mm_malloc(9);   
  void* ptr5 = mm_malloc(13);  
  void* ptr6 = mm_malloc(16);  
  void* ptr7 = mm_malloc(17);  
  printf("\n");
  
  printf("2. Checking address alignment:\n");
  printf("ptr1 address: %p (should end in 0 or 8)\n", ptr1);
  printf("ptr2 address: %p (should end in 0 or 8)\n", ptr2);
  printf("ptr3 address: %p (should end in 0 or 8)\n", ptr3);
  printf("ptr4 address: %p (should end in 0 or 8)\n", ptr4);
  printf("ptr5 address: %p (should end in 0 or 8)\n", ptr5);
  printf("ptr6 address: %p (should end in 0 or 8)\n", ptr6);
  printf("ptr7 address: %p (should end in 0 or 8)\n", ptr7);
  printf("\n");
  
  printf("4. Testing actual memory usage:\n");
  char* str1 = (char*)mm_malloc(10);  // 16 bytes
  char* str2 = (char*)mm_malloc(5);   // 8 bytes
  strcpy(str1, "Hello!");
  strcpy(str2, "World!");
  printf("String 1: %s\n", str1);
  printf("String 2: %s\n", str2);
  printf("\n=== Test Complete ===\n");
}

int main() {
  test_alignment();
  return 0;
}