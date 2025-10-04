#include "malloc.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("=== Free List Reuse Test ===\n");

    printf("1. Allocate some blocks:\n");
    char* ptr1 = (char*)memomall(100);
    char* ptr2 = (char*)memomall(200);
    char* ptr3 = (char*)memomall(50);

    strcpy(ptr1, "Block A");
    strcpy(ptr2, "Block B");
    strcpy(ptr3, "Block C");

    print_heap_stats();

    printf("2. Free the middle block:\n");
    memofree(ptr2);

    printf("3. Allocate smaller block (should reuse freed space):\n");
    char* ptr4 = (char*)memomall(150);
    strcpy(ptr4, "Block D (reused!)");

    printf("   ptr1: %s\n", ptr1);
    printf("   ptr2: %s\n", ptr4);
    printf("   ptr3: %s\n", ptr3);

    printf("4. Allocate another block (should be new):\n");
    char* ptr5 = (char*)memomall(300);
    strcpy(ptr5, "Block E (new)");
    printf("   ptr5: %s\n", ptr5);

    print_heap_stats();

    printf("=== Free List Reuse Test Complete ===\n");
    return 0;
}