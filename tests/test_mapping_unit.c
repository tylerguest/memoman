#include "test_framework.h"
#include "../src/memoman.h"

static int check_mapping(size_t size, int expected_fl, int expected_sl) {
    int fl, sl;
    mm_get_mapping_indices(size, &fl, &sl);
    ASSERT_EQ(fl, expected_fl);
    ASSERT_EQ(sl, expected_sl);
    return 1;
}

int get_expected_fl(size_t size) {
    int fl = 0;
    if (size >= TLSF_MIN_BLOCK_SIZE) {
        fl = (sizeof(size_t) * 8) - 1 - __builtin_clzl(size);
        fl -= TLSF_FLI_OFFSET;
    }
    return fl;
}

static int test_clamping(void) {
    if (!check_mapping(1, 0, 0)) return 0;
    if (!check_mapping(TLSF_MIN_BLOCK_SIZE - 1, 0, 0)) return 0;
    if (!check_mapping(TLSF_MIN_BLOCK_SIZE, 0, 0)) return 0;
    return 1;
}

static int test_powers_of_two(void) {
    size_t start_size = TLSF_MIN_BLOCK_SIZE * 2; 
    for (size_t s = start_size; s <= (1024*1024); s *= 2) {
        int expected_fl = get_expected_fl(s);
        if (!check_mapping(s, expected_fl, 0)) return 0;
    }
    return 1;
}

static int test_sl_ranges(void) {
    size_t base = 512;
    int base_fl = get_expected_fl(base);
    size_t step = base / TLSF_SLI_COUNT;
    
    for (int i = 0; i < TLSF_SLI_COUNT; i++) {
        size_t size = base + (i * step);
        if (!check_mapping(size, base_fl, i)) return 0;
        size_t max_in_bin = size + step - 1;
        if (!check_mapping(max_in_bin, base_fl, i)) return 0;
    }
    return 1;
}

static int test_transition(void) {
    size_t power_of_two = 1024;
    int expected_fl_high = get_expected_fl(power_of_two);
    if (!check_mapping(power_of_two, expected_fl_high, 0)) return 0;
    if (!check_mapping(power_of_two - 1, expected_fl_high - 1, TLSF_SLI_COUNT - 1)) return 0;
    return 1;
}

int main(void) {
    TEST_SUITE_BEGIN("TLSF Mapping");
    RUN_TEST(test_clamping);
    RUN_TEST(test_powers_of_two);
    RUN_TEST(test_sl_ranges);
    RUN_TEST(test_transition);
    TEST_SUITE_END();
    TEST_MAIN_END();
}