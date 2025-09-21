#include "malloc.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("=== Block Coalescing Test ===\n");

    printf("1. Allocate three adjacent blocks:\n");
    char* ptr1 = (char*)my_malloc(100);
    char* ptr2 = (char*)my_malloc(200);
    char* ptr3 = (char*)my_malloc(150);

    strcpy(ptr1, "Block 1");
    strcpy(ptr2, "Block 2");
    strcpy(ptr3, "Block 3");

    printf("   ptr1: %s at %p\n", ptr1, (void*)ptr1);
    printf("   ptr2: %s at %p\n", ptr2, (void*)ptr2);
    printf("   ptr3: %s at %p\n", ptr3, (void*)ptr3);

    printf("\n2. Free middle block:\n");
    my_free(ptr2);
    print_free_list();

    printf("3. Free first block (should coalesve with middle):\n");
    my_free(ptr1);
    print_free_list();

    printf("4. Free third block (should coalesce with combined block):\n");
    my_free(ptr3);
    print_free_list();

    printf("5. Allocate large block (should reuse coalesced space):\n");
    char* ptr4 = (char*)my_malloc(400);
    strcpy(ptr4, "Large reused block");
    printf("   ptr4: %s at %p\n", ptr4, (void*)ptr4);

    print_free_list();

    printf("=== Coalescing Test Complete ===\n");

    printf("=== True Coalescing Test ===\n");

    char* ptr5 = (char*)my_malloc(100);
    char* ptr6 = (char*)my_malloc(200);
    char* ptr7 = (char*)my_malloc(150);

    printf("Addresses: ptr5=%p, ptr6=%p, ptr7=%p\n",
    (void*)ptr5, (void*)ptr6, (void*)ptr7);

    printf("\n1. Free ptr5:\n");
    my_free(ptr5);
    print_free_list();

    printf("\n2. Free ptr6 (adjacent to ptr5):\n");
    my_free(ptr6);
    print_free_list();

    printf("\n3. Free ptr7 (adjacent to ptr6):\n");
    my_free(ptr7);
    print_free_list();

    return 0;
}