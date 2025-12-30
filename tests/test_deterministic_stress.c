#include "test_framework.h"
#include "../src/memoman.h"

#include <stdint.h>
#include <stdio.h>

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

static int test_deterministic_stress(void) {
  const char* env_seed = getenv("MM_STRESS_SEED");
  const char* env_steps = getenv("MM_STRESS_STEPS");
  const char* env_slots = getenv("MM_STRESS_SLOTS");
  const char* env_validate = getenv("MM_STRESS_VALIDATE_SHIFT");

  uint32_t seed = 0x12345678u;
  size_t steps =
#ifdef MM_DEBUG
    5000;
#else
    20000;
#endif
  size_t slots_n = 512;
  unsigned validate_shift =
#ifdef MM_DEBUG
    18; /* explicit mm_validate every 262144 ops (MM_DEBUG already validates periodically) */
#else
    12; /* explicit mm_validate every 4096 ops */
#endif

  if (env_seed && *env_seed) seed = (uint32_t)strtoul(env_seed, NULL, 0);
  if (env_steps && *env_steps) steps = (size_t)strtoull(env_steps, NULL, 0);
  if (env_slots && *env_slots) slots_n = (size_t)strtoull(env_slots, NULL, 0);
  if (env_validate && *env_validate) validate_shift = (unsigned)strtoul(env_validate, NULL, 0);

  if (slots_n == 0) slots_n = 1;
  if (steps == 0) steps = 1;
  if (validate_shift > 30) validate_shift = 30;

  slot_t* slots = (slot_t*)calloc(slots_n, sizeof(slot_t));
  ASSERT_NOT_NULL(slots);

  uint32_t rng = seed;
  for (size_t i = 0; i < steps; i++) {
    uint32_t r = xorshift32(&rng);
    op_kind_t op = (op_kind_t)(r & 3u);
    size_t idx = (size_t)((r >> 2) % (uint32_t)slots_n);
    slot_t* s = &slots[idx];

    if (s->ptr) {
      if (!check_pattern(s->ptr, s->req, s->pat)) {
        printf("[FAIL] pattern mismatch at step=%zu slot=%zu ptr=%p req=%zu\n", i, idx, s->ptr, s->req);
        free(slots);
        return 0;
      }
    }

    if (op == OP_FREE) {
      if (s->ptr) {
        mm_free(s->ptr);
        s->ptr = NULL;
        s->req = 0;
        s->align = 0;
        s->pat = 0;
      }
    } else if (op == OP_MALLOC) {
      if (!s->ptr) {
        size_t req = pick_size(xorshift32(&rng));
        void* p = mm_malloc(req);
        if (p) {
          ASSERT_NOT_NULL(mm_get_pool_for_ptr(sys_allocator, p));
          ASSERT(ptr_aligned(p, sizeof(void*)));
          ASSERT_GE((mm_block_size)(p), req);
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
        void* p = (mm_memalign)(sys_allocator, a, req);
        if (p) {
          ASSERT_NOT_NULL(mm_get_pool_for_ptr(sys_allocator, p));
          ASSERT(ptr_aligned(p, a));
          ASSERT_GE((mm_block_size)(p), req);
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
        void* p = mm_realloc(NULL, new_req);
        if (p) {
          ASSERT_NOT_NULL(mm_get_pool_for_ptr(sys_allocator, p));
          ASSERT(ptr_aligned(p, sizeof(void*)));
          ASSERT_GE((mm_block_size)(p), new_req);
          s->ptr = p;
          s->req = new_req;
          s->align = sizeof(void*);
          s->pat = (uint8_t)(xorshift32(&rng) & 0xff);
          fill_pattern(s->ptr, s->req, s->pat);
        }
      } else if (new_req == 0) {
        void* p = mm_realloc(s->ptr, 0);
        ASSERT_NULL(p);
        s->ptr = NULL;
        s->req = 0;
        s->align = 0;
        s->pat = 0;
      } else {
        void* old = s->ptr;
        size_t old_req = s->req;
        uint8_t old_pat = s->pat;

        void* p = mm_realloc(old, new_req);
        if (!p) {
          ASSERT(check_pattern(old, old_req, old_pat));
        } else {
          ASSERT_NOT_NULL(mm_get_pool_for_ptr(sys_allocator, p));
          ASSERT(ptr_aligned(p, sizeof(void*)));
          ASSERT_GE((mm_block_size)(p), new_req);

          size_t preserved = old_req < new_req ? old_req : new_req;
          ASSERT(check_pattern(p, preserved, old_pat));

          s->ptr = p;
          s->req = new_req;
          s->align = sizeof(void*);
          s->pat = (uint8_t)(xorshift32(&rng) & 0xff);
          fill_pattern(s->ptr, s->req, s->pat);
        }
      }
    }

    if (((i + 1) & (((size_t)1u << validate_shift) - 1u)) == 0) {
      if (!mm_validate()) {
        printf("[FAIL] mm_validate failed at step=%zu seed=0x%08x slots=%zu\n", i, seed, slots_n);
        free(slots);
        return 0;
      }
    }
  }

  for (size_t i = 0; i < slots_n; i++) {
    if (slots[i].ptr) mm_free(slots[i].ptr);
  }

  ASSERT(mm_validate());
  free(slots);
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("deterministic_stress");
  RUN_TEST(test_deterministic_stress);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
