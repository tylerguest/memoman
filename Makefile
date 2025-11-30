CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
LDFLAGS =
DEBUG_FLAGS = -g -DDEBUG_OUTPUT
BENCHMARK_FLAGS = -O3 -march=native -DNDEBUG

$(shell mkdir -p tests/bin)

tests/bin/%: tests/%.c src/memoman.c src/memoman.h
	mkdir -p tests/bin
	$(CC) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ src/memoman.c $<

TEST_SOURCES = $(wildcard tests/test*.c)
TESTS = $(patsubst tests/%.c,tests/bin/%,$(TEST_SOURCES))

all: CFLAGS += $(DEBUG_FLAGS)
all: $(TESTS)

benchmark: CFLAGS += $(BENCHMARK_FLAGS)
benchmark: LDFLAGS += -lrt
benchmark: clean $(TESTS)
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
	fi	

.PHONY: all benchmark debug clean run