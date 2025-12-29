# Memoman Roadmap: Conte-Grade TLSF (Production-Ready)

This document tracks the remaining work to make memoman a production-grade TLSF allocator comparable to Matthew Conte’s TLSF 3.1: same core invariants, equivalent API surface, strong validation tooling, and clean portability.

## Phase 0: Lock In Conte’s Core Invariants (Non-Negotiable)

Before adding features, make sure your layout + invariants match the reference model.

- [x] **Block layout matches TLSF 3.1 semantics**
  - **Goal**: Keep “user data starts right after size” semantics (Conte’s block_start_offset).
  - **Requirement**: Ensure your “free-list pointers live in user payload when block is free” behavior is always valid.
  - **Deliverable**: Document the exact header/payload layout with an ASCII diagram in memoman.h.

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

- [ ] **Mapping + bitmap operations are reference-faithful**
  - **Goal**: mapping(size)->(fl,sl) and “find suitable block” behavior must match TLSF expectations.
  - **Requirement**: Bit-scan wrappers must be safe for 32-bit/64-bit and for edge cases.
  - **Deliverable**: A small deterministic test that compares your (fl,sl) results to Conte’s for a sweep of sizes.

## Phase 1: API Parity (The “Conte Standard” Surface)

Get to a point where someone can swap your allocator in where they’d use TLSF 3.1.

- [x] **Aligned Allocation API (`mm_memalign_inst`)**
  - **Goal**: Support arbitrary alignment requests (4KB pages, cache lines, SIMD).
  - **Requirement**: Correctly handle padding, gap filling, and split behavior without breaking coalescing invariants.
  - **Add**: A torture test that allocates various alignments and frees in random order.

- [x] **Discontiguous Pools (`mm_add_pool`)**
  - **Goal**: Multiple non-adjacent regions managed by one allocator instance.
  - **Requirement**: Pool boundaries must be robust (prologue/epilogue/sentinels) and never allow cross-pool coalescing.

- [ ] **Pool tracking + pool iteration utilities (Conte-style tooling)**
  - **Goal**: Restore “physical walk” capability safely even with multiple pools.
  - **Requirement**: Track pools in allocator metadata (a small list/array or intrusive headers in pool memory).
  - **Deliverables**:
    - `mm_walk_pool(pool, cb)` or `mm_walk_all_pools(cb)`
    - optional `mm_remove_pool(...)` (see Phase 2)

- [ ] **Optional: Remove pool (`mm_remove_pool`)**
  - **Goal**: Feature parity with more complete TLSF usage scenarios.
  - **Requirement**: Only allow removal when all blocks in that pool are free (or provide a strict contract).
  - **Deliverable**: Tests for “remove after full free” and “reject removal if allocated blocks exist”.

- [ ] **Allocator lifecycle polish**
  - **Goal**: Clear create/init story like TLSF: create, add pool, allocate, free, destroy/reset.
  - **Deliverables**:
    - `mm_init_in_place(mem, bytes)` docs and constraints
    - `mm_reset()` (optional but very useful)
    - clear error behavior (NULL returns, validation failure modes)

## Phase 2: Correctness & Debuggability (Conte’s Big Differentiator)

This is what makes TLSF implementations usable by other people.

- [ ] **Full internal validation (real `mm_validate_inst`)**
  - **Goal**: Equivalent of Conte’s internal checks: bitmap consistency, free-list correctness, block boundary invariants.
  - **Requirements**:
    - Verify free list pointers are coherent (next/prev links)
    - Verify bitmaps match list emptiness
    - Verify prev_free flags match the physical neighbor state
    - Verify prev-physical pointers are correct
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
    - comparison mode against malloc for basic invariants (not performance)

## Phase 3: Portability & Configuration (32/64-bit, alignment, knobs)

Your current headers still read like “64-bit only, 8-byte aligned forever”.

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
  - **Goal**: Your memoman.h/memoman.c should read like a small allocator library others can adopt.
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
