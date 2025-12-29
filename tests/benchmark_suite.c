#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <dlfcn.h>
#include "../src/memoman.h"

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_RESET   "\x1b[0m"

/* Configuration */
#define NUM_OPS 1000000
#define MAX_ALLOC_SIZE 4096
#define RANDOM_SEED 42

typedef void* (*malloc_func)(size_t);
typedef void (*free_func)(void*);
typedef void (*init_func)(void);
typedef void (*destroy_func)(void);

typedef struct {
    const char* name;
    malloc_func malloc;
    free_func free;
    init_func init;
    destroy_func destroy;
} allocator_vtable_t;

/* Wrappers for System */
void sys_init_stub(void) {}
void sys_destroy_stub(void) {}

/* Wrappers for Memoman */
static mm_allocator_t* bench_allocator = NULL;
static void* bench_pool = NULL;
#define BENCH_POOL_SIZE (1024 * 1024 * 1024) /* 1GB */

void mm_init_wrapper(void) {
    bench_pool = malloc(BENCH_POOL_SIZE);
    if (!bench_pool) { perror("malloc failed"); exit(1); }
    bench_allocator = mm_create(bench_pool, BENCH_POOL_SIZE);
}

void mm_destroy_wrapper(void) {
    free(bench_pool);
    bench_pool = NULL;
    bench_allocator = NULL;
}

void* mm_malloc_wrapper(size_t size) {
    return mm_malloc_inst(bench_allocator, size);
}

void mm_free_wrapper(void* ptr) {
    mm_free_inst(bench_allocator, ptr);
}

/* Timing Utils */
double get_time_sec() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

long get_max_rss_kb() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

/* --- Workloads --- */

/* 1. Bulk Allocation: Alloc N, Free N */
void run_bulk_alloc_free(const allocator_vtable_t* alloc, int count) {
    printf("  [Bulk Alloc/Free] %d blocks of 128 bytes...\n", count);
    void** ptrs = calloc(count, sizeof(void*));
    if (!ptrs) { perror("calloc failed"); exit(1); }
    
    double start = get_time_sec();
    
    for (int i = 0; i < count; i++) {
        ptrs[i] = alloc->malloc(128);
    }
    for (int i = 0; i < count; i++) {
        alloc->free(ptrs[i]);
    }
    
    double end = get_time_sec();
    double duration = end - start;
    printf("    Time: %.4f sec | Throughput: %.0f ops/sec\n", 
           duration, (count * 2) / duration);
    
    free(ptrs);
}

/* 2. Random Churn: Mix of malloc/free with varying sizes */
void run_random_churn(const allocator_vtable_t* alloc, int iterations) {
    printf("  [Random Churn] %d ops, sizes 1-%d bytes...\n", iterations, MAX_ALLOC_SIZE);
    
    /* We keep a pool of pointers to fragment the heap */
    int pool_size = 10000;
    void** ptrs = calloc(pool_size, sizeof(void*));
    if (!ptrs) { perror("calloc failed"); exit(1); }
    
    srand(RANDOM_SEED);
    
    double start = get_time_sec();
    
    for (int i = 0; i < iterations; i++) {
        int idx = rand() % pool_size;
        if (ptrs[idx]) {
            alloc->free(ptrs[idx]);
            ptrs[idx] = NULL;
        } else {
            size_t sz = (rand() % MAX_ALLOC_SIZE) + 1;
            ptrs[idx] = alloc->malloc(sz);
        }
    }
    
    /* Cleanup remaining */
    for (int i = 0; i < pool_size; i++) {
        if (ptrs[i]) alloc->free(ptrs[i]);
    }
    
    double end = get_time_sec();
    double duration = end - start;
    printf("    Time: %.4f sec | Throughput: %.0f ops/sec\n", 
           duration, iterations / duration);
    
    free(ptrs);
}

/* 3. Binary Tree: Stress recursion and many small allocs */
typedef struct node {
    struct node* left;
    struct node* right;
    int payload;
} node_t;

node_t* build_tree(const allocator_vtable_t* alloc, int depth) {
    if (depth == 0) return NULL;
    node_t* n = (node_t*)alloc->malloc(sizeof(node_t));
    if (!n) return NULL;
    n->left = build_tree(alloc, depth - 1);
    n->right = build_tree(alloc, depth - 1);
    return n;
}

void free_tree(const allocator_vtable_t* alloc, node_t* n) {
    if (!n) return;
    free_tree(alloc, n->left);
    free_tree(alloc, n->right);
    alloc->free(n);
}

void run_tree_stress(const allocator_vtable_t* alloc, int depth) {
    int nodes = (1 << depth) - 1;
    printf("  [Binary Tree] Depth %d (~%d nodes of %zu bytes)...\n", depth, nodes, sizeof(node_t));
    
    double start = get_time_sec();
    
    node_t* root = build_tree(alloc, depth);
    free_tree(alloc, root);
    
    double end = get_time_sec();
    double duration = end - start;
    printf("    Time: %.4f sec | Throughput: %.0f ops/sec\n", 
           duration, (nodes * 2) / duration);
}

void run_suite(const allocator_vtable_t* alloc) {
    printf("========================================\n");
    printf("Benchmarking: %s%s%s\n", ANSI_COLOR_GREEN, alloc->name, ANSI_COLOR_RESET);
    printf("========================================\n");
    
    alloc->init();
    long rss_start = get_max_rss_kb();
    
    run_bulk_alloc_free(alloc, NUM_OPS);
    run_random_churn(alloc, NUM_OPS);
    run_tree_stress(alloc, 16); // 2^16 - 1 = 65535 nodes
    
    long rss_end = get_max_rss_kb();
    printf("  RSS Delta: %ld KB\n", rss_end - rss_start);
    
    alloc->destroy();
    printf("\n");
}

/* Helper to try loading jemalloc dynamically */
int try_load_jemalloc(allocator_vtable_t* vtable) {
    const char* libs[] = { "libjemalloc.so.2", "libjemalloc.so.1", "libjemalloc.so", NULL };
    void* handle = NULL;
    
    for (int i = 0; libs[i]; i++) {
        /* Clear errors */
        dlerror();
        handle = dlopen(libs[i], RTLD_LAZY | RTLD_LOCAL);
        if (handle) {
            vtable->name = "Jemalloc (Dynamic)";
            /* Try standard names first, then je_ prefix */
            vtable->malloc = (malloc_func)dlsym(handle, "malloc");
            if (!vtable->malloc) vtable->malloc = (malloc_func)dlsym(handle, "je_malloc");
            
            vtable->free = (free_func)dlsym(handle, "free");
            if (!vtable->free) vtable->free = (free_func)dlsym(handle, "je_free");
            
            vtable->init = sys_init_stub;
            vtable->destroy = sys_destroy_stub;
            
            if (vtable->malloc && vtable->free) return 1;
            dlclose(handle);
        }
    }
    return 0;
}

int main(void) {
    printf("Starting Benchmark Suite...\n");
    printf("System Allocator: glibc (default)\n\n");

    allocator_vtable_t system_alloc = { 
        "System (malloc)", malloc, free, sys_init_stub, sys_destroy_stub 
    };
    
    allocator_vtable_t memoman_alloc = { 
        "Memoman", mm_malloc_wrapper, mm_free_wrapper, mm_init_wrapper, mm_destroy_wrapper 
    };
    
    run_suite(&system_alloc);
    
    allocator_vtable_t jemalloc_alloc = {0};
    if (try_load_jemalloc(&jemalloc_alloc)) {
        run_suite(&jemalloc_alloc);
    } else {
        printf("Jemalloc not found (checked standard library paths).\n\n");
    }
    
    run_suite(&memoman_alloc);
    
    return 0;
}