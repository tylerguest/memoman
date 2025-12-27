#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ANSI Color Codes */
#define COLOR_GREEN   "\033[32m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RESET   "\033[0m"

#define PASS_TAG COLOR_GREEN "[PASS]" COLOR_RESET
#define FAIL_TAG COLOR_RED "[FAIL]" COLOR_RESET

/* Test result tracking */
static int _tests_run = 0;
static int _tests_passed = 0;
static int _tests_failed = 0;

/* Assertion macros - return 0 on failure */
#define ASSERT(cond) do { \
  if (!(cond)) { \
    printf(FAIL_TAG " %s (line %d)\n", #cond, __LINE__); \
    return 0; \
  } \
} while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_NULL(p) ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)
#define ASSERT_GE(a, b) ASSERT((a) >= (b))
#define ASSERT_GT(a, b) ASSERT((a) > (b))
#define ASSERT_LE(a, b) ASSERT((a) <= (b))
#define ASSERT_LT(a, b) ASSERT((a) < (b))

/* Run a single test function */
#define RUN_TEST(fn) do { \
  _tests_run++; \
  if (fn()) { \
    _tests_passed++; \
    printf(PASS_TAG " %s\n", #fn); \
  } else { \
    _tests_failed++; \
  } \
} while(0)

/* Common test sizes covering TLSF and large block paths */
static const size_t TEST_SIZES[] = {1, 16, 64, 256, 1024, 4096, 65536};
#define TEST_SIZES_COUNT (sizeof(TEST_SIZES) / sizeof(TEST_SIZES[0]))

/* Large sizes that trigger mmap bypass (>= 1MB) */
static const size_t TEST_LARGE_SIZES[] = {1024*1024, 2*1024*1024, 4*1024*1024};
#define TEST_LARGE_SIZES_COUNT (sizeof(TEST_LARGE_SIZES) / sizeof(TEST_LARGE_SIZES[0]))

/* Format size nicely for output */
static inline const char* _format_size(size_t size, char* buf, size_t buflen) {
  if (size >= 1024*1024) { snprintf(buf, buflen, "%zuMB", size / (1024*1024)); }
  else if (size >= 1024) { snprintf(buf, buflen, "%zuKB", size / 1024); }
  else { snprintf(buf, buflen, "%zuB", size); }
  return buf;
}

/* Run parameterized test for each size */
#define RUN_PARAMETERIZED(fn, sizes, count) do { \
  char _sizebuf[32]; \
  for (size_t _i = 0; _i < (count); _i++) { \
    _tests_run++; \
    if (fn((sizes)[_i])) { \
      _tests_passed++; \
      printf(PASS_TAG " %s(%s)\n", #fn, _format_size((sizes)[_i], _sizebuf, sizeof(_sizebuf))); \
    } else { \
      _tests_failed++; \
    } \
  } \
} while(0)

/* Seciton header for grouping tests */
#define TEST_SECTION(name) \
  printf("\n" COLOR_CYAN "--- %s ---" COLOR_RESET "\n", name)

/* Test suite lifecycle */
#define TEST_SUITE_BEGIN(name) do { \
  printf("\n" COLOR_BOLD "=== %s ===" COLOR_RESET "\n", name); \
  mm_init(); \
} while(0)

#define TEST_SUITE_END() do { \
  mm_destroy(); \
  printf("\n" COLOR_BOLD "=== Results ===" COLOR_RESET "\n"); \
  printf("Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", _tests_passed); \
  printf("Failed: %s%d" COLOR_RESET "\n", _tests_failed > 0 ? COLOR_RED : "", _tests_failed); \
} while(0)

/* Return exit code based on test results */
#define TEST_MAIN_END() \
  return (_tests_failed == 0) ? 0 : 1

/* Reset between tests if needed */
#define TEST_RESET() mm_reset_allocator()

#endif  /* TEST_FRAMEWORK */