#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../src/memoman.h"

#ifndef MM_HIST_HAVE_CONTE_TLSF
#define MM_HIST_HAVE_CONTE_TLSF 0
#endif

#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#ifndef MM_HIST_POOL_BYTES
#define MM_HIST_POOL_BYTES (1u * 1024u * 1024u)
#endif

#ifndef MM_HIST_SAMPLES
#define MM_HIST_SAMPLES 0u
#endif

#ifndef MM_HIST_MAX_LIVE
#define MM_HIST_MAX_LIVE 128u
#endif

#ifndef MM_HIST_FRAME_BYTES
#define MM_HIST_FRAME_BYTES 4096u
#endif

#ifndef MM_HIST_BURST_MIN
#define MM_HIST_BURST_MIN 4u
#endif

#ifndef MM_HIST_BURST_MAX
#define MM_HIST_BURST_MAX 12u
#endif

#ifndef MM_HIST_KEEP_MIN
#define MM_HIST_KEEP_MIN 8u
#endif

#ifndef MM_HIST_KEEP_MAX
#define MM_HIST_KEEP_MAX 16u
#endif

#ifndef MM_HIST_REPORT_MS
#define MM_HIST_REPORT_MS 1000u
#endif

#ifndef MM_HIST_RT
#define MM_HIST_RT 0u
#endif

#ifndef MM_HIST_RT_PRIO
#define MM_HIST_RT_PRIO 80u
#endif

#ifndef MM_HIST_RT_CPU
#define MM_HIST_RT_CPU 0u
#endif

#define HIST_BUCKETS 10u
#define HIST_BAR_WIDTH 40u

static const uint64_t hist_limits[HIST_BUCKETS] = {
  50u, 100u, 200u, 400u, 800u, 1600u, 3200u, 6400u, 12800u, 25600u
};

typedef struct hist_t {
  uint64_t counts[HIST_BUCKETS + 1u];
  uint64_t min;
  uint64_t max;
  uint64_t total;
  uint64_t samples;
} hist_t;

static uint64_t now_ns(void) {
  struct timespec ts;
#if defined(CLOCK_MONOTONIC_RAW)
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
  clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
  return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

#if MM_HIST_HAVE_CONTE_TLSF
typedef void* conte_tlsf_t;
#define tlsf_t conte_tlsf_t
#include "../examples/matt_conte/tlsf.h"
#undef tlsf_t

static conte_tlsf_t conte_create_with_pool(void* mem, size_t bytes) {
  return tlsf_create_with_pool(mem, bytes);
}

static void conte_destroy(conte_tlsf_t tlsf) {
  tlsf_destroy(tlsf);
}

static void* conte_malloc(conte_tlsf_t tlsf, size_t bytes) {
  return tlsf_malloc(tlsf, bytes);
}

static void conte_free(conte_tlsf_t tlsf, void* ptr) {
  tlsf_free(tlsf, ptr);
}

static int conte_check(conte_tlsf_t tlsf) {
  return tlsf_check(tlsf);
}
#else
typedef void* conte_tlsf_t;

static conte_tlsf_t conte_create_with_pool(void* mem, size_t bytes) {
  (void)mem;
  (void)bytes;
  return NULL;
}

static void conte_destroy(conte_tlsf_t tlsf) {
  (void)tlsf;
}

static void* conte_malloc(conte_tlsf_t tlsf, size_t bytes) {
  (void)tlsf;
  (void)bytes;
  return NULL;
}

static void conte_free(conte_tlsf_t tlsf, void* ptr) {
  (void)tlsf;
  (void)ptr;
}

static int conte_check(conte_tlsf_t tlsf) {
  (void)tlsf;
  return 0;
}
#endif

static uint32_t lcg_next(uint32_t* state) {
  *state = (*state * 1664525u) + 1013904223u;
  return *state;
}

static void hist_init(hist_t* hist) {
  for (size_t i = 0; i < HIST_BUCKETS + 1u; i++) {
    hist->counts[i] = 0;
  }
  hist->min = UINT64_MAX;
  hist->max = 0;
  hist->total = 0;
  hist->samples = 0;
}

static void hist_record(hist_t* hist, uint64_t value) {
  if (value < hist->min) {
    hist->min = value;
  }
  if (value > hist->max) {
    hist->max = value;
  }
  hist->total += value;
  hist->samples += 1;

  for (size_t i = 0; i < HIST_BUCKETS; i++) {
    if (value <= hist_limits[i]) {
      hist->counts[i] += 1;
      return;
    }
  }

  hist->counts[HIST_BUCKETS] += 1;
}

static void hist_print(const char* label, const hist_t* hist) {
  printf("\n%s latency (ns)\n", label);
  for (size_t i = 0; i < HIST_BUCKETS; i++) {
    printf("  <= %5" PRIu64 " : %" PRIu64 "\n", hist_limits[i], hist->counts[i]);
  }
  printf("  >  %5" PRIu64 " : %" PRIu64 "\n", hist_limits[HIST_BUCKETS - 1u], hist->counts[HIST_BUCKETS]);

  if (hist->samples == 0) {
    printf("  no samples\n");
    return;
  }

  printf("  min=%" PRIu64 " avg=%" PRIu64 " max=%" PRIu64 "\n",
    hist->min, hist->total / hist->samples, hist->max);
}

static void hist_copy(hist_t* dst, const hist_t* src) {
  *dst = *src;
}

static void hist_print_bar(uint64_t count, uint64_t max_count) {
  unsigned bar = 0u;
  if (max_count > 0u) {
    bar = (unsigned)((count * HIST_BAR_WIDTH) / max_count);
  }
  printf(" |");
  for (unsigned i = 0; i < bar; i++) {
    putchar('#');
  }
  for (unsigned i = bar; i < HIST_BAR_WIDTH; i++) {
    putchar(' ');
  }
  printf("|");
}

static void hist_print_delta(const char* label, const hist_t* total, const hist_t* prev) {
  hist_t delta;
  hist_copy(&delta, total);

  if (delta.samples >= prev->samples) {
    delta.samples -= prev->samples;
  } else {
    delta.samples = 0;
  }

  if (delta.total >= prev->total) {
    delta.total -= prev->total;
  } else {
    delta.total = 0;
  }

  if (delta.samples == 0) {
    printf("\n%s latency (ns)\n", label);
    printf("  no samples\n");
    return;
  }

  for (size_t i = 0; i < HIST_BUCKETS; i++) {
    if (delta.counts[i] >= prev->counts[i]) {
      delta.counts[i] -= prev->counts[i];
    } else {
      delta.counts[i] = 0;
    }
  }

  if (delta.counts[HIST_BUCKETS] >= prev->counts[HIST_BUCKETS]) {
    delta.counts[HIST_BUCKETS] -= prev->counts[HIST_BUCKETS];
  } else {
    delta.counts[HIST_BUCKETS] = 0;
  }

  uint64_t max_count = 0u;
  for (size_t i = 0; i < HIST_BUCKETS + 1u; i++) {
    if (delta.counts[i] > max_count) {
      max_count = delta.counts[i];
    }
  }

  printf("\n%s latency (ns)\n", label);
  for (size_t i = 0; i < HIST_BUCKETS; i++) {
    printf("  <= %5" PRIu64 " : %" PRIu64, hist_limits[i], delta.counts[i]);
    hist_print_bar(delta.counts[i], max_count);
    printf("\n");
  }
  printf("  >  %5" PRIu64 " : %" PRIu64, hist_limits[HIST_BUCKETS - 1u], delta.counts[HIST_BUCKETS]);
  hist_print_bar(delta.counts[HIST_BUCKETS], max_count);
  printf("\n");

  printf("  avg=%" PRIu64 "\n", delta.total / delta.samples);
}

static void clear_screen(void) {
  printf("\033[2J\033[H");
}

static size_t parse_samples(const char* value, size_t fallback) {
  if (!value || value[0] == '\0') {
    return fallback;
  }
  char* end = NULL;
  unsigned long parsed = strtoul(value, &end, 10);
  if (!end || end == value || *end != '\0') {
    return fallback;
  }
  return (size_t)parsed;
}

static int apply_rt(size_t enabled, size_t prio, size_t cpu) {
  if (enabled == 0u) {
    return 0;
  }

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET((int)cpu, &cpuset);
  if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
    printf("RT: sched_setaffinity failed: %s\n", strerror(errno));
  }

  struct sched_param param;
  memset(&param, 0, sizeof(param));
  param.sched_priority = (int)prio;
  if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
    printf("RT: sched_setscheduler failed: %s\n", strerror(errno));
  }

  if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
    printf("RT: mlockall failed: %s\n", strerror(errno));
  }

  return 0;
}

static void print_progress(const char* label, size_t samples, size_t progress, size_t live_count, size_t failures) {
  if (samples == 0u) {
    printf("%sprogress=%zu/inf live=%zu failures=%zu\n", label, progress, live_count, failures);
  } else {
    printf("%sprogress=%zu/%zu live=%zu failures=%zu\n", label, progress, samples, live_count, failures);
  }
}

static size_t pick_between(uint32_t* rng, size_t min, size_t max) {
  if (min >= max) {
    return min;
  }
  return min + (size_t)(lcg_next(rng) % (uint32_t)(max - min + 1u));
}

static size_t align_up(size_t value, size_t align) {
  return (value + align - 1u) & ~(align - 1u);
}

int main(void) {
  static uint8_t pool[MM_HIST_POOL_BYTES] __attribute__((aligned(16)));
  static uint8_t conte_pool[MM_HIST_POOL_BYTES] __attribute__((aligned(16)));
  static void* live_ptrs[MM_HIST_MAX_LIVE];
  static void* live_conte_ptrs[MM_HIST_MAX_LIVE];

  static const size_t sizes[] = {
    8u, 16u, 24u, 32u, 48u, 64u, 96u, 128u, 160u, 192u, 224u, 256u, 320u, 384u, 448u, 512u
  };

  size_t samples = parse_samples(getenv("MM_HIST_SAMPLES"), MM_HIST_SAMPLES);
  size_t report_ms = parse_samples(getenv("MM_HIST_REPORT_MS"), MM_HIST_REPORT_MS);
  size_t frame_bytes = parse_samples(getenv("MM_HIST_FRAME_BYTES"), MM_HIST_FRAME_BYTES);
  size_t burst_min = parse_samples(getenv("MM_HIST_BURST_MIN"), MM_HIST_BURST_MIN);
  size_t burst_max = parse_samples(getenv("MM_HIST_BURST_MAX"), MM_HIST_BURST_MAX);
  size_t keep_min = parse_samples(getenv("MM_HIST_KEEP_MIN"), MM_HIST_KEEP_MIN);
  size_t keep_max = parse_samples(getenv("MM_HIST_KEEP_MAX"), MM_HIST_KEEP_MAX);
  size_t live_count = 0;
  size_t conte_live_count = 0;
  size_t failures = 0;
  size_t conte_failures = 0;
  uint32_t size_rng = 0x12345678u;
  uint32_t pick_rng = 0x87654321u;
  hist_t alloc_hist;
  hist_t free_hist;
  hist_t alloc_prev;
  hist_t free_prev;
  hist_t conte_alloc_hist;
  hist_t conte_free_hist;
  hist_t conte_alloc_prev;
  hist_t conte_free_prev;

  setvbuf(stdout, NULL, _IONBF, 0);
  apply_rt(parse_samples(getenv("MM_HIST_RT"), MM_HIST_RT),
    parse_samples(getenv("MM_HIST_RT_PRIO"), MM_HIST_RT_PRIO),
    parse_samples(getenv("MM_HIST_RT_CPU"), MM_HIST_RT_CPU));

  tlsf_t mm = mm_create_with_pool(pool, sizeof(pool));
  if (!mm) {
    printf("mm_create_with_pool failed\n");
    return 1;
  }

  conte_tlsf_t conte = conte_create_with_pool(conte_pool, sizeof(conte_pool));
  if (!conte) {
    printf("tlsf_create_with_pool failed\n");
    return 1;
  }

  hist_init(&alloc_hist);
  hist_init(&free_hist);
  hist_init(&alloc_prev);
  hist_init(&free_prev);
  hist_init(&conte_alloc_hist);
  hist_init(&conte_free_hist);
  hist_init(&conte_alloc_prev);
  hist_init(&conte_free_prev);

  uint64_t next_report = now_ns() + (uint64_t)report_ms * 1000000u;

  size_t i = 0;
  size_t frame = 0;
  while (samples == 0u || i < samples) {
    size_t burst = pick_between(&size_rng, burst_min, burst_max);
    size_t keep = pick_between(&size_rng, keep_min, keep_max);
    if (keep > burst) {
      keep = burst;
    }

    size_t frame_budget = frame_bytes;
    for (size_t op = 0; op < burst && (samples == 0u || i < samples); op++, i++) {
      if (frame_budget < sizeof(size_t)) {
        break;
      }

      if (live_count == MM_HIST_MAX_LIVE) {
        size_t pick = (size_t)(lcg_next(&pick_rng) % live_count);
        void* ptr = live_ptrs[pick];
        live_ptrs[pick] = live_ptrs[live_count - 1u];
        live_count -= 1u;

        uint64_t start = now_ns();
        mm_free(mm, ptr);
        uint64_t end = now_ns();
        hist_record(&free_hist, end - start);
      }

      if (conte_live_count == MM_HIST_MAX_LIVE) {
        size_t pick = (size_t)(lcg_next(&pick_rng) % conte_live_count);
        void* ptr = live_conte_ptrs[pick];
        live_conte_ptrs[pick] = live_conte_ptrs[conte_live_count - 1u];
        conte_live_count -= 1u;

        uint64_t start = now_ns();
        conte_free(conte, ptr);
        uint64_t end = now_ns();
        hist_record(&conte_free_hist, end - start);
      }

      size_t size = sizes[lcg_next(&size_rng) % (sizeof(sizes) / sizeof(sizes[0]))];
      size_t alloc_size = align_up(size, sizeof(size_t));
      if (alloc_size > frame_budget) {
        break;
      }

      uint64_t start = now_ns();
      void* ptr = mm_malloc(mm, size);
      uint64_t end = now_ns();
      hist_record(&alloc_hist, end - start);

      if (!ptr) {
        failures += 1u;
      } else {
        live_ptrs[live_count] = ptr;
        live_count += 1u;
      }

      start = now_ns();
      void* conte_ptr = conte_malloc(conte, size);
      end = now_ns();
      hist_record(&conte_alloc_hist, end - start);

      if (!conte_ptr) {
        conte_failures += 1u;
      } else {
        live_conte_ptrs[conte_live_count] = conte_ptr;
        conte_live_count += 1u;
      }

      frame_budget -= alloc_size;
    }

    while (live_count > keep) {
      size_t pick = (size_t)(lcg_next(&pick_rng) % live_count);
      void* ptr = live_ptrs[pick];
      live_ptrs[pick] = live_ptrs[live_count - 1u];
      live_count -= 1u;

      uint64_t start = now_ns();
      mm_free(mm, ptr);
      uint64_t end = now_ns();
      hist_record(&free_hist, end - start);
    }

    while (conte_live_count > keep) {
      size_t pick = (size_t)(lcg_next(&pick_rng) % conte_live_count);
      void* ptr = live_conte_ptrs[pick];
      live_conte_ptrs[pick] = live_conte_ptrs[conte_live_count - 1u];
      conte_live_count -= 1u;

      uint64_t start = now_ns();
      conte_free(conte, ptr);
      uint64_t end = now_ns();
      hist_record(&conte_free_hist, end - start);
    }

    frame += 1u;
    uint64_t now = now_ns();
    if (now >= next_report) {
      clear_screen();
      printf("memoman vs conte latency histogram (frame loop)\n");
      printf("pool=%u bytes samples=%zu frames=%zu max_live=%u report=%zums\n",
        (unsigned)sizeof(pool), samples, frame, (unsigned)MM_HIST_MAX_LIVE, report_ms);
      printf("frame_bytes=%zu burst=%zu..%zu keep=%zu..%zu\n", frame_bytes, burst_min, burst_max, keep_min, keep_max);
      print_progress("memoman ", samples, i, live_count, failures);
      hist_print_delta("memoman mm_malloc", &alloc_hist, &alloc_prev);
      hist_print_delta("memoman mm_free", &free_hist, &free_prev);
      print_progress("conte   ", samples, i, conte_live_count, conte_failures);
      hist_print_delta("conte tlsf_malloc", &conte_alloc_hist, &conte_alloc_prev);
      hist_print_delta("conte tlsf_free", &conte_free_hist, &conte_free_prev);

      hist_copy(&alloc_prev, &alloc_hist);
      hist_copy(&free_prev, &free_hist);
      hist_copy(&conte_alloc_prev, &conte_alloc_hist);
      hist_copy(&conte_free_prev, &conte_free_hist);
      next_report = now + (uint64_t)report_ms * 1000000u;
    }

  }

  while (live_count > 0u) {
    live_count -= 1u;
    uint64_t start = now_ns();
    mm_free(mm, live_ptrs[live_count]);
    uint64_t end = now_ns();
    hist_record(&free_hist, end - start);
  }

  while (conte_live_count > 0u) {
    conte_live_count -= 1u;
    uint64_t start = now_ns();
    conte_free(conte, live_conte_ptrs[conte_live_count]);
    uint64_t end = now_ns();
    hist_record(&conte_free_hist, end - start);
  }

  clear_screen();
  printf("memoman vs conte latency histogram (mixed sizes)\n");
  printf("pool=%u bytes samples=%zu max_live=%u report=%zums\n", (unsigned)sizeof(pool), samples, (unsigned)MM_HIST_MAX_LIVE, report_ms);
  print_progress("memoman ", samples, samples, 0u, failures);
  hist_print("memoman mm_malloc", &alloc_hist);
  hist_print("memoman mm_free", &free_hist);
  print_progress("conte   ", samples, samples, 0u, conte_failures);
  hist_print("conte tlsf_malloc", &conte_alloc_hist);
  hist_print("conte tlsf_free", &conte_free_hist);

  if (failures > 0u || conte_failures > 0u) {
    printf("\nalloc failures: memoman=%zu conte=%zu\n", failures, conte_failures);
  }

  if (!mm_validate(mm)) {
    printf("mm_validate failed\n");
    return 1;
  }

  if (conte_check(conte) != 0) {
    printf("tlsf_check failed\n");
    return 1;
  }

  mm_destroy(mm);
  conte_destroy(conte);
  return 0;
}
