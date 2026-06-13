# CCCC Benchmarks

A focused cross-compiler benchmark suite that measures the cost of the **CCCC bytecode VM** (the runtime that powers `[[cccc::macro]]` execution, the safety suite, the debugger, and the profiler) by comparing it against **GCC** (across `-O0` through `-O3`). Every benchmark is plain C99/C11, so the comparison is apples-to-apples. The suite also times CCCC's precompiled-bytecode mode (`cccc-jbc*`) so you can separate the bytecode VM cost from the source-to-bytecode compile cost.

For production builds, CCCC also offers a `-c=native` mode that hands macro-expanded C to `cc` / `clang` / `gcc` — that path bypasses the VM entirely and matches the `gcc*` columns below. The benchmarks in this document are deliberately scoped to the VM, because that is the part CCCC is responsible for.

## Quick start

```bash
make bench-compare            # full run: 3 timed iterations per (bench, config), ~10 min
make bench-compare-quick      # 2 iterations, ~5 min, good for quick checks
python3 tools/bench.py --filter fib.c    # run a single benchmark
python3 tools/bench.py --no-jbc --filter fib.c   # skip the cccc-jbc columns
python3 tools/bench.py --filter fib.c --vm-profile   # also write opcode profile JSON
```

Sample output:

```
====================================================================================================
 CCCC vs GCC benchmark results (median ms, lower is better)
====================================================================================================
benchmark    cccc    cccc-O1  cccc-O2  cccc-O3  cccc-jbc  cccc-jbc-O1  cccc-jbc-O2  cccc-jbc-O3  gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  ------  -------  -------  -------  --------  -----------  -----------  -----------  ------  ------  ------  ------
ackermann    690.1   685.6    918.5    919.4    666.1     660.7        883.2        885.7        19.1    12.6    4.4     4.7
binary_tree  808.4   797.7    900.8    901.5    805.8     771.5        893.8        865.3        18.4    17.4    33.5    16.1
fib          562.5   557.5    656.0    656.8    533.4     531.2        622.6        621.9        6.2     6.2     3.6     3.8
mandelbrot   6062.2  6108.3   5937.0   5980.9   6031.4    6114.4       5887.8       5925.8       63.7    29.6    29.4    27.8
matrix_mul   4654.2  4660.3   3927.5   3940.1   4618.1    4638.1       3894.2       3903.5       19.9    5.8     4.0     3.8
nqueens      1270.1  1179.7   1081.0   1076.7   1242.0    1155.9       1046.7       1047.0       8.5     5.4     5.8     4.9
quicksort    1727.7  1646.4   1577.0   1534.6   1696.8    1616.2       1512.4       1508.6       12.5    7.9     8.2     7.7
sieve        8926.8  8704.1   6683.1   6745.0   9252.9    8949.5       6951.6       6875.4       36.4    23.0    18.5    19.3

Speedup vs gcc -O2 (>1.0x = slower than gcc -O2):
benchmark    cccc     cccc-O1  cccc-O2  cccc-O3  cccc-jbc  cccc-jbc-O1  cccc-jbc-O2  cccc-jbc-O3  gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  -------  -------  -------  -------  --------  -----------  -----------  -----------  ------  ------  ------  ------
ackermann    157.2x   156.1x   209.2x   209.4x   151.7x    150.5x       201.1x       201.7x       4.3x    2.9x    1.0x    1.1x
binary_tree  24.1x    23.8x    26.9x    26.9x    24.0x     23.0x        26.7x        25.8x        0.55x   0.52x   1.0x    0.48x
fib          154.9x   153.5x   180.7x   180.9x   146.9x    146.3x       171.5x       171.2x       1.7x    1.7x    1.0x    1.0x
mandelbrot   206.5x   208.1x   202.3x   203.8x   205.5x    208.3x       200.6x       201.9x       2.2x    1.0x    1.0x    0.95x
matrix_mul   1158.6x  1160.2x  977.7x   980.9x   1149.6x   1154.6x      969.4x       971.8x       5.0x    1.4x    1.0x    0.96x
nqueens      219.2x   203.6x   186.6x   185.9x   214.4x    199.5x       180.7x       180.7x       1.5x    0.94x   1.0x    0.85x
quicksort    211.7x   201.8x   193.3x   188.1x   207.9x    198.1x       185.3x       184.9x       1.5x    0.97x   1.0x    0.94x
sieve        481.9x   469.9x   360.8x   364.1x   499.5x    483.1x       375.3x       371.2x       2.0x    1.2x    1.0x    1.0x
geomean      205.19x  200.95x  200.51x  200.29x  202.41x   197.62x      196.77x      195.90x      1.93x   1.20x   1.00x   0.89x

Correctness: all benchmarks produce identical output across all configs
```

> **Note:** The `gcc*` columns use Homebrew GCC-15 (auto-detected by `bench.py` when the system `gcc` is Apple Clang). GCC-15 is substantially faster than Apple Clang on some workloads — notably `ackermann` (deep recursion) and `fib` — so the `×` ratios for those benchmarks are larger than they were when earlier runs used Clang. The `cccc*` absolute timings are directly comparable with older runs.

Key VM improvements reflected in these numbers:
- **#227 — inlined threaded dispatch**: opcode logic embedded directly at each computed-goto label (~1.2–1.7× on VM-bound workloads).
- **#250 — fused local load/store opcodes**: `LEA3+LDR/STR` two-opcode sequence replaced by a single `LDR_LOCAL_*`/`STR_LOCAL_*` (~23% geomean improvement).
- **#249 — scalar local promotion**: at `--optimize=2`+, hot eligible integer/pointer locals held in VM saved registers, flushed at exits — reduces repeated local load/store traffic in tight loops.
- **#251 — indexed load/store opcodes**: at `--optimize=2`+, `base + index * scale` patterns use `LDR_INDEX_*`/`STR_INDEX_*` fused opcodes — removes explicit MUL+ADD address calculation from array loops.
- **#415 — CSE for `[[gnu::const]]` + extended dead-call elimination**: at `--optimize=2`+, duplicate calls to const functions within a straight-line block are replaced by a register move. Dead-call elimination extended to indirect (CALLN) and FFI (CALLF) calls at `--optimize=1`+. CSE fires on repeated calls with matching constant or local-variable argument values; these benchmarks do not exercise that pattern, so no change is visible in the table above.

Validation run (2026-06-14, `--runs 2 --warmup 1`, Homebrew GCC-15): all correctness checks passed. The dominant wins at `--optimize=2` relative to no optimization remain sieve (8926ms → 6683ms, 25% reduction), matrix_mul (4654ms → 3928ms, 16% reduction), nqueens (1270ms → 1081ms, 15% reduction), and quicksort (1728ms → 1577ms, 9% reduction). Ackermann and fib regress slightly under optimization because the optimizer passes add overhead with no benefit for pure-recursion workloads.

Re-run `make bench-compare` to get updated numbers for your machine.

JSON output is also written to `profile/benchmarks/results/run-<UTC>.json` for tracking over time. Each `cccc-jbc*` row includes a `compile_ms` field showing the one-time cost of producing the bytecode file (this cost is paid once, not in the timed median).

## The benchmark suite

All programs are portable C99/C11, exit with code `42` (so the standard `tools/tests.py` smoke-runs them for free), and print a single canonical `result: …` line on stdout. Each takes a single optional compile-time size via `-DBENCH_N=<value>` (default tuned for ~1-15s on `cccc` default).

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
2. **Compile** the source with CCCC at every `--optimize` level to a `.jbc` bytecode file (cached in `build/`, like the GCC binaries).
3. **Run** CCCC at every `--optimize` level on the source directly — this measures the full parse+execute cost, which is the user-visible cost of using CCCC.
4. **Run** the prebuilt `.jbc` files — this measures just the bytecode VM cost (the source-to-bytecode compile step was paid once and is not in the timed median).
5. **Run** the prebuilt GCC binaries.
6. **Time** each run with `time.perf_counter()`; discard `N` warmup runs, time `R` runs, take min/median/mean.
7. **Verify** that every config's stdout matches the CCCC reference. A mismatch is flagged in the report and causes a non-zero exit.
8. **Report** as a human-readable table + a JSON file.

With `--vm-profile`, CCCC and CCCC-JBC configs also write dynamic opcode count
profiles to `profile/benchmarks/results/vm-profile-<UTC>/`. The benchmark JSON records
the `vm_profile_json` path for each profiled config.

## What's being measured

There are three different timings per benchmark:

- **`cccc*`** — end-to-end wall time: source on disk → bytecode compilation → VM startup → bytecode execution → exit. This is the user-visible cost of just running `cccc myfile.c`.
- **`cccc-jbc*`** — bytecode execution only: load a precompiled `.jbc` from disk and run it. The source-to-bytecode compile cost was paid once (reported in the JSON's `compile_ms` field) and is not part of the timed median.
- **`gcc*`** — execution time of a prebuilt native binary. Compile cost paid once, not in the timed median.

The `cccc-jbc*` columns are the cleanest apples-to-apples comparison with GCC: both are "compile once, run many times" measurements. The `cccc*` columns show what you'd actually pay as an end user of the `cccc` CLI.

`-c=native` is not in the matrix above: it would be redundant with `gcc*` because it just hands the same C to a system compiler. If you want to add a `-c=native` column, run `./cccc -c=native -o build/<bench>.bin profile/benchmarks/<bench>.c` and time `[build/<bench>.bin]` — the numbers should match `gcc-O2`/`gcc-O3` (plus a fixed CCCC frontend cost, which is the same for every column and not what this doc is measuring).

If you want to break out compile time vs execution time for CCCC, see `make bench` (hyperfine) and `make profile-cpu` in the existing [PROFILING.md](PROFILING.md).
If you want to see where VM execution is concentrated, use `tools/bench.py --vm-profile`
and compare dynamic counts for opcodes such as `LDR_LOCAL_D`, `STR_LOCAL_D`,
`LDR_INDEX_W`, `STR_INDEX_W`, `ADD3`, and `MUL3` across optimization levels.

## Bytecode (.jbc) configs

The `cccc-jbc*` configs use CCCC's precompiled-bytecode mode:

```bash
./cccc --optimize=N -o build/fib.jbc profile/benchmarks/fib.c   # compile once
./cccc build/fib.jbc                                    # run many times
```

The `.jbc` files are cached in `build/` (alongside the GCC binaries) and rebuilt only when missing. The timed command is just `[cccc, file.jbc]` — load + execute. The JSON output includes a `compile_ms` field per `cccc-jbc*` config so you can see the upfront compile cost alongside the run-time cost.

The bytecode format self-resolves FFI symbols (libc functions like `printf`, `malloc`) via `dlsym` on load, so `.jbc` files built on one machine run on the same machine without bundling libc. Use `--no-jbc` to skip these columns for faster iteration when you only care about parse+exec numbers.

## Correctness across compilers

C11 leaves some leeway for floating-point contraction (FMA), which can produce bit-different results between `-O0` and `-O2`. To keep the comparison fair, `tools/bench.py` compiles GCC with `-ffp-contract=off -std=c11`. This matches the C11 default FP semantics and matches what CCCC's bytecode interpreter does (no FMA opcodes).

If you see a `MISMATCH` in the output, the CCCC output differs from at least one GCC config. That's worth investigating — either a CCCC bug, a missing `-D` define, or a benchmark that needs a tolerance check.

## Tips for getting clean numbers

- **Close other apps** to reduce noise. These are wall-clock timings.
- **Run multiple iterations** (`--runs 5` or more) for benchmarks under ~50ms.
- **Use `--filter`** to iterate on a single benchmark while tuning it.
- **Use `--no-jbc`** when iterating on parse/compile performance — it cuts the bench in half by skipping the bytecode-execution columns.
- **Use `--vm-profile`** when optimizing bytecode generation or VM dispatch — it shows dynamic opcode mix for each CCCC config.
- **Compare JSON files over time** — `profile/benchmarks/results/run-*.json` includes the compiler versions, host info, and run settings so results are reproducible.

## Adding a new benchmark

1. Drop a `<name>.c` in `profile/benchmarks/`.
2. The contract:
   - Plain C99/C11 (no cccc extensions, no GLIBC-only headers).
   - Optionally `#define BENCH_N <default>` so size is tunable.
   - Print a single canonical line: `result: <value>`.
   - `return 42;`.
3. `python3 tools/bench.py --filter "<name>.c"` to verify it runs and matches GCC.
4. The standard `tools/tests.py` will pick it up automatically (exit code 42).

If the benchmark has a per-program result that varies by FP order of operations, consider using integer-only arithmetic or summing a checksum over a deterministic input.

## Cross-compiler flag notes

- `tools/bench.py` auto-detects when the system `gcc` is actually Apple Clang (on macOS) and switches to a Homebrew `gcc-15`/`gcc-14`/etc. if available. Pass `--gcc PATH` to override.
- Add clang to the matrix by editing `CCCC_CONFIGS` / `GCC_CONFIGS` in `tools/bench.py` (or wait for the v2 extension that adds a `--with-clang` flag).

## Reading the report

- **`median ms`** — middle value of the timed runs. Robust against outliers.
- **`min ms`** — fastest run. Useful as a lower bound.
- **`stable`** — whether every run produced identical stdout.
- **`compile_ms`** — for `cccc-jbc*` configs only, the one-time cost of producing the `.jbc` file. Not part of the timed median.
- **Speedup vs gcc -O2** — `median_ms / median_gcc_O2_ms`. Above 1.0× means slower than gcc -O2. Below 1.0× means faster (rare for CCCC today, but possible on specific workloads).
- **`geomean`** — geometric mean of the per-benchmark ratios, computed across all benchmarks. The right "overall" comparison number.
