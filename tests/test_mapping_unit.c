#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// CRITICAL FIX: Include the .c file directly to see 'static' functions.
// Do NOT include memoman.h
#include "../src/memoman.c"

// ANSI Color codes for readable output
#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define RESET "\033[0m"

int g_tests_passed = 0;
int g_tests_failed = 0;

// Note: No 'extern' declarations needed anymore because 
// we literally pasted the code above via #include.

void assert_mapping(size_t size, int expected_fl, int expected_sl, const char* desc) {
    int fl, sl;
    mapping(size, &fl, &sl);
    
    if (fl == expected_fl && sl == expected_sl) {
        printf(GREEN "[PASS]" RESET " Size %-8zu -> FL: %2d, SL: %2d (%s)\n", size, fl, sl, desc);
        g_tests_passed++;
    } else {
        printf(RED "[FAIL]" RESET " Size %-8zu (%s)\n", size, desc);
        printf("       Expected: FL=%d, SL=%d\n", expected_fl, expected_sl);
        printf("       Actual:   FL=%d, SL=%d\n", fl, sl);
        g_tests_failed++;
    }
}

void test_clamping() {
    printf("\n--- Test Case 1: Minimum Size Clamping ---\n");
    assert_mapping(1, 0, 0, "1 byte");
    assert_mapping(TLSF_MIN_BLOCK_SIZE - 1, 0, 0, "Min Block - 1");
    assert_mapping(TLSF_MIN_BLOCK_SIZE, 0, 0, "Min Block Exact");
}

void test_powers_of_two() {
    printf("\n--- Test Case 2: Exact Powers of Two ---\n");
    size_t start_size = TLSF_MIN_BLOCK_SIZE * 2; 
    
    for (size_t s = start_size; s <= (1024*1024); s *= 2) {
        int expected_fl = mapping_fli(s) - TLSF_FLI_OFFSET;
        assert_mapping(s, expected_fl, 0, "Power of 2");
    }
}

void test_sl_ranges() {
    printf("\n--- Test Case 3: Second Level Granularity ---\n");
    size_t base = 512;
    int base_fl = mapping_fli(base) - TLSF_FLI_OFFSET;
    size_t step = base / TLSF_SLI_COUNT;
    
    printf("Checking FL range starting at %zu with step size %zu...\n", base, step);
    
    for (int i = 0; i < TLSF_SLI_COUNT; i++) {
        size_t size = base + (i * step);
        assert_mapping(size, base_fl, i, "Linear SL Check");
        size_t max_in_bin = size + step - 1;
        assert_mapping(max_in_bin, base_fl, i, "Upper Boundary of SL");
    }
}

void test_transition() {
    printf("\n--- Test Case 4: FL Transitions ---\n");
    size_t power_of_two = 1024;
    int expected_fl_high = mapping_fli(power_of_two) - TLSF_FLI_OFFSET;
    assert_mapping(power_of_two, expected_fl_high, 0, "Power of 2 (1024)");
    assert_mapping(power_of_two - 1, expected_fl_high - 1, TLSF_SLI_COUNT - 1, "One byte less (1023)");
}

int main() {
    printf("=== TLSF Mapping Unit Test ===\n");
    printf("Config: ALIGNMENT=%d, MIN_BLOCK=%d\n", ALIGNMENT, TLSF_MIN_BLOCK_SIZE);
    
    test_clamping();
    test_powers_of_two();
    test_sl_ranges();
    test_transition();
    
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    
    return g_tests_failed > 0 ? 1 : 0;
}