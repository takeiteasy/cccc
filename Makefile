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

fuzz: afl
	@echo "AFL++ binary built: ./jcc-afl"
	@echo "Run fuzzing with:"
	@echo "  cd fuzz && make seed && make fuzz"
	@echo ""
	@echo "For ASan + AFL++ combo (slower, more sensitive):"
	@echo "  make afl-asan"

# libFuzzer harness (optional)
fuzz_harness: src/fuzz_harness.c $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=fuzzer,address -o $@ $(filter-out src/main.c, $(SRCS)) $< $(LDFLAGS)

test: clean $(EXE_OUT)
	@python3 tests.py

all: clean $(EXE_OUT) $(LIB_OUT) test docs

docs:
	@headerdoc2html src/jcc.h include/reflection_api.h -o docs/; \
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

.PHONY: default test clean docs all asan ubsan tsan sanitizers afl afl-asan fuzz fuzz_harness bench profile-cpu profile-cpu-build profile-mem
ifeq ($(UNAME_S),Linux)
.PHONY: msan
endif
