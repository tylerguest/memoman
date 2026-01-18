# memoman Roadmap

## Free Speed

- **Free speed pass**: make `mm_free` match Conte TLSF’s strategy while keeping memoman’s pool safety.
  - **Default fast path (Conte-style)**: mirror `tlsf_free` semantics by using `user_to_block` directly, marking free, merging prev/next, and inserting into the free list with no extra pointer validation on the hot path.
  - **Pool boundaries (Conte-like)**: keep pool-safe coalescing but make checks debug-only unless a boundary is needed.
    - in release, assume the pointer is valid and rely on block metadata (Conte-style)
    - in `MM_DEBUG`, use a per-pool range table sorted by start and binary-search for the pool once per free, then pass the pool descriptor into coalesce
  - **Coalesce structure**:
    - follow Conte’s sequence: mark free → merge prev → merge next → insert
    - use `PREV_FREE`/`prev_phys` as the sole required invariants in release
    - reuse a single `block_next_safe` result; avoid repeated calls
  - **Debug integrity preserved**: keep full pointer validation in `MM_DEBUG` and add tests to confirm
    - invalid pointer rejected
    - double free handled per config
  - **Performance target**: add a microbench that reports p50/p99 `mm_free` vs Conte TLSF and gate on parity.
