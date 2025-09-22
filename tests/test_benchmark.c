#include "malloc.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

int main() {
    printf("=== Allocator Benchmark Comparison ===\n");

    const int iterations = 1000000;
    printf("Testing %d operations...\n\n", iterations);

    printf("Testing your allocator...\n");
    reset_allocator();

    clock_t start = clock();
    void* ptrs[100];
    int active_count = 0;

    for (int i = 0; i < iterations; i++) {
        if (active_count < 100 || (i % 2 == 0)) {
            void* ptr = my_malloc(100 + i % 400);
            if (ptr && active_count < 100) {
                ptrs[active_count++] = ptr;
            } else if (ptr) { my_free(ptr); }
        } else {
            int idx = rand() % active_count;
            my_free(ptrs[idx]);
            ptrs[idx] = ptrs[--active_count];
        }
    }

    for (int i = 0; i < active_count; i++) {
        my_free(ptrs[i]);
    }

    clock_t end = clock();
    double your_time = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("Testing system malloc...\n");
    start = clock();

    void* sys_ptrs[100];
    active_count = 0;

    for (int i = 0; i < iterations; i++) {
        if (active_count < 100 || (i % 2 == 0)) {
            void* ptr = malloc(100 + i % 400);
            if (ptr && active_count < 100) {
                sys_ptrs[active_count++] = ptr;
            } else if (ptr) { free(ptr); }
        } else {
            int idx = rand() % active_count;
            free(sys_ptrs[idx]);
            sys_ptrs[idx] = sys_ptrs[--active_count];
        }
    }

    for (int i = 0; i < active_count; i++) {
        free(sys_ptrs[i]);
    }

    end = clock();
    double system_time = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("\n=== RESULTS ===\n");
    printf("Your allocator:     %.6f seconds\n", your_time);
    printf("System malloc:      %.6f seconds\n", system_time);
    printf("Difference:         %.2fx %s\n",
           system_time > your_time ? system_time / your_time : your_time / system_time,
           system_time > your_time ? "faster" : "slower");
    printf("\nOperations per second:\n");
    printf("Your allocator:     %.0f ops/sec\n", iterations / your_time);
    printf("System allocator:   %.0f ops/sec\n", iterations / system_time);

    return 0;
}