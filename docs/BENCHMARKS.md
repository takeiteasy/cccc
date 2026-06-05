# JCC Benchmarks

A focused cross-compiler benchmark suite that measures the cost of the **JCC bytecode VM** (the runtime that powers `[[jcc::macro]]` execution, the safety suite, the debugger, and the profiler) by comparing it against **GCC** (across `-O0` through `-O3`). Every benchmark is plain C99/C11, so the comparison is apples-to-apples. The suite also times JCC's precompiled-bytecode mode (`jcc-jbc*`) so you can separate the bytecode VM cost from the source-to-bytecode compile cost.

For production builds, JCC also offers a `--native` mode that hands macro-expanded C to `cc` / `clang` / `gcc` — that path bypasses the VM entirely and matches the `gcc*` columns below. The benchmarks in this document are deliberately scoped to the VM, because that is the part JCC is responsible for.

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
benchmark    jcc       jcc-O1   jcc-O2   jcc-O3   jcc-jbc  jcc-jbc-O1  jcc-jbc-O2  jcc-jbc-O3  gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  --------  -------  -------  -------  -------  ----------  ----------  ----------  ------  ------  ------  ------
ackermann    721.2     734.9    726.3    715.0    706.5    698.7       720.9       688.0       235.8   132.9   123.7   123.9
binary_tree  862.1     858.8    876.2    861.0    922.2    880.8       878.5       853.7       150.5   138.5   139.1   140.6
fib          625.9     613.4    625.0    605.5    595.2    617.0       611.6       586.2       138.0   127.1   124.4   149.0
mandelbrot   6880.9    6942.3   6924.6   6803.2   7772.1   7459.9      8366.3      7094.8      260.4   158.8   152.8   149.7
matrix_mul   5827.0    6169.2   5537.7   5701.0   6474.0   5524.3      5296.7      5327.5      192.1   126.1   126.0   129.6
nqueens      1367.2    1246.4   1260.0   1318.3   1302.0   1221.1      1338.5      1242.9      131.5   123.1   124.2   123.0
quicksort    1841.7    2001.6   2074.0   2197.4   1843.2   1993.2      2339.9      2065.6      143.7   125.5   125.5   131.3
sieve        10107.7   9802.9   9710.2   9915.6   10313.1  11712.1     11935.8     11817.5     179.3   147.3   144.4   140.0

Speedup vs gcc -O2 (>1.0x = slower than gcc -O2):
benchmark    jcc     jcc-O1  jcc-O2  jcc-O3  jcc-jbc  jcc-jbc-O1  jcc-jbc-O2  jcc-jbc-O3  gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  ------  ------  ------  ------  -------  ----------  ----------  ----------  ------  ------  ------  ------
ackermann    5.8x    5.9x    5.9x    5.8x    5.7x     5.6x        5.8x        5.6x        1.9x    1.1x    1.0x    1.0x
binary_tree  6.2x    6.2x    6.3x    6.2x    6.6x     6.3x        6.3x        6.1x        1.1x    1.0x    1.0x    1.0x
fib          5.0x    4.9x    5.0x    4.9x    4.8x     5.0x        4.9x        4.7x        1.1x    1.0x    1.0x    1.2x
mandelbrot   45.0x   45.4x   45.3x   44.5x   50.9x    48.8x       54.8x       46.4x       1.7x    1.0x    1.0x    1.0x
matrix_mul   46.3x   49.0x   44.0x   45.3x   51.4x    43.9x       42.0x       42.3x       1.5x    1.0x    1.0x    1.0x
nqueens      11.0x   10.0x   10.1x   10.6x   10.5x    9.8x        10.8x       10.0x       1.1x    1.0x    1.0x    1.0x
quicksort    14.7x   15.9x   16.5x   17.5x   14.7x    15.9x       18.6x       16.5x       1.1x    1.0x    1.0x    1.0x
sieve        70.0x   67.9x   67.3x   68.7x   71.4x    81.1x       82.7x       81.9x       1.2x    1.0x    1.0x    1.0x
geomean      15.99x  16.04x  15.94x  16.08x  16.39x   16.23x      16.98x      15.98x      1.32x   1.02x   1.00x   1.03x

Correctness: all benchmarks produce identical output across all configs
```

Two significant VM improvements are reflected in these numbers:
- **#227 — inlined threaded dispatch**: opcode logic is embedded directly at each computed-goto label rather than dispatched through a C function call per instruction (1.2–1.7× improvement on VM-bound workloads).
- **#250 — fused local load/store opcodes**: the ubiquitous `LEA3+LDR/STR` two-opcode address+dereference sequence for local variables is replaced by a single `LDR_LOCAL_*`/`STR_LOCAL_*` opcode (~23% geomean improvement over the pre-#250 baseline).

Re-run `make bench-compare` to get updated numbers for your machine.

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

`--native` is not in the matrix above: it would be redundant with `gcc*` because it just hands the same C to a system compiler. If you want to add a `--native` column, run `./jcc --native -o build/<bench>.bin benchmarks/<bench>.c` and time `[build/<bench>.bin]` — the numbers should match `gcc-O2`/`gcc-O3` (plus a fixed JCC frontend cost, which is the same for every column and not what this doc is measuring).

If you want to break out compile time vs execution time for JCC, see `make bench` (hyperfine) and `make profile-cpu` in the existing [PROFILING.md](PROFILING.md).
If you want to see where VM execution is concentrated, use `tools/bench.py --vm-profile`
and compare dynamic counts for opcodes such as `LDR_LOCAL_D`, `STR_LOCAL_D`,
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
