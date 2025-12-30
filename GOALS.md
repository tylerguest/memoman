# Memoman Roadmap: TLSF 3.1-Equivalent (Then Better)

This document tracks the remaining work to make memoman a production-grade TLSF allocator targeting **TLSF 3.1
semantics** (Matthew Conte’s TLSF 3.1): same core invariants, equivalent API surface, strong validation tooling, and
clean portability.

Repo policy:
- Core never calls OS allocation APIs (allocator manages caller-provided memory only).
- Hot-path operations must remain **O(1)** (bounded by FL/SL parameters, never proportional to heap size).
- Validation/walk tooling may be O(n) in block count (debug and tooling paths only).
- We do not vendor third-party TLSF sources into this repo; parity testing against other TLSF implementations is
  supported as an out-of-tree/dev-only harness.

## What “TLSF 3.1-Equivalent” Means

We’re “equivalent” when these are true:

- **Semantics parity**: For the same pool(s) and the same deterministic sequence of alloc/free/realloc/memalign operations,
  memoman produces valid results with the same rules as Conte TLSF 3.1 (alignment, usable size behavior,
  splitting/coalescing rules, and failure modes).
- **Invariant parity**: All TLSF invariants hold at all times (bitmaps, free lists, prev-phys linkage, flags, boundaries, sentinels).
- **Complexity parity**: Critical-path operations remain O(1) with only bounded loops (bounded by FL/SL counts), never proportional to heap size.

“Better than TLSF 3.1” comes only after parity, and should be **additive** (stronger validation, better determinism
tooling, nicer integration), never at the expense of correctness or O(1) guarantees.

## Phase 0: Prove Parity (Dev-Only, Out-of-Tree)

- [ ] **Differential parity harness (out-of-tree)**
  - **Goal**: Make regressions obvious by running the same operation stream against a known TLSF implementation.
  - **Requirements**:
    - fixed seed, deterministic operation generator
    - “shrinkable” failures (log the minimal reproducer)
    - build only if an external TLSF implementation is present locally

- [ ] **Strict parity mode passes**
  - **Goal**: Make strict behavior parity mode pass for representative workloads (then enable it by default in the dev harness).

## Phase 1: API Parity (The TLSF-Style Surface)

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

- [ ] **Return-code semantics parity**
  - **Goal**: Make it trivially obvious (from return values alone) whether checks passed, like TLSF’s `tlsf_check*`.
  - **Deliverables**:
    - decide and document whether `mm_validate*` returns `0` on success (TLSF-style) or nonzero on success (current style)
    - if keeping current style, consider adding `mm_check` / `mm_check_pool` wrappers with TLSF-style semantics

## Phase 2: Correctness & Debuggability (TLSF’s Big Differentiator)

This is what makes TLSF implementations usable by other people.

- [ ] **Sanitizer matrix (dev-only)**
  - **Goal**: Catch UB and memory errors early during development.
  - **Deliverables**: `make asan` / `make ubsan` targets for tests (core still does not call OS alloc APIs).

## Phase 3: Portability & Configuration (32/64-bit, alignment, knobs)

Keep the public API small, but make the implementation portable and configurable like TLSF.

- [ ] **Portability baseline**
  - **Goal**: Support 32-bit and 64-bit cleanly (LP64 + LLP64 assumptions avoided).
  - **Requirements**:
    - `uintptr_t` for pointer math everywhere
    - bit scan wrappers that work with your chosen bitmap widths
    - no hidden dependence on `sizeof(long)` vs `sizeof(size_t)`
  - **Deliverable**: A “small build matrix” script (or Make targets) that compiles tests in `-m32` when available.

- [ ] **Configurable alignment**
  - **Goal**: Allow default alignment = 16 (SIMD-friendly) or configured at compile time.
  - **Deliverable**: `MM_ALIGNMENT` compile-time override, with asserts that it’s power-of-two.

- [ ] **Configurable FL/SL parameters**
  - **Goal**: Make SLI and FL range a compile-time configuration (within sane bounds).
  - **Deliverable**: Document the performance/memory tradeoffs in Known Limitations.

- [ ] **Configurable pool limits**
  - **Goal**: Make max pool count a compile-time knob without breaking O(1).
  - **Deliverable**: `MM_MAX_POOLS` compile-time override (bounded array, no heap allocations).

## Phase 4: Performance & Competitive Behavior

Once correctness is solid, tune toward TLSF-like throughput and predictability.

- [ ] **Fast-path auditing**
  - **Goal**: Ensure the hot path is branch-light and inlined (search, remove, insert, split).
  - **Deliverables**:
    - microbench: alloc/free small sizes, mixed sizes, worst-case patterns
    - compare against a known TLSF implementation in an out-of-tree harness (no vendoring)

- [ ] **Reduce metadata overhead + cache misses**
  - **Goal**: Keep headers minimal; ensure free-list pointers are only “paid for” on free blocks.
  - **Deliverable**: Document exact per-block overhead in release vs debug.

## Phase 5: Fragmentation Profiling (Make claims you can back up)

- [ ] **Internal/external fragmentation measurement**
  - **Goal**: Produce real numbers for common allocation distributions.
  - **Deliverables**:
    - report from test harness (CSV is fine)
    - plots optional, but at least summary stats (p50/p95 worst-case)

## Phase 6: Documentation & Polish (TLSF-style)

- [ ] **Header + internal comment pass (“TLSF tone”)**
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
    - thread-safety model (non-thread-safe by default; locking is the caller’s responsibility)
    - pool removal safety rules (and what “no live allocations” means)
    - pointer safety policy: debug vs release behavior
    - which public APIs are TLSF-equivalent vs memoman extensions (if any are added later)

## Phase 7: Surpass TLSF 3.1 (Only After Parity)

Once parity is proven by differential testing, memoman can go beyond TLSF 3.1 without compromising O(1):

- [ ] **Better determinism tooling**
  - **Goal**: Make worst-case behavior auditable and repeatable.
  - **Deliverables**: optional trace hooks, operation counters, and per-op timing hooks (no OS calls in core).

- [ ] **Stronger debug diagnostics**
  - **Goal**: Catch corruption earlier than “something crashed”.
  - **Deliverables**: optional red-zones, poison patterns, and stricter invalid-pointer handling in debug builds.

- [ ] **Integration-grade documentation + examples**
  - **Goal**: Make integration trivially obvious (one page).
  - **Deliverables**: minimal example, FAQ (alignment, pools, min block), and “gotchas” section.
