<img src="memoman.png" alt="Memoman" width=100% />

# memoman

`memoman` is a pool-based TLSF allocator targeting **TLSF 3.1 semantics** (Matthew Conte’s TLSF 3.1).

Core principles:
- Core **never calls OS allocation APIs**. The allocator manages memory you provide.
- Operations are **O(1)** (bounded by FL/SL bitmap sizes, not heap size).
- Block layout matches TLSF 3.1: user payload starts immediately after the size word; free-list pointers live in the payload when free.

## Features

- TLSF-style two-level bitmaps and segregated free lists (O(1) search/insert/remove).
- Discontiguous pools (`mm_add_pool`) in a single allocator instance.
- Immediate coalescing and correct prev-physical linkage (TLSF 3.1 semantics).
- `mm_memalign` with Conte-style gap handling.
- Internal validation (`mm_validate`).

## Quick Build & Test

```bash
make run          # build + run all tests
make benchmark    # optimized build (for benchmark suite)
./tests/bin/benchmark_suite
make demo
./demo
```

### Minimal Example

```c
#include "memoman.h"
#include <stdint.h>

int main(void) {
  static uint8_t arena[1024 * 1024] __attribute__((aligned(16)));

  tlsf_t mm = mm_create(arena, sizeof(arena));
  if (!mm) return 1;

  void* p = mm_malloc(mm, 128);
  if (!p) return 1;

  p = mm_realloc(mm, p, 256);
  mm_free(mm, p);

  if (!mm_validate(mm)) return 1;
  mm_destroy(mm); /* no-op by design */
  return 0;
}
```

### Multiple Pools

```c
uint8_t pool1[64 * 1024] __attribute__((aligned(16)));
uint8_t pool2[64 * 1024] __attribute__((aligned(16)));

tlsf_t mm = mm_create(pool1, sizeof(pool1));
pool_t pool = mm_add_pool(mm, pool2, sizeof(pool2));
```

## API

```c
#include "memoman.h"

/* Create/destroy. All memory is caller-owned. */
tlsf_t mm_create(void* mem, size_t bytes);
tlsf_t mm_create_with_pool(void* mem, size_t bytes);
void mm_destroy(tlsf_t alloc);

/* Pools. */
pool_t mm_get_pool(tlsf_t alloc);
pool_t mm_add_pool(tlsf_t alloc, void* mem, size_t bytes);
void mm_remove_pool(tlsf_t alloc, pool_t pool);

/* malloc/memalign/realloc/free replacements. */
void* mm_malloc(tlsf_t alloc, size_t size);
void* mm_memalign(tlsf_t alloc, size_t alignment, size_t size);
void* mm_realloc(tlsf_t alloc, void* ptr, size_t size);
void  mm_free(tlsf_t alloc, void* ptr);

/* Returns internal block size, not original request size. */
size_t mm_block_size(void* ptr);

/* Debugging. Returns nonzero if internal consistency checks pass. */
int mm_validate(tlsf_t alloc);
```

## Structure

```
.
├── Makefile
├── README.md
├── src/
│   ├── memoman.c
│   └── memoman.h
└── tests/
    ├── test_*.c              # unit tests
    ├── memoman_test_internal.h
    └── bin/                  # compiled test binaries
```
