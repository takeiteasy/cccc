# Fuzz regression corpus

`tests/fuzz/corpus/` holds plain `.c` files — one input per file, committed,
small, each a minimized reproducer for a crash a fuzzer found (or a
pathological input worth keeping as a regression guard).

## Format

- One `.c` file per reproducer, named `crash_NNNN.c` or `repro_<short-description>.c`.
- Keep files minimal — trim to the smallest input that still reproduces the
  issue before committing.
- Files are **never** discovered as tests by `tools/tests.py` (see the
  `"fuzz" not in f.parts` guard in `tools/testing/discovery.py`) and are
  **never executed** as guest programs — `tools/fuzz_replay.py` only
  compiles them.

## Adding a new reproducer

1. Minimize the crashing input (afl/libFuzzer `-minimize_crash=1`, or by
   hand).
2. Save it as `tests/fuzz/corpus/crash_NNNN.c` (next unused number) or a
   descriptively named `repro_*.c`.
3. Run `python3 tools/fuzz_replay.py --binary build/cccc` to confirm it now
   passes (compiles cleanly or is cleanly rejected — no crash, no timeout, no
   sanitizer report).

## Pass/fail criteria

For each corpus file, `tools/fuzz_replay.py` runs
`<binary> -I./include -c -o <tmp> <file>` under a per-file timeout:

- **pass** — process exits normally, whether that's a clean compile or a
  clean diagnostic (the compiler is allowed to reject garbage).
- **fail** — killed by a signal (SIGSEGV/SIGBUS/SIGABRT/…), exceeds the
  timeout, or stderr contains an ASan/UBSan report.

An empty or missing corpus directory is a clean skip, not a failure.
