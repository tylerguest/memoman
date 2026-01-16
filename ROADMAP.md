# memoman Roadmap (Correctness)

## Current Findings

- Pool end alignment is not enforced in `mm_add_pool`, so `desc->end` can be misaligned even though `mm_validate_pool` requires aligned ends.
- Pool overlap is unchecked, which makes pool ownership ambiguous if ranges overlap.
- Release pointer validation can accept aligned interior pointers because `user_to_block(ptr)` is trusted without verifying the payload start.
- Header sanity checks lack overflow guards when computing `block + header + size`.
- `coalesce` trusts `prev`/`next` pointers without pool-bound checks in release builds.

## Plan to Fix

1. **Pool alignment & overlap**
   - Round pool end down to alignment in `mm_add_pool` (or reject when misaligned).
   - Reject new pools that overlap any active pool descriptor.
   - Add tests for non-aligned pool sizes and overlapping pools.

2. **Pointer validation hardening**
   - In non-`MM_DEBUG` builds, validate that `ptr` equals `block_to_user(block)` before accepting.
   - Add a test that crafts an aligned interior pointer with a plausible header and verifies `mm_free`/`mm_realloc` reject it.

3. **Overflow and bounds guards**
   - Guard `mm_block_header_sane` against size wraparound (`size > desc->bytes` and `block + header + size` overflow).
   - Add tests or debug assertions around oversized headers.

4. **Coalescing safety**
   - Validate `prev` and `next` are within the same pool before coalescing in release builds (cheap bounds checks).
   - Add a regression test that corrupts `PREV_FREE` in a controlled way and ensures allocator safely rejects/ignores.

## Parity Phase (Conte TLSF)

- **Handle semantics**: decide whether `pool_t` should become a pool-base handle like Conte’s TLSF or keep the internal descriptor.
- **Check API parity**: map `tlsf_check`/`tlsf_check_pool` return semantics and decide whether to add wrapper APIs for TLSF-style results.
- **Behavioral diffs**: document and test any remaining differences in memalign, pool removal, and validation flow.
- **Checklist**: build a function-by-function parity table for `memoman.h` vs `examples/matt_conte/tlsf.h`.

### Parity Checklist

| Conte TLSF | memoman | Status | Notes |
| --- | --- | --- | --- |
| `tlsf_create(void* mem)` | `mm_create(void* mem)` | ✅ | Control-only create matches signature. |
| `tlsf_create_with_pool(void* mem, size_t bytes)` | `mm_create_with_pool(void* mem, size_t bytes)` | ✅ | Same shape, in-place init. |
| `tlsf_destroy(tlsf_t)` | `mm_destroy(tlsf_t)` | ✅ | No-op in both. |
| `tlsf_get_pool(tlsf_t)` | `mm_get_pool(tlsf_t)` | ⚠️ | Returns descriptor handle, not pool base. |
| `tlsf_add_pool(tlsf_t, void*, size_t)` | `mm_add_pool(tlsf_t, void*, size_t)` | ⚠️ | Accepts unaligned mem; trims start/end. |
| `tlsf_remove_pool(tlsf_t, pool_t)` | `mm_remove_pool(tlsf_t, pool_t)` | ⚠️ | Requires descriptor handle; behavior differs from pool-base handle. |
| `tlsf_malloc(tlsf_t, size_t)` | `mm_malloc(tlsf_t, size_t)` | ✅ | Same signature, TLSF-aligned size rounding. |
| `tlsf_memalign(tlsf_t, size_t, size_t)` | `mm_memalign(tlsf_t, size_t, size_t)` | ✅ | Conte-style gap handling implemented. |
| `tlsf_realloc(tlsf_t, void*, size_t)` | `mm_realloc(tlsf_t, void*, size_t)` | ✅ | Same signature; additional pointer safety checks. |
| `tlsf_free(tlsf_t, void*)` | `mm_free(tlsf_t, void*)` | ✅ | Same signature; additional pointer safety checks. |
| `tlsf_block_size(void*)` | `mm_block_size(void*)` | ✅ | Same behavior: returns internal block size. |
| `tlsf_size(void)` | `mm_size(void)` | ✅ | Same overhead helper. |
| `tlsf_align_size(void)` | `mm_align_size(void)` | ✅ | Same helper. |
| `tlsf_block_size_min(void)` | `mm_block_size_min(void)` | ✅ | Same helper. |
| `tlsf_block_size_max(void)` | `mm_block_size_max(void)` | ✅ | Same helper. |
| `tlsf_pool_overhead(void)` | `mm_pool_overhead(void)` | ✅ | Same helper. |
| `tlsf_alloc_overhead(void)` | `mm_alloc_overhead(void)` | ✅ | Same helper. |
| `tlsf_walk_pool(pool_t, ...)` | `mm_walk_pool(pool_t, ...)` | ✅ | Pool handles now base addresses. |
| `tlsf_check(tlsf_t)` | `mm_check(tlsf_t)` | ✅ | Wrapper returns nonzero on failure. |
| `tlsf_check_pool(pool_t)` | `mm_check_pool(pool_t)` | ✅ | Wrapper returns nonzero on failure. |
| `tlsf_check(tlsf_t)` | `mm_validate(tlsf_t)` | ⚠️ | Return semantics inverted; keep for memoman style. |
| `tlsf_check_pool(pool_t)` | `mm_validate_pool(pool_t)` | ⚠️ | Return semantics inverted; keep for memoman style. |

## Suggested Order

1. Pool alignment + overlap checks + tests
2. Pointer validation hardening + tests
3. Overflow guards + coalescing bounds checks + tests
4. Parity phase checklist and decisions
