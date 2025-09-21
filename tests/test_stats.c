#include "malloc.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("=== Heap Statistics Test ===");

    printf("1. Initial heap state:\n");
    print_heap_stats();

    printf("2. After allocations:\n");
    char* ptr1 = (char*)my_malloc(100);
    char* ptr2 = (char*)my_malloc(100);
    int*  ptr3 = (int*)my_malloc(sizeof(int) * 50);

    print_heap_stats();

    printf("3. Individual function tests:\n");
    printf("   get_total_allocated(): %zu bytes\n", get_total_allocated());
    printf("   get_free_space(): %zu bytes\n", get_free_space());

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
    my_malloc(1000);   // 1KB
    my_malloc(5000);   // 5KB

    print_heap_stats();

    printf("=== Test Complete ===\n");
    return 0;
}