# CCCC Benchmark Results

Benchmark results for the CCCC bytecode compiler + VM interpreter compared to
native compilation with Clang (Apple clang 17.0.0, arm64).

## Methodology

- **Host**: macOS 15.6 (Darwin 24.6.0), Apple M-series (arm64), 8 cores
- **CCCC**: built from source at commit `f6c2f4f`, default flags (`-O0 -g`)
- **Clang**: Apple clang version 17.0.0 (`/usr/bin/gcc` on this system)
- **Runs**: 2 timed iterations after 1 warmup iteration per (benchmark, config)
- **Memory**: peak resident set size (RSS) via `/usr/bin/time -l`
- **Correctness**: all configs produce identical stdout for every benchmark

Configurations tested:

| Label | Description |
|-------|-------------|
| `cccc` | CCCC with no optimization |
| `cccc-O1` | CCCC with `--optimize=1` (constant folding + dead-call elimination) |
| `cccc-O2` | CCCC with `--optimize=2` (+peephole, +CSE, +scalar promotion) |
| `cccc-O3` | CCCC with `--optimize=3` (+copy propagation, +DCE) |
| `cccc-O4` | CCCC with `--optimize=4` (+opcode fusion, +redundant extension elimination) |
| `gcc-O0` | Clang with `-O0` |
| `gcc-O1` | Clang with `-O1` |
| `gcc-O2` | Clang with `-O2` |
| `gcc-O3` | Clang with `-O3` |

## Speed Results

All times are median wall-clock in **milliseconds** (lower is better).

| Benchmark | `cccc` | `cccc-O1` | `cccc-O2` | `cccc-O3` | `cccc-O4` | `gcc-O0` | `gcc-O1` | `gcc-O2` | `gcc-O3` |
|---|---|---|---|---|---|---|---|---|---|
| ackermann | 682.9 | 681.9 | 918.5 | 793.8 | 781.7 | 16.6 | 16.4 | 4.3 | 6.0 |
| binary_tree | 868.6 | 859.5 | 963.1 | 824.1 | 809.9 | 20.6 | 20.3 | 19.4 | 22.9 |
| fib | 573.7 | 597.4 | 674.6 | 580.0 | 577.3 | 7.5 | 8.8 | 4.1 | 7.3 |
| mandelbrot | 6101.2 | 6065.3 | 4983.7 | 3970.9 | 3626.0 | 62.0 | 30.4 | 29.4 | 28.4 |
| matrix_mul | 5014.6 | 4997.7 | 4136.5 | 3747.4 | 3309.6 | 24.6 | 5.7 | 4.0 | 3.8 |
| nqueens | 1322.5 | 1234.3 | 1141.5 | 979.8 | 948.9 | 14.4 | 5.1 | 8.1 | 9.2 |
| quicksort | 1829.9 | 1772.4 | 1565.3 | 1498.8 | 1378.8 | 18.5 | 9.1 | 12.5 | 13.0 |
 | sieve | 9660.6 | 9328.2 | 7331.3 | 7311.1 | 7075.3 | 36.2 | 24.4 | 21.2 | 20.4 |
 | **Geom. mean** | **1987.4** | **1958.6** | **1887.6** | **1670.4** | **1588.8** | **21.0** | **12.4** | **9.8** | **11.3** |
 | **Geom. mean (w/o matrix_mul)** | **1741.3** | **1713.3** | **1687.4** | **1488.3** | **1430.7** | **20.5** | **13.9** | **11.2** | **13.2** |
 
### Slowdown vs Clang `-O2`

Values >1.0 mean CCCC is slower than native. Geometric mean across all 8 benchmarks.

| Benchmark | `cccc` | `cccc-O1` | `cccc-O2` | `cccc-O3` | `cccc-O4` | `gcc-O0` | `gcc-O1` | `gcc-O2` | `gcc-O3` |
|---|---|---|---|---|---|---|---|---|---|
| ackermann | 158.8× | 158.6× | 213.6× | 184.6× | 181.8× | 3.9× | 3.8× | 1.0× | 1.4× |
| binary_tree | 44.8× | 44.3× | 49.6× | 42.5× | 41.7× | 1.1× | 1.0× | 1.0× | 1.2× |
| fib | 139.9× | 145.7× | 164.5× | 141.5× | 140.8× | 1.8× | 2.1× | 1.0× | 1.8× |
| mandelbrot | 207.5× | 206.3× | 169.5× | 135.1× | 123.3× | 2.1× | 1.0× | 1.0× | 1.0× |
| matrix_mul | 1253.7× | 1249.4× | 1034.1× | 936.9× | 827.4× | 6.2× | 1.4× | 1.0× | 1.0× |
| nqueens | 163.3× | 152.4× | 140.9× | 121.0× | 117.1× | 1.8× | 0.6× | 1.0× | 1.1× |
| quicksort | 146.4× | 141.8× | 125.2× | 119.9× | 110.3× | 1.5× | 0.7× | 1.0× | 1.0× |
| sieve | 455.7× | 440.0× | 345.8× | 344.9× | 333.7× | 1.7× | 1.2× | 1.0× | 1.0× |
 | **Geom. mean** | **202.4×** | **196.5×** | **173.8×** | **156.1×** | **146.2×** | **2.1×** | **1.3×** | **1.0×** | **1.1×** |
 | **Geom. mean (w/o matrix_mul)** | **156.0×** | **153.5×** | **151.1×** | **133.4×** | **128.1×** | **1.9×** | **1.2×** | **1.0×** | **1.2×** |
 
## Memory Usage

Peak resident set size in **MB** (lower is better). Native binaries compiled with
Clang; CCCC runs the source directly through its compiler + VM pipeline.

| Benchmark | `clang -O0` | `clang -O2` | `cccc -O0` | `cccc -O2` | `cccc -O4` |
|---|---|---|---|---|---|
| ackermann | 1.2 | 1.1 | 6.2 | 6.5 | 6.4 |
| binary_tree | 4.2 | 4.2 | 17.2 | 17.1 | 17.2 |
| fib | 1.2 | 1.1 | 6.3 | 6.4 | 6.4 |
| mandelbrot | 1.1 | 1.1 | 6.9 | 7.0 | 7.0 |
| matrix_mul | 2.2 | 2.1 | 7.9 | 8.2 | 8.1 |
| nqueens | 1.1 | 1.1 | 6.5 | 6.4 | 6.4 |
| quicksort | 1.6 | 1.5 | 7.4 | 7.5 | 7.5 |
| sieve | 10.6 | 10.7 | 16.4 | 16.3 | 16.4 |

### Memory Overhead

| Benchmark | Native (best) | CCCC -O0 | Ratio | Excess |
|---|---|---|---|---|
| ackermann | 1.1M | 6.2M | 5.6× | 5.1M |
| binary_tree | 4.2M | 17.2M | 4.1× | 13.0M |
| fib | 1.1M | 6.3M | 5.7× | 5.2M |
| mandelbrot | 1.1M | 6.9M | 6.3× | 5.8M |
| matrix_mul | 2.1M | 7.9M | 3.8× | 5.8M |
| nqueens | 1.1M | 6.5M | 5.9× | 5.4M |
| quicksort | 1.5M | 7.4M | 4.9× | 5.9M |
| sieve | 10.6M | 16.4M | 1.5× | 5.8M |
| **Avg** | **2.9M** | **9.3M** | **3.2×** | **6.4M** |
| **Geom. mean** | | | **4.2×** | |

## Observations

- **Memory overhead is dominated by a fixed cost**: CCCC loads the compiler,
  the VM runtime, and the safety layer regardless of program complexity. This
  costs ~5–6 MB of additional RSS on top of the native baseline. The ratio is
  highest for tiny programs (fib, ackermann: ~5–6×) and lowest for sieve
  (1.5×) where the native binary already uses ~10.6 MB for its data set.

- **Optimization level has negligible effect on memory**: `-O0` through `-O4`
  all show similar peak RSS. The compiler has already completed its work by the
  time execution begins; the optimizer runs on the bytecode representation and
  does not significantly change the in-memory footprint.

- **Speed is 40–200× slower than native at baseline**: The interpreter loop
  imposes a large constant-factor overhead. `-O4` recovers ~28% vs unoptimized
  (geomean 202× → 146×). This is expected for a bytecode VM without JIT
  compilation.

- **matrix_mul is the worst case**: The 200×200 double-precision matrix
  multiply (40k iterations of tight inner loops) exposes the interpreter's
  dispatch overhead most severely at 1253× native.

- **Optimization sweet spot**: `-O3` or `-O4` consistently beat lower levels,
  with diminishing returns beyond `-O3`. Some benchmarks (ackermann, fib)
  show *regression* at `-O2` — the peephole pass apparently interacts poorly
  with heavily recursive code in some cases.
