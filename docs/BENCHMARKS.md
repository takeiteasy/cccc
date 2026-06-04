# JCC Benchmarks

A focused cross-compiler benchmark suite that compares **JCC** (across all `--optimize` levels) against **GCC** (across `-O0` through `-O3`). Every benchmark is plain C99/C11, so the comparison is apples-to-apples.

## Quick start

```bash
make bench-compare            # full run: 3 timed iterations per (bench, config), ~10 min
make bench-compare-quick      # 2 iterations, ~5 min, good for quick checks
python3 bench.py --filter fib.c    # run a single benchmark
```

Sample output:

```
================================================================================
 JCC vs GCC benchmark results (median ms, lower is better)
================================================================================
benchmark    jcc      jcc-O1   jcc-O2   jcc-O3   gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  -------  -------  -------  -------  ------  ------  ------  ------
ackermann    832.3    829.9    838.6    890.0    16.6    13.7    4.4     4.8
binary_tree  1184.8   1172.5   1154.2   1153.4   19.8    20.2    20.0    17.1
fib          702.4    708.9    710.4    710.5    7.2     6.7     4.0     6.4
mandelbrot   8323.5   8415.6   8639.9   8392.8   69.2    32.7    33.3    29.7
matrix_mul   7229.8   7174.4   7219.8   7286.1   21.2    6.0     4.8     5.0
nqueens      1727.9   1733.2   1721.9   1704.0   10.5    5.5     5.5     5.7
quicksort    2439.1   2443.8   2434.7   2435.8   14.0    8.4     8.4     8.8
sieve        13329.7  13823.8  13327.7  13716.6  41.2    24.9    21.0    23.9

Speedup vs gcc -O2 (>1.0x = slower than gcc -O2):
benchmark    jcc      jcc-O1   jcc-O2   jcc-O3   gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  -------  -------  -------  -------  ------  ------  ------  ------
ackermann    188.6x   188.0x   190.0x   201.6x   3.7x    3.1x    1.0x    1.1x
...
geomean      284.62x  286.07x  285.45x  287.52x  2.11x   1.29x   1.00x   1.07x

Correctness: all benchmarks produce identical output across all configs
```

JSON output is also written to `benchmarks/results/run-<UTC>.json` for tracking over time.

## The benchmark suite

All programs are portable C99/C11, exit with code `42` (so the standard `tests.py` smoke-runs them for free), and print a single canonical `result: …` line on stdout. Each takes a single optional compile-time size via `-DBENCH_N=<value>` (default tuned for ~1-15s on `jcc` default).

| Benchmark | What it measures | Default size | Result |
|-----------|------------------|--------------|--------|
| `fib.c` | Recursive Fibonacci, call overhead, int math | `n = 30` | `result: 832040` |
| `sieve.c` | Sieve of Eratosthenes, array access, int math | `limit = 10,000,000` | prime count + sum |
| `nqueens.c` | 10-queens backtracking, branching, recursion | `N = 10` | `result: 724` |
| `matrix_mul.c` | 200×200 double matrix multiply, FP, cache | `N = 200` | `result: <checksum>` |
| `quicksort.c` | Quicksort 100k random ints, recursion, arrays | `N = 100,000` | sorted-array sum |
| `mandelbrot.c` | 400×400 mandelbrot, 200 iters, FP, branching | `400×400, 200` | total iter count |
| `binary_tree.c` | BST insert + inorder traversal, pointer chasing, malloc | `N = 100,000` | visit count + sum |
| `ackermann.c` | `ack(3, 8)`, deep recursion, stack pressure | `M=3, N=8` | `result: 2045` |

## How it works

`bench.py` does the following for each benchmark:

1. **Compile** the source with GCC at every optimization level (cached in `build/`).
2. **Run** JCC at every `--optimize` level (cold each time — this measures the full compile+execute cost, which is the user-visible cost of using JCC).
3. **Run** the prebuilt GCC binaries.
4. **Time** each run with `time.perf_counter()`; discard `N` warmup runs, time `R` runs, take min/median/mean.
5. **Verify** that every config's stdout matches the JCC reference. A mismatch is flagged in the report and causes a non-zero exit.
6. **Report** as a human-readable table + a JSON file.

## What's being measured

The JCC column measures **end-to-end wall time**: source on disk → bytecode compilation → VM startup → bytecode execution → exit. The GCC columns measure **only execution time** of a prebuilt native binary. This is the honest comparison: it answers "if I just run my program, how long does it take?"

If you want to break out compile time vs execution time for JCC, see `make bench` (hyperfine) and `make profile-cpu` in the existing [PROFILING.md](PROFILING.md).

## Correctness across compilers

C11 leaves some leeway for floating-point contraction (FMA), which can produce bit-different results between `-O0` and `-O2`. To keep the comparison fair, `bench.py` compiles GCC with `-ffp-contract=off -std=c11`. This matches the C11 default FP semantics and matches what JCC's bytecode interpreter does (no FMA opcodes).

If you see a `MISMATCH` in the output, the JCC output differs from at least one GCC config. That's worth investigating — either a JCC bug, a missing `-D` define, or a benchmark that needs a tolerance check.

## Tips for getting clean numbers

- **Close other apps** to reduce noise. These are wall-clock timings.
- **Run multiple iterations** (`--runs 5` or more) for benchmarks under ~50ms.
- **Use `--filter`** to iterate on a single benchmark while tuning it.
- **Compare JSON files over time** — `benchmarks/results/run-*.json` includes the compiler versions, host info, and run settings so results are reproducible.

## Adding a new benchmark

1. Drop a `<name>.c` in `benchmarks/`.
2. The contract:
   - Plain C99/C11 (no jcc extensions, no GLIBC-only headers).
   - Optionally `#define BENCH_N <default>` so size is tunable.
   - Print a single canonical line: `result: <value>`.
   - `return 42;`.
3. `python3 bench.py --filter "<name>.c"` to verify it runs and matches GCC.
4. The standard `tests.py` will pick it up automatically (exit code 42).

If the benchmark has a per-program result that varies by FP order of operations, consider using integer-only arithmetic or summing a checksum over a deterministic input.

## Cross-compiler flag notes

- `bench.py` auto-detects when the system `gcc` is actually Apple Clang (on macOS) and switches to a Homebrew `gcc-15`/`gcc-14`/etc. if available. Pass `--gcc PATH` to override.
- Add clang to the matrix by editing `JCC_CONFIGS` / `GCC_CONFIGS` in `bench.py` (or wait for the v2 extension that adds a `--with-clang` flag).

## Reading the report

- **`median ms`** — middle value of the timed runs. Robust against outliers.
- **`min ms`** — fastest run. Useful as a lower bound.
- **`stable`** — whether every run produced identical stdout.
- **Speedup vs gcc -O2** — `median_ms / median_gcc_O2_ms`. Above 1.0× means slower than gcc -O2. Below 1.0× means faster (rare for JCC today, but possible on specific workloads).
- **`geomean`** — geometric mean of the per-benchmark ratios, computed across all benchmarks. The right "overall" comparison number.
