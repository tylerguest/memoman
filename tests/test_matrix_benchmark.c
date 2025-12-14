#define _POSIX_C_SOURCE 199309L
#include "../src/memoman.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

double get_time() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

void multiply_matrices(double** A, double** B, double** C, int N) {
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      C[i][j] = 0.0;
      for (int k = 0; k < N; k++) {
        C[i][j] += A[i][k] * B[k][j];
      }
    }
  }
}

void init_matrix(double** matrix, int N) {
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      matrix[i][j] = (double)(rand() % 100) / 10.0;
    }
  }
}

double test_memoman(int N, int iterations) {
  double start = get_time();
  
  for (int iter = 0; iter < iterations; iter++) {
    reset_allocator();
    
    double** A = (double**)mm_malloc(N * sizeof(double*));
    double** B = (double**)mm_malloc(N * sizeof(double*));
    double** C = (double**)mm_malloc(N * sizeof(double*));
    
    for (int i = 0; i < N; i++) {
        A[i] = (double*)mm_malloc(N * sizeof(double));
        B[i] = (double*)mm_malloc(N * sizeof(double));
        C[i] = (double*)mm_malloc(N * sizeof(double));
    }
    
    init_matrix(A, N);
    init_matrix(B, N);
    
    multiply_matrices(A, B, C, N);
    
    for (int i = 0; i < N; i++) {
        mm_free(A[i]);
        mm_free(B[i]);
        mm_free(C[i]);
    }
    mm_free(A);
    mm_free(B);
    mm_free(C);
  }
  
  double end = get_time();
  return end - start;
}

double test_malloc(int N, int iterations) {
  double start = get_time();
  
  for (int iter = 0; iter < iterations; iter++) {
    double** A = (double**)malloc(N * sizeof(double*));
    double** B = (double**)malloc(N * sizeof(double*));
    double** C = (double**)malloc(N * sizeof(double*));
    
    for (int i = 0; i < N; i++) {
      A[i] = (double*)malloc(N * sizeof(double));
      B[i] = (double*)malloc(N * sizeof(double));
      C[i] = (double*)malloc(N * sizeof(double));
    }
    
    init_matrix(A, N);
    init_matrix(B, N);
    
    multiply_matrices(A, B, C, N);
    
    for (int i = 0; i < N; i++) {
      free(A[i]);
      free(B[i]);
      free(C[i]);
    }

    free(A);
    free(B);
    free(C);
  }
  
  double end = get_time();
  return end - start;
}

int main() {
  printf("=== Matrix Multiplication Benchmark ===\n\n");
  
  srand(42);  // Consistent results
  
  int sizes[] = {10, 50, 100, 200};
  int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
  
  printf("Testing allocation/deallocation performance with matrix multiplication\n");
  printf("Each test runs multiple iterations of: allocate -> compute -> free\n\n");
  
  for (int s = 0; s < num_sizes; s++) {
    int N = sizes[s];
    int iterations = (N <= 50) ? 100 : (N <= 100) ? 20 : 5;
    
    printf("Matrix size: %dx%d (%.2f KB per matrix)\n", 
           N, N, (N * N * sizeof(double)) / 1024.0);
    printf("Iterations: %d\n", iterations);
    
    printf("  Testing memoman...");
    fflush(stdout);
    double memoman_time = test_memoman(N, iterations);
    printf(" %.6f seconds\n", memoman_time);
    
    printf("  Testing malloc...");
    fflush(stdout);
    double malloc_time = test_malloc(N, iterations);
    printf(" %.6f seconds\n", malloc_time);
    
    double speedup = malloc_time / memoman_time;
    printf("  Result: memoman is %.2fx %s than malloc\n", 
           speedup > 1.0 ? speedup : 1.0 / speedup,
           speedup > 1.0 ? "faster" : "slower");
    printf("  Operations/sec (memoman): %.2f\n", iterations / memoman_time);
    printf("  Operations/sec (malloc):  %.2f\n\n", iterations / malloc_time);
  }
  
  printf("=== Benchmark Complete ===\n");
  return 0;
}