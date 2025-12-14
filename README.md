<img src="memoman.png" alt="Memoman" width=50% />

a tiny memory allocator

## Features

- **Header-based block metadata (`block_header_t`)**
- **Segregated free lists for fast allocation**
- **Coalescing of adjacent free blocks**
- **Unit tests and simple benchmarks**

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

## Benchmark (Example)

```
Your allocator:   0.211 s avg  (~47.38M ops/sec)
System malloc:    1.029 s avg  (~9.72M ops/sec)
Relative speed:   ~4.9× faster in this test

(Results vary by hardware and compiler.)
```

## API

```c
#include "memoman.h"

void* mm_malloc(size_t size);
void mm_free(void* ptr);
void reset_allocator(void);
void print_heap_stats(void);
void print_free_list(void);
size_t get_total_allocated(void);
size_t get_free_space(void);
```

## Design Overview

- Each block stores a header before user data
- Segregated free lists for small/medium blocks
- First-fit fallback for larger blocks
- Freeing triggers coalescing with neighbors

## Structure

```
.
├── Makefile
├── src/
│   ├── memoman.c
│   └── memoman.h
└── tests/
	├── *.c
	└── bin/
```

