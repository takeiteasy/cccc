# CCCC Benchmarks

A focused cross-compiler benchmark suite that measures the cost of the **CCCC bytecode VM** (the runtime that powers `[[cccc::macro]]` execution, the safety suite, the debugger, and the profiler) by comparing it against **GCC** (across `-O0` through `-O3`). Every benchmark is plain C99/C11, so the comparison is apples-to-apples. The suite also times CCCC's precompiled-bytecode mode (`cccc-c4*`) so you can separate the bytecode VM cost from the source-to-bytecode compile cost.

For production builds, CCCC also offers a `-c=native` mode that hands macro-expanded C to `cc` / `clang` / `gcc` — that path bypasses the VM entirely and matches the `gcc*` columns below. The benchmarks in this document are deliberately scoped to the VM, because that is the part CCCC is responsible for.

## Quick start

```bash
make bench-compare            # full run: 3 timed iterations per (bench, config), ~10 min
make bench-compare-quick      # 2 iterations, ~5 min, good for quick checks
python3 tools/bench.py --filter fib.c    # run a single benchmark
python3 tools/bench.py --no-c4 --filter fib.c   # skip the cccc-c4 columns
python3 tools/bench.py --filter fib.c --vm-profile   # also write opcode profile JSON
```

Sample output:

```
====================================================================================================
 CCCC vs GCC benchmark results (median ms, lower is better)
====================================================================================================
benchmark    cccc    cccc-O1  cccc-O2  cccc-O3  cccc-O4  cccc-c4  cccc-c4-O1  cccc-c4-O2  cccc-c4-O3  cccc-c4-O4  gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  ------  -------  -------  -------  -------  -------  ----------  ----------  ----------  ----------  ------  ------  ------  ------
ackermann    682.9   681.9    918.5    793.8    781.7    652.3    652.2       889.8       765.9       749.6       16.6    16.4    4.3     6.0   
binary_tree  868.6   859.5    963.1    824.1    809.9    821.1    823.5       922.9       788.2       777.2       20.6    20.3    19.4    22.9  
fib          573.7   597.4    674.6    580.0    577.3    550.2    543.8       651.5       545.8       550.3       7.5     8.8     4.1     7.3   
mandelbrot   6101.2  6065.3   4983.7   3970.9   3626.0   6064.4   6078.5      4992.8      3970.4      3610.3      62.0    30.4    29.4    28.4  
matrix_mul   5014.6  4997.7   4136.5   3747.4   3309.6   4980.4   5024.9      4131.1      3741.5      3318.6      24.6    5.7     4.0     3.8   
nqueens      1322.5  1234.3   1141.5   979.8    948.9    1297.3   1210.7      1121.7      953.7       921.3       14.4    5.1     8.1     9.2   
quicksort    1829.9  1772.4   1565.3   1498.8   1378.8   1808.9   1745.4      1531.0      1466.5      1343.3      18.5    9.1     12.5    13.0  
sieve        9660.6  9328.2   7331.3   7311.1   7075.3   9519.7   9295.3      7865.2      7292.1      7067.4      36.2    24.4    21.2    20.4  

Speedup vs gcc -O2 (>1.0x = slower than gcc -O2):
benchmark    cccc     cccc-O1  cccc-O2  cccc-O3  cccc-O4  cccc-c4  cccc-c4-O1  cccc-c4-O2  cccc-c4-O3  cccc-c4-O4  gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  -------  -------  -------  -------  -------  -------  ----------  ----------  ----------  ----------  ------  ------  ------  ------
ackermann    158.4x   158.2x   213.0x   184.1x   181.3x   151.3x   151.3x      206.4x      177.6x      173.9x      3.9x    3.8x    1.0x    1.4x  
binary_tree  44.8x    44.3x    49.6x    42.5x    41.7x    42.3x    42.4x       47.6x       40.6x       40.1x       1.1x    1.0x    1.0x    1.2x  
fib          141.2x   147.1x   166.1x   142.8x   142.1x   135.4x   133.9x      160.4x      134.4x      135.5x      1.9x    2.2x    1.0x    1.8x  
mandelbrot   207.3x   206.1x   169.4x   135.0x   123.2x   206.1x   206.6x      169.7x      134.9x      122.7x      2.1x    1.0x    1.0x    0.97x 
matrix_mul   1265.2x  1260.9x  1043.6x  945.5x   835.0x   1256.6x  1267.8x     1042.3x     944.0x      837.3x      6.2x    1.4x    1.0x    0.97x 
nqueens      162.6x   151.8x   140.4x   120.5x   116.7x   159.5x   148.9x      137.9x      117.3x      113.3x      1.8x    0.63x   1.0x    1.1x  
quicksort    146.6x   142.0x   125.4x   120.1x   110.5x   144.9x   139.8x      122.7x      117.5x      107.6x      1.5x    0.73x   1.0x    1.0x  
sieve        455.0x   439.4x   345.3x   344.4x   333.3x   448.4x   437.8x      370.5x      343.5x      332.9x      1.7x    1.1x    1.0x    0.96x 
geomean      202.70x  199.76x  192.52x  170.37x  162.05x  197.64x  194.54x     190.65x     166.28x     158.25x     2.14x   1.27x   1.00x   1.15x 

Correctness: all benchmarks produce identical output across all configs
```

> **Note:** The `gcc*` columns use Homebrew GCC-15 (auto-detected by `bench.py` when the system `gcc` is Apple Clang). GCC-15 is substantially faster than Apple Clang on some workloads — notably `ackermann` (deep recursion) and `fib` — so the `×` ratios for those benchmarks are larger than they were when earlier runs used Clang. The `cccc*` absolute timings are directly comparable with older runs.

Key VM improvements reflected in these numbers:
- **#227 — inlined threaded dispatch**: opcode logic embedded directly at each computed-goto label (~1.2–1.7× on VM-bound workloads).
- **#250 — fused local load/store opcodes**: `LEA3+LDR/STR` two-opcode sequence replaced by a single `LDR_LOCAL_*`/`STR_LOCAL_*` (~23% geomean improvement).
- **#249 — scalar local promotion**: at `--optimize=2`+, hot eligible integer/pointer locals held in VM saved registers, flushed at exits — reduces repeated local load/store traffic in tight loops.
- **#251 — indexed load/store opcodes**: at `--optimize=2`+, `base + index * scale` patterns use `LDR_INDEX_*`/`STR_INDEX_*` fused opcodes — removes explicit MUL+ADD address calculation from array loops.
- **#261 — automatic opcode fusion**: at `--optimize=4` or with `--fuse-ops`, adjacent single-def/single-use arithmetic chains are rewritten to fused opcodes (`MULI3`, `MULADD3`, `MULADDI3`).
- **#415 — CSE for `[[gnu::const]]` + extended dead-call elimination**: at `--optimize=2`+, duplicate calls to const functions within a straight-line block are replaced by a register move. Dead-call elimination extended to indirect (CALLN) and FFI (CALLF) calls at `--optimize=1`+.
- **#461 — float/double local promotion**: at `--optimize=2`+, hot floating-point locals held in VM saved FP registers (`FREG_S0`–`FREG_S3`) — eliminates per-iteration `FLDR_LOCAL`/`FSTR_LOCAL` round-trips in FP-heavy loops. Notable improvement on mandelbrot (6412ms → 5034ms at `--optimize=3`).
- **#462 — fused FP multiply-add (`FMADD3`/`FMADD3_F32`)**: at `--optimize=4` or with `--fuse-ops`, adjacent `FMUL3+FADD3` chains are rewritten to a single `FMADD3` dispatch — one less opcode per multiply-accumulate iteration. Largest visible wins are matrix_mul (4007ms → 3444ms vs `--optimize=3`) and mandelbrot (5034ms → 4782ms). Add `--fma` to additionally enable single-rounding FMA (see correctness note below).
- **#478 — fused FP multiply-subtract (`FMSUB3`/`FMSUB3_F32`)**: extends the fusion pass to the minuend form (`a*b - c`) of `FMUL3+FSUB3` — emits `FMSUB3` when the multiply result is the left-hand operand of the subtraction.
- **#479 — fused negated multiply-subtract (`FNMSUB3`/`FNMSUB3_F32`) + dead-FMOV3 elimination**: adds the accumulating-subtract form (`sum -= a*b`, i.e. `rd = rs1 - rs2*rs3`) as `FNMSUB3`. The dead-FMOV3 elimination in copy-prop (sub-pass C global use-count scan) removes the promoted-register read copy that the float local promotion pass inserts between `FMUL3` and `FSUB3`, restoring adjacency so the fusion fires on loop patterns. Combined impact: mandelbrot `--optimize=3` 5034ms → 3990ms (−21%), `--optimize=4` 4782ms → 3643ms (−24%); matrix_mul `--optimize=4` 3444ms → 3325ms (−3%).

Validation run (2026-06-18, `--runs 2`, Homebrew GCC-15): all correctness checks passed. `--optimize=4` geomean is 162.05× slower than GCC-O2 (down from 182.61× before #479). The dead-FMOV3 elimination in copy-prop (#479) delivered the largest single improvement on FP-heavy workloads: mandelbrot O3 167.8× → 135.0×, O4 159.4× → 123.2×. The fused pass helps array/loop-heavy workloads and can regress or barely move pure-recursion/control-flow workloads where there are few profitable adjacent arithmetic chains.

Re-run `make bench-compare` to get updated numbers for your machine.

JSON output is also written to `profile/benchmarks/results/run-<UTC>.json` for tracking over time. Each `cccc-c4*` row includes a `compile_ms` field showing the one-time cost of producing the bytecode file (this cost is paid once, not in the timed median).

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
2. **Compile** the source with CCCC at every `--optimize` level to a `.c4` bytecode file (cached in `build/`, like the GCC binaries).
3. **Run** CCCC at every `--optimize` level on the source directly — this measures the full parse+execute cost, which is the user-visible cost of using CCCC.
4. **Run** the prebuilt `.c4` files — this measures just the bytecode VM cost (the source-to-bytecode compile step was paid once and is not in the timed median).
5. **Run** the prebuilt GCC binaries.
6. **Time** each run with `time.perf_counter()`; discard `N` warmup runs, time `R` runs, take min/median/mean.
7. **Verify** that every config's stdout matches the CCCC reference. A mismatch is flagged in the report and causes a non-zero exit.
8. **Report** as a human-readable table + a JSON file.

With `--vm-profile`, CCCC and CCCC-C4 configs also write dynamic opcode count
profiles to `profile/benchmarks/results/vm-profile-<UTC>/`. The benchmark JSON records
the `vm_profile_json` path for each profiled config.

## What's being measured

There are three different timings per benchmark:

- **`cccc*`** — end-to-end wall time: source on disk → bytecode compilation → VM startup → bytecode execution → exit. This is the user-visible cost of just running `cccc myfile.c`.
- **`cccc-c4*`** — bytecode execution only: load a precompiled `.c4` from disk and run it. The source-to-bytecode compile cost was paid once (reported in the JSON's `compile_ms` field) and is not part of the timed median.
- **`gcc*`** — execution time of a prebuilt native binary. Compile cost paid once, not in the timed median.

The `cccc-c4*` columns are the cleanest apples-to-apples comparison with GCC: both are "compile once, run many times" measurements. The `cccc*` columns show what you'd actually pay as an end user of the `cccc` CLI.

`-c=native` is not in the matrix above: it would be redundant with `gcc*` because it just hands the same C to a system compiler. If you want to add a `-c=native` column, run `./cccc -c=native -o build/<bench>.bin profile/benchmarks/<bench>.c` and time `[build/<bench>.bin]` — the numbers should match `gcc-O2`/`gcc-O3` (plus a fixed CCCC frontend cost, which is the same for every column and not what this doc is measuring).

If you want to break out compile time vs execution time for CCCC, see `make bench` (hyperfine) and `make profile-cpu` in the existing [PROFILING.md](PROFILING.md).
If you want to see where VM execution is concentrated, use `tools/bench.py --vm-profile`
and compare dynamic counts for opcodes such as `LDR_LOCAL_D`, `STR_LOCAL_D`,
`LDR_INDEX_W`, `STR_INDEX_W`, `ADD3`, and `MUL3` across optimization levels.

## Bytecode (.c4) configs

The `cccc-c4*` configs use CCCC's precompiled-bytecode mode:

```bash
./cccc --optimize=N -o build/fib.c4 profile/benchmarks/fib.c   # compile once
./cccc build/fib.c4                                    # run many times
```

The `.c4` files are cached in `build/` (alongside the GCC binaries) and rebuilt only when missing. The timed command is just `[cccc, file.c4]` — load + execute. The JSON output includes a `compile_ms` field per `cccc-c4*` config so you can see the upfront compile cost alongside the run-time cost.

The bytecode format self-resolves FFI symbols (libc functions like `printf`, `malloc`) via `dlsym` on load, so `.c4` files built on one machine run on the same machine without bundling libc. Use `--no-c4` to skip these columns for faster iteration when you only care about parse+exec numbers.

## Correctness across compilers

C11 leaves some leeway for floating-point contraction (FMA), which can produce bit-different results between `-O0` and `-O2`. To keep the comparison fair, `tools/bench.py` compiles GCC with `-ffp-contract=off -std=c11`. CCCC's default `FMADD3` opcode uses two separate roundings (product rounded to `double` first, then added) — semantically identical to the separate `FMUL3`+`FADD3` it replaces, so the benchmark outputs match GCC `-ffp-contract=off` exactly.

The optional `--fma` flag enables true single-rounding FMA (`fma()`/`fmaf()` in the VM handler). This can yield a few percent additional speedup on multiply-accumulate loops but **will diverge from GCC `-ffp-contract=off`** on inputs where the intermediate product has rounding error. The benchmark suite does not run with `--fma` by default; it is only appropriate when your program can tolerate slightly different FP results.

If you see a `MISMATCH` in the output, the CCCC output differs from at least one GCC config. That's worth investigating — either a CCCC bug, a missing `-D` define, or a benchmark that needs a tolerance check.

## Tips for getting clean numbers

- **Close other apps** to reduce noise. These are wall-clock timings.
- **Run multiple iterations** (`--runs 5` or more) for benchmarks under ~50ms.
- **Use `--filter`** to iterate on a single benchmark while tuning it.
- **Use `--no-c4`** when iterating on parse/compile performance — it cuts the bench in half by skipping the bytecode-execution columns.
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
- **`compile_ms`** — for `cccc-c4*` configs only, the one-time cost of producing the `.c4` file. Not part of the timed median.
- **Speedup vs gcc -O2** — `median_ms / median_gcc_O2_ms`. Above 1.0× means slower than gcc -O2. Below 1.0× means faster (rare for CCCC today, but possible on specific workloads).
- **`geomean`** — geometric mean of the per-benchmark ratios, computed across all benchmarks. The right "overall" comparison number.
