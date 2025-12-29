#include "test_framework.h"
#include "memoman_test_internal.h"

static int ref_fls_u32(unsigned int word) {
  if (!word) return -1;
  int bit = 31;
  while ((word & (1U << bit)) == 0) bit--;
  return bit;
}

static int ref_fls_sizet(size_t size) {
  if (!size) return -1;
#if SIZE_MAX > 0xffffffffu
  unsigned int high = (unsigned int)(size >> 32);
  if (high) return 32 + ref_fls_u32(high);
  return ref_fls_u32((unsigned int)(size & 0xffffffffu));
#else
  return ref_fls_u32((unsigned int)size);
#endif
}

/* Reference mapping (ported from Matthew Conte TLSF 3.1). */
static void conte_mapping_insert(size_t size, int* fli, int* sli) {
  const size_t small_block_size = (size_t)1 << FL_INDEX_SHIFT;
  int fl, sl;

  if (size < small_block_size) {
    fl = 0;
    sl = (int)size / (int)(small_block_size / SL_INDEX_COUNT);
  } else {
    fl = ref_fls_sizet(size);
    sl = (int)(size >> (fl - SL_INDEX_COUNT_LOG2)) ^ (1 << SL_INDEX_COUNT_LOG2);
    fl -= (FL_INDEX_SHIFT - 1);
  }

  *fli = fl;
  *sli = sl;
}

static void conte_mapping_search(size_t size, int* fli, int* sli) {
  const size_t small_block_size = (size_t)1 << FL_INDEX_SHIFT;
  if (size >= small_block_size) {
    const int fl = ref_fls_sizet(size);
    const size_t round = ((size_t)1 << (fl - SL_INDEX_COUNT_LOG2)) - 1;
    if (size <= SIZE_MAX - round) size += round;
  }
  conte_mapping_insert(size, fli, sli);
}

static int check_mapping_insert(size_t size) {
  int got_fl, got_sl;
  int exp_fl, exp_sl;

  mm_get_mapping_indices(size, &got_fl, &got_sl);
  conte_mapping_insert(size, &exp_fl, &exp_sl);

  if (got_fl != exp_fl || got_sl != exp_sl) {
    printf("    [FAIL] insert size %zu: got (%d,%d) expected (%d,%d)\n", size, got_fl, got_sl, exp_fl, exp_sl);
    return 0;
  }
  return 1;
}

static int check_mapping_search(size_t size) {
  int got_fl, got_sl;
  int exp_fl, exp_sl;

  mm_get_mapping_search_indices(size, &got_fl, &got_sl);
  conte_mapping_search(size, &exp_fl, &exp_sl);

  if (got_fl != exp_fl || got_sl != exp_sl) {
    printf("    [FAIL] search size %zu: got (%d,%d) expected (%d,%d)\n", size, got_fl, got_sl, exp_fl, exp_sl);
    return 0;
  }
  return 1;
}

static int test_small_blocks(void) {
  /* Exact parity spot checks (insert + search). */
  static const size_t sizes[] = {0, 1, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    if (!check_mapping_insert(sizes[i])) return 0;
    if (!check_mapping_search(sizes[i])) return 0;
  }
  return 1;
}

static int test_large_blocks(void) {
  static const size_t sizes[] = {256, 257, 511, 512, 513, 1023, 1024, 4095, 4096};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    if (!check_mapping_insert(sizes[i])) return 0;
    if (!check_mapping_search(sizes[i])) return 0;
  }
  return 1;
}

static int test_automated_coverage(void) {
  /* Sweep sizes deterministically and compare to Conte mapping. */
  for (size_t s = 0; s < 1024 * 1024; s += (s < 512 ? 1 : 64)) {
    if (!check_mapping_insert(s)) return 0;
    if (!check_mapping_search(s)) return 0;
  }
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("TLSF Mapping Parity");
  RUN_TEST(test_small_blocks);
  RUN_TEST(test_large_blocks);
  RUN_TEST(test_automated_coverage);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
