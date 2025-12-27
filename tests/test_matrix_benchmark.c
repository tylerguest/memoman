#define _POSIX_C_SOURCE 199309L
#include "../src/memoman.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h> // For memset

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

// Helper to shuffle arrays (simulates random free order)
void shuffle(void **array, int n) {
    for (int i = 0; i < n - 1; i++) {
        int j = i + rand() / (RAND_MAX / (n - i) + 1);
        void *t = array[j];
        array[j] = array[i];
        array[i] = t;
    }
}

double get_time() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

// TEST 1: LINEAR (Stack-like behavior)
// This favors glibc malloc heavily because of "tcache" (hot stack)
double test_memoman_linear(int num_allocs, size_t size) {
  void** ptrs = malloc(num_allocs * sizeof(void*));
  // Note: NOT resetting allocator inside loop to test sustainment
  mm_reset_allocator(); 
  
  double start = get_time();
  
  for(int k=0; k<100; k++) { // Run 100 times to get measurable time
      for (int i = 0; i < num_allocs; i++) {
        ptrs[i] = mm_malloc(size);
        // Optional: Write to memory to force page fault
        if(ptrs[i]) memset(ptrs[i], 0, size); 
      }
      for (int i = 0; i < num_allocs; i++) {
        if(ptrs[i]) mm_free(ptrs[i]);
      }
  }
  
  double end = get_time();
  free(ptrs);
  return end - start;
}

double test_malloc_linear(int num_allocs, size_t size) {
  void** ptrs = malloc(num_allocs * sizeof(void*));
  
  double start = get_time();
  
  for(int k=0; k<100; k++) {
      for (int i = 0; i < num_allocs; i++) {
        ptrs[i] = malloc(size);
        if(ptrs[i]) memset(ptrs[i], 0, size);
      }
      for (int i = 0; i < num_allocs; i++) {
        if(ptrs[i]) free(ptrs[i]);
      }
  }
  
  double end = get_time();
  free(ptrs);
  return end - start;
}

// TEST 2: RANDOM FREE (Fragmentation Stress)
// This is where TLSF should show stability vs malloc's fragmentation issues
double test_memoman_random(int num_allocs, size_t size) {
  void** ptrs = malloc(num_allocs * sizeof(void*));
  mm_reset_allocator();
  
  double start = get_time();
  
  for(int k=0; k<50; k++) {
      // 1. Allocate all
      for (int i = 0; i < num_allocs; i++) {
        ptrs[i] = mm_malloc(size);
      }
      
      // 2. Shuffle the pointers (Free in random order)
      shuffle(ptrs, num_allocs);
      
      // 3. Free all
      for (int i = 0; i < num_allocs; i++) {
        if(ptrs[i]) mm_free(ptrs[i]);
      }
  }
  
  double end = get_time();
  free(ptrs);
  return end - start;
}

double test_malloc_random(int num_allocs, size_t size) {
  void** ptrs = malloc(num_allocs * sizeof(void*));
  
  double start = get_time();
  
  for(int k=0; k<50; k++) {
      for (int i = 0; i < num_allocs; i++) {
        ptrs[i] = malloc(size);
      }
      
      shuffle(ptrs, num_allocs);
      
      for (int i = 0; i < num_allocs; i++) {
        if(ptrs[i]) free(ptrs[i]);
      }
  }
  
  double end = get_time();
  free(ptrs);
  return end - start;
}

int main() {
  printf("=== Pure Allocator Benchmark ===\n");
  srand(42); 

  int num_allocs = 5000;
  size_t alloc_size = 256; // 256 bytes

  printf("\n--- Test 1: Linear Alloc/Free (Stack Behavior) ---\n");
  printf("Allocating %d blocks of %zu bytes, 100 times.\n", num_allocs, alloc_size);
  
  double t_mem = test_memoman_linear(num_allocs, alloc_size);
  double t_mal = test_malloc_linear(num_allocs, alloc_size);

  printf("memoman: %.6f s\n", t_mem);
  printf("malloc: %.6f s\n", t_mal);
  
  if (t_mal < t_mem) { printf("Result:  malloc is %.2fx faster\n", t_mem / t_mal); }
  else { printf("Result:  memoman is %.2fx faster \n", t_mal / t_mem); }

  printf("\n--- Test 2: Random Free (Fragmentation Stress) ---\n");
  printf("Allocating %d blocks, shuffling, freeing, 50 times.\n", num_allocs);
  
  t_mem = test_memoman_random(num_allocs, alloc_size);
  t_mal = test_malloc_random(num_allocs, alloc_size);

  printf("memoman: %.6f s\n", t_mem);
  printf("malloc: %.6f s\n", t_mal);
  
  if (t_mal < t_mem) { printf("Result:  malloc is %.2fx faster\n", t_mem / t_mal); }
  else { printf("Result:  memoman is %.2fx faster \n", t_mal / t_mem); }

  return 0;
}