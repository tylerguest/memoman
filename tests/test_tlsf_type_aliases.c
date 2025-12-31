#include "test_framework.h"
#include "../src/memoman.h"

#include <stdint.h>

static int test_tlsf_t_and_pool_t_exist_and_work_with_memoman_api(void) {
  uint8_t backing[64 * 1024] __attribute__((aligned(16)));

  tlsf_t tlsf = mm_create_with_pool(backing, sizeof(backing));
  ASSERT_NOT_NULL(tlsf);

  pool_t pool = mm_get_pool(tlsf);
  ASSERT_NOT_NULL(pool);
  ASSERT((mm_validate_pool)(tlsf, pool));

  void* p = (mm_malloc)(tlsf, 128);
  ASSERT_NOT_NULL(p);
  (mm_free)(tlsf, p);
  ASSERT((mm_validate)(tlsf));
  return 1;
}

int main(void) {
  TEST_SUITE_BEGIN("tlsf_type_aliases");
  RUN_TEST(test_tlsf_t_and_pool_t_exist_and_work_with_memoman_api);
  TEST_SUITE_END();
  TEST_MAIN_END();
}
