#define _POSIX_C_SOURCE 199309L
#include "../src/memoman.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

double get_time() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main() {
  const int NUM_ALLOCS = 10000;
  void* ptrs[NUM_ALLOCS];
  
  printf("=== Pure Allocation Speed Test ===\n\n");
  
  reset_allocator();
  double start = get_time();
  for (int i = 0; i < NUM_ALLOCS; i++) { ptrs[i] = mm_malloc(80); }  // 19.53 KB matrix row
  for (int i = 0; i < NUM_ALLOCS; i++) { mm_free(ptrs[i]); }
  double memoman_time = get_time() - start;
  
  start = get_time();
  for (int i = 0; i < NUM_ALLOCS; i++) { ptrs[i] = malloc(80); }
  for (int i = 0; i < NUM_ALLOCS; i++) { free(ptrs[i]); }
  double malloc_time = get_time() - start;
  
  printf("Allocations: %d × 80 bytes\n", NUM_ALLOCS);
  printf("memoman: %.6f seconds (%.0f allocs/sec)\n", 
         memoman_time, NUM_ALLOCS / memoman_time);
  printf("malloc:  %.6f seconds (%.0f allocs/sec)\n", 
         malloc_time, NUM_ALLOCS / malloc_time);
  printf("Speedup: %.2fx\n", malloc_time / memoman_time);
  
  return 0;
}