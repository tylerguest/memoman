# memoman commands

This repo uses `make` targets plus a few environment variables (mostly for the soak harness).

## Build targets

- `make all`  
  Builds all unit tests into `tests/bin/` (debug-flavored flags by default).

- `make clean`  
  Removes `tests/bin/*`.

- `make run`  
  Builds and runs the standard unit test suite (excludes the heavy soak test).

- `make debug`  
  Builds tests with `MM_DEBUG` enabled (adds extra internal validation/guardrails).

- `make benchmark`  
  Builds tests with `-O3 -DNDEBUG` (optimized, for benchmarking).

## Demo

- `make demo`  
  Builds `./demo` from `demo.c` + `src/memoman.c`.

- `./demo`  
  Runs a small smoke demo (pools + memalign + realloc).

## Extras

- `make extras`
  Builds `./extras/bin/latency_histogram` (mixed-size alloc/free latency histogram).

- `./extras/bin/latency_histogram`
  Runs the latency histogram demo (frame loop, live updates every 250ms).
  Override sample count with `MM_HIST_SAMPLES` (0 = infinite) and report interval with `MM_HIST_REPORT_MS`.
  Frame tuning: `MM_HIST_FRAME_BYTES`, `MM_HIST_BURST_MIN`, `MM_HIST_BURST_MAX`, `MM_HIST_KEEP_MIN`, `MM_HIST_KEEP_MAX`.

- `sudo -E MM_HIST_RT=1 MM_HIST_RT_CPU=2 ./extras/bin/latency_histogram`
  Enables RT-ish mode (CPU pinning + SCHED_FIFO + mlockall). Optional: `MM_HIST_RT_PRIO` (default 80).


## Soak / stress testing

The soak harness lives in `tests/test_soak.c` and prints live stats.

### Core targets

- `make soak`  
  Runs the soak in “seed/steps” mode (fast), unless `MM_SOAK_SECONDS` is set.

- `make soak_debug`  
  Runs the soak under `MM_DEBUG`.

- `make soak_30`  
  Runs the soak for 30 seconds (time mode).

### “Real-time-ish” targets

These are best-effort. Some parts (like `SCHED_FIFO`) require privileges.

- `make soak_rt_30`  
  Time mode + CPU pinning + `mlockall` + prefault, reports every 250ms.

### Compare against system malloc

Runs the exact same workload against the system allocator (`malloc/free/realloc/posix_memalign`).

- `make soak_malloc_30`
- `make soak_malloc_rt_30`

### Compare against Matthew Conte TLSF (optional)

If you have a local checkout of Matthew Conte’s TLSF under `./examples/matt_conte` (gitignored), you can build an alternate soak binary that adds `MM_SOAK_BACKEND=conte`:

- `make soak_conte_30`
- `make soak_conte_rt_30`

### Soak environment variables

General:
- `MM_SOAK_BACKEND=memoman|malloc|conte|compare` (default: `memoman`)
  - `conte` requires `tests/bin/test_soak_conte`
  - `compare` runs two backends sequentially in one process:
    - with `test_soak_conte`: `memoman` vs `conte`
    - with `test_soak`: `memoman` vs `malloc`
- `MM_SOAK_SECONDS=<N>` enables time mode (e.g. `30`)
- `MM_SOAK_REPORT_MS=<N>` report interval in ms (default: `1000`)
- `MM_SOAK_STRICT=1` fail immediately on any allocation failure (otherwise failures are counted but tolerated)
- `MM_SOAK_VERBOSE=0` reduce non-essential prints

Seed/steps mode tuning (ignored when `MM_SOAK_SECONDS` is set):
- `MM_SOAK_SEED=<N>` starting seed
- `MM_SOAK_SEEDS=<N>` number of seeds to run
- `MM_SOAK_STEPS=<N>` operations per seed
- `MM_SOAK_SLOTS=<N>` number of live “slots” tracked by the generator
- `MM_SOAK_VALIDATE_SHIFT=<N>` validate every `2^N` ops
- `MM_SOAK_PROGRESS_EVERY=<N>` progress print frequency (in seeds)
- `MM_SOAK_STRESS=1` multiplies soak iterations for heavier runs
- `MM_SOAK_MEMALIGN_ITERS=<N>` overrides memalign torture iterations
- `MM_SOAK_MEMALIGN_CHURN_ITERS=<N>` overrides memalign churn iterations
- `MM_SOAK_MEMALIGN_CHURN_SLOTS=<N>` overrides memalign churn slots (max 1024)

RT-ish tuning:
- `MM_SOAK_RT=1` enables the RT-ish setup prints and attempts
- `MM_SOAK_CPU=<N>` pin to CPU `N` (default: `0`)
- `MM_SOAK_SCHED=fifo|rr|other` attempt to set scheduler policy
- `MM_SOAK_PRIO=<1..99>` RT priority (used for fifo/rr; default: `80`)

### Running with actual `SCHED_FIFO` (needs privileges)

You can run the soak binary directly:

```bash
sudo -E MM_SOAK_SECONDS=30 MM_SOAK_RT=1 MM_SOAK_SCHED=fifo MM_SOAK_PRIO=80 MM_SOAK_REPORT_MS=250 ./tests/bin/test_soak
```

If `sched_setscheduler` fails with `Operation not permitted`, you’ll need `CAP_SYS_NICE` (or system RT limits configured).

### Full RT (`SCHED_FIFO`) comparisons (memoman vs system malloc)

Run both in the same environment (same CPU pin + `mlockall` + FIFO scheduling):

- **memoman (FIFO + prio 80, 30s)**

```bash
sudo -E MM_SOAK_BACKEND=memoman MM_SOAK_SECONDS=30 MM_SOAK_RT=1 MM_SOAK_SCHED=fifo MM_SOAK_PRIO=80 MM_SOAK_REPORT_MS=250 ./tests/bin/test_soak
```

- **system malloc (FIFO + prio 80, 30s)**

```bash
sudo -E MM_SOAK_BACKEND=malloc MM_SOAK_SECONDS=30 MM_SOAK_RT=1 MM_SOAK_SCHED=fifo MM_SOAK_PRIO=80 MM_SOAK_REPORT_MS=250 ./tests/bin/test_soak
```

Tip: you can also pin to a different CPU with `MM_SOAK_CPU=<N>`.

### Full RT (`SCHED_FIFO`) comparisons (Conte TLSF)

These require the Conte-enabled binary:

```bash
make soak_conte_rt_30
```

Then run with FIFO scheduling:

```bash
sudo -E MM_SOAK_BACKEND=conte MM_SOAK_SECONDS=30 MM_SOAK_RT=1 MM_SOAK_SCHED=fifo MM_SOAK_PRIO=80 MM_SOAK_REPORT_MS=250 ./tests/bin/test_soak_conte
```

## One-command comparisons (recommended)

These targets build the right binary and run both backends sequentially, printing a summary and a delta line at the end.

- **memoman vs malloc (single command, FIFO)**

```bash
make compare_fifo_30
```

- **memoman vs Conte TLSF (single command, FIFO)**

```bash
make compare_conte_fifo_30
```

If you don’t want to use FIFO scheduling (no `sudo` required):

```bash
make compare_conte_rt_30
```

### Fair FIFO comparison (memoman vs Conte TLSF)

Build both soak binaries with the **same** compiler flags, then run them with one shared environment block (same CPU pin, same report interval, same validate cadence).

```bash
# Build both in release mode with identical flags.
make clean
make CFLAGS="-Wall -Wextra -std=c99 -Isrc -O2 -DNDEBUG" tests/bin/test_soak
make CFLAGS="-Wall -Wextra -std=c99 -Isrc -O2 -DNDEBUG" tests/bin/test_soak_conte

# Shared runtime configuration.
COMMON_ENV="MM_SOAK_SECONDS=30 MM_SOAK_RT=1 MM_SOAK_CPU=0 MM_SOAK_SCHED=fifo MM_SOAK_PRIO=80 MM_SOAK_REPORT_MS=250 MM_SOAK_VALIDATE_SHIFT=12 MM_SOAK_SEED=1"

# Run a single compare run (memoman vs Conte) and save a combined log.
sudo -E env $COMMON_ENV MM_SOAK_BACKEND=compare ./tests/bin/test_soak_conte | tee compare_conte_fifo_30.log
```
