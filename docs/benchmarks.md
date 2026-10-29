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
| ackermann | 572.2 | 16.1 | 14.9 | 3.7 | 8.8 |
| binary_tree | 762.7 | 19.1 | 17.5 | 17.8 | 16.4 |
| fib | 510.1 | 8.4 | 12.1 | 10.0 | 10.1 |
| mandelbrot | 2853.3 | 60.8 | 27.9 | 27.7 | 26.2 |
| matrix_mul | 2322.4 | 19.8 | 8.0 | 9.0 | 7.5 |
| nqueens | 538.9 | 12.9 | 7.6 | 9.4 | 13.2 |
| quicksort | 908.7 | 16.9 | 10.4 | 12.4 | 12.3 |
| sieve | 3439.1 | 33.4 | 21.2 | 18.8 | 17.6 |
| **Geometric mean** | **1120.5** | **19.7** | **13.6** | **11.7** | **13.0** |

The VM is roughly 43x to 258x slower than `gcc -O2`, worst on tight double-precision loops such as `matrix_mul`. Loop-heavy programs gain most from keeping loop locals in registers and from the per-function clean-up; recursion-heavy ones (`ackermann`, `fib`, `binary_tree`) gain least. `-c=native` hands the program to a host compiler and runs at native speed.

## Reproducing

```sh
python3 tools/bench.py                    # all benchmarks
python3 tools/bench.py --filter fib.c     # one benchmark
```

The programs are in `tests/benchmarks/`; `tools/bench.py` pins `-ffp-contract=off -std=c11` for GCC so floating-point results match the VM, and checks that every configuration prints the same output.
