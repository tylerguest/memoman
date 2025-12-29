#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memoman_test_internal.h"
#include "../examples/TLSF/matt_conte/tlsf.h"

typedef enum {
  OP_MALLOC = 0,
  OP_FREE = 1,
  OP_REALLOC = 2,
  OP_MEMALIGN = 3,
} op_kind_t;

typedef struct {
  op_kind_t kind;
  uint32_t slot;
  size_t size;
  size_t align;
} op_t;

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
    1, 2, 3, 4, 7, 8, 15, 16, 24, 31, 32, 48, 63, 64, 80, 96, 127, 128, 192, 255, 256,
    384, 512, 768, 1024, 1536, 2048, 3072, 4096, 8192, 16384,
  };
  return sizes[r % (sizeof(sizes) / sizeof(sizes[0]))];
}

static size_t pick_align(uint32_t r) {
  static const size_t aligns[] = {8, 16, 32, 64, 128, 256, 512, 1024, 4096};
  return aligns[r % (sizeof(aligns) / sizeof(aligns[0]))];
}

static void print_op(FILE* out, size_t i, const op_t* op) {
  const char* name = "UNKNOWN";
  switch (op->kind) {
    case OP_MALLOC: name = "MALLOC"; break;
    case OP_FREE: name = "FREE"; break;
    case OP_REALLOC: name = "REALLOC"; break;
    case OP_MEMALIGN: name = "MEMALIGN"; break;
  }

  if (op->kind == OP_FREE) {
    fprintf(out, "%zu %s slot=%u\n", i, name, op->slot);
  } else if (op->kind == OP_MEMALIGN) {
    fprintf(out, "%zu %s slot=%u size=%zu align=%zu\n", i, name, op->slot, op->size, op->align);
  } else {
    fprintf(out, "%zu %s slot=%u size=%zu\n", i, name, op->slot, op->size);
  }
}

static int ptr_aligned(const void* p, size_t a) {
  if (!p) return 1;
  if (a == 0) return 0;
  return ((uintptr_t)p & (a - 1)) == 0;
}

static void fill_pattern(void* p, size_t bytes, uint8_t pat) {
  if (!p || !bytes) return;
  const size_t n = bytes > 64 ? 64 : bytes;
  memset(p, pat, n);
}

static int check_pattern(const void* p, size_t bytes, uint8_t pat) {
  if (!p || !bytes) return 1;
  const size_t n = bytes > 64 ? 64 : bytes;
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; i++) {
    if (b[i] != pat) return 0;
  }
  return 1;
}

typedef struct {
  size_t free_bytes;
  size_t used_bytes;
  size_t max_free;
  size_t blocks;
  size_t free_blocks;
} heap_stats_t;

static heap_stats_t mm_collect_stats(mm_allocator_t* mm, size_t mm_bytes) {
  heap_stats_t st = {0};
  if (!mm || mm_bytes < sizeof(mm_allocator_t)) return st;

  uintptr_t pool_base = (uintptr_t)mm + sizeof(mm_allocator_t);
  pool_base = (pool_base + (ALIGNMENT - 1)) & ~(uintptr_t)(ALIGNMENT - 1);
  tlsf_block_t* block = (tlsf_block_t*)pool_base;

  for (size_t i = 0; i < 100000; i++) {
    const size_t sz = block->size & TLSF_SIZE_MASK;
    if (sz == 0) break; /* epilogue */

    st.blocks++;
    if (block->size & TLSF_BLOCK_FREE) {
      st.free_blocks++;
      st.free_bytes += sz;
      if (sz > st.max_free) st.max_free = sz;
    } else {
      st.used_bytes += sz;
    }

    block = (tlsf_block_t*)((char*)block + BLOCK_HEADER_OVERHEAD + sz);
  }

  return st;
}

static void mm_dump_search_debug(mm_allocator_t* mm, size_t size, size_t align) {
  if (!mm) return;

  /* Mirror memoman's memalign search sizing. */
  size_t req = size;
  if (req < TLSF_MIN_BLOCK_SIZE) req = TLSF_MIN_BLOCK_SIZE;
  req = (req + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);

  const size_t gap_minimum = BLOCK_HEADER_OVERHEAD + TLSF_MIN_BLOCK_SIZE;
  size_t search = 0;
  if (req <= SIZE_MAX - align && (req + align) <= SIZE_MAX - gap_minimum) {
    const size_t with_gap = req + align + gap_minimum;
    search = (with_gap + (align - 1)) & ~(align - 1);
  }

  int fl = -1, sl = -1;
  if (search) mm_get_mapping_search_indices(search, &fl, &sl);

  fprintf(stderr, "memoman debug: align=%zu size=%zu search=%zu fl=%d sl=%d fl_bitmap=0x%08x\n",
    align, size, search, fl, sl, mm->fl_bitmap);
  if (fl >= 0 && fl < TLSF_FLI_MAX) {
    fprintf(stderr, "memoman debug: sl_bitmap[%d]=0x%08x\n", fl, mm->sl_bitmap[fl]);
  }
  for (int i = 0; i < TLSF_FLI_MAX; i++) {
    if (mm->fl_bitmap & (1U << i)) {
      fprintf(stderr, "memoman debug: fl=%d sl_bitmap=0x%08x\n", i, mm->sl_bitmap[i]);
    }
  }
}

static void tlsf_stats_walker(void* ptr, size_t size, int used, void* user) {
  (void)ptr;
  heap_stats_t* st = (heap_stats_t*)user;
  st->blocks++;
  if (!used) {
    st->free_blocks++;
    st->free_bytes += size;
    if (size > st->max_free) st->max_free = size;
  } else {
    st->used_bytes += size;
  }
}

static heap_stats_t tlsf_collect_stats(tlsf_t tlsf) {
  heap_stats_t st = {0};
  if (!tlsf) return st;
  pool_t pool = tlsf_get_pool(tlsf);
  tlsf_walk_pool(pool, tlsf_stats_walker, &st);
  return st;
}

static void* alloc_base_for_user_mod(void* raw, size_t raw_bytes, size_t control_bytes, size_t mod, size_t desired_mod) {
  uintptr_t base = (uintptr_t)raw;
  base = (base + 7u) & ~(uintptr_t)7u;
  const size_t want = desired_mod % mod;
  const size_t have = (size_t)((base + control_bytes + sizeof(size_t)) % mod);
  size_t pad = (want + mod - have) % mod;
  /* All inputs are 8-aligned, so pad is also 8-aligned. */
  if (pad > raw_bytes) return NULL;
  return (void*)(base + pad);
}

static int run_prefix(
  int strict,
  size_t prefix_len,
  uint32_t seed,
  size_t slots,
  size_t pool_payload,
  size_t desired_user_mod,
  const op_t* ops,
  size_t fail_out[1],
  char fail_msg[256]
) {
  const size_t mod = 4096;
  const size_t mm_bytes = sizeof(mm_allocator_t) + (2 * sizeof(size_t)) + pool_payload;
  const size_t tlsf_bytes = tlsf_size() + tlsf_pool_overhead() + pool_payload;

  void* mm_raw = malloc(mm_bytes + mod);
  void* tlsf_raw = malloc(tlsf_bytes + mod);
  if (!mm_raw || !tlsf_raw) {
    snprintf(fail_msg, 256, "pool malloc failed");
    if (mm_raw) free(mm_raw);
    if (tlsf_raw) free(tlsf_raw);
    return 0;
  }

  void* mm_pool = alloc_base_for_user_mod(mm_raw, mm_bytes + mod, sizeof(mm_allocator_t), mod, desired_user_mod);
  void* tlsf_pool = alloc_base_for_user_mod(tlsf_raw, tlsf_bytes + mod, tlsf_size(), mod, desired_user_mod);
  if (!mm_pool || !tlsf_pool) {
    snprintf(fail_msg, 256, "pool alignment failed");
    free(mm_raw);
    free(tlsf_raw);
    return 0;
  }
  if ((uintptr_t)mm_pool + mm_bytes > (uintptr_t)mm_raw + mm_bytes + mod) {
    snprintf(fail_msg, 256, "mm pool overflow");
    free(mm_raw);
    free(tlsf_raw);
    return 0;
  }
  if ((uintptr_t)tlsf_pool + tlsf_bytes > (uintptr_t)tlsf_raw + tlsf_bytes + mod) {
    snprintf(fail_msg, 256, "tlsf pool overflow");
    free(mm_raw);
    free(tlsf_raw);
    return 0;
  }

  mm_allocator_t* mm = mm_create(mm_pool, mm_bytes);
  tlsf_t tlsf = tlsf_create_with_pool(tlsf_pool, tlsf_bytes);
  if (!mm || !tlsf) {
    snprintf(fail_msg, 256, "allocator create failed");
    free(mm_raw);
    free(tlsf_raw);
    return 0;
  }

  void** mm_ptrs = calloc(slots, sizeof(void*));
  void** tlsf_ptrs = calloc(slots, sizeof(void*));
  size_t* mm_req = calloc(slots, sizeof(size_t));
  size_t* tlsf_req = calloc(slots, sizeof(size_t));
  uint8_t* mm_pat = calloc(slots, sizeof(uint8_t));
  uint8_t* tlsf_pat = calloc(slots, sizeof(uint8_t));
  if (!mm_ptrs || !tlsf_ptrs || !mm_req || !tlsf_req || !mm_pat || !tlsf_pat) {
    snprintf(fail_msg, 256, "calloc failed");
    free(mm_ptrs); free(tlsf_ptrs); free(mm_req); free(tlsf_req); free(mm_pat); free(tlsf_pat);
    tlsf_destroy(tlsf);
    free(mm_raw);
    free(tlsf_raw);
    return 0;
  }

  for (size_t i = 0; i < prefix_len; i++) {
    const op_t* op = &ops[i];
    const uint32_t s = op->slot % (uint32_t)slots;

    if (mm_ptrs[s] && !check_pattern(mm_ptrs[s], mm_req[s], mm_pat[s])) {
      *fail_out = i;
      snprintf(fail_msg, 256, "memoman pattern mismatch (slot=%u)", s);
      goto fail;
    }
    if (tlsf_ptrs[s] && !check_pattern(tlsf_ptrs[s], tlsf_req[s], tlsf_pat[s])) {
      *fail_out = i;
      snprintf(fail_msg, 256, "conte pattern mismatch (slot=%u)", s);
      goto fail;
    }

    if (op->kind == OP_FREE) {
      if (strict && (!!mm_ptrs[s] != !!tlsf_ptrs[s])) {
        *fail_out = i;
        snprintf(fail_msg, 256, "free slot mismatch (mm=%p conte=%p slot=%u)", mm_ptrs[s], tlsf_ptrs[s], s);
        goto fail;
      }
      if (mm_ptrs[s]) { (mm_free)(mm, mm_ptrs[s]); mm_ptrs[s] = NULL; }
      if (tlsf_ptrs[s]) { tlsf_free(tlsf, tlsf_ptrs[s]); tlsf_ptrs[s] = NULL; }
      mm_req[s] = 0; tlsf_req[s] = 0;
      mm_pat[s] = 0; tlsf_pat[s] = 0;
    } else if (op->kind == OP_MALLOC) {
      if (strict && (!!mm_ptrs[s] != !!tlsf_ptrs[s])) {
        *fail_out = i;
        snprintf(fail_msg, 256, "malloc slot mismatch (mm=%p conte=%p slot=%u)", mm_ptrs[s], tlsf_ptrs[s], s);
        goto fail;
      }
      if (mm_ptrs[s] || tlsf_ptrs[s]) continue;

      void* mp = (mm_malloc)(mm, op->size);
      void* tp = tlsf_malloc(tlsf, op->size);

      if (strict && (!!mp != !!tp)) {
        heap_stats_t ms = mm_collect_stats(mm, mm_bytes);
        heap_stats_t ts = tlsf_collect_stats(tlsf);
        fprintf(stderr, "strict malloc mismatch stats: mm free=%zu max_free=%zu used=%zu blocks=%zu\n",
          ms.free_bytes, ms.max_free, ms.used_bytes, ms.blocks);
        fprintf(stderr, "strict malloc mismatch stats: tlsf free=%zu max_free=%zu used=%zu blocks=%zu\n",
          ts.free_bytes, ts.max_free, ts.used_bytes, ts.blocks);
        *fail_out = i;
        snprintf(fail_msg, 256, "malloc success mismatch (mm=%p conte=%p size=%zu slot=%u)", mp, tp, op->size, s);
        goto fail;
      }

      const uint8_t pat = (uint8_t)(xorshift32(&seed) & 0xff);

      if (mp) {
        const size_t mu = (mm_usable_size)(mm, mp);
        if (mu < op->size) {
          *fail_out = i;
          snprintf(fail_msg, 256, "memoman usable too small (mu=%zu req=%zu)", mu, op->size);
          goto fail;
        }
        if (!ptr_aligned(mp, sizeof(void*))) {
          *fail_out = i;
          snprintf(fail_msg, 256, "memoman malloc alignment mismatch");
          goto fail;
        }

        mm_ptrs[s] = mp;
        mm_req[s] = op->size;
        mm_pat[s] = pat;
        fill_pattern(mm_ptrs[s], mm_req[s], mm_pat[s]);
      }

      if (tp) {
        const size_t tu = tlsf_block_size(tp);
        if (tu < op->size) {
          *fail_out = i;
          snprintf(fail_msg, 256, "conte usable too small (tu=%zu req=%zu)", tu, op->size);
          goto fail;
        }
        if (!ptr_aligned(tp, sizeof(void*))) {
          *fail_out = i;
          snprintf(fail_msg, 256, "conte malloc alignment mismatch");
          goto fail;
        }

        tlsf_ptrs[s] = tp;
        tlsf_req[s] = op->size;
        tlsf_pat[s] = pat;
        fill_pattern(tlsf_ptrs[s], tlsf_req[s], tlsf_pat[s]);
      }

      if (strict && mp && tp) {
        const size_t mu = (mm_usable_size)(mm, mp);
        const size_t tu = tlsf_block_size(tp);
        if (mu != tu) {
          *fail_out = i;
          snprintf(fail_msg, 256, "usable mismatch (mm=%zu conte=%zu req=%zu)", mu, tu, op->size);
          goto fail;
        }
      }
    } else if (op->kind == OP_MEMALIGN) {
      if (strict && (!!mm_ptrs[s] != !!tlsf_ptrs[s])) {
        *fail_out = i;
        snprintf(fail_msg, 256, "memalign slot mismatch (mm=%p conte=%p slot=%u)", mm_ptrs[s], tlsf_ptrs[s], s);
        goto fail;
      }
      if (mm_ptrs[s] || tlsf_ptrs[s]) continue;

      void* mp = (mm_memalign)(mm, op->align, op->size);
      void* tp = tlsf_memalign(tlsf, op->align, op->size);

      if (strict && (!!mp != !!tp)) {
        heap_stats_t ms = mm_collect_stats(mm, mm_bytes);
        heap_stats_t ts = tlsf_collect_stats(tlsf);
        fprintf(stderr, "strict memalign mismatch stats: mm free=%zu max_free=%zu used=%zu blocks=%zu\n",
          ms.free_bytes, ms.max_free, ms.used_bytes, ms.blocks);
        fprintf(stderr, "strict memalign mismatch stats: tlsf free=%zu max_free=%zu used=%zu blocks=%zu\n",
          ts.free_bytes, ts.max_free, ts.used_bytes, ts.blocks);
        mm_dump_search_debug(mm, op->size, op->align);
        *fail_out = i;
        snprintf(fail_msg, 256, "memalign success mismatch (mm=%p conte=%p size=%zu align=%zu slot=%u)", mp, tp, op->size, op->align, s);
        goto fail;
      }

      const uint8_t pat = (uint8_t)(xorshift32(&seed) & 0xff);

      if (mp) {
        const size_t mu = (mm_usable_size)(mm, mp);
        if (mu < op->size) {
          *fail_out = i;
          snprintf(fail_msg, 256, "memoman memalign usable too small (mu=%zu req=%zu)", mu, op->size);
          goto fail;
        }
        if (!ptr_aligned(mp, op->align)) {
          *fail_out = i;
          snprintf(fail_msg, 256, "memoman memalign alignment mismatch (align=%zu)", op->align);
          goto fail;
        }

        mm_ptrs[s] = mp;
        mm_req[s] = op->size;
        mm_pat[s] = pat;
        fill_pattern(mm_ptrs[s], mm_req[s], mm_pat[s]);
      }

      if (tp) {
        const size_t tu = tlsf_block_size(tp);
        if (tu < op->size) {
          *fail_out = i;
          snprintf(fail_msg, 256, "conte memalign usable too small (tu=%zu req=%zu)", tu, op->size);
          goto fail;
        }
        if (!ptr_aligned(tp, op->align)) {
          *fail_out = i;
          snprintf(fail_msg, 256, "conte memalign alignment mismatch (align=%zu)", op->align);
          goto fail;
        }

        tlsf_ptrs[s] = tp;
        tlsf_req[s] = op->size;
        tlsf_pat[s] = pat;
        fill_pattern(tlsf_ptrs[s], tlsf_req[s], tlsf_pat[s]);
      }

      if (strict && mp && tp) {
        const size_t mu = (mm_usable_size)(mm, mp);
        const size_t tu = tlsf_block_size(tp);
        if (mu != tu) {
          *fail_out = i;
          snprintf(fail_msg, 256, "memalign usable mismatch (mm=%zu conte=%zu req=%zu)", mu, tu, op->size);
          goto fail;
        }
      }
    } else if (op->kind == OP_REALLOC) {
      if (strict && (!!mm_ptrs[s] != !!tlsf_ptrs[s])) {
        *fail_out = i;
        snprintf(fail_msg, 256, "realloc slot mismatch (mm=%p conte=%p slot=%u)", mm_ptrs[s], tlsf_ptrs[s], s);
        goto fail;
      }
      if (!mm_ptrs[s] && !tlsf_ptrs[s]) continue;

      const uint8_t new_pat = (uint8_t)(xorshift32(&seed) & 0xff);

      void* mp = NULL;
      void* tp = NULL;
      size_t mm_old_req = mm_req[s];
      size_t tlsf_old_req = tlsf_req[s];
      uint8_t mm_old_pat = mm_pat[s];
      uint8_t tlsf_old_pat = tlsf_pat[s];

      if (mm_ptrs[s]) mp = (mm_realloc)(mm, mm_ptrs[s], op->size);
      if (tlsf_ptrs[s]) tp = tlsf_realloc(tlsf, tlsf_ptrs[s], op->size);

      if (strict && (!!mp != !!tp)) {
        heap_stats_t ms = mm_collect_stats(mm, mm_bytes);
        heap_stats_t ts = tlsf_collect_stats(tlsf);
        fprintf(stderr, "strict realloc mismatch stats: mm free=%zu max_free=%zu used=%zu blocks=%zu\n",
          ms.free_bytes, ms.max_free, ms.used_bytes, ms.blocks);
        fprintf(stderr, "strict realloc mismatch stats: tlsf free=%zu max_free=%zu used=%zu blocks=%zu\n",
          ts.free_bytes, ts.max_free, ts.used_bytes, ts.blocks);
        *fail_out = i;
        snprintf(fail_msg, 256, "realloc success mismatch (mm=%p conte=%p new=%zu)", mp, tp, op->size);
        goto fail;
      }

      if (mm_ptrs[s] && !mp) {
        if (!check_pattern(mm_ptrs[s], mm_old_req, mm_old_pat)) {
          *fail_out = i;
          snprintf(fail_msg, 256, "memoman realloc fail corrupted old contents");
          goto fail;
        }
      }
      if (tlsf_ptrs[s] && !tp) {
        if (!check_pattern(tlsf_ptrs[s], tlsf_old_req, tlsf_old_pat)) {
          *fail_out = i;
          snprintf(fail_msg, 256, "conte realloc fail corrupted old contents");
          goto fail;
        }
      }

      if (mp) {
        const size_t mu = (mm_usable_size)(mm, mp);
        if (mu < op->size) {
          *fail_out = i;
          snprintf(fail_msg, 256, "memoman realloc usable too small (mu=%zu req=%zu)", mu, op->size);
          goto fail;
        }

        const size_t preserved = mm_old_req < op->size ? mm_old_req : op->size;
        if (!check_pattern(mp, preserved, mm_old_pat)) {
          *fail_out = i;
          snprintf(fail_msg, 256, "memoman realloc data mismatch (preserve=%zu)", preserved);
          goto fail;
        }

        mm_ptrs[s] = mp;
        mm_req[s] = op->size;
        mm_pat[s] = new_pat;
        fill_pattern(mm_ptrs[s], mm_req[s], mm_pat[s]);
      }

      if (tp) {
        const size_t tu = tlsf_block_size(tp);
        if (tu < op->size) {
          *fail_out = i;
          snprintf(fail_msg, 256, "conte realloc usable too small (tu=%zu req=%zu)", tu, op->size);
          goto fail;
        }

        const size_t preserved = tlsf_old_req < op->size ? tlsf_old_req : op->size;
        if (!check_pattern(tp, preserved, tlsf_old_pat)) {
          *fail_out = i;
          snprintf(fail_msg, 256, "conte realloc data mismatch (preserve=%zu)", preserved);
          goto fail;
        }

        tlsf_ptrs[s] = tp;
        tlsf_req[s] = op->size;
        tlsf_pat[s] = new_pat;
        fill_pattern(tlsf_ptrs[s], tlsf_req[s], tlsf_pat[s]);
      }

      if (strict && mp && tp) {
        const size_t mu = (mm_usable_size)(mm, mp);
        const size_t tu = tlsf_block_size(tp);
        if (mu != tu) {
          *fail_out = i;
          snprintf(fail_msg, 256, "realloc usable mismatch (mm=%zu conte=%zu req=%zu)", mu, tu, op->size);
          goto fail;
        }
      }
    }

    if ((i % 256) == 0) {
      if (!(mm_validate)(mm)) {
        *fail_out = i;
        snprintf(fail_msg, 256, "mm_validate failed");
        goto fail;
      }
      if (tlsf_check(tlsf) != 0) {
        *fail_out = i;
        snprintf(fail_msg, 256, "tlsf_check failed");
        goto fail;
      }
    }
  }

  if (!(mm_validate)(mm)) {
    *fail_out = prefix_len;
    snprintf(fail_msg, 256, "mm_validate failed at end");
    goto fail;
  }
  if (tlsf_check(tlsf) != 0) {
    *fail_out = prefix_len;
    snprintf(fail_msg, 256, "tlsf_check failed at end");
    goto fail;
  }

  for (size_t s = 0; s < slots; s++) {
    if (mm_ptrs[s]) (mm_free)(mm, mm_ptrs[s]);
    if (tlsf_ptrs[s]) tlsf_free(tlsf, tlsf_ptrs[s]);
  }
  free(mm_ptrs); free(tlsf_ptrs); free(mm_req); free(tlsf_req); free(mm_pat); free(tlsf_pat);
  tlsf_destroy(tlsf);
  free(mm_raw);
  free(tlsf_raw);
  return 1;

fail:
  for (size_t s = 0; s < slots; s++) {
    if (mm_ptrs[s]) (mm_free)(mm, mm_ptrs[s]);
    if (tlsf_ptrs[s]) tlsf_free(tlsf, tlsf_ptrs[s]);
  }
  free(mm_ptrs); free(tlsf_ptrs); free(mm_req); free(tlsf_req); free(mm_pat); free(tlsf_pat);
  tlsf_destroy(tlsf);
  free(mm_raw);
  free(tlsf_raw);
  return 0;
}

int main(void) {
  uint32_t seed = 0xC0FFEEu;
  size_t slots = 256;
  size_t steps = 20000;
  size_t pool_payload = 8u * 1024u * 1024u;

  const char* env_seed = getenv("MM_PARITY_SEED");
  const char* env_steps = getenv("MM_PARITY_STEPS");
  const char* env_slots = getenv("MM_PARITY_SLOTS");
  const char* env_pool = getenv("MM_PARITY_POOL");

  if (env_seed && *env_seed) seed = (uint32_t)strtoul(env_seed, NULL, 0);
  if (env_steps && *env_steps) steps = (size_t)strtoull(env_steps, NULL, 0);
  if (env_slots && *env_slots) slots = (size_t)strtoull(env_slots, NULL, 0);
  if (env_pool && *env_pool) pool_payload = (size_t)strtoull(env_pool, NULL, 0);

  if (slots == 0 || steps == 0) return 1;
  pool_payload &= ~(size_t)7u;
  if (pool_payload < (64u * 1024u)) return 1;

  op_t* ops = calloc(steps, sizeof(op_t));
  if (!ops) return 1;

  uint32_t rng = seed;
  for (size_t i = 0; i < steps; i++) {
    const uint32_t r = xorshift32(&rng);
    const uint32_t slot = r % (uint32_t)slots;
    const uint32_t k = (r >> 8) & 3u;

    op_t op;
    op.slot = slot;
    op.align = 0;
    op.size = 0;

    if (k == 0) {
      op.kind = OP_FREE;
    } else if (k == 1) {
      op.kind = OP_REALLOC;
      op.size = pick_size(xorshift32(&rng));
    } else if (k == 2) {
      op.kind = OP_MEMALIGN;
      op.size = pick_size(xorshift32(&rng));
      op.align = pick_align(xorshift32(&rng));
    } else {
      op.kind = OP_MALLOC;
      op.size = pick_size(xorshift32(&rng));
    }

    ops[i] = op;
  }

  size_t fail_at = 0;
  char fail_msg[256] = {0};
  int strict = 1;
  const char* env_strict = getenv("MM_PARITY_STRICT");
  if (env_strict && *env_strict) strict = atoi(env_strict) != 0;

  size_t desired_mod = 0;
  int ok = run_prefix(strict, steps, seed, slots, pool_payload, desired_mod, ops, &fail_at, fail_msg);
  if (!ok) {
    fprintf(stderr, "parity failure: %s\n", fail_msg);
    fprintf(stderr, "repro: seed=0x%08x slots=%zu pool_payload=%zu user_mod=%zu steps=%zu strict=%d\n",
      seed, slots, pool_payload, desired_mod, fail_at + 1, strict);
    fprintf(stderr, "ops (prefix=%zu):\n", fail_at + 1);
    for (size_t i = 0; i <= fail_at; i++) {
      print_op(stderr, i, &ops[i]);
    }
    free(ops);
    return 1;
  }

  desired_mod = 24;
  ok = run_prefix(strict, steps, seed, slots, pool_payload, desired_mod, ops, &fail_at, fail_msg);
  if (ok) {
    free(ops);
    return 0;
  }

  fprintf(stderr, "parity failure: %s\n", fail_msg);
  fprintf(stderr, "repro: seed=0x%08x slots=%zu pool_payload=%zu user_mod=%zu steps=%zu strict=%d\n",
    seed, slots, pool_payload, desired_mod, fail_at + 1, strict);
  fprintf(stderr, "ops (prefix=%zu):\n", fail_at + 1);
  for (size_t i = 0; i <= fail_at; i++) {
    print_op(stderr, i, &ops[i]);
  }

  free(ops);
  return 1;
}
