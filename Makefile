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

# Optional IEEE-754-2008 decimal floating-point (_Decimal32/64/128) support
# via the Intel BID library. The library is NEVER vendored in this repo --
# run tools/fetch_intel_bid.sh first to fetch, verify, and build it.
# Enable with: make CCCC_HAS_DECIMAL=1 [CCCC_BID_PREFIX=build/intel-bid]
ifdef CCCC_HAS_DECIMAL
  ifneq ($(CCCC_HAS_DECIMAL),0)
    CCCC_BID_PREFIX ?= build/intel-bid
    BID_A := $(CCCC_BID_PREFIX)/lib/libbid.a
    ifeq ($(wildcard $(BID_A)),)
      $(error CCCC_HAS_DECIMAL=1 but $(BID_A) is missing. Run tools/fetch_intel_bid.sh \
first (the Intel BID library is never vendored in this repo).)
    endif
    CFLAGS  += -DCCCC_HAS_DECIMAL=1 -I$(CCCC_BID_PREFIX)/src
    LDFLAGS += $(BID_A)
  endif
endif

# Optional readline support for the interactive REPL (-r/--repl) line
# history/editing. Falls back to plain fgets (no history) when not found.
LIBREADLINE_CFLAGS := $(shell $(PKG_CONFIG) --cflags readline 2>/dev/null)
LIBREADLINE_LDFLAGS := $(shell $(PKG_CONFIG) --libs readline 2>/dev/null)
ifeq ($(LIBREADLINE_LDFLAGS),)
  ifeq ($(shell uname -s),Darwin)
    # readline is keg-only on Homebrew (macOS ships libedit under the same
    # name), so it is never on the default include/lib search path.
    READLINE_TEST_CFLAGS := -I/opt/homebrew/opt/readline/include -I/usr/local/opt/readline/include
    READLINE_TEST_LDFLAGS := -L/opt/homebrew/opt/readline/lib -L/usr/local/opt/readline/lib -lreadline
  else
    READLINE_TEST_CFLAGS := -I/usr/include -I/usr/local/include
    READLINE_TEST_LDFLAGS := -L/usr/lib -L/usr/local/lib -lreadline
  endif
  ifneq ($(shell echo 'int main(void){return 0;}' | $(CC) -x c - $(READLINE_TEST_CFLAGS) $(READLINE_TEST_LDFLAGS) -o /dev/null 2>/dev/null && echo ok),)
    LIBREADLINE_CFLAGS := $(READLINE_TEST_CFLAGS)
    LIBREADLINE_LDFLAGS := $(READLINE_TEST_LDFLAGS)
  endif
endif

ifneq ($(LIBREADLINE_LDFLAGS),)
  CFLAGS += -DHAVE_READLINE $(LIBREADLINE_CFLAGS)
  LDFLAGS += $(LIBREADLINE_LDFLAGS)
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
		# iconv() is declared in libSystem's <iconv.h> but the symbols only
		# resolve at link time via libiconv (verified: link fails without
		# it, succeeds with it). glibc ships iconv in libc itself, so no
		# extra flag is needed on Linux.
		LDFLAGS += -liconv
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
    # Namespace the archive/objects by $(CC) so a cross-arch invocation (e.g.
    # macos-x86_64-build's CC="clang -arch x86_64") never links against a
    # stale archive built for a different architecture/compiler.
    empty :=
    space := $(empty) $(empty)
    LIBBACKTRACE_CC_TAG := $(subst /,_,$(subst $(space),_,$(strip $(CC))))
    LIBBACKTRACE_A    := build/libbacktrace-$(LIBBACKTRACE_CC_TAG).a
    LIBBACKTRACE_OBJS_DIR := build/libbacktrace-$(LIBBACKTRACE_CC_TAG)
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
# src/fuzzing.c is already part of $(SRCS) via the src/*.c wildcard -- do not
# also pass it as an explicit prerequisite/dependency, or it links twice and
# fails with a duplicate _LLVMFuzzerTestOneInput symbol on strict linkers (#708).
fuzz_harness: $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=fuzzer,address -o $@ $(filter-out src/main.c, $(SRCS)) $(LDFLAGS)

# --- Host-side test harnesses (#707) ---
# Link directly against the compiler sources (like fuzz_harness), bypassing
# tools/tests.py's guest-.c/exit-code-42 protocol. For regression tests that
# need to construct multiple VirtualMachine instances (or threads) in one
# host process -- not expressible as a single cccc-compiled source file.
HOST_TEST_SRCS := $(wildcard tests/host/test_*.c)
HOST_TEST_BINS := $(patsubst tests/host/%.c,build/host_tests/%,$(HOST_TEST_SRCS))

build/host_tests/%: tests/host/%.c $(filter-out src/main.c, $(SRCS)) | $(LIBBACKTRACE_A)
	@mkdir -p build/host_tests
	$(CC) -Isrc $(CFLAGS) -o $@ $< $(filter-out src/main.c, $(SRCS)) $(LDFLAGS)

host-tests: $(HOST_TEST_BINS)
	@fail=0; \
	for bin in $(HOST_TEST_BINS); do \
		echo "Running $$bin..."; \
		if ! $$bin; then fail=1; fi; \
	done; \
	if [ $$fail -eq 0 ]; then echo "All host tests passed! 🎉"; else exit 1; fi

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

# Audit src/stdlib/*.c FFI registrations against include/**/*.h declarations
# (num_args/returns_double/double_arg_mask mismatches, unregistered
# declarations, headers that register nothing at all). Pure source scan, no
# build required. See tools/audit_ffi.py and docs/TESTING.md.
audit-ffi:
	@python3 tools/audit_ffi.py

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
		LIBREADLINE_CFLAGS="-I/usr/local/opt/readline/include" \
		LIBREADLINE_LDFLAGS="-L/usr/local/opt/readline/lib -lreadline" \
		$(MACOS_X86_64_BINARY)
	@file $(MACOS_X86_64_BINARY)
	@file $(MACOS_X86_64_BINARY) | grep -q 'x86_64'

# --build-cache cross-arch regression check (#730): reusing the same
# --build-out-dir across a native build and a different-arch cccc binary
# must not serve wrong-arch objects to the link step. Reproduces the
# ticket's exact repro sequence.
build-cache-arch-smoke: $(EXE_OUT) macos-x86_64-build
	@set -eu; \
		outdir=build/build_cache_arch_smoke; \
		rm -rf "$$outdir"; \
		./$(EXE_OUT) -I./include --build --build-out-dir="$$outdir" --build-cache tests/test_build_cache.c >/dev/null; \
		file "$$outdir/bin/cache_app" | grep -q 'arm64\|aarch64\|arm64e' || file "$$outdir/bin/cache_app"; \
		/usr/bin/arch -x86_64 ./$(MACOS_X86_64_BINARY) -I./include --build --build-out-dir="$$outdir" --build-cache tests/test_build_cache.c; \
		file "$$outdir/bin/cache_app" | grep -q 'x86_64'; \
		/usr/bin/arch -x86_64 "$$outdir/bin/cache_app"; \
		rm -rf "$$outdir"; \
		echo "build-cache-arch-smoke: OK"

macos-x86_64-smoke: macos-x86_64-build build-cache-arch-smoke
	@set -eu; \
		machine=$$(/usr/bin/arch -x86_64 /usr/bin/uname -m); \
		echo "Rosetta machine: $$machine"; \
		test "$$machine" = "x86_64"; \
		rc=0; \
		/usr/bin/arch -x86_64 ./$(MACOS_X86_64_BINARY) -I./include tests/test_fortytwo.c || rc=$$?; \
		test "$$rc" -eq 42; \
		rc=0; \
		CCCC_NATIVE_CC=/usr/bin/clang /usr/bin/arch -x86_64 \
			./$(MACOS_X86_64_BINARY) -I./include --asm-passthru tests/test_asm_passthru.c || rc=$$?; \
		test "$$rc" -eq 42; \
		tmp=$$(mktemp /tmp/cccc-native-x86_64.XXXXXX); \
		trap 'rm -f "$$tmp"' EXIT; \
		CCCC_NATIVE_CC=/usr/bin/clang /usr/bin/arch -x86_64 \
			./$(MACOS_X86_64_BINARY) -c=native -o "$$tmp" tests/test_fortytwo.c; \
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
			rc=0; ./cccc -I./include tests/test_fortytwo.c || rc=$$?; \
			test "$$rc" -eq 42'

linux-x86_64-test: linux-x86_64-smoke
	@set +e; \
		rc=0; \
		for pattern in 'test_[a-f]*.c' 'test_[g-l]*.c' 'test_[m-r]*.c' \
			'test_[s-u]*.c' 'test_[v-z]*.c'; do \
			$(COLIMA_NERDCTL) run --rm --platform linux/amd64 \
				$(LINUX_AMD64_IMAGE) timeout 300 python3 tools/tests.py \
				--match "$$pattern" --quiet -j $(TEST_JOBS) || rc=1; \
		done; \
		for pattern in 'test_[a-f]*.c' 'test_[g-l]*.c' 'test_[m-r]*.c' \
			'test_[s-u]*.c' 'test_[v-z]*.c'; do \
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
			rc=0; ./cccc -I./include tests/test_fortytwo.c || rc=$$?; \
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
	@$(RM) -rf build/host_tests
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

.PHONY: default test clean all asan ubsan tsan sanitizers afl afl-asan fuzz fuzz_harness host-tests bench profile-cpu profile-cpu-build profile-mem fuzz-all fuzz-seed fuzz-run fuzz-crashes fuzz-triage fuzz-minimize fuzz-info stdlib bench-compare bench-compare-quick bench-compare-json macos-x86_64-build build-cache-arch-smoke macos-x86_64-smoke macos-x86_64-test linux-x86_64-check linux-x86_64-build linux-x86_64-smoke linux-x86_64-test linux-x86_64-msan-test linux-aarch64-check linux-aarch64-build linux-aarch64-smoke linux-aarch64-test sqlite-smoke audit-ffi dsym
ifeq ($(UNAME_S),Linux)
.PHONY: msan
endif
