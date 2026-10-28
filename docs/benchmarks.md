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
| ackermann | 720.9 | 19.6 | 15.2 | 3.8 | 10.5 |
| binary_tree | 886.8 | 19.1 | 17.8 | 19.1 | 16.6 |
| fib | 635.5 | 10.4 | 9.4 | 5.5 | 9.2 |
| mandelbrot | 4425.4 | 61.0 | 27.9 | 27.7 | 26.3 |
| matrix_mul | 3607.3 | 20.1 | 7.4 | 7.7 | 7.0 |
| nqueens | 1241.0 | 12.2 | 10.3 | 10.0 | 9.0 |
| quicksort | 1669.4 | 16.2 | 10.8 | 13.0 | 13.2 |
| sieve | 5622.2 | 33.8 | 22.0 | 18.4 | 18.6 |
| **Geometric mean** | **1717.0** | **20.6** | **13.8** | **10.9** | **12.6** |

The VM is roughly 46x to 470x slower than `gcc -O2`, worst on tight double-precision loops such as `matrix_mul`. Loop-heavy programs gain most from keeping loop locals in registers; recursion-heavy ones (`ackermann`, `fib`, `binary_tree`) do not. `-c=native` hands the program to a host compiler and runs at native speed.

## Reproducing

```sh
python3 tools/bench.py                    # all benchmarks
python3 tools/bench.py --filter fib.c     # one benchmark
```

The programs are in `tests/benchmarks/`; `tools/bench.py` pins `-ffp-contract=off -std=c11` for GCC so floating-point results match the VM, and checks that every configuration prints the same output.
