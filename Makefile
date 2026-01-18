CC = gcc
BASE_FLAGS = -Wall -Wextra -std=c99 -Isrc
CFLAGS = $(BASE_FLAGS) -g -DDEBUG_OUTPUT
SRC = src/memoman.c
TEST_DIR = tests
BIN_DIR = tests/bin
EXTRAS_DIR = extras
EXTRAS_BIN_DIR = $(EXTRAS_DIR)/bin
HIST_BIN = $(EXTRAS_BIN_DIR)/latency_histogram

# Heavy/long-running tests should not run under `make run` by default.
TEST_SRCS = $(filter-out $(TEST_DIR)/test_soak.c,$(wildcard $(TEST_DIR)/*.c))
TEST_BINS = $(patsubst $(TEST_DIR)/%.c, $(BIN_DIR)/%, $(TEST_SRCS))
SOAK_BIN = $(BIN_DIR)/test_soak
CONTE_TLSF_SRC = examples/matt_conte/tlsf.c
SOAK_CONTE_BIN = $(BIN_DIR)/test_soak_conte

.PHONY: all clean debug benchmark run
.PHONY: demo
.PHONY: extras
.PHONY: soak soak_debug
.PHONY: soak_30
.PHONY: soak_rt_30
.PHONY: soak_malloc_30
.PHONY: soak_malloc_rt_30
.PHONY: soak_conte_30
.PHONY: soak_conte_rt_30
.PHONY: compare_rt_30
.PHONY: compare_fifo_30
.PHONY: compare_conte_rt_30
.PHONY: compare_conte_fifo_30

all: $(TEST_BINS)
	@echo "Built with debug output enabled"

$(BIN_DIR)/%: $(TEST_DIR)/%.c $(SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $<

$(SOAK_BIN): $(TEST_DIR)/test_soak.c $(SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $<

ifeq ($(wildcard $(CONTE_TLSF_SRC)),)
$(SOAK_CONTE_BIN):
	@echo "Conte TLSF not found: $(CONTE_TLSF_SRC) (folder is gitignored)."
	@echo "Add a local checkout under ./examples/matt_conte to build this target."
	@exit 1
else
$(SOAK_CONTE_BIN): $(TEST_DIR)/test_soak.c $(SRC) $(CONTE_TLSF_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -DMM_SOAK_HAVE_CONTE_TLSF=1 -Iexamples/matt_conte -o $@ $(SRC) $(CONTE_TLSF_SRC) $<
endif

clean:
	rm -f $(BIN_DIR)/*
	rmdir $(BIN_DIR) 2>/dev/null || true

debug: CFLAGS = $(BASE_FLAGS) -g -DDEBUG_OUTPUT -DMM_DEBUG=1 -DMM_DEBUG_VALIDATE_SHIFT=10 -DMM_DEBUG_ABORT_ON_INVALID_POINTER=1 -DMM_DEBUG_ABORT_ON_DOUBLE_FREE=0
debug: clean $(TEST_BINS)
	@echo "Built with MM_DEBUG enabled"

benchmark: CFLAGS = $(BASE_FLAGS) -O3 -DNDEBUG
benchmark: clean $(TEST_BINS)
	@echo "Built with optimizations for benchmarking"

demo: demo.c $(SRC)
	$(CC) $(BASE_FLAGS) -O2 -DNDEBUG -o demo demo.c $(SRC)

extras: $(HIST_BIN)

ifeq ($(wildcard $(CONTE_TLSF_SRC)),)
$(HIST_BIN): $(EXTRAS_DIR)/latency_histogram.c $(SRC)
	@mkdir -p $(EXTRAS_BIN_DIR)
	$(CC) $(BASE_FLAGS) -O3 -flto -DNDEBUG -o $(HIST_BIN) $(EXTRAS_DIR)/latency_histogram.c $(SRC)
else
$(HIST_BIN): $(EXTRAS_DIR)/latency_histogram.c $(SRC) $(CONTE_TLSF_SRC)
	@mkdir -p $(EXTRAS_BIN_DIR)
	$(CC) $(BASE_FLAGS) -O3 -flto -DNDEBUG -DMM_HIST_HAVE_CONTE_TLSF=1 -Iexamples/matt_conte -o $(HIST_BIN) $(EXTRAS_DIR)/latency_histogram.c $(SRC) $(CONTE_TLSF_SRC)
endif

soak: CFLAGS = $(BASE_FLAGS) -O2 -DNDEBUG
soak: clean $(SOAK_BIN)
	./$(SOAK_BIN)

soak_debug: CFLAGS = $(BASE_FLAGS) -g -DDEBUG_OUTPUT -DMM_DEBUG=1 -DMM_DEBUG_VALIDATE_SHIFT=10 -DMM_DEBUG_ABORT_ON_INVALID_POINTER=1 -DMM_DEBUG_ABORT_ON_DOUBLE_FREE=0
soak_debug: clean $(SOAK_BIN)
	./$(SOAK_BIN)

soak_30: CFLAGS = $(BASE_FLAGS) -O2 -DNDEBUG
soak_30: clean $(SOAK_BIN)
	MM_SOAK_SECONDS=30 ./$(SOAK_BIN)

soak_rt_30: CFLAGS = $(BASE_FLAGS) -O2 -DNDEBUG
soak_rt_30: clean $(SOAK_BIN)
	MM_SOAK_SECONDS=30 MM_SOAK_RT=1 MM_SOAK_REPORT_MS=250 ./$(SOAK_BIN)

soak_malloc_30: CFLAGS = $(BASE_FLAGS) -O2 -DNDEBUG
soak_malloc_30: clean $(SOAK_BIN)
	MM_SOAK_BACKEND=malloc MM_SOAK_SECONDS=30 ./$(SOAK_BIN)

soak_malloc_rt_30: CFLAGS = $(BASE_FLAGS) -O2 -DNDEBUG
soak_malloc_rt_30: clean $(SOAK_BIN)
	MM_SOAK_BACKEND=malloc MM_SOAK_SECONDS=30 MM_SOAK_RT=1 MM_SOAK_REPORT_MS=250 ./$(SOAK_BIN)

soak_conte_30: CFLAGS = $(BASE_FLAGS) -O2 -DNDEBUG
soak_conte_30: clean $(SOAK_CONTE_BIN)
	MM_SOAK_BACKEND=conte MM_SOAK_SECONDS=30 ./$(SOAK_CONTE_BIN)

soak_conte_rt_30: CFLAGS = $(BASE_FLAGS) -O2 -DNDEBUG
soak_conte_rt_30: clean $(SOAK_CONTE_BIN)
	MM_SOAK_BACKEND=conte MM_SOAK_SECONDS=30 MM_SOAK_RT=1 MM_SOAK_REPORT_MS=250 ./$(SOAK_CONTE_BIN)

compare_rt_30: CFLAGS = $(BASE_FLAGS) -O2 -DNDEBUG
compare_rt_30: clean $(SOAK_BIN)
	MM_SOAK_BACKEND=compare MM_SOAK_SECONDS=30 MM_SOAK_RT=1 MM_SOAK_REPORT_MS=250 ./$(SOAK_BIN)

compare_fifo_30: CFLAGS = $(BASE_FLAGS) -O2 -DNDEBUG
compare_fifo_30: clean $(SOAK_BIN)
	sudo -E MM_SOAK_BACKEND=compare MM_SOAK_SECONDS=30 MM_SOAK_RT=1 MM_SOAK_SCHED=fifo MM_SOAK_PRIO=80 MM_SOAK_REPORT_MS=250 ./$(SOAK_BIN)

compare_conte_rt_30: CFLAGS = $(BASE_FLAGS) -O2 -DNDEBUG
compare_conte_rt_30: clean $(SOAK_CONTE_BIN)
	MM_SOAK_BACKEND=compare MM_SOAK_SECONDS=30 MM_SOAK_RT=1 MM_SOAK_REPORT_MS=250 ./$(SOAK_CONTE_BIN)

compare_conte_fifo_30: CFLAGS = $(BASE_FLAGS) -O2 -DNDEBUG
compare_conte_fifo_30: clean $(SOAK_CONTE_BIN)
	sudo -E MM_SOAK_BACKEND=compare MM_SOAK_SECONDS=30 MM_SOAK_RT=1 MM_SOAK_SCHED=fifo MM_SOAK_PRIO=80 MM_SOAK_REPORT_MS=250 ./$(SOAK_CONTE_BIN)

run: $(TEST_BINS)
	@echo "=== Running All Tests ==="
	@failed=0; \
	for test in $(TEST_BINS); do \
		name=$$(basename $$test); \
		if [ "$(TIMING)" = "1" ]; then start=$$(date +%s%N); fi; \
		if [ "$(DEBUG)" = "1" ]; then \
			./$$test; \
			exit_code=$$?; \
		else \
			output=$$(./$$test 2>&1); \
			exit_code=$$?; \
		fi; \
		if [ "$(TIMING)" = "1" ]; then end=$$(date +%s%N); elapsed_ms=$$((($$end - $$start)/1000000)); fi; \
		if [ $$exit_code -eq 0 ]; then \
			if [ "$(TIMING)" = "1" ]; then \
				echo "PASSED: $$name ($${elapsed_ms}ms)"; \
			else \
				echo "PASSED: $$name"; \
			fi; \
		else \
			if [ "$(TIMING)" = "1" ]; then \
				echo "FAILED: $$name (Exit code: $$exit_code, $${elapsed_ms}ms)"; \
			else \
				echo "FAILED: $$name (Exit code: $$exit_code)"; \
			fi; \
			if [ "$(DEBUG)" != "1" ]; then \
				echo "$$output" | sed 's/^/  /'; \
			fi; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "==================="; \
	if [ $$failed -gt 0 ]; then \
		echo "$$failed test(s) failed"; \
		exit 1; \
	fi
