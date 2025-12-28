# Memoman Roadmap: Path to Competitiveness

This document tracks the remaining work to make **memoman** a production-grade, competitive TLSF allocator comparable to reference implementations.

## Phase 1: Feature Parity (The "Conte" Standard)
These features are standard in serious TLSF implementations but currently missing in Memoman.

- [x] **Aligned Allocation API (`mm_memalign`)**
  - **Goal**: Support arbitrary alignment requests (e.g., 4KB for pages, 64B for cache lines).
  - **Requirement**: Handle padding, gap filling, and potential splitting of the gap.
  
- [ ] **Discontiguous Pools (`mm_add_pool`)**
  - **Goal**: Allow an allocator instance to manage multiple non-adjacent memory regions.
  - **Current State**: `mm_create` assumes one contiguous block.
  - **Requirement**: Refactor `mm_allocator_t` to decouple the control structure from the heap memory itself.

- [ ] **Portability & Architecture Support**
  - **Goal**: Support 32-bit systems automatically.
  - **Current State**: Hardcoded 8-byte alignment and 64-bit assumptions.
  - **Requirement**: Use `uintptr_t` math and detect word size for `fls`/`ffs`.

## Phase 2: Advanced Configuration & Optimization

- [ ] **Configurable Alignment**
  - **Goal**: Support 16-byte alignment by default (SIMD friendly) or via compile-time config.

- [ ] **Fragmentation Profiling**
  - **Goal**: Measure and document worst-case internal/external fragmentation.
  - **Deliverable**: A report or test suite outputting fragmentation metrics.

## Phase 3: Documentation & Polish

- [ ] **Known Limitations**
  - Goal: Explicitly list tradeoffs (e.g., 32-byte physical min block size).
