#include "./src/memoman.h"

#include <stdint.h>
#include <stdio.h>

static int ptr_aligned(const void* p, size_t a) {
  return p && a && (((uintptr_t)p & (a - 1)) == 0);
}

static void print_alloc(const char* label, void* p, size_t req, size_t align) {
  if (!p) {
    printf("%s: NULL (req=%zu align=%zu)\n", label, req, align);
    return;
  }
  printf("%s: %p req=%zu align=%zu block=%zu aligned=%s\n",
    label, p, req, align, mm_block_size(p), ptr_aligned(p, align) ? "yes" : "no");
}

int main(void) {
  static uint8_t pool1[128 * 1024] __attribute__((aligned(16)));
  static uint8_t pool2[128 * 1024] __attribute__((aligned(16)));

  tlsf_t mm = mm_create_with_pool(pool1, sizeof(pool1));
  if (!mm) {
    printf("mm_create failed\n");
    return 1;
  }

  printf("memoman demo: TLSF 3.1 style pools + memalign + realloc\n");

  void* a = mm_malloc(mm, 24);
  void* b = mm_malloc(mm, 256);
  void* c = mm_memalign(mm, 4096, 128);

  print_alloc("a malloc", a, 24, sizeof(void*));
  print_alloc("b malloc", b, 256, sizeof(void*));
  print_alloc("c memalign", c, 128, 4096);

  if (!mm_validate(mm)) {
    printf("mm_validate failed after initial allocs\n");
    return 1;
  }

  printf("free(b)\n");
  mm_free(mm, b);

  printf("realloc(a, 1024)\n");
  a = mm_realloc(mm, a, 1024);
  print_alloc("a realloc", a, 1024, sizeof(void*));

  if (!mm_validate(mm)) {
    printf("mm_validate failed after free/realloc\n");
    return 1;
  }

  printf("add second pool\n");
  if (!mm_add_pool(mm, pool2, sizeof(pool2))) {
    printf("mm_add_pool failed\n");
    return 1;
  }

  void* d = mm_malloc(mm, 64 * 1024);
  print_alloc("d malloc (after add_pool)", d, 64 * 1024, sizeof(void*));

  mm_free(mm, a);
  mm_free(mm, c);
  mm_free(mm, d);

  if (!mm_validate(mm)) {
    printf("mm_validate failed at end\n");
    return 1;
  }

  mm_destroy(mm);
  printf("ok\n");
  return 0;
}
