# Fuzzing Makefile for JCC
# Requires AFL++ to be installed (afl-clang-fast, afl-fuzz, afl-tmin)

JCC := ../jcc-afl
CORPUS_DIR := corpus
OUT_DIR := out
CRASH_DIR := $(OUT_DIR)/crashes

# AFL++ settings
AFL_TIMEOUT := 1000
AFL_MEMORY := none
AFL_FLAGS := -I../include -c

.PHONY: all seed fuzz crashes triage minimize clean clean-crashes libfuzzer

all: seed fuzz

seed:
	@echo "Seeding corpus from tests/..."
	@mkdir -p $(CORPUS_DIR)
	@cp ../tests/test_*.c $(CORPUS_DIR)/ 2>/dev/null || true
	@cp ../tests/pragma/test_pragma_*.c $(CORPUS_DIR)/ 2>/dev/null || true
	@echo "Corpus seeded with $$(ls $(CORPUS_DIR) | wc -l) files"

fuzz:
	@if [ ! -f "$(JCC)" ]; then \
		echo "Error: $(JCC) not found. Run 'make afl' in parent directory first."; \
		exit 1; \
	fi
	@mkdir -p $(OUT_DIR)
	@echo "Starting AFL++ fuzzing..."
	@echo "  input:  $(CORPUS_DIR)"
	@echo "  output: $(OUT_DIR)"
	afl-fuzz -i $(CORPUS_DIR) -o $(OUT_DIR) -m $(AFL_MEMORY) -t $(AFL_TIMEOUT) \
		-- $(JCC) $(AFL_FLAGS) @@

crashes:
	@if [ -d "$(CRASH_DIR)" ]; then \
		echo "Crashes found:"; \
		ls -1 $(CRASH_DIR)/id* 2>/dev/null || echo "  (none yet)"; \
	else \
		echo "No crash directory yet."; \
	fi

triage:
	@if [ -d "$(CRASH_DIR)" ]; then \
		for f in $(CRASH_DIR)/id*; do \
			echo "=== $$f ==="; \
			$(JCC) $(AFL_FLAGS) "$$f" 2>&1 | head -n 20; \
			echo; \
		done \
	else \
		echo "No crash directory yet."; \
	fi

minimize:
	@if [ -d "$(CRASH_DIR)" ]; then \
		for f in $(CRASH_DIR)/id*; do \
			base=$$(basename "$$f"); \
			echo "Minimizing $$base..."; \
			afl-tmin -i "$$f" -o "$(CRASH_DIR)/$${base}.min" -- $(JCC) $(AFL_FLAGS) @@; \
		done \
	else \
		 echo "No crash directory yet."; \
	fi

libfuzzer:
	@echo "Build libFuzzer harness from ../src/fuzz_harness.c"
	@echo "  cd .. && make fuzz_harness"

clean-crashes:
	@rm -rf $(OUT_DIR)
	@echo "Removed $(OUT_DIR)/"

clean: clean-crashes
	@rm -rf $(CORPUS_DIR)
	@echo "Removed $(CORPUS_DIR)/ and $(OUT_DIR)/"
