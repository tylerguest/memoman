CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g

# Pattern rule
tests/%: tests/%.c src/malloc.c src/malloc.h
	$(CC) $(CFLAGS) -Isrc -o $@ src/malloc.c $<

# Auto-detect all test files
TEST_SOURCES = $(wildcard tests/test*.c)
TESTS = $(patsubst tests/%.c,tests/%,$(TEST_SOURCES))

all: $(TESTS)

clean:
	rm -f $(TESTS)

run: all
	@for test in $(TESTS); do echo "=== $$test ==="; $$test; echo; done

.PHONY: all clean run