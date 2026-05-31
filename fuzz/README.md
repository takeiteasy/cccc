# JCC Fuzzing

Fuzzing harnesses and scripts for JCC using AFL++ and libFuzzer.

## Quick Start

### 1. Build the AFL++ instrumented binary

```bash
make afl
```

This produces `jcc-afl` in the project root, compiled with `afl-clang-fast`.

### 2. Seed the corpus

```bash
make seed
```

Copies all `tests/test_*.c` files into `fuzz/corpus/` as seed inputs.

### 3. Run AFL++

```bash
make fuzz
```

Starts `afl-fuzz` in the background with sensible defaults:
- Input: `fuzz/corpus/`
- Output: `fuzz/out/`
- Timeout: 1000ms
- Memory: none (unlimited)
- Flags: `-I../include -c` (compile-only, no `main()` required)

### 4. Inspect crashes

```bash
make crashes        # list crash files
make triage         # run each crash to see ASan/UBSan output
make minimize       # minimize all crashes with afl-tmin
```

## Manual Usage

```bash
# Seed corpus from existing tests
cp ../tests/test_*.c corpus/

# Run AFL++ (single instance)
afl-fuzz -i corpus -o out -m none -t 1000 -- ../jcc-afl -I../include -c @@

# Run with ASan + AFL++ (slower but catches more bugs)
make afl-asan
afl-fuzz -i corpus -o out -m none -t 1000 -- ../jcc-afl-asan -I../include -c @@

# Resume a stopped session
afl-fuzz -i - -o out -m none -t 1000 -- ../jcc-afl -I../include -c @@
```

## Corpus Tips

- The existing `tests/` suite provides excellent seeds — they cover many C constructs.
- AFL++ will mutate these; even removing `main()` is fine because `-c` (compile-only)
  does not require an entry point.
- For deeper fuzzing, add hand-crafted seeds for edge cases:
  - Empty files
  - Very long identifiers
  - Deeply nested parentheses/braces
  - Unicode in comments/strings
  - Malformed preprocessor directives

## libFuzzer (optional)

A persistent-mode harness is available in `../src/fuzz_harness.c`.
Build and run it with:

```bash
make libfuzzer
./fuzz_harness corpus/
```

## Cleanup

```bash
make clean          # remove corpus, output, and crash directories
make clean-crashes # keep corpus, remove only crashes and out/
```
