#include "test_framework.h"
#include "../src/memoman.h"

/* 
 * Helper to calculate expected indices based on the new 8-byte alignment logic.
 * This mirrors the logic in memoman.c:mapping()
 */
static void get_expected_indices(size_t size, int* fl, int* sl) {
    if (size < (1 << TLSF_FLI_OFFSET)) {
        *fl = 0;
        *sl = (int)size / ALIGNMENT;
    } else {
        /* Calculate log2(size) manually for verification */
        int fli = 0;
        size_t temp = size;
        while (temp >>= 1) fli++;
        
        *fl = fli - (TLSF_FLI_OFFSET - 1);
        *sl = (int)((size >> (fli - TLSF_SLI)) & (TLSF_SLI_COUNT - 1));
        
        /* Clamp FL/SL as the implementation does */
        if (*fl < 0) { *fl = 0; *sl = 0; }
        if (*fl >= TLSF_FLI_MAX) { *fl = TLSF_FLI_MAX - 1; *sl = TLSF_SLI_COUNT - 1; }
    }
}

static int check_mapping(size_t size, int expected_fl, int expected_sl) {
    int fl, sl;
    mm_get_mapping_indices(size, &fl, &sl);
    
    if (fl != expected_fl || sl != expected_sl) {
        printf("    [FAIL] Size %zu: Got (%d, %d), Expected (%d, %d)\n", 
               size, fl, sl, expected_fl, expected_sl);
        return 0;
    }
    return 1;
}

static int test_small_blocks(void) {
    /* Small blocks (< 256 bytes) map linearly to FL 0 */
    
    /* 0-7 bytes -> SL 0 */
    if (!check_mapping(1, 0, 0)) return 0;
    if (!check_mapping(7, 0, 0)) return 0;
    
    /* 8-15 bytes -> SL 1 */
    if (!check_mapping(8, 0, 1)) return 0;
    if (!check_mapping(15, 0, 1)) return 0;
    
    /* 16-23 bytes -> SL 2 */
    if (!check_mapping(16, 0, 2)) return 0;
    
    /* Boundary check: 255 -> SL 31 */
    if (!check_mapping(255, 0, 31)) return 0;
    
    return 1;
}

static int test_large_blocks(void) {
    /* Large blocks (>= 256 bytes) use logarithmic mapping */
    
    /* 256 (1<<8) -> FLI 8. FL = 8 - 7 = 1. SL = 0 */
    if (!check_mapping(256, 1, 0)) return 0;
    
    /* 512 (1<<9) -> FLI 9. FL = 9 - 7 = 2. SL = 0 */
    if (!check_mapping(512, 2, 0)) return 0;
    
    return 1;
}

static int test_automated_coverage(void) {
    /* Verify a wide range of sizes against the reference logic */
    for (size_t s = 1; s < 1024 * 1024; s += (s < 512 ? 1 : 64)) {
        int exp_fl, exp_sl;
        get_expected_indices(s, &exp_fl, &exp_sl);
        if (!check_mapping(s, exp_fl, exp_sl)) return 0;
    }
    return 1;
}

int main(void) {
    TEST_SUITE_BEGIN("TLSF Mapping");
    RUN_TEST(test_small_blocks);
    RUN_TEST(test_large_blocks);
    RUN_TEST(test_automated_coverage);
    TEST_SUITE_END();
    TEST_MAIN_END();
}