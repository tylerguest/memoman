<img src="memoman.png" alt="Memoman" width=100% />

# memoman

`memoman` is a deterministic, pool-based allocator inspired by TLSF (Matthew Conte’s TLSF 3.1).
It is designed for real-time and embedded workloads where predictability matters more than peak throughput.

## Design Goals

- **O(1)** malloc/free/memalign/realloc on the hot path (bounded by FL/SL bitmap sizes, not heap size).
- **Caller-owned memory**: the allocator never allocates or frees OS memory.
- **TLSF-inspired block layout**: user payload starts immediately after the size word; free-list pointers live in the
  payload when a block is free.
- **Immediate coalescing** with correct `prev_phys` linkage.

## Features

- TLSF-style two-level bitmaps and segregated free lists.
- Discontiguous pools (`mm_add_pool`) in a single allocator instance.
- `mm_memalign` implements Conte-style gap handling.
- Internal validation (`mm_validate`, `mm_validate_pool`).
- Debug helpers: `mm_walk_pool`, `mm_block_size`, `mm_get_pool_for_ptr`.
- Overhead/limits helpers: `mm_size`, `mm_align_size`, `mm_block_size_min`, `mm_block_size_max`, `mm_pool_overhead`,
  `mm_alloc_overhead`.

## Constraints and Invariants

- `mm_create()` and `mm_create_with_pool()` require the control buffer aligned to `sizeof(size_t)`.
- `mm_add_pool()` requires `mem` and `bytes` aligned to `sizeof(size_t)`; misaligned pools are rejected.
- Pools must be large enough for allocator overhead and at least one minimum block. Use
  `mm_pool_overhead()` and `mm_block_size_min()` to size pools.
- Maximum pools per allocator: **32** (`MM_MAX_POOLS`).
- `mm_destroy()` is a no-op by design; the caller owns all memory.

## Quick Build & Test

```bash
make run                    # build + run all tests (summary output)
make run DEBUG=1            # stream full per-test output
make run TIMING=1           # show per-test timing
make run DEBUG=1 TIMING=1   # full output + timing
make benchmark              # optimized build (for benchmark suite)
make demo                   # build demo binary
./demo
make extras                 # build extras (latency histogram demo)
./bin/latency_histogram
```

Additional soak/benchmark targets are available (see `Makefile`). Some compare targets require a local checkout of
Conte’s TLSF under `examples/matt_conte`.

## Minimal Example

```c
#include "memoman.h"
#include <stdint.h>

int main(void) {
  static uint8_t arena[1024 * 1024] __attribute__((aligned(16)));

  tlsf_t mm = mm_create_with_pool(arena, sizeof(arena));
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

## Multiple Pools

```c
uint8_t pool1[64 * 1024] __attribute__((aligned(16)));
uint8_t pool2[64 * 1024] __attribute__((aligned(16)));

tlsf_t mm = mm_create_with_pool(pool1, sizeof(pool1));
pool_t pool = mm_add_pool(mm, pool2, sizeof(pool2));
```

## API

```c
#include "memoman.h"

/* Create/destroy. All memory is caller-owned. */
tlsf_t mm_create(void* mem);                         /* control-only (TLSF-style) */
tlsf_t mm_create_with_pool(void* mem, size_t bytes); /* convenience: create + add initial pool */
void mm_destroy(tlsf_t alloc);

/* Pools. */
pool_t mm_get_pool(tlsf_t alloc);
pool_t mm_add_pool(tlsf_t alloc, void* mem, size_t bytes);
void mm_remove_pool(tlsf_t alloc, pool_t pool);

/* malloc/memalign/realloc/free replacements. */
void* mm_malloc(tlsf_t alloc, size_t bytes);
void* mm_memalign(tlsf_t alloc, size_t align, size_t bytes);
void* mm_realloc(tlsf_t alloc, void* ptr, size_t size);
void  mm_free(tlsf_t alloc, void* ptr);

/* Returns internal block size, not original request size. */
size_t mm_block_size(void* ptr);

/* Overheads/limits of internal structures. */
size_t mm_size(void);
size_t mm_align_size(void);
size_t mm_block_size_min(void);
size_t mm_block_size_max(void);
size_t mm_pool_overhead(void);
size_t mm_alloc_overhead(void);

/* Debugging. */
typedef void (*mm_walker)(void* ptr, size_t size, int used, void* user);
void mm_walk_pool(pool_t pool, mm_walker walker, void* user);
int mm_validate(tlsf_t alloc);
int mm_validate_pool(pool_t pool);
int mm_check(tlsf_t alloc);      /* TLSF-style: returns nonzero on failure */
int mm_check_pool(pool_t pool);  /* TLSF-style: returns nonzero on failure */

/* Memoman extensions (TLSF does not define these). */
tlsf_t mm_init_in_place(void* mem, size_t bytes);
pool_t mm_get_pool_for_ptr(tlsf_t alloc, const void* ptr);
int mm_reset(tlsf_t alloc);
```

## Debug Builds

- `make debug` enables `MM_DEBUG`, which adds integrity checks and can assert on invalid frees/reallocs.
- Behavior is controlled with:
  - `MM_DEBUG_VALIDATE_SHIFT` (default 10): validate every 2^N operations.
  - `MM_DEBUG_ABORT_ON_INVALID_POINTER` (default 1).
  - `MM_DEBUG_ABORT_ON_DOUBLE_FREE` (default 0).

## Repository Layout

```
.
├── Makefile
├── README.md
├── demo.c
├── src/
│   ├── memoman.c
│   └── memoman.h
└── tests/
    ├── test_*.c              # unit tests
    ├── memoman_test_internal.h
    └── bin/                  # compiled test binaries
```
