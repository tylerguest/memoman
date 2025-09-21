#include "malloc.h"
#include <string.h>
#include <stdio.h>

int main() {
    // test basic allocation
    char* str = my_malloc(50);
    strcpy(str, "Hello World!");
    printf("String: %s\n", str);

    // test multiple allocations
    int* nums = my_malloc(sizeof(int) * 5);
    for (int i = 0; i < 5; i++) {
        nums[i] = i * 10;
        printf("nums[%d] = %d\n", i, nums[i]);
    }

    return 0;
}