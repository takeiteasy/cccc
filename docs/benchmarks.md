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
| ackermann | 767.0 | 16.4 | 15.7 | 4.1 | 9.2 |
| binary_tree | 924.5 | 20.5 | 18.0 | 19.6 | 15.9 |
| fib | 623.4 | 10.4 | 9.8 | 3.5 | 7.3 |
| mandelbrot | 5843.6 | 60.9 | 32.6 | 27.6 | 26.5 |
| matrix_mul | 4824.7 | 19.8 | 5.4 | 6.1 | 5.9 |
| nqueens | 1392.0 | 12.7 | 10.9 | 11.1 | 12.7 |
| quicksort | 1931.8 | 16.4 | 9.4 | 10.7 | 11.5 |
| sieve | 8353.8 | 33.5 | 20.9 | 17.8 | 18.0 |
| **Geometric mean** | **2022.5** | **20.4** | **13.4** | **10.0** | **12.0** |

The VM is roughly 47x to 790x slower than `gcc -O2`, worst on tight double-precision loops such as `matrix_mul`. `-c=native` hands the program to a host compiler and runs at native speed.

## Reproducing

```sh
python3 tools/bench.py                    # all benchmarks
python3 tools/bench.py --filter fib.c     # one benchmark
```

The programs are in `tests/benchmarks/`; `tools/bench.py` pins `-ffp-contract=off -std=c11` for GCC so floating-point results match the VM, and checks that every configuration prints the same output.
