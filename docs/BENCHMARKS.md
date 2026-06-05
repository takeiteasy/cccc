# JCC Benchmarks

A focused cross-compiler benchmark suite that compares **JCC** (across all `--optimize` levels) against **GCC** (across `-O0` through `-O3`). Every benchmark is plain C99/C11, so the comparison is apples-to-apples. The suite also times JCC's precompiled-bytecode mode (`jcc-jbc*`) so you can separate the bytecode VM cost from the source-to-bytecode compile cost.

## Quick start

```bash
make bench-compare            # full run: 3 timed iterations per (bench, config), ~10 min
make bench-compare-quick      # 2 iterations, ~5 min, good for quick checks
python3 tools/bench.py --filter fib.c    # run a single benchmark
python3 tools/bench.py --no-jbc --filter fib.c   # skip the jcc-jbc columns
python3 tools/bench.py --filter fib.c --vm-profile   # also write opcode profile JSON
```

Sample output:

```
====================================================================================================
 JCC vs GCC benchmark results (median ms, lower is better)
====================================================================================================
benchmark    jcc      jcc-O1   jcc-O2   jcc-O3   jcc-jbc  jcc-jbc-O1  jcc-jbc-O2  jcc-jbc-O3  gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  -------  -------  -------  -------  -------  ----------  ----------  ----------  ------  ------  ------  ------
ackermann    883.1    851.5    842.2    860.9    827.1    822.5       899.9       835.1       153.0   134.3   123.9   123.5
binary_tree  1283.5   1210.5   1275.6   1459.1   1446.1   1396.5      1482.3      1200.0      186.9   152.9   153.0   137.3
fib          822.5    824.3    847.3    752.0    682.4    684.0       671.7       673.7       132.1   125.5   123.0   124.0
mandelbrot   8372.5   8423.1   8341.1   9158.5   9125.2   9076.9      8784.4      9123.3      268.9   160.7   153.4   152.6
matrix_mul   7580.8   7542.4   7429.3   7307.1   8485.5   7790.3      7315.6      7337.3      149.4   114.6   118.7   113.4
nqueens      1862.7   1740.3   1946.8   1746.4   2190.2   1691.2      1666.1      1695.1      132.2   114.0   115.3   117.3
quicksort    2456.1   2557.5   2462.7   2448.3   2369.1   2403.7      2385.1      2383.7      133.2   116.7   127.0   178.4
sieve        13309.6  13263.9  13244.3  13297.3  13397.8  13256.5     13208.6     13171.6     222.1   139.6   146.2   141.9

Speedup vs gcc -O2 (>1.0x = slower than gcc -O2):
benchmark    jcc      jcc-O1   jcc-O2   jcc-O3   jcc-jbc  jcc-jbc-O1  jcc-jbc-O2  jcc-jbc-O3  gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  -------  -------  -------  -------  -------  ----------  ----------  ----------  ------  ------  ------  ------
ackermann    7.1x     6.9x     6.8x     6.9x     6.7x     6.6x        7.3x        6.7x        1.2x    1.1x    1.0x    1.00x
binary_tree  8.4x     7.9x     8.3x     9.5x     9.5x     9.1x        9.7x        7.8x        1.2x    1.00x   1.0x    0.90x
fib          6.7x     6.7x     6.9x     6.1x     5.5x     5.6x        5.5x        5.5x        1.1x    1.0x    1.0x    1.0x
mandelbrot   54.6x    54.9x    54.4x    59.7x    59.5x    59.2x       57.3x       59.5x       1.8x    1.0x    1.0x    0.99x
matrix_mul   63.8x    63.5x    62.6x    61.5x    71.5x    65.6x       61.6x       61.8x       1.3x    0.96x   1.0x    0.96x
nqueens      16.2x    15.1x    16.9x    15.1x    19.0x    14.7x       14.5x       14.7x       1.1x    0.99x   1.0x    1.0x
quicksort    19.3x    20.1x    19.4x    19.3x    18.7x    18.9x       18.8x       18.8x       1.0x    0.92x   1.0x    1.4x
sieve        91.0x    90.7x    90.6x    90.9x    91.6x    90.7x       90.3x       90.1x       1.5x    0.95x   1.0x    0.97x
geomean      21.12x   20.80x   21.11x   21.12x   21.65x   20.64x      20.66x      20.08x      1.26x   1.00x   1.00x   1.02x

Correctness: all benchmarks produce identical output across all configs
```

> **Note:** The table above predates the inlined threaded dispatch introduced in the VM (ticket #227). That change embeds each opcode's logic directly at its computed-goto label instead of calling a separate C function per instruction, delivering a **1.2–1.7× speedup** on VM-bound workloads when the JCC binary is built at `-O2` (fib: 1.21×, nqueens: 1.50×, sieve: 1.69×). Re-run `make bench-compare` to get updated numbers for your machine.

JSON output is also written to `benchmarks/results/run-<UTC>.json` for tracking over time. Each `jcc-jbc*` row includes a `compile_ms` field showing the one-time cost of producing the bytecode file (this cost is paid once, not in the timed median).

## The benchmark suite

All programs are portable C99/C11, exit with code `42` (so the standard `tools/tests.py` smoke-runs them for free), and print a single canonical `result: …` line on stdout. Each takes a single optional compile-time size via `-DBENCH_N=<value>` (default tuned for ~1-15s on `jcc` default).

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

`tools/bench.py` does the following for each benchmark:

1. **Compile** the source with GCC at every optimization level (cached in `build/`).
2. **Compile** the source with JCC at every `--optimize` level to a `.jbc` bytecode file (cached in `build/`, like the GCC binaries).
3. **Run** JCC at every `--optimize` level on the source directly — this measures the full parse+execute cost, which is the user-visible cost of using JCC.
4. **Run** the prebuilt `.jbc` files — this measures just the bytecode VM cost (the source-to-bytecode compile step was paid once and is not in the timed median).
5. **Run** the prebuilt GCC binaries.
6. **Time** each run with `time.perf_counter()`; discard `N` warmup runs, time `R` runs, take min/median/mean.
7. **Verify** that every config's stdout matches the JCC reference. A mismatch is flagged in the report and causes a non-zero exit.
8. **Report** as a human-readable table + a JSON file.

With `--vm-profile`, JCC and JCC-JBC configs also write dynamic opcode count
profiles to `benchmarks/results/vm-profile-<UTC>/`. The benchmark JSON records
the `vm_profile_json` path for each profiled config.

## What's being measured

There are three different timings per benchmark:

- **`jcc*`** — end-to-end wall time: source on disk → bytecode compilation → VM startup → bytecode execution → exit. This is the user-visible cost of just running `jcc myfile.c`.
- **`jcc-jbc*`** — bytecode execution only: load a precompiled `.jbc` from disk and run it. The source-to-bytecode compile cost was paid once (reported in the JSON's `compile_ms` field) and is not part of the timed median.
- **`gcc*`** — execution time of a prebuilt native binary. Compile cost paid once, not in the timed median.

The `jcc-jbc*` columns are the cleanest apples-to-apples comparison with GCC: both are "compile once, run many times" measurements. The `jcc*` columns show what you'd actually pay as an end user of the `jcc` CLI.

If you want to break out compile time vs execution time for JCC, see `make bench` (hyperfine) and `make profile-cpu` in the existing [PROFILING.md](PROFILING.md).
If you want to see where VM execution is concentrated, use `tools/bench.py --vm-profile`
and compare dynamic counts for opcodes such as `LEA3`, `LDR_D`, `STR_D`,
`ADD3`, and `MUL3` across optimization levels.

## Bytecode (.jbc) configs

The `jcc-jbc*` configs use JCC's precompiled-bytecode mode:

```bash
./jcc --optimize=N -o build/fib.jbc benchmarks/fib.c   # compile once
./jcc build/fib.jbc                                    # run many times
```

The `.jbc` files are cached in `build/` (alongside the GCC binaries) and rebuilt only when missing. The timed command is just `[jcc, file.jbc]` — load + execute. The JSON output includes a `compile_ms` field per `jcc-jbc*` config so you can see the upfront compile cost alongside the run-time cost.

The bytecode format self-resolves FFI symbols (libc functions like `printf`, `malloc`) via `dlsym` on load, so `.jbc` files built on one machine run on the same machine without bundling libc. Use `--no-jbc` to skip these columns for faster iteration when you only care about parse+exec numbers.

## Correctness across compilers

C11 leaves some leeway for floating-point contraction (FMA), which can produce bit-different results between `-O0` and `-O2`. To keep the comparison fair, `tools/bench.py` compiles GCC with `-ffp-contract=off -std=c11`. This matches the C11 default FP semantics and matches what JCC's bytecode interpreter does (no FMA opcodes).

If you see a `MISMATCH` in the output, the JCC output differs from at least one GCC config. That's worth investigating — either a JCC bug, a missing `-D` define, or a benchmark that needs a tolerance check.

## Tips for getting clean numbers

- **Close other apps** to reduce noise. These are wall-clock timings.
- **Run multiple iterations** (`--runs 5` or more) for benchmarks under ~50ms.
- **Use `--filter`** to iterate on a single benchmark while tuning it.
- **Use `--no-jbc`** when iterating on parse/compile performance — it cuts the bench in half by skipping the bytecode-execution columns.
- **Use `--vm-profile`** when optimizing bytecode generation or VM dispatch — it shows dynamic opcode mix for each JCC config.
- **Compare JSON files over time** — `benchmarks/results/run-*.json` includes the compiler versions, host info, and run settings so results are reproducible.

## Adding a new benchmark

1. Drop a `<name>.c` in `benchmarks/`.
2. The contract:
   - Plain C99/C11 (no jcc extensions, no GLIBC-only headers).
   - Optionally `#define BENCH_N <default>` so size is tunable.
   - Print a single canonical line: `result: <value>`.
   - `return 42;`.
3. `python3 tools/bench.py --filter "<name>.c"` to verify it runs and matches GCC.
4. The standard `tools/tests.py` will pick it up automatically (exit code 42).

If the benchmark has a per-program result that varies by FP order of operations, consider using integer-only arithmetic or summing a checksum over a deterministic input.

## Cross-compiler flag notes

- `tools/bench.py` auto-detects when the system `gcc` is actually Apple Clang (on macOS) and switches to a Homebrew `gcc-15`/`gcc-14`/etc. if available. Pass `--gcc PATH` to override.
- Add clang to the matrix by editing `JCC_CONFIGS` / `GCC_CONFIGS` in `tools/bench.py` (or wait for the v2 extension that adds a `--with-clang` flag).

## Reading the report

- **`median ms`** — middle value of the timed runs. Robust against outliers.
- **`min ms`** — fastest run. Useful as a lower bound.
- **`stable`** — whether every run produced identical stdout.
- **`compile_ms`** — for `jcc-jbc*` configs only, the one-time cost of producing the `.jbc` file. Not part of the timed median.
- **Speedup vs gcc -O2** — `median_ms / median_gcc_O2_ms`. Above 1.0× means slower than gcc -O2. Below 1.0× means faster (rare for JCC today, but possible on specific workloads).
- **`geomean`** — geometric mean of the per-benchmark ratios, computed across all benchmarks. The right "overall" comparison number.
