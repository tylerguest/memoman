#include "malloc.h"
#include <stdio.h>
#include <time.h>

int main() {
    printf("=== Performance Test ===\n");
    clock_t start = clock();

    for (int i = 0; i < 10000; i++) {
        void* ptr = my_malloc(100 + i % 500);
        if (i % 3 == 0) my_free(ptr);
    }

    clock_t end = clock();
    printf("10k operations took: %f seconds\n",
          ((double)(end - start)) / CLOCKS_PER_SEC);

    return 0;
}
