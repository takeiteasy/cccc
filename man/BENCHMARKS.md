# CCCC Benchmark Results

Benchmark results for the CCCC bytecode compiler + VM interpreter compared to
native compilation.

> **Stale numbers.** The tables below predate the removal of the VM bytecode
> optimiser (#1214). The VM now has a single configuration (`-O<n>` only
> affects `-c=native`), so the `cccc-O1`..`cccc-O4` columns no longer exist.
> The removed optimiser's best geomean was ~1.25× the `cccc` (O0) column and
> was frequently net-negative on recursion-heavy code, which is why it went.
> Re-run `make bench-compare` for current figures.

## Methodology

- **Host**: macOS 15.6 (Darwin 24.6.0), Apple M-series (arm64), 8 cores
- **CCCC**: default flags (`-g`, no VM optimiser)
- **GCC**: `tools/bench.py` auto-detects Homebrew GCC when the system `gcc`
  is Apple Clang, and pins `-ffp-contract=off -std=c11` for a fair FP
  comparison (the VM never contracts `a*b+c`)
- **Runs**: timed iterations after a warmup, per (benchmark, config)
- **Correctness**: all configs must produce identical stdout for every benchmark

## Speed (pre-#1214, for the shape of the gap)

Median wall-clock **milliseconds**, lower is better. `cccc` is the VM
(source → bytecode → interpret); `gcc-*` are prebuilt native binaries.

| Benchmark | `cccc` | `gcc-O0` | `gcc-O1` | `gcc-O2` | `gcc-O3` |
|---|---|---|---|---|---|
| ackermann | 682.9 | 16.6 | 16.4 | 4.3 | 6.0 |
| binary_tree | 868.6 | 20.6 | 20.3 | 19.4 | 22.9 |
| fib | 573.7 | 7.5 | 8.8 | 4.1 | 7.3 |
| mandelbrot | 6101.2 | 62.0 | 30.4 | 29.4 | 28.4 |
| matrix_mul | 5014.6 | 24.6 | 5.7 | 4.0 | 3.8 |
| nqueens | 1322.5 | 14.4 | 5.1 | 8.1 | 9.2 |
| quicksort | 1829.9 | 18.5 | 9.1 | 12.5 | 13.0 |
| sieve | 9660.6 | 36.2 | 24.4 | 21.2 | 20.4 |
| **Geom. mean** | **1987.4** | **21.0** | **12.4** | **9.8** | **11.3** |

The VM is ~40–460× slower than native `gcc -O2` depending on workload,
worst on the tight double-precision inner loop of `matrix_mul`. This is
expected for a bytecode interpreter with no JIT — and VM speed is an
explicit non-goal, which is why the optimiser was not worth keeping.

## Memory

Peak RSS is dominated by a fixed ~5–6 MB cost (the compiler, the VM
runtime, and the safety layer load regardless of program size). Optimisation
level never had a meaningful effect on it, and now there is no VM
optimisation level at all.

## Observations

- **Speed is 40–460× slower than native.** The interpreter loop's
  constant-factor overhead dominates. VM execution speed is a non-goal;
  `-c=native` bypasses the VM entirely for production builds.
- **`matrix_mul` is the worst case.** A 200×200 double-precision matrix
  multiply exposes dispatch overhead most severely.
- **The removed optimiser was not worth it.** Best-case ~1.25× geomean and
  frequently net-negative on `ackermann`/`fib`-style recursion-heavy code
  (#1214).
