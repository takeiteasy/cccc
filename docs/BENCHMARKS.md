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
benchmark    cccc    cccc-O1  cccc-O2  cccc-O3  cccc-O4  cccc-jbc  cccc-jbc-O1  cccc-jbc-O2  cccc-jbc-O3  cccc-jbc-O4  gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  ------  -------  -------  -------  -------  --------  -----------  -----------  -----------  -----------  ------  ------  ------  ------
ackermann    694.2   747.0    927.6    927.9    890.6    664.9     670.8        896.5        898.4        865.0        14.4    12.6    4.0     4.0
binary_tree  851.9   856.9    961.0    960.5    954.1    819.6     819.8        927.1        940.1        939.8        18.0    17.2    16.9    17.1
fib          577.7   587.1    676.4    680.0    681.5    549.6     548.6        648.7        652.6        648.8        6.2     5.9     3.2     4.2
mandelbrot   6515.9  6509.9   6438.1   6412.2   6231.2   6515.4    6821.4       6402.3       6533.0       6179.3       64.0    29.5    29.4    27.9
matrix_mul   5174.2  5060.7   4410.0   4573.6   4286.4   5049.3    5264.3       4405.7       4587.3       4198.2       19.5    5.5     3.9     3.6
nqueens      1331.6  1270.5   1429.0   1172.5   1193.4   1252.9    1191.9       1144.2       1191.6       1204.4       9.0     6.9     6.5     5.8
quicksort    2024.5  1800.1   1750.3   1764.3   1533.9   1850.7    1818.6       1722.7       1641.0       1509.4       12.3    7.7     7.7     7.6
sieve        9711.1  9531.2   7423.8   7329.7   7071.0   9516.0    9285.7       7406.0       7316.5       7110.6       35.0    21.5    17.8    18.2

Speedup vs gcc -O2 (>1.0x = slower than gcc -O2):
benchmark    cccc     cccc-O1  cccc-O2  cccc-O3  cccc-O4  cccc-jbc  cccc-jbc-O1  cccc-jbc-O2  cccc-jbc-O3  cccc-jbc-O4  gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  -------  -------  -------  -------  -------  --------  -----------  -----------  -----------  -----------  ------  ------  ------  ------
ackermann    175.7x   189.0x   234.8x   234.8x   225.4x   168.3x    169.8x       226.9x       227.4x       218.9x       3.6x    3.2x    1.0x    1.0x
binary_tree  50.5x    50.8x    57.0x    56.9x    56.6x    48.6x     48.6x        55.0x        55.7x        55.7x        1.1x    1.0x    1.0x    1.0x
fib          179.5x   182.4x   210.2x   211.3x   211.8x   170.8x    170.5x       201.6x       202.8x       201.6x       1.9x    1.8x    1.0x    1.3x
mandelbrot   221.8x   221.6x   219.1x   218.3x   212.1x   221.8x    232.2x       217.9x       222.4x       210.3x       2.2x    1.0x    1.0x    0.95x
matrix_mul   1326.9x  1297.8x  1130.9x  1172.9x  1099.3x  1294.9x   1350.0x      1129.8x      1176.4x      1076.6x      5.0x    1.4x    1.0x    0.94x
nqueens      205.1x   195.7x   220.1x   180.6x   183.8x   193.0x    183.6x       176.2x       183.6x       185.5x       1.4x    1.1x    1.0x    0.90x
quicksort    262.5x   233.4x   227.0x   228.8x   198.9x   240.0x    235.8x       223.4x       212.8x       195.7x       1.6x    0.99x   1.0x    0.98x
sieve        545.7x   535.6x   417.2x   411.9x   397.4x   534.8x    521.8x       416.2x       411.2x       399.6x       2.0x    1.2x    1.0x    1.0x
geomean      246.82x  243.42x  248.46x  243.38x  234.50x  236.93x   237.02x      237.55x      239.42x      230.90x      2.08x   1.34x   1.00x   1.01x

Correctness: all benchmarks produce identical output across all configs
```

> **Note:** The `gcc*` columns use Homebrew GCC-15 (auto-detected by `bench.py` when the system `gcc` is Apple Clang). GCC-15 is substantially faster than Apple Clang on some workloads — notably `ackermann` (deep recursion) and `fib` — so the `×` ratios for those benchmarks are larger than they were when earlier runs used Clang. The `cccc*` absolute timings are directly comparable with older runs.

Key VM improvements reflected in these numbers:
- **#227 — inlined threaded dispatch**: opcode logic embedded directly at each computed-goto label (~1.2–1.7× on VM-bound workloads).
- **#250 — fused local load/store opcodes**: `LEA3+LDR/STR` two-opcode sequence replaced by a single `LDR_LOCAL_*`/`STR_LOCAL_*` (~23% geomean improvement).
- **#249 — scalar local promotion**: at `--optimize=2`+, hot eligible integer/pointer locals held in VM saved registers, flushed at exits — reduces repeated local load/store traffic in tight loops.
- **#251 — indexed load/store opcodes**: at `--optimize=2`+, `base + index * scale` patterns use `LDR_INDEX_*`/`STR_INDEX_*` fused opcodes — removes explicit MUL+ADD address calculation from array loops.
- **#261 — automatic opcode fusion**: at `--optimize=4` or with `--fuse-ops`, adjacent single-def/single-use arithmetic chains are rewritten to fused opcodes (`MULI3`, `MULADD3`, `MULADDI3`). The largest wins in this run are quicksort (1764ms → 1534ms vs `--optimize=3`), sieve (7330ms → 7071ms), matrix_mul (4574ms → 4286ms), and mandelbrot (6412ms → 6231ms).
- **#415 — CSE for `[[gnu::const]]` + extended dead-call elimination**: at `--optimize=2`+, duplicate calls to const functions within a straight-line block are replaced by a register move. Dead-call elimination extended to indirect (CALLN) and FFI (CALLF) calls at `--optimize=1`+. CSE fires on repeated calls with matching constant or local-variable argument values; these benchmarks do not exercise that pattern, so no change is visible in the table above.

Validation run (2026-06-14, `--runs 3 --warmup 1`, Homebrew GCC-15): all correctness checks passed. `--optimize=4` improves the geomean from `--optimize=3`'s 243.38× slower-than-GCC-O2 ratio to 234.50×. The fused pass helps array/loop-heavy workloads and can regress or barely move pure-recursion/control-flow workloads where there are few profitable adjacent arithmetic chains.

Re-run `make bench-compare` to get updated numbers for your machine.

JSON output is also written to `profile/benchmarks/results/run-<UTC>.json` for tracking over time. The validation run above is saved as `profile/benchmarks/results/run-20260614T171315Z.json`. Each `cccc-jbc*` row includes a `compile_ms` field showing the one-time cost of producing the bytecode file (this cost is paid once, not in the timed median).

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
