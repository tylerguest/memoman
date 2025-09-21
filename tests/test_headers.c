#include "malloc.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("=== Block Headers Test ===\n");

    printf("1. Test allocations with headers:\n");
    char* ptr1 = (char*)my_malloc(100);
    char* ptr2 = (char*)my_malloc(200);
    char* ptr3 = (char*)my_malloc(50);

    print_heap_stats();

    printf("2. Use the memory:\n");
    strcpy(ptr1, "First block");
    strcpy(ptr2, "Second Block");
    strcpy(ptr3, "Third Block");

    printf("   ptr1: %s\n", ptr1);
    printf("   ptr2: %s\n", ptr2);
    printf("   ptr3: %s\n", ptr3);

    printf("\n3. Test free (adds to free list):\n");
    my_free(ptr2);
    my_free(ptr1);

    printf("   ptr3 (still allocated): %s\n", ptr3);

    printf("\n4. Allocate again (still uses bump for now):\n");
    char* ptr4 = (char*)my_malloc(75);
    strcpy(ptr4, "Fourth block");
    printf("   ptr4: %s\n", ptr4);

    print_heap_stats();

    printf("=== Headers Test Complete ===\n");
    return 0;
}