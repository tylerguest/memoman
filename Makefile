CC = gcc
CFLAGS = -Wall -Wextra -std=c99
LDFLAGS =
DEBUG_FLAGS = -g -DDEBUG_OUTPUT
BENCHMARK_FLAGS = -O3 -march=native -DNDEBUG

# Ensure output directory exists before anything else
$(shell mkdir -p tests/bin)

# Auto-detect all test files
TEST_SOURCES = $(wildcard tests/test*.c)
# Convert .c filenames to bin filenames for the target list
TESTS = $(patsubst tests/%.c,tests/bin/%,$(TEST_SOURCES))

all: CFLAGS += $(DEBUG_FLAGS)
all: $(TESTS)

# --- EXCEPTION RULE: White-Box Testing ---
# 1. Matches ONLY test_mapping_unit
# 2. Depends on memoman.c (so edits trigger rebuild) but does NOT link it (to avoid double definition)
tests/bin/test_mapping_unit: tests/test_mapping_unit.c src/memoman.c src/memoman.h
	$(CC) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ src/memoman.c tests/test_mapping_unit.c

# --- GENERIC RULE: Black-Box Testing ---
# Matches all other tests and links memoman.c normally
tests/bin/%: tests/%.c src/memoman.c src/memoman.h
	$(CC) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ src/memoman.c $<

benchmark: clean
	@mkdir -p tests/bin
	@$(MAKE) CFLAGS="$(CFLAGS) $(BENCHMARK_FLAGS)" LDFLAGS="$(LDFLAGS) -lrt" $(TESTS)
	@echo "Built with optimizations for benchmarking"

debug: CFLAGS += $(DEBUG_FLAGS)
debug: clean $(TESTS)
	@echo "Built with debug output enabled"

clean:
	rm -f tests/bin/*
	rmdir tests/bin 2>/dev/null || true

run: all
	@echo "=== Running All Tests ==="
	@failed=0; \
	for test in $(TESTS); do \
		if $$test > /dev/null 2>&1; then \
			echo "PASSED: $$(basename $$test)"; \
		else \
			echo "FAILED: $$(basename $$test)"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "==================="; \
	if [ $$failed -eq 0 ]; then \
		echo "All tests passed!"; \
	else \
		echo "$$failed test(s) failed"; \
		exit 1; \
	fi;

.PHONY: all benchmark debug clean run