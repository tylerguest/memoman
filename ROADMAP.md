# memoman Roadmap (Condensed)

## Immediate Correctness Tasks

- **Pool alignment & overlap**: enforce aligned pool ends, reject overlaps, add tests. ✅
- **Pointer validation**: verify `ptr == block_to_user(block)` in release, add interior-pointer tests. ✅
- **Overflow/bounds guards**: protect `block + header + size` and size wraparound, add regression tests. ✅
- **Coalescing safety**: verify `prev`/`next` are in the same pool before merging, add corruption tests. ✅

## Conte TLSF Parity Tasks

- **Memalign gap parity**: align `gap_minimum` and rounding with Conte (or document divergence) and add targeted tests. ✅
- **Pool layout parity**: decide if first block should be offset (`offset_to_block`) or keep current layout; test both semantics if needed. ✅
- **Pool alignment policy**: decide if `mm_add_pool` should reject misaligned input like Conte or keep align-up behavior. ✅ (reject misaligned)
- **Function parity checklist**: keep a one-page function equivalence table and update status as parity shifts.
- **Check API semantics**: decide whether to add TLSF-style wrappers for `mm_validate` return semantics. ✅ (already via `mm_check`)

## Implementation Order

1. Pool alignment + overlap checks + tests
2. Pointer validation hardening + tests
3. Overflow guards + coalescing bounds checks + tests
4. Memalign parity decision + tests
5. Pool layout/alignment policy decisions + tests
6. Update parity table + docs
