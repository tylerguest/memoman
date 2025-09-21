#include "malloc.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("=== Simple Bump Allocator Test ===\n");

    printf("1. Testing basic allocation:\n");
    char* ptr1 = (char*)my_malloc(10);
    char* ptr2 = (char*)my_malloc(20);

    if (ptr1 && ptr2) {
        strcpy(ptr1, "Hello");
        strcpy(ptr2, "World");
        printf("   ptr1: %s\n", ptr1);
        printf("   ptr2: %s\n", ptr2);
        printf("   Basic allocation works\n");
    }

    printf("\n2. Address check:\n");
    printf("   ptr1 address: %p\n", (void*)ptr1);
    printf("   ptr2 address: %p\n", (void*)ptr2);
    if (ptr2 > ptr1) { printf("   Addresses are sequential\n"); }

    printf("\n3. Testing free:\n");
    my_free(ptr1);

    printf("\n=== Test Complete ===");
    return 0;
}