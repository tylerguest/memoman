<img src="memoman.png" alt="Memoman" width=50% />

A custom memory allocator implementing the Two-Level Segregated Fit (TLSF) algorithm.

## Features

- **TLSF Algorithm**: O(1) malloc/free via two-level bitmap indexing across 30×32 segregated free lists
- **16-byte alignment** for all allocations
- **Immediate coalescing** with boundary-tagged blocks for bidirectional traversal
- **Reserve-then-commit heap**: Pre-reserves 1GB virtual address space, commits on-demand via `mprotect`
- **Large allocation bypass**: Direct `mmap` for allocations ≥1MB to reduce fragmentation
- **Double-free detection** and heap bounds validation
- **In-place realloc**: Grows/shrinks blocks without copying when possible

## Build

```bash
git clone https://github.com/tylerguest/memoman.git
cd memoman
make all         # build tests (default)
make debug       # enable extra debug logging
make benchmark   # optimized benchmark build
```

## Testing

```bash
make run                # run all tests
./tests/bin/test_*      # run individual tests
```

## Benchmark

```
=== Allocator Benchmark Comparison ===
Testing 1000000 operations per trial, 5 trials...

=== AVERAGED RESULTS ===
memoman:            0.037940 seconds
glibc malloc:       0.058137 seconds
Difference:         1.53x faster

Operations per second:
memoman:            26,357,475 ops/sec
glibc malloc:       17,200,652 ops/sec

(Results vary by hardware, compiler, and workload.)
```

## API

```c
#include "memoman.h"

void* mm_malloc(size_t size);
void  mm_free(void* ptr);
void* mm_calloc(size_t nmemb, size_t size);
void* mm_realloc(void* ptr, size_t size);
size_t mm_get_usable_size(void* ptr);

void   reset_allocator(void);
size_t get_total_allocated(void);
size_t get_free_space(void);
```

## Design Overview

- **TLSF Control Structure**: Two-level bitmap enables O(1) block lookup
- **Block Headers**: Store size + flags (free/prev_free) in LSBs, boundary tags for backward traversal
- **Segregated Free Lists**: 30 first-level × 32 second-level bins for good-fit allocation
- **Heap Growth**: Uses `mmap` with `PROT_NONE` + `mprotect` to grow without relocating pointers
- **Large Blocks**: Allocations ≥1MB use dedicated `mmap` with magic number tracking

## Structure

```
.
├── Makefile
├── README.md
├── src/
│   ├── memoman.c
│   ├── memoman.h
│   └── mmdebug.c
└── tests/
    ├── test_*.c      # 20+ unit tests
    └── bin/          # compiled test binaries
```

## Test Coverage

| Test | Description |
|------|-------------|
| `test_alignment` | Verifies 16-byte alignment |
| `test_coalescing` | Block merging (left/right/both) |
| `test_double_free` | Double-free detection |
| `test_realloc` | In-place grow/shrink |
| `test_mmap` | Large allocation handling |
| `test_benchmark` | Performance comparison |
| ... | 15+ additional edge case tests |