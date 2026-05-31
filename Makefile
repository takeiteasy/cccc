SRCS := $(wildcard src/*.c src/stdlib/*.c)
CFLAGS := -Wall -O0 -g -std=c23 -Wno-deprecated-declarations -Wno-switch -Wno-inline-asm
LDFLAGS :=

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
EXE_OUT := jcc$(EXE)
LIB_OUT := libjcc$(DYLIB)
SAN_OUT := jcc-asan jcc-ubsan jcc-tsan

ifeq ($(UNAME_S),Linux)
	SAN_OUT += jcc-msan
endif

default: $(EXE_OUT)


$(EXE_OUT): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(LIB_OUT): $(SRCS)
	$(CC) -fpic -shared $(CFLAGS) -o $@ $(filter-out src/main.c, $^) $(LDFLAGS)

jcc-asan: $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ $^ $(LDFLAGS)

jcc-ubsan: $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=undefined -o $@ $^ $(LDFLAGS)

jcc-tsan: $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=thread -o $@ $^ $(LDFLAGS)

ifeq ($(UNAME_S),Linux)
jcc-msan: $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=memory -o $@ $^ $(LDFLAGS)
endif

asan: jcc-asan
ubsan: jcc-ubsan
tsan: jcc-tsan
ifeq ($(UNAME_S),Linux)
msan: jcc-msan
endif

sanitizers: asan ubsan tsan
ifeq ($(UNAME_S),Linux)
sanitizers: msan
endif

# AFL++ fuzzing build
# Detect available AFL compiler wrapper (afl-clang-fast preferred, fallback to afl-clang)
AFL_CC := $(shell which afl-clang-fast 2>/dev/null || which afl-clang 2>/dev/null || echo "")

jcc-afl: $(SRCS)
ifeq ($(AFL_CC),)
	@echo "Error: AFL++ compiler wrapper not found. Install AFL++ first."
	@exit 1
else
	$(AFL_CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
endif

afl: jcc-afl

# AFL++ + AddressSanitizer combo (slower but catches memory errors immediately)
jcc-afl-asan: $(SRCS)
ifeq ($(AFL_CC),)
	@echo "Error: AFL++ compiler wrapper not found. Install AFL++ first."
	@exit 1
else
	AFL_USE_ASAN=1 $(AFL_CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
endif

afl-asan: jcc-afl-asan

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
	@cp tests/pragma/test_pragma_*.c $(FUZZ_CORPUS)/ 2>/dev/null || true
	@echo "Corpus seeded with $$(ls $(FUZZ_CORPUS) | wc -l) files"

fuzz-run: jcc-afl
	@if [ ! -f "jcc-afl" ]; then \
		echo "Error: jcc-afl not found. Run 'make afl' first."; \
		exit 1; \
	fi
	@mkdir -p $(FUZZ_OUT)
	@echo "Starting AFL++ fuzzing..."
	@echo "  input:  $(FUZZ_CORPUS)"
	@echo "  output: $(FUZZ_OUT)"
	afl-fuzz -i $(FUZZ_CORPUS) -o $(FUZZ_OUT) -m $(FUZZ_MEMORY) -t $(FUZZ_TIMEOUT) \
		-- ./jcc-afl $(FUZZ_FLAGS) @@

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
			./jcc-afl $(FUZZ_FLAGS) "$$f" 2>&1 | head -n 20; \
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
			afl-tmin -i "$$f" -o "$(FUZZ_CRASHES)/$${base}.min" -- ./jcc-afl $(FUZZ_FLAGS) @@; \
		done \
	else \
		echo "No crash directory yet."; \
	fi

fuzz-info:
	@echo "AFL++ binary built: ./jcc-afl"
	@echo "Run fuzzing with:"
	@echo "  make fuzz-seed && make fuzz-run"
	@echo ""
	@echo "For ASan + AFL++ combo (slower, more sensitive):"
	@echo "  make afl-asan"

fuzz: fuzz-info

STD_TEMPLATE := gen_std.c

# Regenerate src/std.c from the template.
# src/std.c is committed so the normal build never needs this; run it
# explicitly after editing gen_std.c or include/*.h.
.PHONY: generate-std
generate-std: $(EXE_OUT)
	@printf '#include <string.h>\n\n' > src/std.c
	./$(EXE_OUT) -G -I./include $(STD_TEMPLATE) >> src/std.c

test: clean $(EXE_OUT)
	@python3 tests.py

all: clean $(EXE_OUT) $(LIB_OUT) test docs

docs:
	@headerdoc2html src/jcc.h include/reflection.h -o docs/; \
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
		'./jcc -I./include $(PROFILE_TEST)'
endif

profile-cpu-build: $(SRCS)
ifeq ($(HAS_GPROFILER),)
	@echo "Error: gperftools libprofiler not found. Install with 'brew install gperftools'."
	@exit 1
else
ifeq ($(UNAME_S),Darwin)
	$(CC) $(CFLAGS) -o jcc-prof $^ $(LDFLAGS) -L/opt/homebrew/lib -lprofiler
else
	$(CC) $(CFLAGS) -o jcc-prof $^ $(LDFLAGS) -lprofiler
endif
endif

profile-cpu: profile-cpu-build
	@mkdir -p profile
	CPUPROFILE=profile/cpu.prof ./jcc-prof -I./include $(PROFILE_TEST) || true
	@echo "CPU profile saved to profile/cpu.prof"
	@echo "To view: install Go pprof (go install github.com/google/pprof@latest)"
	@echo "  pprof -text jcc-prof profile/cpu.prof"

profile-mem:
ifeq ($(UNAME_S),Darwin)
	@mkdir -p profile
	@echo "Running with leaks tool..."
	leaks -atExit -- ./jcc -I./include $(PROFILE_TEST) > profile/mem-leaks.txt 2>&1 || true
	@echo "Memory leak report: profile/mem-leaks.txt"
else ifeq ($(UNAME_S),Linux)
	@mkdir -p profile
	valgrind --tool=massif --massif-out-file=profile/mem.massif \
		./jcc -I./include $(PROFILE_TEST)
	@echo "Massif output: profile/mem.massif"
else
	@echo "Memory profiling not supported on this platform."
	@exit 1
endif

clean:
	@$(RM) -f $(EXE_OUT) $(LIB_OUT) $(SAN_OUT) jcc-afl jcc-afl-asan jcc-prof fuzz_harness
	@$(RM) -rf profile/*.prof profile/*.txt profile/*.json profile/*.massif
	@$(RM) -rf fuzz/corpus fuzz/out

.PHONY: default test clean docs all asan ubsan tsan sanitizers afl afl-asan fuzz fuzz_harness bench profile-cpu profile-cpu-build profile-mem fuzz-all fuzz-seed fuzz-run fuzz-crashes fuzz-triage fuzz-minimize fuzz-info generate-std
ifeq ($(UNAME_S),Linux)
.PHONY: msan
endif
