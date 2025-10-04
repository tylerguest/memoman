#include "malloc.h"
#include <stdio.h>

int main() {
    printf("=== Comprehensive Allocator Test ===\n");

    void* ptrs[10];

    for (int i = 0; i < 10; i++) {
        ptrs[i] = memomall(50 + i * 20);
        printf("Allocated ptr[%d]\n", i);
    }

    print_free_list();

    for (int i = 1; i < 10; i += 2) {
        memofree(ptrs[i]);
        printf("Freed ptr[%d]\n", i);
    }

    print_free_list();

    void* big = memomall(500);
    print_free_list();

    return 0;
}