CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
SRCDIR = src
TESTDIR = tests

# Source files
MALLOC_SRC = $(SRCDIR)/malloc.c
MALLOC_HDR = $(SRCDIR)/malloc.h

# Build all tests
all: test_alignment test_basic test_simple

# Individual test targets
test_alignment: $(TESTDIR)/test_alignment.c $(MALLOC_SRC) $(MALLOC_HDR)
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $(MALLOC_SRC) $(TESTDIR)/test_alignment.c

test_basic: $(TESTDIR)/test.c $(MALLOC_SRC) $(MALLOC_HDR)
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $(MALLOC_SRC) $(TESTDIR)/test.c

test_simple: $(TESTDIR)/test_simple_bump_allocator.c $(MALLOC_SRC) $(MALLOC_HDR)
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $(MALLOC_SRC) $(TESTDIR)/test_simple_bump_allocator.c

# Clean up
clean:
	rm -f test_alignment test_basic test_simple

# Run tests
run_tests: all
	./test_alignment
	echo "---"
	./test_basic
	echo "---"
	./test_simple

.PHONY: all clean run_tests