CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
DEBUG_FLAGS = -g -DDEBUG_OUTPUT
BENCHMARK_FLAGS = -03 -march=native -DNDEBUG

$(shell mkdir -p tests/bin)

tests/bin/%: tests/%.c src/malloc.c src/malloc.h
	$(CC) $(CFLAGS) -Isrc -o $@ src/malloc.c $<

TEST_SOURCES = $(wildcard tests/test*.c)
TESTS = $(patsubst tests/%.c,tests/bin/%,$(TEST_SOURCES))

all: CFLAGS += $(DEBUG_FLAGS)
all: $(TESTS)

# Fast benchmark build (use this for performance testing)
benchmark: CFLAGS += $(BENCHMARK_FLAGS)
benchmark: clean $(TESTS)
	@echo "Built with optimizations for benchmarking"

# Debug build with output enabled
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
	fi	
	
.PHONY: all benchmark debug clean run