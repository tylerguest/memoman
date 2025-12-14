#define _POSIX_C_SOURCE 199309L  
#include "../src/memoman.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

double get_time() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main() {
  printf("=== Allocator Benchmark Comparison ===\n");  
  
  const int iterations = 1000000;  
  const int trials = 5;
  
  printf("Testing %d operations per trial, %d trials...\n\n", iterations, trials);  
  
  srand(42);  
  
  double your_times[trials];
  double system_times[trials];  
  
  for (int t = 0; t < trials; t++) {
    printf("Trial %d/%d\n", t + 1, trials);
    printf("  Testing memoman...\n");
    reset_allocator();  
    double start = get_time();
    void* ptrs[100];
    int active_count = 0;
    size_t total_allocated = 0;  
    for (int i = 0; i < iterations; i++) {
      size_t size = (i % 10 == 0) ? (100 + rand() % 1000000) : (100 + i % 400);  
      if (active_count < 100 || (i % 2 == 0)) {
        void* ptr = mm_malloc(size);
        if (ptr) {
          if (size >= sizeof(uint64_t)) { *(uint64_t*)ptr = (uint64_t)i; }
          if (active_count < 100) {
            ptrs[active_count++] = ptr;
            total_allocated += size;
          } else { mm_free(ptr); }
        }
      } else {
          int idx = rand() % active_count;
          mm_free(ptrs[idx]);
          ptrs[idx] = ptrs[--active_count];
      }
    }  
    for (int i = 0; i < active_count; i++) { mm_free(ptrs[i]); }  
    double end = get_time();
    your_times[t] = end - start;  
    printf("  Testing system malloc...\n");
    start = get_time();  
    void* sys_ptrs[100];
    active_count = 0;  
    for (int i = 0; i < iterations; i++) {
      size_t size = (i % 10 == 0) ? (100 + rand() % 1000000) : (100 + i % 400);
      if (active_count < 100 || (i % 2 == 0)) {
        void* ptr = malloc(size);
        if (ptr) {
          if (size >= sizeof(uint64_t)) { *(uint64_t*)ptr = (uint64_t)i; }
          if (active_count < 100) { sys_ptrs[active_count++] = ptr; } 
          else { free(ptr); }
        }
      } else {
          int idx = rand() % active_count;
          free(sys_ptrs[idx]);
          sys_ptrs[idx] = sys_ptrs[--active_count];
      }
    }  
    for (int i = 0; i < active_count; i++) { free(sys_ptrs[i]); }  
    end = get_time();
    system_times[t] = end - start;
  }  
  double avg_your = 0, avg_system = 0;
  for (int t = 0; t < trials; t++) {
    avg_your += your_times[t];
    avg_system += system_times[t];
  }
  avg_your /= trials;
  avg_system /= trials;  
  printf("\n=== AVERAGED RESULTS ===\n");
  printf("Your allocator:     %.6f seconds\n", avg_your);
  printf("System malloc:      %.6f seconds\n", avg_system);
  printf("Difference:         %.2fx %s\n",
         avg_system > avg_your ? avg_system / avg_your : avg_your / avg_system,
         avg_system > avg_your ? "faster" : "slower");
  printf("\nOperations per second:\n");
  printf("Your allocator:     %.0f ops/sec\n", iterations / avg_your);
  printf("System allocator:   %.0f ops/sec\n", iterations / avg_system);  
  return 0;
}