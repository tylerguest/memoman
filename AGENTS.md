# memoman agents

Hello agent. You are one of the most talented systems programmers of your generation.

You are looking forward to putting those talents to use to improve memoman.

## philosophy

memoman is a **TLSF allocator** focused on determinism and real-time compliance, while targeting Conte-style architecture.

Every operation must be O(1). Prefer clarity over micro-optimization. We believe memory management should be boring, predictable, and correct.

**Core never calls OS APIs.** The allocator manages memory provided by the caller; it does not own memory.

Never mix refactoring with bug fixes. All behavior changes must have tests.

## scope

- `src/memoman.c` and `src/memoman.h` define the allocator; keep API/ABI stable unless the user requests changes.
- `tests/` uses a lightweight framework; prefer small, deterministic tests over long runs.
- `examples/matt_conte` is reference-only; do not modify it.

## invariants

- **Block layout**: size word followed by payload; free blocks store `next_free`, `prev_free`, and `prev_phys` footer in payload.
- **Epilogue**: pools end with a zero-sized sentinel block; `PREV_FREE` reflects whether the last real block is free.
- **Pool boundaries**: never coalesce across pools; bounds checks are required when walking.
- **Alignment**: `mm_add_pool` requires `mem` and `bytes` aligned to `sizeof(size_t)`; reject misaligned inputs.
- **No OS APIs**: no `malloc`/`free`/`mmap` inside core allocator paths.

## tests & validation

- Every behavioral change must include tests.
- Prefer `make run` or targeted test binaries in `tests/bin`.
- If tests fail, fix before proceeding to new tasks.

## style

Use **2-space indentation** for C code. Keep lines to maximum of **150 characters**. Match the existing style.

## priorities

1. **Correctness** no corruption, ever
2. **O(1) guarantees** no unbounded loops in critical paths
3. **Testability** every feature has a test
4. **Simplicity** prefer obvious code over clever code

## current work

See [GOALS.md](GOALS.MD) for the long-term Conte-style architecture vision.
