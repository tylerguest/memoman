# Memoman - Custom Memory Allocator

A custom memory allocator implementation in C.

## Quick Setup

### Prerequisites
- GCC compiler
- Make
- Linux/Unix system (uses POSIX features)

### Build

```bash
# Clone the repository
git clone <repository-url>
cd memoman

# Build with debug output
make debug

# Build optimized for benchmarking
make benchmark

# Build all tests (default with debug flags)
make all
```

## Usage

### Run All Tests
```bash
make run
```

### Run Individual Tests
```bash
# After building
./tests/bin/test_benchmark
./tests/bin/test_<other_test>
```

### API

```c
#include "memoman.h"

// Allocate memory
void* ptr = memomall(size);

// Free memory
memofree(ptr);

// Reset allocator state
reset_allocator();
```

## Project Structure

```
memoman/
├── src/
│   ├── memoman.c      # Allocator implementation
│   └── memoman.h      # Public API
├── tests/
│   ├── test_benchmark.c  # Performance benchmarks
│   └── bin/           # Compiled test binaries
├── Makefile           # Build configuration
└── README.md
```

## Build Targets

- `make all` - Build all tests with debug flags
- `make benchmark` - Build optimized for performance testing
- `make debug` - Build with debug output enabled
- `make clean` - Remove build artifacts
- `make run` - Build and run all tests

## Performance Testing

The benchmark compares the custom allocator against system malloc:

```bash
make benchmark
./tests/bin/test_benchmark
```

## Makefile Flags

- `DEBUG_OUTPUT` - Enable debug logging
- `NDEBUG` - Disable assertions (benchmark mode)
- `-O3 -march=native` - Optimization flags for benchmarking
