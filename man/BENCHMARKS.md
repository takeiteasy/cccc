# CCCC Benchmark Results

Benchmark results for the CCCC bytecode compiler + VM interpreter compared to
native compilation.

The VM has a single configuration — `-O<n>` only affects `-c=native`, never
VM codegen — so there is one `cccc` column. Re-run `make bench-compare` (or
`python3 tools/bench.py`) to refresh these figures.

## Running Benchmarks

A focused cross-compiler benchmark suite that measures the cost of the **CCCC bytecode VM** by comparing it against **GCC** (across `-O0` through `-O3`). Every benchmark is plain C99/C11, so the comparison is apples-to-apples.

VM execution speed is an explicit non-goal, so there is **one** CCCC
configuration (the VM has no optimiser; `-O<n>` only affects `-c=native`).
The numbers below are from before the optimiser was removed and are kept
only for the shape of the gap — re-run `make bench-compare` for current
figures against the single `cccc` column.

For production builds, CCCC also offers a `-c=native` mode that hands macro-expanded C to `cc` / `clang` / `gcc` — that path bypasses the VM entirely and matches the `gcc*` columns below. The benchmarks in this document are deliberately scoped to the VM, because that is the part CCCC is responsible for.

### Quick Start

```bash
make bench-compare            # full run: 3 timed iterations per (bench, config), ~10 min
make bench-compare-quick      # 2 iterations, ~5 min, good for quick checks
python3 tools/bench.py --filter fib.c    # run a single benchmark
python3 tools/bench.py --filter fib.c --vm-profile   # also write opcode profile JSON
```

Sample output:

```
====================================================================================================
 CCCC vs GCC benchmark results (median ms, lower is better)
====================================================================================================
benchmark    cccc     gcc-O0   gcc-O1   gcc-O2   gcc-O3
-----------  -------  -------  -------  -------  -------
ackermann    682.9    16.6     16.4     4.3      6.0
fib          573.7    7.5      8.8      4.1      7.3
mandelbrot   6101.2   62.0     30.4     29.4     28.4
matrix_mul   5014.6   24.6     5.7      4.0      3.8
sieve        9660.6   36.2     24.4     21.2     20.4
(pre-#1214 `cccc` column; the removed optimiser's best geomean was ~1.25x
this. Re-run for current numbers.)

Correctness: all benchmarks produce identical output
```

> **Note:** The `gcc*` columns use Homebrew GCC-15 (auto-detected by `bench.py` when the system `gcc` is Apple Clang). GCC-15 is substantially faster than Apple Clang on some workloads — notably `ackermann` (deep recursion) and `fib` — so the `×` ratios for those benchmarks are larger than they were when earlier runs used Clang. The `cccc*` absolute timings are directly comparable with older runs.

Two always-on lowerings keep the interpreter from being needlessly slow (they are not tunable):
- **#227 — inlined threaded dispatch**: opcode logic embedded directly at each computed-goto label (~1.2–1.7× on VM-bound workloads).
- **#250 — fused local load/store opcodes**: `LEA3+LDR/STR` two-opcode sequence replaced by a single `LDR_LOCAL_*`/`STR_LOCAL_*` (~23% geomean improvement).

Everything the VM optimiser used to add on top (scalar/FP local promotion, indexed load/store fusion, opcode fusion, CSE, dead-call elimination) was removed in #1214 — it was ~1.25× geomean best case and frequently net-negative on recursion-heavy code, which did not justify its footprint.

Re-run `make bench-compare` to get numbers for your machine.

JSON output is also written to `profile/bench-results/run-<UTC>.json` for tracking over time.

### The Benchmark Suite

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

### How It Works

`tools/bench.py` does the following for each benchmark:

1. **Compile** the source with GCC at every optimization level (cached in `build/`).
2. **Run** CCCC on the source directly — this measures the full parse+execute cost.
3. **Run** the prebuilt GCC binaries.
4. **Time** each run with `time.perf_counter()`; discard `N` warmup runs, time `R` runs, take min/median/mean.
5. **Verify** that every config's stdout matches the CCCC reference. A mismatch is flagged and causes a non-zero exit.
6. **Report** as a human-readable table + a JSON file.

With `--vm-profile`, CCCC configs also write dynamic opcode count
profiles to `profile/bench-results/vm-profile-<UTC>/`.

### What's Being Measured

- **`cccc*`** — end-to-end wall time: source on disk → bytecode compilation → VM startup → bytecode execution → exit.
- **`gcc*`** — execution time of a prebuilt native binary.

### Correctness

C11 leaves some leeway for floating-point contraction (FMA), which can produce bit-different results between `-O0` and `-O2`. To keep the comparison fair, `tools/bench.py` compiles GCC with `-ffp-contract=off -std=c11`. The VM never contracts `a*b+c`, so its outputs match GCC `-ffp-contract=off` exactly.

### Tips for Clean Numbers

- **Close other apps** to reduce noise.
- **Run multiple iterations** (`--runs 5` or more) for benchmarks under ~50ms.
- **Use `--filter`** to iterate on a single benchmark.
- **Use `--vm-profile`** when optimizing bytecode generation or VM dispatch.
- **Compare JSON files over time** — `profile/bench-results/run-*.json` includes compiler versions, host info, and run settings.

### Adding a New Benchmark

1. Drop a `<name>.c` in `tests/benchmarks/`.
2. The contract: plain C99/C11, optionally `#define BENCH_N <default>`, print `result: <value>`, `return 42`.
3. `python3 tools/bench.py --filter "<name>.c"` to verify it runs and matches GCC.
4. The standard `tools/tests.py` will pick it up automatically (exit code 42).

### Reading the Report

- **`median ms`** — middle value of the timed runs.
- **`min ms`** — fastest run; useful as a lower bound.
- **`stable`** — whether every run produced identical stdout.
- **Speedup vs gcc -O2** — `median_ms / median_gcc_O2_ms`. Above 1.0× means slower than gcc -O2.
- **`geomean`** — geometric mean of per-benchmark ratios; the right "overall" comparison number.

### Cross-Compiler Flag Notes

- `tools/bench.py` auto-detects when the system `gcc` is actually Apple Clang (on macOS) and switches to a Homebrew `gcc-15`/`gcc-14`/etc. if available. Pass `--gcc PATH` to override.
- Add clang to the matrix by editing `CCCC_CONFIGS` / `GCC_CONFIGS` in `tools/bench.py`.

---

## Methodology

- **Host**: macOS 15.7.5 (Darwin 24.6.0), Apple M-series (arm64)
- **CCCC**: default flags (`-g`, no VM optimiser)
- **GCC**: `tools/bench.py` auto-detects Homebrew GCC (16.1.0 here) when the
  system `gcc` is Apple Clang, and pins `-ffp-contract=off -std=c11` for a
  fair FP comparison (the VM never contracts `a*b+c`)
- **Runs**: 3 timed iterations after 1 warmup, per (benchmark, config)
- **Correctness**: all configs must produce identical stdout for every benchmark

## Speed

Median wall-clock **milliseconds**, lower is better. `cccc` is the VM
(source → bytecode → interpret); `gcc-*` are prebuilt native binaries.

| Benchmark | `cccc` | `gcc-O0` | `gcc-O1` | `gcc-O2` | `gcc-O3` |
|---|---|---|---|---|---|
| ackermann | 839.3 | 14.8 | 17.2 | 15.1 | 13.9 |
| binary_tree | 1035.8 | 22.9 | 18.4 | 17.9 | 17.8 |
| fib | 747.6 | 10.4 | 8.2 | 9.5 | 11.2 |
| mandelbrot | 6219.9 | 62.9 | 29.7 | 30.3 | 29.7 |
| matrix_mul | 5180.4 | 20.4 | 9.2 | 6.0 | 6.4 |
| nqueens | 1486.6 | 10.0 | 6.4 | 11.0 | 11.8 |
| quicksort | 2051.1 | 16.9 | 11.6 | 12.5 | 15.7 |
| sieve | 10074.6 | 44.8 | 17.8 | 17.5 | 17.5 |
| **Geom. mean** | **2244.2** | **20.7** | **13.2** | **13.5** | **14.3** |

The VM is ~55–870× slower than native `gcc -O2` depending on workload,
worst on the tight double-precision inner loop of `matrix_mul`. This is
expected for a bytecode interpreter with no JIT — and VM speed is an
explicit non-goal, which is why the optimiser was not worth keeping.

## Memory

Peak RSS is dominated by a fixed ~5–6 MB cost (the compiler, the VM
runtime, and the safety layer load regardless of program size). There is no
VM optimisation level to trade against it.

## Observations

- **Speed is ~55–870× slower than native.** The interpreter loop's
  constant-factor overhead dominates. VM execution speed is a non-goal;
  `-c=native` bypasses the VM entirely for production builds.
- **`matrix_mul` is the worst case.** A 200×200 double-precision matrix
  multiply exposes dispatch overhead most severely.
- **The removed optimiser was not worth it.** Best-case ~1.25× geomean and
  frequently net-negative on `ackermann`/`fib`-style recursion-heavy code
  (#1214).
