#include "malloc.h"
#include <stdio.h>
#include <string.h>

void test_alignment() {
    printf("=== Testing Memory Alignment ===\n\n");

    printf("1. Testing different allocation sizes:\n");
    void* ptr1 = my_malloc(1);     // 8 bytes
    void* ptr2 = my_malloc(7);     // 8 bytes
    void* ptr3 = my_malloc(8);     // 8 bytes
    void* ptr4 = my_malloc(9);     // 16 bytes
    void* ptr5 = my_malloc(13);    // 16 bytes
    void* ptr6 = my_malloc(16);    // 16 bytes
    void* ptr7 = my_malloc(17);    // 24 bytes

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
    char* str1 = (char*)my_malloc(10);  // 16 bytes
    char* str2 = (char*)my_malloc(5);   // 8 bytes

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