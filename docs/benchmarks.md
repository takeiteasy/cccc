# Benchmarks

Compile speed is close to a host compiler's; the VM runs code far slower than native code. Use `-c=native` when speed matters.

## Compiling cccc

Building cccc's own sources (about 100 files) on an Apple M1, 8 cores, macOS 15.

| Build | Wall time | Notes |
|---|---|---|
| Host `cc -O2` via `./cccc --build build.c --build-target=release` | 19 s | Parallel, one `cc` per file |
| `cccc -c=native` ([recipe](selfhosting.md#recipe)) | 1 m 55 s | One merged unit, single thread |

`cccc -c=native` parses every file into one program and writes it as C before the host compiler runs, so the frontend does not scale with `--build-jobs`.

Measure a build with the release binary (`./cccc --build build.c --build-target=release`, then `build/cccc-release`). The `make` binary is built at `-O0` and is about 3x slower.

## Running code

Median wall-clock milliseconds, lower is better. `cccc` is the VM (source to bytecode, then interpreted); `gcc-*` are prebuilt native binaries. Apple M1, Homebrew GCC 16.2.0, 3 timed runs after 1 warmup.

| Benchmark | `cccc` | `gcc-O0` | `gcc-O1` | `gcc-O2` | `gcc-O3` |
|---|---|---|---|---|---|
| ackermann | 576.3 | 18.4 | 13.7 | 3.8 | 6.7 |
| binary_tree | 767.7 | 20.0 | 18.5 | 18.4 | 16.4 |
| fib | 518.1 | 6.6 | 6.9 | 4.6 | 6.5 |
| mandelbrot | 2682.5 | 61.7 | 29.5 | 28.2 | 26.6 |
| matrix_mul | 2096.5 | 22.6 | 5.6 | 4.7 | 5.2 |
| nqueens | 533.4 | 8.7 | 5.6 | 9.3 | 7.1 |
| quicksort | 830.2 | 14.2 | 8.5 | 9.5 | 9.6 |
| sieve | 3111.9 | 34.1 | 22.7 | 19.2 | 19.6 |
| **Geometric mean** | **1074.5** | **18.6** | **11.5** | **9.6** | **10.4** |

The VM is roughly 42x to 446x slower than `gcc -O2`, worst on tight double-precision loops such as `matrix_mul`. Loop-heavy programs gain most from keeping loop locals in registers, from using those registers directly as instruction operands and from the per-function clean-up; recursion-heavy ones (`ackermann`, `fib`, `binary_tree`) gain least. `-c=native` hands the program to a host compiler and runs at native speed.

## Reproducing

```sh
python3 tools/bench.py                    # all benchmarks
python3 tools/bench.py --filter fib.c     # one benchmark
```

The programs are in `tests/benchmarks/`; `tools/bench.py` pins `-ffp-contract=off -std=c11` for GCC so floating-point results match the VM, and checks that every configuration prints the same output.
