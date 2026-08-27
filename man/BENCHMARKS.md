# CCCC Benchmark Results

Benchmark results for the CCCC bytecode compiler + VM interpreter compared to
native compilation.

The VM has a single configuration — `-O<n>` only affects `-c=native`, never
VM codegen — so there is one `cccc` column. Re-run `make bench-compare` (or
`python3 tools/bench.py`) to refresh these figures.

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
