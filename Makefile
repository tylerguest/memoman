CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g

$(shell mkdir -p tests/bin)

tests/bin/%: tests/%.c src/malloc.c src/malloc.h
	$(CC) $(CFLAGS) -Isrc -o $@ src/malloc.c $<

TEST_SOURCES = $(wildcard tests/test*.c)
TESTS = $(patsubst tests/%.c,tests/bin/%,$(TEST_SOURCES))

all: $(TESTS)

clean:
	rm -f tests/bin/*
	rmdir tests/bin 2>/dev/null || true

run: all
	@for test in $(TESTS); do echo "=== $$test ==="; $$test; echo; done

.PHONY: all clean run