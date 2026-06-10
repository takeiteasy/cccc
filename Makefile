SRCS := $(filter-out src/ops.c, $(wildcard src/*.c src/stdlib/*.c))
BASE_CFLAGS := -Wall -O0 -g -std=c23 -Wno-deprecated-declarations -Wno-switch
CFLAGS := $(BASE_CFLAGS)
LDFLAGS :=
LLVM_CONFIG ?= llvm-config
PKG_CONFIG ?= pkg-config

ifeq ($(CCCC_HAS_LLVM),1)
	LLVM_CONFIG_FOUND := $(shell command -v $(LLVM_CONFIG) 2>/dev/null)
ifeq ($(LLVM_CONFIG_FOUND),)
	$(error CCCC_HAS_LLVM=1 requires llvm-config; set LLVM_CONFIG=/path/to/llvm-config)
endif
	LLVM_CFLAGS := $(shell $(LLVM_CONFIG) --cflags)
	LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags)
	LLVM_LIBS := $(shell $(LLVM_CONFIG) --libs core native analysis) $(shell $(LLVM_CONFIG) --system-libs)
	CFLAGS += -DCCCC_HAS_LLVM=1 $(LLVM_CFLAGS)
	LDFLAGS += $(LLVM_LDFLAGS) $(LLVM_LIBS)
endif

# libffi is required for native FFI calls.
LIBFFI_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libffi 2>/dev/null)
LIBFFI_LDFLAGS ?= $(shell $(PKG_CONFIG) --libs libffi 2>/dev/null)

ifeq ($(LIBFFI_LDFLAGS),)
  ifeq ($(shell uname -s),Darwin)
    LIBFFI_CFLAGS := -I/opt/homebrew/opt/libffi/include -I/usr/local/opt/libffi/include
    LIBFFI_LDFLAGS := -L/opt/homebrew/opt/libffi/lib -L/usr/local/opt/libffi/lib -lffi
  else
    LIBFFI_CFLAGS := -I/usr/include -I/usr/local/include
    LIBFFI_LDFLAGS := -L/usr/lib -L/usr/local/lib -lffi
  endif
endif

CFLAGS += $(LIBFFI_CFLAGS)
LDFLAGS += $(LIBFFI_LDFLAGS)

ifeq ($(OS),Windows_NT)
	EXE := .EXE
	DYLIB := .dll
else
	EXE :=
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
		DYLIB := .dylib
	else
		DYLIB := .so
	endif
endif
EXE_OUT := cccc$(EXE)
LIB_OUT := libcccc$(DYLIB)
SAN_OUT := cccc-asan cccc-ubsan cccc-tsan

ifeq ($(UNAME_S),Linux)
	SAN_OUT += cccc-msan
endif

default: $(EXE_OUT)


$(EXE_OUT): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(LIB_OUT): $(SRCS)
	$(CC) -fpic -shared $(CFLAGS) -o $@ $(filter-out src/main.c, $^) $(LDFLAGS)

cccc-asan: $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ $^ $(LDFLAGS)

cccc-ubsan: $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=undefined -o $@ $^ $(LDFLAGS)

cccc-tsan: $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=thread -o $@ $^ $(LDFLAGS)

ifeq ($(UNAME_S),Linux)
cccc-msan: $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=memory -o $@ $^ $(LDFLAGS)
endif

asan: cccc-asan
ubsan: cccc-ubsan
tsan: cccc-tsan
ifeq ($(UNAME_S),Linux)
msan: cccc-msan
endif

sanitizers: asan ubsan tsan
ifeq ($(UNAME_S),Linux)
sanitizers: msan
endif

# AFL++ fuzzing build
# Detect available AFL compiler wrapper (afl-clang-fast preferred, fallback to afl-clang)
AFL_CC := $(shell which afl-clang-fast 2>/dev/null || which afl-clang 2>/dev/null || echo "")

cccc-afl: $(SRCS)
ifeq ($(AFL_CC),)
	@echo "Error: AFL++ compiler wrapper not found. Install AFL++ first."
	@exit 1
else
	$(AFL_CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
endif

afl: cccc-afl

# AFL++ + AddressSanitizer combo (slower but catches memory errors immediately)
cccc-afl-asan: $(SRCS)
ifeq ($(AFL_CC),)
	@echo "Error: AFL++ compiler wrapper not found. Install AFL++ first."
	@exit 1
else
	AFL_USE_ASAN=1 $(AFL_CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
endif

afl-asan: cccc-afl-asan

# libFuzzer harness (optional)
fuzz_harness: src/fuzzing.c $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=fuzzer,address -o $@ $(filter-out src/main.c, $(SRCS)) $< $(LDFLAGS)

# --- Fuzzing targets (moved from fuzz/Makefile) ---

FUZZ_CORPUS := fuzz/corpus
FUZZ_OUT := fuzz/out
FUZZ_CRASHES := $(FUZZ_OUT)/crashes
FUZZ_TIMEOUT := 1000
FUZZ_MEMORY := none
FUZZ_FLAGS := -I./include -c

fuzz-all: fuzz-seed fuzz-run

fuzz-seed:
	@echo "Seeding corpus from tests/..."
	@mkdir -p $(FUZZ_CORPUS)
	@cp tests/test_*.c $(FUZZ_CORPUS)/ 2>/dev/null || true
	@cp tests/macros/test_macros_*.c $(FUZZ_CORPUS)/ 2>/dev/null || true
	@echo "Corpus seeded with $$(ls $(FUZZ_CORPUS) | wc -l) files"

fuzz-run: cccc-afl
	@if [ ! -f "cccc-afl" ]; then \
		echo "Error: cccc-afl not found. Run 'make afl' first."; \
		exit 1; \
	fi
	@mkdir -p $(FUZZ_OUT)
	@echo "Starting AFL++ fuzzing..."
	@echo "  input:  $(FUZZ_CORPUS)"
	@echo "  output: $(FUZZ_OUT)"
	afl-fuzz -i $(FUZZ_CORPUS) -o $(FUZZ_OUT) -m $(FUZZ_MEMORY) -t $(FUZZ_TIMEOUT) \
		-- ./cccc-afl $(FUZZ_FLAGS) @@

fuzz-crashes:
	@if [ -d "$(FUZZ_CRASHES)" ]; then \
		echo "Crashes found:"; \
		ls -1 $(FUZZ_CRASHES)/id* 2>/dev/null || echo "  (none yet)"; \
	else \
		echo "No crash directory yet."; \
	fi

fuzz-triage:
	@if [ -d "$(FUZZ_CRASHES)" ]; then \
		for f in $(FUZZ_CRASHES)/id*; do \
			echo "=== $$f ==="; \
			./cccc-afl $(FUZZ_FLAGS) "$$f" 2>&1 | head -n 20; \
			echo; \
		done \
	else \
		echo "No crash directory yet."; \
	fi

fuzz-minimize:
	@if [ -d "$(FUZZ_CRASHES)" ]; then \
		for f in $(FUZZ_CRASHES)/id*; do \
			base=$$(basename "$$f"); \
			echo "Minimizing $$base..."; \
			afl-tmin -i "$$f" -o "$(FUZZ_CRASHES)/$${base}.min" -- ./cccc-afl $(FUZZ_FLAGS) @@; \
		done \
	else \
		echo "No crash directory yet."; \
	fi

fuzz-info:
	@echo "AFL++ binary built: ./cccc-afl"
	@echo "Run fuzzing with:"
	@echo "  make fuzz-seed && make fuzz-run"
	@echo ""
	@echo "For ASan + AFL++ combo (slower, more sensitive):"
	@echo "  make afl-asan"

fuzz: fuzz-info

STD_TEMPLATE := tools/generate_stdlib.c

# Regenerate src/std.c from the template.
# src/std.c is committed so the normal build never needs this; run it
# explicitly after editing tools/generate_stdlib.c or include/*.h.
.PHONY: stdlib
stdlib: $(EXE_OUT)
	@set -e; \
	tmp=$$(mktemp src/std.c.tmp.XXXXXX); \
	trap 'rm -f "$$tmp"' EXIT; \
	./$(EXE_OUT) -G -I./include $(STD_TEMPLATE) > "$$tmp"; \
	if cmp -s "$$tmp" src/std.c; then \
		rm -f "$$tmp"; \
	else \
		mv "$$tmp" src/std.c; \
	fi

test: clean $(EXE_OUT)
	@python3 tools/tests.py

all: clean $(EXE_OUT) $(LIB_OUT) test docs

docs:
	@headerdoc2html src/cccc.h include/cccc/reflection.h -o docs/; \
	gatherheaderdoc docs/; \
	mv docs/masterTOC.html docs/index.html

# Profiling
# Detect available profiling tools
HAS_HYPERFINE := $(shell which hyperfine 2>/dev/null || echo "")
HAS_GPROFILER := $(shell test -f /opt/homebrew/lib/libprofiler.dylib && echo "yes" || test -f /usr/lib/libprofiler.so && echo "yes" || echo "")

PROFILE_TEST ?= tests/test_comprehensive.c

bench:
ifeq ($(HAS_HYPERFINE),)
	@echo "Error: hyperfine not found. Install it with 'brew install hyperfine' or 'cargo install hyperfine'."
	@exit 1
else
	@mkdir -p profile
	hyperfine --warmup 3 --ignore-failure --export-json profile/bench.json \
		'./cccc -I./include $(PROFILE_TEST)'
endif

# Cross-compiler benchmark suite: CCCC vs GCC.
# Runs every benchmark under CCCC × {none,O1,O2,O3} and GCC × {O0,O1,O2,O3},
# verifies identical output, and emits a table + JSON report.
BENCH_RUNS ?= 3
BENCH_WARMUP ?= 1

bench-compare: cccc
	@python3 tools/bench.py --runs $(BENCH_RUNS) --warmup $(BENCH_WARMUP)

bench-compare-quick: cccc
	@python3 tools/bench.py --runs 2 --warmup 1

bench-compare-json: cccc
	@python3 tools/bench.py --format json --runs $(BENCH_RUNS) --warmup $(BENCH_WARMUP)

profile-cpu-build: $(SRCS)
ifeq ($(HAS_GPROFILER),)
	@echo "Error: gperftools libprofiler not found. Install with 'brew install gperftools'."
	@exit 1
else
ifeq ($(UNAME_S),Darwin)
	$(CC) $(CFLAGS) -o cccc-prof $^ $(LDFLAGS) -L/opt/homebrew/lib -lprofiler
else
	$(CC) $(CFLAGS) -o cccc-prof $^ $(LDFLAGS) -lprofiler
endif
endif

profile-cpu: profile-cpu-build
	@mkdir -p profile
	CPUPROFILE=profile/cpu.prof ./cccc-prof -I./include $(PROFILE_TEST) || true
	@echo "CPU profile saved to profile/cpu.prof"
	@echo "To view: install Go pprof (go install github.com/google/pprof@latest)"
	@echo "  pprof -text cccc-prof profile/cpu.prof"

profile-mem:
ifeq ($(UNAME_S),Darwin)
	@mkdir -p profile
	@echo "Running with leaks tool..."
	leaks -atExit -- ./cccc -I./include $(PROFILE_TEST) > profile/mem-leaks.txt 2>&1 || true
	@echo "Memory leak report: profile/mem-leaks.txt"
else ifeq ($(UNAME_S),Linux)
	@mkdir -p profile
	valgrind --tool=massif --massif-out-file=profile/mem.massif \
		./cccc -I./include $(PROFILE_TEST)
	@echo "Massif output: profile/mem.massif"
else
	@echo "Memory profiling not supported on this platform."
	@exit 1
endif

clean:
	@$(RM) -f $(EXE_OUT) $(LIB_OUT) $(SAN_OUT) cccc-afl cccc-afl-asan cccc-prof fuzz_harness
	@$(RM) -rf profile/*.prof profile/*.txt profile/*.json profile/*.massif
	@$(RM) -rf fuzz/corpus fuzz/out

.PHONY: default test clean docs all asan ubsan tsan sanitizers afl afl-asan fuzz fuzz_harness bench profile-cpu profile-cpu-build profile-mem fuzz-all fuzz-seed fuzz-run fuzz-crashes fuzz-triage fuzz-minimize fuzz-info stdlib bench-compare bench-compare-quick bench-compare-json
ifeq ($(UNAME_S),Linux)
.PHONY: msan
endif
