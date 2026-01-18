#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "test_framework.h"
#include "../src/memoman.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <sys/mman.h>
#include <sys/resource.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>

#if defined(MM_SOAK_HAVE_CONTE_TLSF)
/* Optional: Matthew Conte TLSF (local checkout in ./matt_conte). */
#define tlsf_t conte_tlsf_t
#define pool_t conte_pool_t
#include "tlsf.h"
#undef tlsf_t
#undef pool_t
#endif

typedef enum {
  OP_MALLOC = 0,
  OP_FREE = 1,
  OP_REALLOC = 2,
  OP_MEMALIGN = 3,
} op_kind_t;

typedef struct {
  void* ptr;
  size_t req;
  size_t align;
  uint8_t pat;
} slot_t;

typedef struct {
  const char* name;
  int (*init_fn)(void); /* returns nonzero */
  void (*reset_fn)(void);
  void (*destroy_fn)(void);
  void* (*malloc_fn)(size_t size);
  void (*free_fn)(void* ptr);
  void* (*realloc_fn)(void* ptr, size_t size);
  void* (*memalign_fn)(size_t alignment, size_t size);
  size_t (*block_size_fn)(void* ptr); /* 0 means unknown */
  int (*validate_fn)(void); /* returns nonzero */
  int has_pools;
} soak_alloc_api_t;

static const soak_alloc_api_t* g_override_api;

static int mm_backend_init(void) { return 1; }
static void mm_backend_reset(void) { TEST_RESET(); }
static void mm_backend_destroy(void) {}
static void* mm_backend_malloc(size_t size) { return (mm_malloc)(sys_allocator, size); }
static void mm_backend_free(void* ptr) { (mm_free)(sys_allocator, ptr); }
static void* mm_backend_realloc(void* ptr, size_t size) { return (mm_realloc)(sys_allocator, ptr, size); }
static void* mm_backend_memalign(size_t a, size_t size) { return (mm_memalign)(sys_allocator, a, size); }
static size_t mm_backend_block_size(void* ptr) { return (mm_block_size)(ptr); }
static int mm_backend_validate(void) { return (mm_validate)(sys_allocator); }

static int sys_backend_init(void) { return 1; }
static void sys_backend_reset(void) {}
static void sys_backend_destroy(void) {}
static void* sys_backend_malloc(size_t size) { return malloc(size); }
static void sys_backend_free(void* ptr) { free(ptr); }
static void* sys_backend_realloc(void* ptr, size_t size) {
  if (!ptr && size == 0) return NULL;
  if (ptr && size == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, size);
}

static void* sys_backend_memalign(size_t alignment, size_t size) {
  if (size == 0) return NULL;
  if (alignment < sizeof(void*)) alignment = sizeof(void*);
  if ((alignment & (alignment - 1)) != 0) return NULL;

  void* p = NULL;
  if (posix_memalign(&p, alignment, size) != 0) return NULL;
  return p;
}

static size_t sys_backend_block_size(void* ptr) {
  if (!ptr) return 0;
#if defined(__GLIBC__)
  return malloc_usable_size(ptr);
#else
  (void)ptr;
  return 0;
#endif
}

static int sys_backend_validate(void) { return 1; }

static const soak_alloc_api_t g_memoman_api = {
  .name = "memoman",
  .init_fn = mm_backend_init,
  .reset_fn = mm_backend_reset,
  .destroy_fn = mm_backend_destroy,
  .malloc_fn = mm_backend_malloc,
  .free_fn = mm_backend_free,
  .realloc_fn = mm_backend_realloc,
  .memalign_fn = mm_backend_memalign,
  .block_size_fn = mm_backend_block_size,
  .validate_fn = mm_backend_validate,
  .has_pools = 1,
};

static const soak_alloc_api_t g_malloc_api = {
  .name = "malloc",
  .init_fn = sys_backend_init,
  .reset_fn = sys_backend_reset,
  .destroy_fn = sys_backend_destroy,
  .malloc_fn = sys_backend_malloc,
  .free_fn = sys_backend_free,
  .realloc_fn = sys_backend_realloc,
  .memalign_fn = sys_backend_memalign,
  .block_size_fn = sys_backend_block_size,
  .validate_fn = sys_backend_validate,
  .has_pools = 0,
};

#if defined(MM_SOAK_HAVE_CONTE_TLSF)
static conte_tlsf_t conte_tlsf;
static void* conte_pool;
static size_t conte_pool_bytes;

static int conte_backend_init(void) {
  if (conte_tlsf) return 1;

  conte_pool_bytes = (size_t)TEST_POOL_SIZE;
  if (conte_pool_bytes < (tlsf_size() + tlsf_pool_overhead() + 4096u)) return 0;

  /* Page-aligned simplifies mlock/prefault behavior and is safe for TLSF. */
  void* p = NULL;
  if (posix_memalign(&p, 4096, conte_pool_bytes) != 0) return 0;

  conte_pool = p;
  conte_tlsf = tlsf_create_with_pool(conte_pool, conte_pool_bytes);
  if (!conte_tlsf) {
    free(conte_pool);
    conte_pool = NULL;
    conte_pool_bytes = 0;
    return 0;
  }

  return 1;
}

static void conte_backend_destroy(void) {
  if (conte_tlsf) {
    tlsf_destroy(conte_tlsf);
    conte_tlsf = NULL;
  }
  if (conte_pool) {
    free(conte_pool);
    conte_pool = NULL;
  }
  conte_pool_bytes = 0;
}

static void conte_backend_reset(void) {
  if (!conte_tlsf) {
    (void)conte_backend_init();
    return;
  }

  tlsf_destroy(conte_tlsf);
  conte_tlsf = tlsf_create_with_pool(conte_pool, conte_pool_bytes);
}

static void* conte_backend_malloc(size_t size) {
  if (!conte_tlsf && !conte_backend_init()) return NULL;
  if (size == 0) return NULL;
  return tlsf_malloc(conte_tlsf, size);
}

static void conte_backend_free(void* ptr) {
  if (!ptr || !conte_tlsf) return;
  tlsf_free(conte_tlsf, ptr);
}

static void* conte_backend_realloc(void* ptr, size_t size) {
  if (!conte_tlsf && !conte_backend_init()) return NULL;
  if (!ptr && size == 0) return NULL;
  if (ptr && size == 0) {
    tlsf_free(conte_tlsf, ptr);
    return NULL;
  }
  return tlsf_realloc(conte_tlsf, ptr, size);
}

static void* conte_backend_memalign(size_t alignment, size_t size) {
  if (!conte_tlsf && !conte_backend_init()) return NULL;
  if (size == 0) return NULL;
  if (alignment < sizeof(void*)) alignment = sizeof(void*);
  if ((alignment & (alignment - 1)) != 0) return NULL;
  return tlsf_memalign(conte_tlsf, alignment, size);
}

static size_t conte_backend_block_size(void* ptr) {
  if (!ptr) return 0;
  return tlsf_block_size(ptr);
}

static int conte_backend_validate(void) {
  if (!conte_tlsf) return 0;
  return tlsf_check(conte_tlsf) == 0;
}

static const soak_alloc_api_t g_conte_api = {
  .name = "conte",
  .init_fn = conte_backend_init,
  .reset_fn = conte_backend_reset,
  .destroy_fn = conte_backend_destroy,
  .malloc_fn = conte_backend_malloc,
  .free_fn = conte_backend_free,
  .realloc_fn = conte_backend_realloc,
  .memalign_fn = conte_backend_memalign,
  .block_size_fn = conte_backend_block_size,
  .validate_fn = conte_backend_validate,
  .has_pools = 0,
};
#endif

static const soak_alloc_api_t* soak_backend_by_name(const char* name) {
  if (!name || !*name) return &g_memoman_api;
  if (!strcmp(name, "memoman")) return &g_memoman_api;
  if (!strcmp(name, "malloc")) return &g_malloc_api;
#if defined(MM_SOAK_HAVE_CONTE_TLSF)
  if (!strcmp(name, "conte")) return &g_conte_api;
#endif
  return &g_memoman_api;
}

static const soak_alloc_api_t* soak_backend(void) {
  if (g_override_api) return g_override_api;
  const char* env = getenv("MM_SOAK_BACKEND");
  if (!env || !*env) return &g_memoman_api;
  return soak_backend_by_name(env);
}

static uint32_t xorshift32(uint32_t* state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

static size_t pick_size(uint32_t r) {
  static const size_t sizes[] = {
    0, 1, 2, 3, 4, 7, 8, 15, 16, 24, 31, 32, 48, 63, 64, 80, 96, 127, 128, 192, 255, 256,
    384, 512, 768, 1024, 1536, 2048, 3072, 4096, 8192, 16384, 32768, 65536,
  };
  return sizes[r % (sizeof(sizes) / sizeof(sizes[0]))];
}

static size_t pick_align(uint32_t r) {
  static const size_t aligns[] = {8, 16, 32, 64, 128, 256, 512, 1024, 4096};
  return aligns[r % (sizeof(aligns) / sizeof(aligns[0]))];
}

static int ptr_aligned(const void* p, size_t a) {
  if (!p) return 1;
  if (!a) return 0;
  return (((uintptr_t)p & (a - 1)) == 0);
}

static void fill_pattern(void* p, size_t bytes, uint8_t pat) {
  if (!p || !bytes) return;
  size_t n = bytes > 64 ? 64 : bytes;
  memset(p, pat, n);
}

static int check_pattern(const void* p, size_t bytes, uint8_t pat) {
  if (!p || !bytes) return 1;
  size_t n = bytes > 64 ? 64 : bytes;
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; i++) {
    if (b[i] != pat) return 0;
  }
  return 1;
}

static void print_repro(
  const char* phase,
  uint32_t seed,
  size_t seed_index,
  size_t step,
  op_kind_t op,
  size_t slot,
  size_t req,
  size_t align
) {
  const char* opname = "UNKNOWN";
  if (op == OP_MALLOC) opname = "MALLOC";
  else if (op == OP_FREE) opname = "FREE";
  else if (op == OP_REALLOC) opname = "REALLOC";
  else if (op == OP_MEMALIGN) opname = "MEMALIGN";

  printf("MM_SOAK_REPRO phase=%s seed=0x%08x seed_index=%zu step=%zu op=%s slot=%zu req=%zu align=%zu\n",
    phase, seed, seed_index, step, opname, slot, req, align);
}

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int soak_verbose(void) {
  const char* env = getenv("MM_SOAK_VERBOSE");
  if (!env || !*env) return 1;
  return atoi(env) != 0;
}

static unsigned soak_seconds(void) {
  const char* env = getenv("MM_SOAK_SECONDS");
  if (!env || !*env) return 0;
  return (unsigned)strtoul(env, NULL, 0);
}

static int soak_stress(void) {
  const char* env = getenv("MM_SOAK_STRESS");
  if (!env || !*env) return 0;
  return atoi(env) != 0;
}

static size_t soak_iter_override(const char* env_name, size_t fallback) {
  const char* env = getenv(env_name);
  if (!env || !*env) return fallback;
  size_t v = (size_t)strtoull(env, NULL, 0);
  return v ? v : fallback;
}

static unsigned soak_report_ms(void) {
  const char* env = getenv("MM_SOAK_REPORT_MS");
  if (!env || !*env) return 1000;
  unsigned v = (unsigned)strtoul(env, NULL, 0);
  return v ? v : 1000;
}

static size_t soak_progress_every(void) {
  const char* env = getenv("MM_SOAK_PROGRESS_EVERY");
  if (!env || !*env) return 10;
  size_t v = (size_t)strtoull(env, NULL, 0);
  return v ? v : 10;
}

static int soak_strict(void) {
  const char* env = getenv("MM_SOAK_STRICT");
  if (!env || !*env) return 0;
  return atoi(env) != 0;
}

static int soak_rt(void) {
  const char* env = getenv("MM_SOAK_RT");
  if (!env || !*env) return 0;
  return atoi(env) != 0;
}

static int soak_cpu(void) {
  const char* env = getenv("MM_SOAK_CPU");
  if (!env || !*env) return 0;
  return atoi(env);
}

static int soak_rt_priority(void) {
  const char* env = getenv("MM_SOAK_PRIO");
  if (!env || !*env) return 80;
  int p = atoi(env);
  if (p < 1) p = 1;
  if (p > 99) p = 99;
  return p;
}

static int soak_rt_set_affinity(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return sched_setaffinity(0, sizeof(set), &set);
}

static int soak_rt_set_scheduler(void) {
  const char* env = getenv("MM_SOAK_SCHED");
  if (!env || !*env) return 1; /* skipped */

  int policy = -1;
  if (!strcmp(env, "fifo")) policy = SCHED_FIFO;
  else if (!strcmp(env, "rr")) policy = SCHED_RR;
  else if (!strcmp(env, "other")) policy = SCHED_OTHER;
  else return -1; /* invalid */

  struct sched_param p;
  memset(&p, 0, sizeof(p));
  p.sched_priority = (policy == SCHED_OTHER) ? 0 : soak_rt_priority();
  if (sched_setscheduler(0, policy, &p) != 0) return -2; /* failed */
  return 0; /* ok */
}

static void soak_rt_print_limits(void) {
  struct rlimit r;
  if (getrlimit(RLIMIT_MEMLOCK, &r) == 0) {
    unsigned long long cur = (unsigned long long)r.rlim_cur;
    unsigned long long max = (unsigned long long)r.rlim_max;
    printf("soak: rt rlimit_memlock cur=%llub max=%llub\n", cur, max);
  }
}

static void soak_rt_try_locking(void) {
  const int verbose = soak_verbose();
  if (!soak_rt()) return;

  soak_rt_print_limits();

  if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
    if (verbose) printf("soak: rt mlockall failed errno=%d (%s)\n", errno, strerror(errno));
  } else {
    if (verbose) printf("soak: rt mlockall ok\n");
  }

  /* Lock/prefault memoman's static test pool (even if we're not using it). */
  if (_test_pool) {
    if (mlock(_test_pool, TEST_POOL_SIZE) != 0) {
      if (verbose) printf("soak: rt mlock(pool) failed errno=%d (%s)\n", errno, strerror(errno));
    } else {
      if (verbose) printf("soak: rt mlock(pool) ok bytes=%zu\n", (size_t)TEST_POOL_SIZE);
    }

    long page = sysconf(_SC_PAGESIZE);
    if (page < 1) page = 4096;
    volatile uint8_t sink = 0;
    const uint8_t* b = (const uint8_t*)_test_pool;
    for (size_t off = 0; off < TEST_POOL_SIZE; off += (size_t)page) sink ^= b[off];
    (void)sink;
    if (verbose) printf("soak: rt prefault(pool) ok bytes=%zu page=%ld\n", (size_t)TEST_POOL_SIZE, page);
  }

#if defined(MM_SOAK_HAVE_CONTE_TLSF)
  if (conte_pool && conte_pool_bytes) {
    if (mlock(conte_pool, conte_pool_bytes) != 0) {
      if (verbose) printf("soak: rt mlock(conte_pool) failed errno=%d (%s)\n", errno, strerror(errno));
    } else {
      if (verbose) printf("soak: rt mlock(conte_pool) ok bytes=%zu\n", conte_pool_bytes);
    }

    long page = sysconf(_SC_PAGESIZE);
    if (page < 1) page = 4096;
    volatile uint8_t sink = 0;
    const uint8_t* b = (const uint8_t*)conte_pool;
    for (size_t off = 0; off < conte_pool_bytes; off += (size_t)page) sink ^= b[off];
    (void)sink;
    if (verbose) printf("soak: rt prefault(conte_pool) ok bytes=%zu page=%ld\n", conte_pool_bytes, page);
  }
#endif
}

static void soak_rt_try_process_tuning(void) {
  const int verbose = soak_verbose();
  if (!soak_rt()) return;

  int cpu = soak_cpu();
  if (soak_rt_set_affinity(cpu) != 0) {
    if (verbose) printf("soak: rt sched_setaffinity cpu=%d failed errno=%d (%s)\n", cpu, errno, strerror(errno));
  } else {
    if (verbose) printf("soak: rt pinned cpu=%d\n", cpu);
  }

  int s = soak_rt_set_scheduler();
  if (verbose) {
    if (s == 1) {
      printf("soak: rt sched_setscheduler skipped (set MM_SOAK_SCHED=fifo|rr|other)\n");
    } else if (s == -1) {
      printf("soak: rt sched policy invalid (use fifo|rr|other)\n");
    } else if (s == -2) {
      printf("soak: rt sched_setscheduler failed errno=%d (%s)\n", errno, strerror(errno));
    } else {
      int pol = sched_getscheduler(0);
      printf("soak: rt sched_setscheduler ok policy=%d prio=%d\n", pol, soak_rt_priority());
    }
  }
}

typedef struct {
  uint64_t ops;
  uint64_t failed;
  uint64_t max_ns;
  uint64_t total_ns;
} op_stat_t;

typedef struct {
  op_stat_t malloc_s;
  op_stat_t free_s;
  op_stat_t realloc_s;
  op_stat_t memalign_s;
  uint64_t validates;
  uint64_t validate_fail;
} soak_stats_t;

typedef struct {
  const char* backend;
  uint32_t seed;
  unsigned seconds;
  size_t steps;
  size_t slots;
  uint64_t elapsed_ns;
  soak_stats_t stats;
} soak_time_result_t;

static void stat_add(op_stat_t* s, uint64_t ns, int ok) {
  s->ops++;
  if (!ok) s->failed++;
  if (ns > s->max_ns) s->max_ns = ns;
  s->total_ns += ns;
}

static void print_stats_line(
  const char* phase,
  const char* backend,
  uint32_t seed,
  uint64_t elapsed_ns,
  uint64_t interval_ns,
  uint64_t total_ops,
  uint64_t interval_ops,
  size_t in_use,
  const soak_stats_t* st
) {
  double sec = (double)elapsed_ns / 1000000000.0;
  double isec = (double)interval_ns / 1000000000.0;
  double ops_s = sec > 0.0 ? (double)total_ops / sec : 0.0;
  double iops_s = isec > 0.0 ? (double)interval_ops / isec : 0.0;

  uint64_t max_us_m = st->malloc_s.max_ns / 1000ull;
  uint64_t max_us_f = st->free_s.max_ns / 1000ull;
  uint64_t max_us_r = st->realloc_s.max_ns / 1000ull;
  uint64_t max_us_a = st->memalign_s.max_ns / 1000ull;

  printf(
    "soak: phase=%s backend=%s seed=0x%08x t=%.2fs ops=%llu ops/s=%.0f (%.0f) in_use=%zu max_us{m=%llu f=%llu r=%llu a=%llu} "
    "fails{m=%llu r=%llu a=%llu} validate=%llu\n",
    phase,
    backend,
    seed,
    sec,
    (unsigned long long)total_ops,
    ops_s,
    iops_s,
    in_use,
    (unsigned long long)max_us_m,
    (unsigned long long)max_us_f,
    (unsigned long long)max_us_r,
    (unsigned long long)max_us_a,
    (unsigned long long)st->malloc_s.failed,
    (unsigned long long)st->realloc_s.failed,
    (unsigned long long)st->memalign_s.failed,
    (unsigned long long)st->validates
  );
  fflush(stdout);
}

static int soak_memalign_torture(uint32_t seed) {
  uint32_t rng = seed;
  const soak_alloc_api_t* api = soak_backend();
  size_t iters =
#ifdef MM_DEBUG
    2000;
#else
    20000;
#endif
  if (soak_stress()) iters *= 5;
  iters = soak_iter_override("MM_SOAK_MEMALIGN_ITERS", iters);
  const int verbose = soak_verbose();

  if (verbose) {
    printf("soak: phase=memalign_torture backend=%s seed=0x%08x iters=%zu\n", api->name, seed, iters);
  }
  for (size_t i = 0; i < iters; i++) {
    size_t req = pick_size(xorshift32(&rng));
    size_t a = pick_align(xorshift32(&rng));
    if (req == 0) req = 1;

    void* p = api->memalign_fn(a, req);
    if (!p) continue;

    size_t bs = api->block_size_fn ? api->block_size_fn(p) : 0;
    if (!ptr_aligned(p, a) || (bs && bs < req)) {
      print_repro("memalign", seed, 0, i, OP_MEMALIGN, 0, req, a);
      api->free_fn(p);
      return 0;
    }

    fill_pattern(p, req, (uint8_t)(rng & 0xff));
    api->free_fn(p);

    if (((i + 1) & 1023u) == 0) {
      if (!api->validate_fn()) {
        print_repro("memalign", seed, 0, i, OP_MEMALIGN, 0, req, a);
        return 0;
      }
    }
  }
  return 1;
}

static int soak_memalign_churn(uint32_t seed) {
  uint32_t rng = seed ^ 0xC001D00Du;
  const soak_alloc_api_t* api = soak_backend();
  const int verbose = soak_verbose();
  size_t slots_n = 256;
  size_t iters =
#ifdef MM_DEBUG
    2000;
#else
    8000;
#endif
  if (soak_stress()) {
    iters *= 4;
    slots_n = 512;
  }
  iters = soak_iter_override("MM_SOAK_MEMALIGN_CHURN_ITERS", iters);
  slots_n = soak_iter_override("MM_SOAK_MEMALIGN_CHURN_SLOTS", slots_n);
  if (slots_n == 0) slots_n = 1;
  if (slots_n > 1024) slots_n = 1024;

  void* slots[1024] = {0};
  size_t reqs[1024] = {0};
  size_t aligns[1024] = {0};

  if (verbose) {
    printf("soak: phase=memalign_churn backend=%s seed=0x%08x iters=%zu\n", api->name, seed, iters);
  }

  for (size_t i = 0; i < iters; i++) {
    size_t idx = (size_t)(xorshift32(&rng) % (uint32_t)slots_n);
    if (slots[idx]) {
      if (!check_pattern(slots[idx], reqs[idx], (uint8_t)(idx & 0xff))) {
        print_repro("memalign_churn", seed, 0, i, OP_MEMALIGN, idx, reqs[idx], aligns[idx]);
        api->free_fn(slots[idx]);
        return 0;
      }
      api->free_fn(slots[idx]);
      slots[idx] = NULL;
      reqs[idx] = 0;
      aligns[idx] = 0;
      continue;
    }

    size_t req = pick_size(xorshift32(&rng));
    size_t a = pick_align(xorshift32(&rng));
    if (req == 0) req = 1;
    void* p = api->memalign_fn(a, req);
    if (!p) continue;

    size_t bs = api->block_size_fn ? api->block_size_fn(p) : 0;
    if (!ptr_aligned(p, a) || (bs && bs < req)) {
      print_repro("memalign_churn", seed, 0, i, OP_MEMALIGN, idx, req, a);
      api->free_fn(p);
      return 0;
    }

    fill_pattern(p, req, (uint8_t)(idx & 0xff));
    slots[idx] = p;
    reqs[idx] = req;
    aligns[idx] = a;

    if (((i + 1) & 511u) == 0) {
      if (!api->validate_fn()) {
        print_repro("memalign_churn", seed, 0, i, OP_MEMALIGN, idx, req, a);
        return 0;
      }
    }
  }

  for (size_t i = 0; i < slots_n; i++) {
    if (slots[i]) api->free_fn(slots[i]);
  }
  return api->validate_fn();
}

static int soak_pool_add_remove(uint32_t seed) {
  const soak_alloc_api_t* api = soak_backend();
  const int verbose = soak_verbose();
  if (verbose) {
    printf("soak: phase=pool_add_remove backend=%s seed=0x%08x\n", api->name, seed);
  }
  (void)seed;
  if (!api->has_pools) return 1;

  TEST_RESET();

  size_t pool_bytes = 256 * 1024;
  void* raw = malloc(pool_bytes);
  ASSERT_NOT_NULL(raw);

  pool_t pool2 = mm_add_pool(sys_allocator, raw, pool_bytes);
  if (!pool2) {
    free(raw);
    ASSERT(api->validate_fn());
    return 1;
  }

  void* a = (mm_malloc)(sys_allocator, 64 * 1024);
  void* b = (mm_malloc)(sys_allocator, 64 * 1024);
  if (a) fill_pattern(a, 64 * 1024, 0xA5);
  if (b) fill_pattern(b, 64 * 1024, 0x5A);
  ASSERT(api->validate_fn());

  if (a) (mm_free)(sys_allocator, a);
  if (b) (mm_free)(sys_allocator, b);
  ASSERT(api->validate_fn());

  mm_remove_pool(sys_allocator, pool2);
  ASSERT(api->validate_fn());

  free(raw);
  return 1;
}

static int soak_random_ops(uint32_t seed, size_t seed_index, size_t steps, size_t slots_n, unsigned validate_shift) {
  uint32_t rng = seed;
  const soak_alloc_api_t* api = soak_backend();
  slot_t* slots = (slot_t*)calloc(slots_n, sizeof(slot_t));
  ASSERT_NOT_NULL(slots);

  for (size_t i = 0; i < steps; i++) {
    uint32_t r = xorshift32(&rng);
    op_kind_t op = (op_kind_t)(r & 3u);
    size_t idx = (size_t)((r >> 2) % (uint32_t)slots_n);
    slot_t* s = &slots[idx];

    if (s->ptr && !check_pattern(s->ptr, s->req, s->pat)) {
      print_repro("random", seed, seed_index, i, op, idx, s->req, s->align);
      free(slots);
      return 0;
    }

    if (op == OP_FREE) {
      if (s->ptr) {
        api->free_fn(s->ptr);
        s->ptr = NULL;
        s->req = 0;
        s->align = 0;
        s->pat = 0;
      }
    } else if (op == OP_MALLOC) {
      if (!s->ptr) {
        size_t req = pick_size(xorshift32(&rng));
        void* p = api->malloc_fn(req);
        if (p) {
          size_t bs = api->block_size_fn ? api->block_size_fn(p) : 0;
          if ((bs && bs < req) || !ptr_aligned(p, sizeof(void*))) {
            print_repro("random", seed, seed_index, i, op, idx, req, sizeof(void*));
            api->free_fn(p);
            free(slots);
            return 0;
          }
          s->ptr = p;
          s->req = req;
          s->align = sizeof(void*);
          s->pat = (uint8_t)(xorshift32(&rng) & 0xff);
          fill_pattern(s->ptr, s->req, s->pat);
        }
      }
    } else if (op == OP_MEMALIGN) {
      if (!s->ptr) {
        size_t req = pick_size(xorshift32(&rng));
        size_t a = pick_align(xorshift32(&rng));
        void* p = api->memalign_fn(a, req);
        if (p) {
          size_t bs = api->block_size_fn ? api->block_size_fn(p) : 0;
          if ((bs && bs < req) || !ptr_aligned(p, a)) {
            print_repro("random", seed, seed_index, i, op, idx, req, a);
            api->free_fn(p);
            free(slots);
            return 0;
          }
          s->ptr = p;
          s->req = req;
          s->align = a;
          s->pat = (uint8_t)(xorshift32(&rng) & 0xff);
          fill_pattern(s->ptr, s->req, s->pat);
        }
      }
    } else if (op == OP_REALLOC) {
      size_t new_req = pick_size(xorshift32(&rng));

      if (!s->ptr) {
        void* p = api->realloc_fn(NULL, new_req);
        if (p) {
          size_t bs = api->block_size_fn ? api->block_size_fn(p) : 0;
          if ((bs && bs < new_req) || !ptr_aligned(p, sizeof(void*))) {
            print_repro("random", seed, seed_index, i, op, idx, new_req, sizeof(void*));
            api->free_fn(p);
            free(slots);
            return 0;
          }
          s->ptr = p;
          s->req = new_req;
          s->align = sizeof(void*);
          s->pat = (uint8_t)(xorshift32(&rng) & 0xff);
          fill_pattern(s->ptr, s->req, s->pat);
        }
      } else if (new_req == 0) {
        void* p = api->realloc_fn(s->ptr, 0);
        if (p != NULL) {
          print_repro("random", seed, seed_index, i, op, idx, new_req, sizeof(void*));
          free(slots);
          return 0;
        }
        s->ptr = NULL;
        s->req = 0;
        s->align = 0;
        s->pat = 0;
      } else {
        void* old = s->ptr;
        size_t old_req = s->req;
        uint8_t old_pat = s->pat;

        void* p = api->realloc_fn(old, new_req);
        if (!p) {
          if (!check_pattern(old, old_req, old_pat)) {
            print_repro("random", seed, seed_index, i, op, idx, new_req, sizeof(void*));
            free(slots);
            return 0;
          }
        } else {
          size_t bs = api->block_size_fn ? api->block_size_fn(p) : 0;
          if ((bs && bs < new_req) || !ptr_aligned(p, sizeof(void*))) {
            print_repro("random", seed, seed_index, i, op, idx, new_req, sizeof(void*));
            api->free_fn(p);
            free(slots);
            return 0;
          }

          size_t preserved = old_req < new_req ? old_req : new_req;
          if (!check_pattern(p, preserved, old_pat)) {
            print_repro("random", seed, seed_index, i, op, idx, new_req, sizeof(void*));
            api->free_fn(p);
            free(slots);
            return 0;
          }

          s->ptr = p;
          s->req = new_req;
          s->align = sizeof(void*);
          s->pat = (uint8_t)(xorshift32(&rng) & 0xff);
          fill_pattern(s->ptr, s->req, s->pat);
        }
      }
    }

    if (((i + 1) & (((size_t)1u << validate_shift) - 1u)) == 0) {
      if (!api->validate_fn()) {
        print_repro("random", seed, seed_index, i, op, idx, s->req, s->align);
        free(slots);
        return 0;
      }
    }
  }

  for (size_t i = 0; i < slots_n; i++) {
    if (slots[i].ptr) api->free_fn(slots[i].ptr);
  }

  if (!api->validate_fn()) {
    print_repro("random", seed, seed_index, steps, OP_FREE, 0, 0, 0);
    free(slots);
    return 0;
  }

  free(slots);
  return 1;
}

static int soak_random_time(uint32_t seed, unsigned seconds, size_t slots_n, unsigned validate_shift, soak_time_result_t* out) {
  uint32_t rng = seed;
  const soak_alloc_api_t* api = soak_backend();
  slot_t* slots = (slot_t*)calloc(slots_n, sizeof(slot_t));
  ASSERT_NOT_NULL(slots);

  soak_stats_t total = {0};
  size_t in_use = 0;
  const int strict = soak_strict();

  const uint64_t t0 = now_ns();
  const uint64_t deadline = t0 + (uint64_t)seconds * 1000000000ull;
  const uint64_t report_period = (uint64_t)soak_report_ms() * 1000000ull;
  uint64_t next_report = t0;
  uint64_t last_report = t0;
  uint64_t last_report_ops = 0;

  size_t step = 0;
  while (now_ns() < deadline) {
    uint32_t r = xorshift32(&rng);
    op_kind_t op = (op_kind_t)(r & 3u);
    size_t idx = (size_t)((r >> 2) % (uint32_t)slots_n);
    slot_t* s = &slots[idx];

    if (s->ptr && !check_pattern(s->ptr, s->req, s->pat)) {
      print_repro("time", seed, 0, step, op, idx, s->req, s->align);
      free(slots);
      return 0;
    }

    uint64_t t_op0 = now_ns();
    int ok = 1;
    int fatal = 0;

    if (op == OP_FREE) {
      if (s->ptr) {
        api->free_fn(s->ptr);
        s->ptr = NULL;
        s->req = 0;
        s->align = 0;
        s->pat = 0;
        if (in_use) in_use--;
      }
    } else if (op == OP_MALLOC) {
      if (!s->ptr) {
        size_t req = pick_size(xorshift32(&rng));
        if (req != 0) {
          void* p = api->malloc_fn(req);
          if (!p) {
            ok = 0;
          } else {
            size_t bs = api->block_size_fn ? api->block_size_fn(p) : 0;
            if ((bs && bs < req) || !ptr_aligned(p, sizeof(void*))) fatal = 1;
            s->ptr = p;
            s->req = req;
            s->align = sizeof(void*);
            s->pat = (uint8_t)(xorshift32(&rng) & 0xff);
            fill_pattern(s->ptr, s->req, s->pat);
            in_use++;
          }
        }
      }
    } else if (op == OP_MEMALIGN) {
      if (!s->ptr) {
        size_t req = pick_size(xorshift32(&rng));
        size_t a = pick_align(xorshift32(&rng));
        if (req != 0) {
          void* p = api->memalign_fn(a, req);
          if (!p) {
            ok = 0;
          } else {
            size_t bs = api->block_size_fn ? api->block_size_fn(p) : 0;
            if ((bs && bs < req) || !ptr_aligned(p, a)) fatal = 1;
            s->ptr = p;
            s->req = req;
            s->align = a;
            s->pat = (uint8_t)(xorshift32(&rng) & 0xff);
            fill_pattern(s->ptr, s->req, s->pat);
            in_use++;
          }
        }
      }
    } else if (op == OP_REALLOC) {
      size_t new_req = pick_size(xorshift32(&rng));
      if (!s->ptr) {
        void* p = api->realloc_fn(NULL, new_req);
        if (new_req == 0) {
          if (p != NULL) fatal = 1;
        } else if (!p) {
          ok = 0;
        } else {
          size_t bs = api->block_size_fn ? api->block_size_fn(p) : 0;
          if ((bs && bs < new_req) || !ptr_aligned(p, sizeof(void*))) fatal = 1;
          s->ptr = p;
          s->req = new_req;
          s->align = sizeof(void*);
          s->pat = (uint8_t)(xorshift32(&rng) & 0xff);
          fill_pattern(s->ptr, s->req, s->pat);
          in_use++;
        }
      } else if (new_req == 0) {
        void* p = api->realloc_fn(s->ptr, 0);
        if (p != NULL) fatal = 1;
        s->ptr = NULL;
        s->req = 0;
        s->align = 0;
        s->pat = 0;
        if (in_use) in_use--;
      } else {
        void* old = s->ptr;
        size_t old_req = s->req;
        uint8_t old_pat = s->pat;
        void* p = api->realloc_fn(old, new_req);
        if (!p) {
          ok = 0;
          if (!check_pattern(old, old_req, old_pat)) fatal = 1;
        } else {
          size_t bs = api->block_size_fn ? api->block_size_fn(p) : 0;
          if ((bs && bs < new_req) || !ptr_aligned(p, sizeof(void*))) fatal = 1;
          size_t preserved = old_req < new_req ? old_req : new_req;
          if (!check_pattern(p, preserved, old_pat)) fatal = 1;
          s->ptr = p;
          s->req = new_req;
          s->align = sizeof(void*);
          s->pat = (uint8_t)(xorshift32(&rng) & 0xff);
          fill_pattern(s->ptr, s->req, s->pat);
        }
      }
    }

    uint64_t op_ns = now_ns() - t_op0;
    if (op == OP_MALLOC) stat_add(&total.malloc_s, op_ns, ok);
    else if (op == OP_FREE) stat_add(&total.free_s, op_ns, ok);
    else if (op == OP_REALLOC) stat_add(&total.realloc_s, op_ns, ok);
    else if (op == OP_MEMALIGN) stat_add(&total.memalign_s, op_ns, ok);

    if (fatal || (strict && !ok)) {
      print_repro("time", seed, 0, step, op, idx, s->req, s->align);
      for (size_t j = 0; j < slots_n; j++) if (slots[j].ptr) api->free_fn(slots[j].ptr);
      free(slots);
      return 0;
    }

    if (((step + 1) & (((size_t)1u << validate_shift) - 1u)) == 0) {
      total.validates++;
      if (!api->validate_fn()) {
        total.validate_fail++;
        print_repro("time", seed, 0, step, op, idx, s->req, s->align);
        for (size_t j = 0; j < slots_n; j++) if (slots[j].ptr) api->free_fn(slots[j].ptr);
        free(slots);
        return 0;
      }
    }

    uint64_t t_now = now_ns();
    if (t_now >= next_report) {
      uint64_t interval_ns = t_now - last_report;
      uint64_t interval_ops = ((uint64_t)step + 1u) - last_report_ops;
      print_stats_line(
        "time",
        api->name,
        seed,
        t_now - t0,
        interval_ns,
        (uint64_t)step + 1,
        interval_ops,
        in_use,
        &total
      );
      last_report = t_now;
      last_report_ops = (uint64_t)step + 1u;
      next_report = t_now + report_period;
    }

    step++;
  }

  for (size_t i = 0; i < slots_n; i++) {
    if (slots[i].ptr) api->free_fn(slots[i].ptr);
  }
  if (!api->validate_fn()) {
    print_repro("time", seed, 0, step, OP_FREE, 0, 0, 0);
    free(slots);
    return 0;
  }

  free(slots);
  {
    uint64_t t_end = now_ns();
    print_stats_line(
      "time",
      api->name,
      seed,
      t_end - t0,
      t_end - last_report,
      (uint64_t)step,
      (uint64_t)step - last_report_ops,
      0,
      &total
    );
    if (out) {
      out->backend = api->name;
      out->seed = seed;
      out->seconds = seconds;
      out->steps = step;
      out->slots = slots_n;
      out->elapsed_ns = t_end - t0;
      out->stats = total;
    }
  }
  printf("MM_SOAK_TIME_DONE backend=%s seed=0x%08x seconds=%u steps=%zu slots=%zu\n", api->name, seed, seconds, step, slots_n);
  return 1;
}

static void soak_print_time_summary(const soak_time_result_t* r) {
  if (!r) return;
  const double sec = r->elapsed_ns ? ((double)r->elapsed_ns / 1000000000.0) : 0.0;
  const double ops_s = sec > 0.0 ? (double)r->steps / sec : 0.0;

  printf(
    "soak: summary backend=%s seconds=%u steps=%zu ops/s=%.0f max_us{m=%llu f=%llu r=%llu a=%llu} fails{m=%llu r=%llu a=%llu} validate=%llu\n",
    r->backend ? r->backend : "?",
    r->seconds,
    r->steps,
    ops_s,
    (unsigned long long)(r->stats.malloc_s.max_ns / 1000ull),
    (unsigned long long)(r->stats.free_s.max_ns / 1000ull),
    (unsigned long long)(r->stats.realloc_s.max_ns / 1000ull),
    (unsigned long long)(r->stats.memalign_s.max_ns / 1000ull),
    (unsigned long long)r->stats.malloc_s.failed,
    (unsigned long long)r->stats.realloc_s.failed,
    (unsigned long long)r->stats.memalign_s.failed,
    (unsigned long long)r->stats.validates
  );
}

static void soak_print_time_compare(const soak_time_result_t* a, const soak_time_result_t* b) {
  if (!a || !b) return;
  double a_sec = a->elapsed_ns ? ((double)a->elapsed_ns / 1000000000.0) : 0.0;
  double b_sec = b->elapsed_ns ? ((double)b->elapsed_ns / 1000000000.0) : 0.0;
  double a_ops = a_sec > 0.0 ? (double)a->steps / a_sec : 0.0;
  double b_ops = b_sec > 0.0 ? (double)b->steps / b_sec : 0.0;
  double pct = (a_ops > 0.0) ? ((b_ops - a_ops) / a_ops) * 100.0 : 0.0;

  printf(
    "soak: compare %s vs %s ops/s %.0f vs %.0f (%.2f%%)\n",
    a->backend ? a->backend : "?",
    b->backend ? b->backend : "?",
    a_ops,
    b_ops,
    pct
  );
}

static int soak_run_backend_time(
  const soak_alloc_api_t* api,
  uint32_t seed0,
  unsigned seconds,
  size_t slots_n,
  unsigned validate_shift,
  soak_time_result_t* out_time
) {
  g_override_api = api;
  ASSERT(api->init_fn ? api->init_fn() : 1);

  if (soak_verbose()) {
    printf("soak: run backend=%s\n", api->name);
  }

  ASSERT(api->validate_fn());
  ASSERT(soak_pool_add_remove(seed0));
  ASSERT(soak_memalign_torture(seed0 ^ 0xA5A5A5A5u));
  ASSERT(soak_memalign_churn(seed0 ^ 0x5A5A5A5Au));

  if (api->reset_fn) api->reset_fn();
  soak_rt_try_locking();

  int ok = soak_random_time(seed0, seconds, slots_n, validate_shift, out_time);
  if (api->destroy_fn) api->destroy_fn();
  g_override_api = NULL;
  return ok;
}

static int test_soak(void) {
  const char* env_backend = getenv("MM_SOAK_BACKEND");
  const char* env_seed0 = getenv("MM_SOAK_SEED");
  const char* env_seed_count = getenv("MM_SOAK_SEEDS");
  const char* env_steps = getenv("MM_SOAK_STEPS");
  const char* env_slots = getenv("MM_SOAK_SLOTS");
  const char* env_validate = getenv("MM_SOAK_VALIDATE_SHIFT");

  uint32_t seed0 = 1u;
  size_t seeds = 25;
  size_t steps =
#ifdef MM_DEBUG
    5000;
#else
    20000;
#endif
  size_t slots_n = 512;
  unsigned validate_shift =
#ifdef MM_DEBUG
    18;
#else
    12;
#endif
  const int verbose = soak_verbose();
  const size_t progress_every = soak_progress_every();
  const int want_compare = (env_backend && !strcmp(env_backend, "compare"));
  const soak_alloc_api_t* api = want_compare ? NULL : soak_backend();

  if (env_seed0 && *env_seed0) seed0 = (uint32_t)strtoul(env_seed0, NULL, 0);
  if (env_seed_count && *env_seed_count) seeds = (size_t)strtoull(env_seed_count, NULL, 0);
  if (env_steps && *env_steps) steps = (size_t)strtoull(env_steps, NULL, 0);
  if (env_slots && *env_slots) slots_n = (size_t)strtoull(env_slots, NULL, 0);
  if (env_validate && *env_validate) validate_shift = (unsigned)strtoul(env_validate, NULL, 0);

  if (seeds == 0) seeds = 1;
  if (slots_n == 0) slots_n = 1;
  if (steps == 0) steps = 1;
  if (validate_shift > 30) validate_shift = 30;

  clock_t t0 = clock();
  if (verbose) {
#ifdef MM_DEBUG
    const char* build = "MM_DEBUG";
#else
    const char* build = "release";
#endif
    if (want_compare) {
      printf("soak: build=%s backend=compare seed0=0x%08x seeds=%zu steps=%zu slots=%zu validate_shift=%u progress_every=%zu\n",
        build, seed0, seeds, steps, slots_n, validate_shift, progress_every);
    } else {
      printf("soak: build=%s backend=%s seed0=0x%08x seeds=%zu steps=%zu slots=%zu validate_shift=%u progress_every=%zu\n",
        build, api->name, seed0, seeds, steps, slots_n, validate_shift, progress_every);
    }
  }

  unsigned seconds = soak_seconds();
  if (want_compare) {
    if (!seconds) {
      printf("soak: compare mode requires MM_SOAK_SECONDS (time mode)\n");
      return 0;
    }

    soak_rt_try_process_tuning();

    soak_time_result_t r0 = {0};
    soak_time_result_t r1 = {0};

#if defined(MM_SOAK_HAVE_CONTE_TLSF)
    const soak_alloc_api_t* left = &g_memoman_api;
    const soak_alloc_api_t* right = &g_conte_api;
#else
    const soak_alloc_api_t* left = &g_memoman_api;
    const soak_alloc_api_t* right = &g_malloc_api;
    printf("soak: compare note: built without Conte TLSF; comparing memoman vs malloc\n");
#endif

    if (!soak_run_backend_time(left, seed0, seconds, slots_n, validate_shift, &r0)) return 0;
    if (!soak_run_backend_time(right, seed0, seconds, slots_n, validate_shift, &r1)) return 0;

    soak_print_time_summary(&r0);
    soak_print_time_summary(&r1);
    soak_print_time_compare(&r0, &r1);
    return 1;
  }

  ASSERT(api->init_fn ? api->init_fn() : 1);
  ASSERT(api->validate_fn());
  ASSERT(soak_pool_add_remove(seed0));
  ASSERT(soak_memalign_torture(seed0 ^ 0xA5A5A5A5u));
  ASSERT(soak_memalign_churn(seed0 ^ 0x5A5A5A5Au));

  if (seconds) {
    soak_rt_try_process_tuning();
    if (api->reset_fn) api->reset_fn();
    soak_rt_try_locking();
    if (!soak_random_time(seed0, seconds, slots_n, validate_shift, NULL)) {
      if (api->destroy_fn) api->destroy_fn();
      return 0;
    }
    if (api->destroy_fn) api->destroy_fn();
    return 1;
  }

  for (size_t i = 0; i < seeds; i++) {
    uint32_t seed = seed0 + (uint32_t)i;
    if (api->reset_fn) api->reset_fn();
    if (verbose && (i == 0 || (i % progress_every) == 0 || i + 1 == seeds)) {
      double elapsed = (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
      printf("soak: phase=random backend=%s seed_index=%zu/%zu seed=0x%08x elapsed=%.2fs\n", api->name, i + 1, seeds, seed, elapsed);
    }
    if (!soak_random_ops(seed, i, steps, slots_n, validate_shift)) {
      if (api->destroy_fn) api->destroy_fn();
      return 0;
    }
  }

  if (verbose) {
    double elapsed = (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
    size_t total_ops = seeds * steps;
    printf("soak: ok elapsed=%.2fs total_ops=%zu\n", elapsed, total_ops);
  }

  if (api->destroy_fn) api->destroy_fn();
  return 1;
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  TEST_SUITE_BEGIN("soak");
  RUN_TEST(test_soak);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
