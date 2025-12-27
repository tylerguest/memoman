#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/memoman.h"

// ANSI Colors for visual feedback
#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define RESET "\033[0m"

int g_passed = 0;
int g_failed = 0;

void print_result(int passed, const char* name) {
    if (passed) {
        printf(GREEN "[PASS]" RESET " %s\n", name);
        g_passed++;
    } else {
        printf(RED "[FAIL]" RESET " %s\n", name);
        g_failed++;
    }
}

void test_split_logic() {
    printf("\n--- Test: Block Splitting Logic ---\n");
    
    /* FIX: Replace mm_reset_allocator() with standard API calls */
    mm_destroy();
    mm_init();

    // 1. Allocate a large block (400 bytes)
    void* ptr1 = mm_malloc(400);
    print_result(ptr1 != NULL, "Initial large allocation (400 bytes)");

    // 2. Free it to create a 400-byte hole
    mm_free(ptr1);

    // 3. Allocate a smaller block (100 bytes)
    // TLSF should reuse the 400-byte hole and split it.
    // The pointer returned MUST be the same as the old ptr1 (Best Fit / First Fit)
    void* ptr2 = mm_malloc(100);
    print_result(ptr2 == ptr1, "Reused freed block address");

    // 4. Allocate from the remainder (200 bytes)
    // This should sit exactly after ptr2 + 100 + header_size
    // We verify this by checking the memory distance.
    void* ptr3 = mm_malloc(200);
    
    // Calculate distance between the two pointers
    size_t distance = (char*)ptr3 - (char*)ptr2;
    
    // We expect the distance to be roughly 100 + sizeof(header)
    // It should definitively be less than 400.
    int split_worked = (distance < 400) && (distance > 100);
    
    print_result(split_worked, "Remainder block was used (Distance Check)");
    
    if (split_worked) {
        printf("       (Distance between blocks: %zu bytes)\n", distance);
    }

    mm_free(ptr2);
    mm_free(ptr3);
}

int main() {
    printf("=== TLSF Block Splitting Test ===\n");
    
    if (mm_init() != 0) {
        printf("Failed to init allocator\n");
        return 1;
    }

    test_split_logic();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);
    
    mm_destroy(); // Cleanup at end of main

    return g_failed > 0 ? 1 : 0;
}