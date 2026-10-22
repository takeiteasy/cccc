# Fuzzing

Fuzzing harnesses and scripts for CCCC using AFL++ and libFuzzer.

## Quick Start

### 1. Build the AFL++ instrumented binary

```bash
make afl
```

This produces `cccc-afl` in the project root, compiled with `afl-clang-fast`.

### 2. Seed the corpus

```bash
make fuzz-seed
```

Copies the committed `tests/fuzz/corpus/` regression corpus and all
`tests/test_*.c` files into `fuzz/seeds/` as seed inputs.

### 3. Run AFL++

```bash
make fuzz-run
```

Starts `afl-fuzz` in the background with sensible defaults:
- Input: `fuzz/seeds/`
- Output: `fuzz/out/`
- Timeout: 1000ms
- Memory: none (unlimited)
- Flags: `-I./include -c=generated` (serialize-and-exit, no `main()` required, no host toolchain invoked)

### 4. Inspect crashes

```bash
make fuzz-crashes    # list crash files
make fuzz-triage     # run each crash to see ASan/UBSan output
make fuzz-minimize   # minimize all crashes with afl-tmin
```

## Manual Usage

```bash
# Seed corpus from existing tests
cp tests/test_*.c fuzz/seeds/

# Run AFL++ (single instance)
afl-fuzz -i fuzz/seeds -o fuzz/out -m none -t 1000 -- ./cccc-afl -I./include -c=generated @@

# Run with ASan + AFL++ (slower but catches more bugs)
make afl-asan
afl-fuzz -i fuzz/seeds -o fuzz/out -m none -t 1000 -- ./cccc-afl-asan -I./include -c=generated @@

# Resume a stopped session
afl-fuzz -i - -o fuzz/out -m none -t 1000 -- ./cccc-afl -I./include -c=generated @@
```

## Corpus Tips

- The existing `tests/` suite provides excellent seeds — they cover many C constructs.
- AFL++ will mutate these; even removing `main()` is fine because
  `-c=generated` does not require an entry point.
- For deeper fuzzing, add hand-crafted seeds for edge cases:
  - Empty files
  - Very long identifiers
  - Deeply nested parentheses/braces
  - Unicode in comments/strings
  - Malformed preprocessor directives

## libFuzzer (optional)

A persistent-mode harness is available in `./src/fuzzing.c`.
Build and run it with:

```bash
make fuzz_harness
./fuzz_harness fuzz/seeds/
```

## Cleanup

```bash
make clean          # remove corpus, output, and crash directories
```
