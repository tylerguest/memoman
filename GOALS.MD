# memoman Development Goals

Production-ready TLSF allocator, Conte-style architecture.

---

## Phase 0: Lock the Target

**Goal:** Production-ready TLSF library, Conte-style.

### Core Principles

- **Goal:** Library-style architecture — allocator does not own memory, it manages memory provided by the caller
- **Non-goal:** Automatic heap growth — core allocator will not call `mmap`/`sbrk`/`VirtualAlloc` or expand itself
- **Complexity:** O(1) alloc/free/search (bounded by fixed FL/SL sizes)

### Exit Criteria

- [x] You can describe, in 5 bullet points, what core does vs what wrapper/system layer does

---

## Phase 1: Architecture

**Goal:** Handle-based core + optional global wrapper.

### 1.1 Struct Isolation

**Conte step:** Struct Isolation

Introduce `mm_allocator_t` holding:
- bitmaps
- free list matrix
- config (fl/sl counts, alignment, etc.)

Remove hidden globals from core logic.

### 1.2 Global Allocator Wrapper

- `mm_default()` returns a singleton allocator instance
- `mm_malloc_global`/`mm_free_global` call through the handle

### Exit Criteria

- [x] You can have two independent allocators in the same process, each managing different pools

---

## Phase 2: Core TLSF Correctness

**Goal:** Adopt Conte's block semantics, header compression, and boundary handling.

> **Note:** This is where people mess up.

### 2.1 Header Compression + Ghost Prev Pointer

**Conte step:** Header Compression

#### The Trap (Fixed)

Do **not** implement a literal always-valid `prev_phys` pointer as a normal field if it keeps a fat header.

**Conte model:** The `prev_phys` storage overlaps with previous block's payload, and is only valid when the previous block is free.

#### New Invariant

If `block_is_prev_free(block)` is false, then `block->prev_phys` storage contains user data and must not be read.

#### Implementation Intent

- Put the prev pointer storage at the beginning of the struct so it can overlap
- Ensure flags encode:
  - `used`/`free`
  - `prev_free`
- Size field carries flags (masking)

#### Exit Criteria

- [x] Header size matches Conte-class overhead (you can still keep debug-only extras behind `#ifdef MM_DEBUG`)
- [x] You never read `prev_phys` unless prev_free flag is set

### 2.2 Sentinels

**Conte step:** Sentinels

> You were right: you can't claim "bounded fragmentation + clean edge handling" without them.

#### Tasks

Add prologue/epilogue (or equivalent) sentinel blocks so:
- coalescing doesn't need "if first/last block" special cases
- pool boundaries are clean

#### Exit Criteria

- [x] No boundary-condition branches in coalescing except sentinel checks
- [x] Alloc/free works for smallest and largest blocks near pool edges

### 2.3 Invariants + Debug Assertions

Enforce:
- Bitmap/list consistency
- No adjacent free blocks left uncoalesced
- Free list links are sane (no self-cycles)
- Alignment and minimum block size

#### Exit Criteria

- [x] `MM_DEBUG` build catches intentional corruption quickly

---

## Phase 3: Validation & Test Harness

**Goal:** Make it adoptable by strangers.

### 3.1 `mm_validate(a)` — Deep Checks

- list cycles
- bitmap matches lists
- physical adjacency correctness
- "ghost prev" invariant respected (never read invalid prev)

### 3.2 Stress Tests

- randomized alloc/free/realloc
- fragmentation patterns
- "worst-case" small allocations

### Exit Criteria

- [ ] Long randomized runs pass under ASAN/UBSAN in debug builds

---

## Phase 4: Multi-Pool + External Memory

**Goal:** Make the allocator passive like Conte: caller supplies memory.

> **Second big trap**

### 4.1 External Memory

**Conte step:** External Memory

#### Architectural Pivot

Core allocator never calls `mmap`, `mprotect`, etc.

Core exposes:
```c
mm_add_pool(mm_allocator_t*, void* mem, size_t bytes);
mm_remove_pool(mm_allocator_t*, void* mem);  // optional, later