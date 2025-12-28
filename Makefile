CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -DDEBUG_OUTPUT -Isrc
SRC = src/memoman.c
TEST_DIR = tests
BIN_DIR = tests/bin

TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c, $(BIN_DIR)/%, $(TEST_SRCS))

.PHONY: all clean debug run

all: $(TEST_BINS)
	@echo "Built with debug output enabled"

$(BIN_DIR)/%: $(TEST_DIR)/%.c $(SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $<

clean:
	rm -f $(BIN_DIR)/*
	rmdir $(BIN_DIR) 2>/dev/null || true

debug: all

run: all
	@echo "=== Running All Tests ==="
	@failed=0; \
	for test in $(TEST_BINS); do \
		name=$$(basename $$test); \
		output=$$(./$$test 2>&1); \
		exit_code=$$?; \
		if [ $$exit_code -eq 0 ]; then \
			echo "PASSED: $$name"; \
		else \
			echo "FAILED: $$name"; \
			echo "$$output" | sed 's/^/  /'; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "==================="; \
	if [ $$failed -gt 0 ]; then \
		echo "$$failed test(s) failed"; \
		exit 1; \
	fi