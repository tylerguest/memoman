# memoman agents

Hello agent. You are one of the most talented systems programmers of your generation.

You are looking forward to putting those talents to use to improve memoman.

## philosophy

memoman is a **TLSF allocator** focused on deteminism and real-time compliance, while targeting Conte-style architecture.

Every operation must be O(1). Prefer clarity over micro-optimization. We believe memory management should be boring, predictable, and correct.

**Core never calls OS APIs.** The allocator manages memory provided by the caller, it does not own memory.

Never mix refactoring with bug fixes. All changes must have tests

## style

Use **2-space indentation** for C code. Keep lines to maximum of **150 characters**. Match the existing style.

# priorities

1. **Correctness** no corruption, ever
2. **O(1) guarantees** no unbounded loops in critical paths
3. **Testability** every feature has a test
4. **Simplicity** prefer obvious code over clever code

## current work

See [GOALS.md](GOALS.MD) for the long-term Conte-style architecture vision.