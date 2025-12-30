# Memoman Roadmap: Conte-Grade TLSF (Production-Ready)

This document tracks the remaining work to make memoman a production-grade TLSF allocator comparable to Matthew Conte’s
TLSF 3.1: same core invariants, equivalent API surface, strong validation tooling, and clean portability.

## What “Equivalent to Conte TLSF 3.1” Means

We’re “equivalent” when these are true:

- **Semantics parity**: For the same pool(s) and the same deterministic sequence of alloc/free/realloc/memalign operations,
  memoman produces valid results with the same rules as Conte TLSF 3.1 (alignment, usable size behavior,
  splitting/coalescing rules, and failure modes).
- **Invariant parity**: All TLSF invariants hold at all times (bitmaps, free lists, prev-phys linkage, flags, boundaries, sentinels).
- **Complexity parity**: Critical-path operations remain O(1) with only bounded loops (bounded by FL/SL counts), never proportional to heap size.

“Surpass Conte” comes only after parity, and should be **additive** (stronger validation, better determinism tooling,
nicer integration), never at the expense of correctness or O(1) guarantees.

## Phase 0: Lock In Conte’s Core Invariants (Non-Negotiable)

Before adding features, make sure your layout + invariants match the reference model.

- [x] **Block layout matches TLSF 3.1 semantics**
  - **Goal**: Keep “user data starts right after size” semantics (Conte’s block_start_offset).
  - **Requirement**: Ensure your “free-list pointers live in user payload when block is free” behavior is always valid.
  - **Deliverable**: Document the exact header/payload layout with an ASCII diagram in memoman.c (public header stays minimal).

- [x] **Prev-physical linkage is correct and updated everywhere**
  - **Goal**: Your equivalent of prev_phys_block must be correct after: split, merge, add_pool, realloc growth, and free.
  - **Requirement**: Every time a block boundary changes, update the next block’s “prev pointer” and “prev_free bit”.

- [x] **Minimum block size rules are derived, not guessed**
  - **Goal**: Replace “magic constants” with derived constraints like Conte:
    - minimum block size required to store free-list pointers
    - header overhead vs. usable payload accounting
  - **Deliverable**: static_asserts for:
    - TLSF_MIN_BLOCK_SIZE >= (2 * sizeof(void*)) (or your exact requirement)
    - alignment correctness
    - FL/SL bounds

- [x] **Mapping + bitmap operations are reference-faithful**
  - **Goal**: mapping(size)->(fl,sl) and “find suitable block” behavior must match TLSF expectations.
  - **Requirement**: Bit-scan wrappers must be safe for 32-bit/64-bit and for edge cases.
  - **Deliverable**: A deterministic test that compares your (fl,sl) results to Conte’s for a sweep of sizes (differential test).

- [ ] **Differential parity harness (Conte vs memoman)**
  - **Goal**: Make regressions obvious by running the same operation stream against both allocators.
  - **Requirement**: Fixed seed, deterministic operation generator, and “shrinkable” failures (log the minimal reproducer).
  - **Note**: Do not vendor Conte TLSF sources in this repo; keep parity harness as a local/dev-only tool that builds only if external TLSF sources are present.

- [ ] **Strict parity mode passes**
  - **Goal**: Make strict behavior parity mode pass for representative workloads (then flip it on by default).

## Phase 1: API Parity (The “Conte Standard” Surface)

Get to a point where someone can swap your allocator in where they’d use TLSF 3.1.

### API Equivalence Map (Conte → memoman)

- `tlsf_create` → `mm_create` (in-place init of allocator control struct)
- `tlsf_create_with_pool` → `mm_create` (current model: control + initial pool in one buffer)
- `tlsf_destroy` → `mm_destroy` (likely a no-op; caller owns memory)
- `tlsf_get_pool` → `mm_get_pool` (define “primary pool” semantics)
- `tlsf_add_pool` → `mm_add_pool` (should return a `mm_pool_t` handle like TLSF)
- `tlsf_remove_pool` → `mm_remove_pool`
- `tlsf_malloc` / `tlsf_free` → `mm_malloc` / `mm_free`
- `tlsf_realloc` → `mm_realloc`
- `tlsf_memalign` → `mm_memalign`
- `tlsf_block_size` → `mm_block_size` (usable payload, not original request size)
- `tlsf_walk_pool` → `mm_walk_pool`
- `tlsf_check` / `tlsf_check_pool` → `mm_validate` / `mm_validate_pool`
- `tlsf_size`, `tlsf_align_size`, `tlsf_block_size_min/max`, `tlsf_pool_overhead`, `tlsf_alloc_overhead`
  → `mm_size`, `mm_align_size`, `mm_block_size_min/max`, `mm_pool_overhead`, `mm_alloc_overhead`

### Header Parity Checklist (tlsf.h → memoman.h)

Target: memoman exposes a TLSF-like surface (namespaced as `mm_*`). Any non-TLSF convenience APIs should be clearly labeled as memoman extensions.

- [x] **Lifecycle parity (`tlsf_create*`, `tlsf_destroy`)**
  - **Goal**: Make lifecycle semantics as boring and explicit as TLSF.
  - **Deliverables**:
    - `mm_destroy(mm_allocator_t*)` (no-op allowed, but defined)
    - document ownership + alignment rules exactly like TLSF
    - optional symmetry API: `mm_create_with_pool(...)`

- [x] **Pool handle parity (`pool_t`, `tlsf_get_pool`, `tlsf_add/remove_pool`)**
  - **Goal**: Support TLSF-style pool handles so tooling can walk/check/remove specific pools.
  - **Deliverables**:
    - `typedef void* mm_pool_t;` (or an opaque handle)
    - `mm_get_pool(mm_allocator_t*) -> mm_pool_t`
    - `mm_add_pool(mm_allocator_t*, void*, size_t) -> mm_pool_t` (optional boolean wrapper for convenience)
    - `mm_remove_pool(mm_allocator_t*, mm_pool_t)` with Conte-like safety rules (no live allocations in that pool)

- [x] **Overheads/limits parity (`tlsf_size`, `tlsf_*_overhead`, `tlsf_block_size_min/max`)**
  - **Goal**: Let integrators size buffers without reading the source.
  - **Deliverables**: `mm_size`, `mm_align_size`, `mm_pool_overhead`, `mm_alloc_overhead`, `mm_block_size_min`, `mm_block_size_max`

- [x] **Debug tooling parity (`tlsf_walk_pool`, `tlsf_check*`)**
  - **Goal**: Match TLSF’s “walk + check” capabilities (what makes TLSF easy to integrate safely).
  - **Deliverables**:
    - `typedef void (*mm_walker)(void* ptr, size_t size, int used, void* user);`
    - `mm_walk_pool(mm_pool_t, mm_walker, void*)`
    - `mm_validate_pool(mm_allocator_t*, mm_pool_t)` and `mm_validate(mm_allocator_t*)`

- [x] **Aligned Allocation API (`mm_memalign`)**
  - **Goal**: Support arbitrary alignment requests (4KB pages, cache lines, SIMD).
  - **Requirement**: Correctly handle padding, gap filling, and split behavior without breaking coalescing invariants.
  - **Add**: A torture test that allocates various alignments and frees in random order.

- [x] **Discontiguous Pools (`mm_add_pool`)**
  - **Goal**: Multiple non-adjacent regions managed by one allocator instance.
  - **Requirement**: Pool boundaries must be robust (prologue/epilogue/sentinels) and never allow cross-pool coalescing.

- [x] **Pool tracking (required for TLSF-equivalent tooling)**
  - **Goal**: Enable `mm_get_pool`, `mm_walk_pool`, `mm_validate_pool`, and `mm_remove_pool` without ever walking across pools.
  - **Requirement**: Track pools in allocator metadata (bounded list/array) or via in-pool headers.
  - **Deliverables**:
    - O(1) pool membership check for a pointer (debug mode)
    - per-pool physical walk for tooling (never used in hot path)

- [x] **Remove pool (`mm_remove_pool`)**
  - **Goal**: Match TLSF’s `tlsf_remove_pool` capability.
  - **Requirement**: Only allow removal when all blocks in that pool are free (or provide a strict contract).
  - **Deliverable**: Tests for “remove after full free” and “reject removal if allocated blocks exist”.

- [x] **Allocator lifecycle polish**
  - **Goal**: Clear create/init story like TLSF: create, add pool, allocate, free, destroy/reset.
  - **Deliverables**:
    - `mm_init_in_place(mem, bytes)` docs and constraints
    - `mm_reset()` (optional but very useful)
    - clear error behavior (NULL returns, validation failure modes)

## Phase 2: Correctness & Debuggability (Conte’s Big Differentiator)

This is what makes TLSF implementations usable by other people.

- [x] **Full internal validation (real `mm_validate`)**
  - **Goal**: Equivalent of Conte’s internal checks: bitmap consistency, free-list correctness, block boundary invariants.
  - **Requirements**:
    - Verify free list pointers are coherent (next/prev links)
    - Verify bitmaps match list emptiness
    - Verify prev_free flags match the physical neighbor state
    - Verify prev-physical pointers are correct
    - Verify per-pool prologue/epilogue invariants (no cross-pool walks)
  - **Deliverable**: `MM_DEBUG` build flag that turns validation on aggressively.

- [ ] **Pointer safety policy (debug mode)**
  - **Goal**: Make it obvious when the user passes garbage pointers.
  - **Options**:
    - cookies/magic checks
    - bounds checks against known pools
    - double-free detection behavior (warn vs abort)
  - **Deliverable**: Documented behavior for invalid frees/reallocs.

- [ ] **Deterministic stress tests**
  - **Goal**: Prove no corruption under randomized allocate/free/realloc patterns.
  - **Deliverables**:
    - random test runner with fixed seed support
    - comparison mode against Conte TLSF 3.1 for behavior parity (preferred)

## Phase 3: Portability & Configuration (32/64-bit, alignment, knobs)

Keep the public API small, but make the implementation portable and configurable like TLSF.

- [ ] **Portability baseline**
  - **Goal**: Support 32-bit and 64-bit cleanly (LP64 + LLP64 assumptions avoided).
  - **Requirements**:
    - `uintptr_t` for pointer math everywhere
    - bit scan wrappers that work with your chosen bitmap widths
    - no hidden dependence on `sizeof(long)` vs `sizeof(size_t)`

- [ ] **Configurable alignment**
  - **Goal**: Allow default alignment = 16 (SIMD-friendly) or configured at compile time.
  - **Deliverable**: `MM_ALIGNMENT` compile-time override, with asserts that it’s power-of-two.

- [ ] **Configurable FL/SL parameters**
  - **Goal**: Make SLI and FL range a compile-time configuration (within sane bounds).
  - **Deliverable**: Document the performance/memory tradeoffs in Known Limitations.

## Phase 4: Performance & Competitive Behavior

Once correctness is solid, tune toward “Conte-like” throughput and predictability.

- [ ] **Fast-path auditing**
  - **Goal**: Ensure the hot path is branch-light and inlined (search, remove, insert, split).
  - **Deliverables**:
    - microbench: alloc/free small sizes, mixed sizes, worst-case patterns
    - compare against TLSF 3.1 in identical harness

- [ ] **Reduce metadata overhead + cache misses**
  - **Goal**: Keep headers minimal; ensure free-list pointers are only “paid for” on free blocks.
  - **Deliverable**: Document exact per-block overhead in release vs debug.

## Phase 5: Fragmentation Profiling (Make claims you can back up)

- [ ] **Internal/external fragmentation measurement**
  - **Goal**: Produce real numbers for common allocation distributions.
  - **Deliverables**:
    - report from test harness (CSV is fine)
    - plots optional, but at least summary stats (p50/p95 worst-case)

## Phase 6: Documentation & Polish (Conte-style)

- [ ] **Header + internal comment pass (“Conte tone”)**
  - **Goal**: Your `memoman.h`/`memoman.c` should read like a small allocator library others can adopt.
  - **Deliverables**:
    - top-of-file overview comment: algorithm + invariants + constraints
    - per-function contracts: inputs, outputs, failure modes
    - “How to integrate” section + minimal example

- [ ] **Known limitations**
  - **Goal**: Explicitly list tradeoffs and constraints.
  - **Include**:
    - minimum block size (and why)
    - alignment defaults/config
    - pool removal support (if not implemented)
    - which public APIs are TLSF-equivalent vs memoman extensions (if any are added later)

## Phase 7: Surpass Conte (Only After Parity)

Once parity is proven by differential testing, memoman can go beyond TLSF 3.1 without compromising O(1):

- [ ] **Better determinism tooling**
  - **Goal**: Make worst-case behavior auditable and repeatable.
  - **Deliverables**: optional trace hooks, operation counters, and per-op timing hooks (no OS calls in core).

- [ ] **Stronger debug diagnostics**
  - **Goal**: Catch corruption earlier than “something crashed”.
  - **Deliverables**: optional red-zones, poison patterns, and stricter invalid-pointer handling in debug builds.

- [ ] **Conte-grade documentation + examples**
  - **Goal**: Make integration trivially obvious (one page).
  - **Deliverables**: minimal example, FAQ (alignment, pools, min block), and “gotchas” section.
