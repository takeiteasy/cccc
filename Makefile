SRCS := $(filter-out src/ops.c, $(wildcard src/*.c src/stdlib/*.c))
BASE_CFLAGS := -Wall -O0 -g -std=c23 -Wno-deprecated-declarations -Wno-switch
CFLAGS := $(BASE_CFLAGS)
LDFLAGS :=
PKG_CONFIG ?= pkg-config

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

# Optional libcurl support for URL-based #include directives
# Enable with: make CCCC_HAS_CURL=1 or export CCCC_HAS_CURL=1
ifdef CCCC_HAS_CURL
  ifneq ($(CCCC_HAS_CURL),0)
    CFLAGS += -DCCCC_HAS_CURL=1
    LIBCURL_CFLAGS := $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null)
    LIBCURL_LDFLAGS := $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null)
    ifeq ($(LIBCURL_CFLAGS),)
      ifeq ($(shell uname -s),Darwin)
        LIBCURL_CFLAGS := -I/opt/homebrew/opt/curl/include -I/usr/local/opt/curl/include -I/opt/homebrew/include
        LIBCURL_LDFLAGS := -L/opt/homebrew/opt/curl/lib -L/usr/local/opt/curl/lib -L/opt/homebrew/lib -lcurl
      else
        LIBCURL_CFLAGS := -I/usr/include -I/usr/local/include
        LIBCURL_LDFLAGS := -L/usr/lib -L/usr/local/lib -lcurl
      endif
    endif
    CFLAGS += $(LIBCURL_CFLAGS)
    LDFLAGS += $(LIBCURL_LDFLAGS)
  endif
endif

ifeq ($(OS),Windows_NT)
	EXE := .EXE
	DYLIB := .dll
else
	CFLAGS += -pthread
	LDFLAGS += -pthread
	EXE :=
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
		DYLIB := .dylib
	else
		DYLIB := .so
	endif
	ifeq ($(UNAME_S),Linux)
		CFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
		LDFLAGS += -lm
	endif
endif
EXE_OUT := cccc$(EXE)
LIB_OUT := libcccc$(DYLIB)
SAN_OUT := cccc-asan cccc-ubsan cccc-tsan
TEST_JOBS ?= 8

# Reproducible cross-platform build/test workflows.
MACOS_X86_64_CC ?= /usr/bin/clang
MACOS_X86_64_BINARY ?= cccc-macos-x86_64
MACOS_SDK_PATH ?= $(shell xcrun --sdk macosx --show-sdk-path 2>/dev/null)
COLIMA ?= colima
COLIMA_PROFILE ?= cccc-linux-amd64
COLIMA_NERDCTL = $(COLIMA) -p $(COLIMA_PROFILE) nerdctl --
LINUX_AMD64_IMAGE ?= cccc-linux-amd64
LINUX_ARM64_PROFILE ?= cccc-linux-arm64
LINUX_ARM64_NERDCTL = $(COLIMA) -p $(LINUX_ARM64_PROFILE) nerdctl --
LINUX_ARM64_IMAGE ?= cccc-linux-arm64


ifeq ($(UNAME_S),Linux)
	SAN_OUT += cccc-msan
endif

# libbacktrace — symbolic host C stack traces on crash.
# On by default; disable with: make CCCC_HAS_BACKTRACE=0
ifndef CCCC_HAS_BACKTRACE
  CCCC_HAS_BACKTRACE := 1
endif
LIBBACKTRACE_A :=
ifneq ($(OS),Windows_NT)
  ifneq ($(CCCC_HAS_BACKTRACE),0)
    LIBBACKTRACE_DIR  := src/backtrace
    LIBBACKTRACE_A    := build/libbacktrace.a
    LIBBACKTRACE_OBJS_DIR := build/libbacktrace
    # Flags used only for compiling vendored libbacktrace (not -std=c23, no -Wall strictness)
    LIBBACKTRACE_CC_FLAGS := -O2 -g -I$(LIBBACKTRACE_DIR) -D_GNU_SOURCE \
      -Wno-unused-parameter -Wno-unused-variable \
      -Wno-missing-field-initializers -Wno-shift-count-overflow \
      -Wno-implicit-function-declaration -Wno-deprecated-declarations
    # Platform-specific source list (elf.c on Linux, macho.c on macOS)
    LIBBACKTRACE_COMMON := \
      $(LIBBACKTRACE_DIR)/backtrace.c \
      $(LIBBACKTRACE_DIR)/atomic.c \
      $(LIBBACKTRACE_DIR)/dwarf.c \
      $(LIBBACKTRACE_DIR)/fileline.c \
      $(LIBBACKTRACE_DIR)/mmap.c \
      $(LIBBACKTRACE_DIR)/mmapio.c \
      $(LIBBACKTRACE_DIR)/posix.c \
      $(LIBBACKTRACE_DIR)/print.c \
      $(LIBBACKTRACE_DIR)/simple.c \
      $(LIBBACKTRACE_DIR)/sort.c \
      $(LIBBACKTRACE_DIR)/state.c
    ifeq ($(UNAME_S),Darwin)
      LIBBACKTRACE_SRCS := $(LIBBACKTRACE_COMMON) $(LIBBACKTRACE_DIR)/macho.c
    else
      LIBBACKTRACE_SRCS := $(LIBBACKTRACE_COMMON) $(LIBBACKTRACE_DIR)/elf.c
    endif
    LIBBACKTRACE_OBJS := $(patsubst $(LIBBACKTRACE_DIR)/%.c, \
                           $(LIBBACKTRACE_OBJS_DIR)/%.o, $(LIBBACKTRACE_SRCS))
    CFLAGS  += -DCCCC_HAS_BACKTRACE=1 -I$(LIBBACKTRACE_DIR)
    LDFLAGS += $(LIBBACKTRACE_A)
  endif
endif

default: $(EXE_OUT)

# Build vendored libbacktrace as an isolated static archive.
# All binary targets carry an order-only dep on this so it is built before linking.
ifneq ($(LIBBACKTRACE_A),)
$(LIBBACKTRACE_OBJS_DIR)/%.o: $(LIBBACKTRACE_DIR)/%.c
	@mkdir -p $(LIBBACKTRACE_OBJS_DIR)
	$(CC) $(LIBBACKTRACE_CC_FLAGS) -c -o $@ $<

$(LIBBACKTRACE_A): $(LIBBACKTRACE_OBJS)
	ar rcs $@ $^

# Declare order-only dependency on the archive for all binary targets so that
# they are never linked before the archive exists.
$(EXE_OUT) $(LIB_OUT) cccc-asan cccc-ubsan cccc-tsan cccc-msan \
  cccc-afl cccc-afl-asan fuzz_harness profile-cpu-build: | $(LIBBACKTRACE_A)
endif

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
	@python3 tools/run_tests.py -j $(TEST_JOBS)

# Run only the [[cccc::test]] framework suites in tests/suites/
test-suites: $(EXE_OUT)
	@python3 tools/tests.py --suites -j $(TEST_JOBS)

# Run only the legacy single-file tests in tests/ (excludes tests/suites/)
test-legacy: $(EXE_OUT)
	@python3 tools/tests.py --legacy -j $(TEST_JOBS)

# SQLite amalgamation smoke-test (preprocess #584 + compile+run #587/#588).
# Skips cleanly when tools/sqlite-amalgamation-3530200.zip is absent; see docs/TESTING.md.
sqlite-smoke: $(EXE_OUT)
	@python3 tools/sqlite_smoke.py

macos-x86_64-build:
	@if [ "$(UNAME_S)" != "Darwin" ]; then \
		echo "Error: macos-x86_64-build requires macOS."; \
		exit 1; \
	fi
	@if [ -z "$(MACOS_SDK_PATH)" ]; then \
		echo "Error: macOS SDK not found. Install the Xcode Command Line Tools."; \
		exit 1; \
	fi
	@$(MAKE) EXE_OUT=$(MACOS_X86_64_BINARY) \
		CC="$(MACOS_X86_64_CC) -arch x86_64" \
		LIBFFI_CFLAGS="-I$(MACOS_SDK_PATH)/usr/include/ffi" \
		LIBFFI_LDFLAGS="-lffi" \
		$(MACOS_X86_64_BINARY)
	@file $(MACOS_X86_64_BINARY)
	@file $(MACOS_X86_64_BINARY) | grep -q 'x86_64'

macos-x86_64-smoke: macos-x86_64-build
	@set -eu; \
		machine=$$(/usr/bin/arch -x86_64 /usr/bin/uname -m); \
		echo "Rosetta machine: $$machine"; \
		test "$$machine" = "x86_64"; \
		rc=0; \
		/usr/bin/arch -x86_64 ./$(MACOS_X86_64_BINARY) -I./include tests/test_arithmetic.c || rc=$$?; \
		test "$$rc" -eq 42; \
		rc=0; \
		CCCC_NATIVE_CC=/usr/bin/clang /usr/bin/arch -x86_64 \
			./$(MACOS_X86_64_BINARY) -I./include --asm-passthru tests/test_asm_passthru.c || rc=$$?; \
		test "$$rc" -eq 42; \
		tmp=$$(mktemp /tmp/cccc-native-x86_64.XXXXXX); \
		trap 'rm -f "$$tmp"' EXIT; \
		CCCC_NATIVE_CC=/usr/bin/clang /usr/bin/arch -x86_64 \
			./$(MACOS_X86_64_BINARY) -c=native -o "$$tmp" tests/test_arithmetic.c; \
		file "$$tmp"; \
		file "$$tmp" | grep -q 'x86_64'; \
		rc=0; /usr/bin/arch -x86_64 "$$tmp" || rc=$$?; \
		test "$$rc" -eq 42

macos-x86_64-test: macos-x86_64-smoke
	@set +e; \
		rc=0; \
		/usr/bin/arch -x86_64 /usr/bin/python3 tools/tests.py \
			--binary ./$(MACOS_X86_64_BINARY) -j $(TEST_JOBS) || rc=1; \
		/usr/bin/arch -x86_64 /usr/bin/python3 tools/tests.py \
			--binary ./$(MACOS_X86_64_BINARY) --c4 -j $(TEST_JOBS) || rc=1; \
		/usr/bin/arch -x86_64 /usr/bin/python3 tools/test_host_signal_debugger.py \
			--binary ./$(MACOS_X86_64_BINARY) || rc=1; \
		exit $$rc

linux-x86_64-check:
	@$(COLIMA) status -p $(COLIMA_PROFILE) >/dev/null 2>&1 || { \
		echo "Error: Colima profile '$(COLIMA_PROFILE)' is not running."; \
		echo "See docs/TESTING.md for the VZ/Rosetta profile setup."; \
		exit 1; \
	}

linux-x86_64-build: linux-x86_64-check
	$(COLIMA_NERDCTL) build --platform linux/amd64 \
		-t $(LINUX_AMD64_IMAGE) .

linux-x86_64-smoke: linux-x86_64-build
	@$(COLIMA_NERDCTL) run --rm --platform linux/amd64 \
		$(LINUX_AMD64_IMAGE) sh -ec ' \
			machine=$$(uname -m); \
			echo "Container machine: $$machine"; \
			test "$$machine" = "x86_64"; \
			file ./cccc; \
			file ./cccc | grep -Eq "x86-64|x86_64"; \
			rc=0; ./cccc -I./include tests/test_arithmetic.c || rc=$$?; \
			test "$$rc" -eq 42'

linux-x86_64-test: linux-x86_64-smoke
	@set +e; \
		rc=0; \
		for pattern in 'test_[a-f]*.c' 'test_[g-l]*.c' 'test_[m-r]*.c' \
			'test_[s-u]*.c' 'test_[v-x]*.c' 'test_[y-z]*.c'; do \
			$(COLIMA_NERDCTL) run --rm --platform linux/amd64 \
				$(LINUX_AMD64_IMAGE) timeout 300 python3 tools/tests.py \
				--match "$$pattern" --quiet -j $(TEST_JOBS) || rc=1; \
		done; \
		for pattern in 'test_[a-f]*.c' 'test_[g-l]*.c' 'test_[m-r]*.c' \
			'test_[s-u]*.c' 'test_[v-x]*.c' 'test_[y-z]*.c'; do \
			$(COLIMA_NERDCTL) run --rm --platform linux/amd64 \
				$(LINUX_AMD64_IMAGE) timeout 300 python3 tools/tests.py \
				--match "$$pattern" --c4 --quiet -j $(TEST_JOBS) || rc=1; \
		done; \
		exit $$rc

linux-x86_64-msan-test: linux-x86_64-build
	$(COLIMA_NERDCTL) run --rm --platform linux/amd64 \
		$(LINUX_AMD64_IMAGE) sh -ec \
		'make cccc-msan && python3 tools/tests.py --msan --quiet -j $(TEST_JOBS)'

linux-aarch64-check:
	@$(COLIMA) status -p $(LINUX_ARM64_PROFILE) >/dev/null 2>&1 || { \
		echo "Error: Colima profile '$(LINUX_ARM64_PROFILE)' is not running."; \
		echo "Start it with:"; \
		echo "  colima start $(LINUX_ARM64_PROFILE) --runtime containerd --arch aarch64 --vm-type vz --cpu 4 --memory 4"; \
		echo "See docs/TESTING.md for setup details."; \
		exit 1; \
	}

linux-aarch64-build: linux-aarch64-check
	$(LINUX_ARM64_NERDCTL) build --platform linux/arm64 \
		-t $(LINUX_ARM64_IMAGE) .

linux-aarch64-smoke: linux-aarch64-build
	@$(LINUX_ARM64_NERDCTL) run --rm --platform linux/arm64 \
		$(LINUX_ARM64_IMAGE) sh -ec ' \
			machine=$$(uname -m); \
			echo "Container machine: $$machine"; \
			test "$$machine" = "aarch64"; \
			file ./cccc; \
			file ./cccc | grep -Eq "aarch64|arm64"; \
			rc=0; ./cccc -I./include tests/test_arithmetic.c || rc=$$?; \
			test "$$rc" -eq 42'

linux-aarch64-test: linux-aarch64-smoke
	@$(LINUX_ARM64_NERDCTL) run --rm --platform linux/arm64 \
		$(LINUX_ARM64_IMAGE) timeout 600 python3 tools/run_tests.py \
		-j $(TEST_JOBS)


all: clean $(EXE_OUT) $(LIB_OUT) test

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
# Runs every benchmark under CCCC × {none,O1,O2,O3,O4} and GCC × {O0,O1,O2,O3},
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
	@$(RM) -f $(EXE_OUT) $(LIB_OUT) $(SAN_OUT) $(MACOS_X86_64_BINARY) cccc-afl cccc-afl-asan cccc-prof fuzz_harness
	@$(RM) -rf profile/*.prof profile/*.txt profile/*.json profile/*.massif
	@$(RM) -rf fuzz/corpus fuzz/out
	@$(RM) -rf $(LIBBACKTRACE_OBJS_DIR) $(LIBBACKTRACE_A)

# dsym — (macOS only) run dsymutil to produce cccc.dSYM with full DWARF.
# After running this, host C backtraces will include file:line information.
# On Linux the -g build already embeds DWARF inline; this target is a no-op there.
dsym:
ifeq ($(UNAME_S),Darwin)
	dsymutil $(EXE_OUT)
	@echo "cccc.dSYM written. Host backtraces now resolve to file:line."
else
	@echo "dsym: DWARF is already embedded in the ELF binary on Linux; nothing to do."
endif

.PHONY: default test clean all asan ubsan tsan sanitizers afl afl-asan fuzz fuzz_harness bench profile-cpu profile-cpu-build profile-mem fuzz-all fuzz-seed fuzz-run fuzz-crashes fuzz-triage fuzz-minimize fuzz-info stdlib bench-compare bench-compare-quick bench-compare-json macos-x86_64-build macos-x86_64-smoke macos-x86_64-test linux-x86_64-check linux-x86_64-build linux-x86_64-smoke linux-x86_64-test linux-x86_64-msan-test linux-aarch64-check linux-aarch64-build linux-aarch64-smoke linux-aarch64-test sqlite-smoke dsym
ifeq ($(UNAME_S),Linux)
.PHONY: msan
endif
