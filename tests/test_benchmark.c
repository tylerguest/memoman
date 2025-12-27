#define _POSIX_C_SOURCE 199309L  
#include "../src/memoman.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>

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
  
  double memoman_times[trials];
  double glibc_times[trials];  
  
  for (int t = 0; t < trials; t++) {
    printf("Trial %d/%d\n", t + 1, trials);
    printf("  Testing memoman...\n");
    
    /* FIX: Use API to reset. Destroy old instance, explicitly init new one. */
    mm_destroy(); 
    mm_init();    
    
    double start = get_time();
    void* ptrs[100];
    int active_count = 0;
    
    /* Variable unused warning fix: removed total_allocated if not used, 
       or keep it if you plan to print it. */
    size_t total_allocated = 0;  
    (void)total_allocated; // Silence unused warning
    
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
    memoman_times[t] = end - start;  
    
    printf("  Testing glibc malloc...\n");
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
    glibc_times[t] = end - start;
  }  
  
  double avg_memoman = 0, avg_glibc = 0;
  for (int t = 0; t < trials; t++) {
    avg_memoman += memoman_times[t];
    avg_glibc += glibc_times[t];
  }
  avg_memoman /= trials;
  avg_glibc /= trials;  
  
  printf("\n=== AVERAGED RESULTS ===\n");
  printf("memoman:            %.6f seconds\n", avg_memoman);
  printf("glibc malloc:       %.6f seconds\n", avg_glibc);
  printf("Difference:         %.2fx %s\n",
         avg_glibc > avg_memoman ? avg_glibc / avg_memoman : avg_memoman / avg_glibc,
         avg_glibc > avg_memoman ? "faster" : "slower");
  printf("\nOperations per second:\n");
  printf("memoman:            %.0f ops/sec\n", iterations / avg_memoman);
  printf("glibc malloc:       %.0f ops/sec\n", iterations / avg_glibc);  
  return 0;
}