#include "malloc.h"
#include <stdio.h>

int main() {
    printf("=== Heap Overflow Test ===\n");

    printf("1. Initial state:\n");
    print_heap_stats();

    printf("2. Allocating large chunks to fill heap:\n");

    // allocate 10 chunks of 100KB each = 1MB total
    void* ptrs[12];

    for (int i = 0; i < 12; i++) {
        printf("   Attempt %d: Allocating 100KB...\n", i + 1);
        ptrs[i] = my_malloc(100 * 1024); // 100KB

        if (ptrs[i] == NULL) {
            printf("   Allocation %d FAILED (expected after ~10 allocations)\n", i + 1);
            break;
        } else {
            printf("   Allocation %d succeeded at %p\n", i + 1, ptrs[i]);
        }

        // show stats every n allocations
        if ((i + 1) % 3 == 0) { print_heap_stats(); }
    }

    printf("\n3. Try one massive allocation:\n");
    printf("   Attempting to allocate 2MB (larger than our 1MB heap)...\n");
    void* huge = my_malloc(2 * 1024 * 1024);

    if (huge == NULL) {
        printf("   Correctly rejected oversize allocation\n");
    } else {
        printf("   Unexpectedly succeeded! This shouldn't happen.\n");
    }

    printf("\n4. Final heap state:\n");
    print_heap_stats();

    printf("=== Overflow Test Complete ===\n");
    return 0;
}