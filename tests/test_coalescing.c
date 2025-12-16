#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/memoman.h"

// ANSI Colors
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

// Helper to check if a pointer is aligned
int is_aligned(void* ptr) {
    return ((size_t)ptr % 8) == 0;
}

void test_basic_coalesce_right() {
    printf("\n--- Test: Coalesce Right (Merge with Next) ---\n");
    reset_allocator();

    // 1. Alloc A and B
    void* a = mm_malloc(64);
    void* b = mm_malloc(64);
    
    // Guard allocation to prevent merging with the rest of the heap
    void* guard = mm_malloc(64); 

    // 2. Free B first (It sits there waiting)
    mm_free(b);
    
    // 3. Free A (Should merge with B)
    mm_free(a);

    // 4. Request a size equal to A+B combined (plus headers)
    // A (64) + B (64) + Header(B) ~ 80 bytes available?
    // Let's ask for 100 bytes. If they merged, we should get pointer A back.
    void* c = mm_malloc(100);

    print_result(c == a, "Merged block returned original address (Left Merge)");
    
    mm_free(c);
    mm_free(guard);
}

void test_basic_coalesce_left() {
    printf("\n--- Test: Coalesce Left (Merge with Prev) ---\n");
    reset_allocator();

    void* a = mm_malloc(64);
    void* b = mm_malloc(64);
    void* guard = mm_malloc(64);

    // 1. Free A first
    mm_free(a);
    
    // 2. Free B (Should look left and see A is free, then merge)
    mm_free(b);

    // 3. Alloc large block
    void* c = mm_malloc(100);
    
    print_result(c == a, "Merged block returned original address (Right Merge)");

    mm_free(c);
    mm_free(guard);
}

void test_sandwich_coalesce() {
    printf("\n--- Test: The Sandwich (Merge Left AND Right) ---\n");
    reset_allocator();

    // Setup: [ A ] [ B ] [ C ] [ Guard ]
    void* a = mm_malloc(64);
    void* b = mm_malloc(64);
    void* c = mm_malloc(64);
    void* guard = mm_malloc(64);

    // 1. Free A (Left neighbor)
    mm_free(a);
    
    // 2. Free C (Right neighbor)
    mm_free(c);
    
    // Heap state: [ Free ] [ Used B ] [ Free ] [ Guard ]

    // 3. Free B (Should merge with A and C instantly)
    mm_free(b);

    // 4. Verification
    // If they merged, we now have a hole of size:
    // 64+H + 64+H + 64+H approx 200+ bytes.
    // If we request 150 bytes, we MUST get 'a' back.
    // If we get NULL or a different pointer, fragmentation happened.
    void* d = mm_malloc(150);

    print_result(d == a, "Middle block merged with both neighbors");
    
    mm_free(d);
    mm_free(guard);
}

void test_fragmentation_survival() {
    printf("\n--- Test: Fragmentation Survival ---\n");
    reset_allocator();
    
    // Alloc 100 blocks
    void* ptrs[100];
    for(int i=0; i<100; i++) ptrs[i] = mm_malloc(64);

    // Free every OTHER block (Swiss Cheese)
    // [Free] [Used] [Free] [Used] ...
    for(int i=0; i<100; i+=2) mm_free(ptrs[i]);

    // Check free space
    size_t free_before = get_free_space();
    
    // Now free the remaining used blocks.
    // This triggers 50 merges. 
    // If your coalescing is perfect, we should get 100% of the space back 
    // as one contiguous block.
    for(int i=1; i<100; i+=2) mm_free(ptrs[i]);
    
    // Try to allocate the ENTIRE sum of those blocks.
    // If fragmentation remains, this will fail (return NULL).
    // Total approx: 100 * (64 + header)
    size_t total_size = 100 * 64; 
    void* huge = mm_malloc(total_size);

    print_result(huge != NULL, "Allocated total capacity after random free");

    if (huge) mm_free(huge);
}

int main() {
    printf("=== TLSF Coalescing Test ===\n");
    
    if (mm_init() != 0) {
        printf("Failed to init allocator\n");
        return 1;
    }

    test_basic_coalesce_right();
    test_basic_coalesce_left();
    test_sandwich_coalesce();
    test_fragmentation_survival();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);

    return g_failed > 0 ? 1 : 0;
}