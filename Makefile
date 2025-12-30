CC = gcc
BASE_FLAGS = -Wall -Wextra -std=c99 -Isrc
CFLAGS = $(BASE_FLAGS) -g -DDEBUG_OUTPUT
SRC = src/memoman.c
TEST_DIR = tests
BIN_DIR = tests/bin

TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c, $(BIN_DIR)/%, $(TEST_SRCS))

.PHONY: all clean debug benchmark run
.PHONY: demo

all: $(TEST_BINS)
	@echo "Built with debug output enabled"

$(BIN_DIR)/%: $(TEST_DIR)/%.c $(SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $<

clean:
	rm -f $(BIN_DIR)/*
	rmdir $(BIN_DIR) 2>/dev/null || true

debug: all

benchmark: CFLAGS = $(BASE_FLAGS) -O3 -DNDEBUG
benchmark: clean $(TEST_BINS)
	@echo "Built with optimizations for benchmarking"

demo: demo.c $(SRC)
	$(CC) $(BASE_FLAGS) -O2 -DNDEBUG -o demo demo.c $(SRC)

run: $(TEST_BINS)
	@echo "=== Running All Tests ==="
	@failed=0; \
	for test in $(TEST_BINS); do \
		name=$$(basename $$test); \
		output=$$(./$$test 2>&1); \
		exit_code=$$?; \
		if [ $$exit_code -eq 0 ]; then \
			echo "PASSED: $$name"; \
		else \
			echo "FAILED: $$name (Exit code: $$exit_code)"; \
			echo "$$output" | sed 's/^/  /'; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "==================="; \
	if [ $$failed -gt 0 ]; then \
		echo "$$failed test(s) failed"; \
		exit 1; \
	fi
