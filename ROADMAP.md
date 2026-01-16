# memoman Roadmap (Industry Standard)

## Core Reliability

- **Regression suite expansion**: add targeted tests for pool remove edge cases, memalign stress, and pointer misuse.
- **Soak hardening**: expand `test_soak` with deterministic memalign torture seeds and pool churn sequences.
- **Fuzz harness**: add a small, reproducible fuzz driver for `mm_malloc/mm_free/mm_realloc/mm_memalign`.
- **Invariant checks**: add optional invariant checks (compile‑time toggle) for `prev_phys`, free list loops, and bitmap consistency.

## Performance & Determinism

- **Latency profiling**: add microbench that reports p50/p99 for alloc/free under fragmentation.
- **Fragmentation metrics**: track external/internal fragmentation over time in soak runs.
- **O(1) audits**: document and confirm all hot paths are bounded by bitmap size.

## Portability & Build

- **32‑bit validation**: build/run tests under a 32‑bit target (or compiler flags) and document constraints.
- **Compiler matrix**: CI or local scripts for clang/gcc with `-O0/-O2/-O3` and `-DNDEBUG`.
- **Sanitizer pass**: optional builds for ASan/UBSan to catch undefined behavior.

## API & Usability

- **TLSF compatibility shim**: optional header that maps TLSF API names to `memoman` (zero‑cost wrapper).
- **Behavioral docs**: document alignment, pool layout semantics, and validation return semantics clearly.
- **Examples**: add a minimal “multi‑pool + memalign + realloc” example with failure handling.

## Ecosystem & Benchmarking

- **Cross‑allocator comparisons**: add scripts to compare `memoman` vs Conte TLSF vs system malloc.
- **Real‑time profiles**: measure jitter under the RT-ish soak harness with pinned CPU.

## Suggested Execution Order

1. Regression suite expansion + invariant checks
2. Soak hardening + fragmentation metrics
3. Portability validation (32‑bit + sanitizers)
4. TLSF compatibility shim + docs/examples
5. Microbench + RT jitter profiling
6. Cross‑allocator comparisons
