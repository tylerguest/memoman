#include "test_framework.h"
#include "../src/memoman.h"

static int test_split_logic(void) {
    TEST_RESET();
    void* ptr1 = mm_malloc(400);
    ASSERT_NOT_NULL(ptr1);

    mm_free(ptr1);

    void* ptr2 = mm_malloc(100);
    ASSERT_EQ(ptr2, ptr1);

    void* ptr3 = mm_malloc(200);
    ASSERT_NOT_NULL(ptr3);

    size_t distance = (char*)ptr3 - (char*)ptr2;
    ASSERT_LT(distance, 400);
    ASSERT_GT(distance, 100);

    mm_free(ptr2);
    mm_free(ptr3);
    return 1;
}

int main(void) {
    TEST_SUITE_BEGIN("Block Splitting");
    RUN_TEST(test_split_logic);
    TEST_SUITE_END();
    TEST_MAIN_END();
}