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

Median wall-clock milliseconds, lower is better. `cccc` is the VM (source to bytecode, then interpreted); `gcc-*` are prebuilt native binaries. Apple M1, Homebrew GCC 16.1.0, 3 timed runs after 1 warmup.

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
| **Geometric mean** | **2244.2** | **20.7** | **13.2** | **13.5** | **14.3** |

The VM is roughly 55x to 870x slower than `gcc -O2`, worst on tight double-precision loops such as `matrix_mul`. `-c=native` hands the program to a host compiler and runs at native speed.

## Reproducing

```sh
python3 tools/bench.py                    # all benchmarks
python3 tools/bench.py --filter fib.c     # one benchmark
```

The programs are in `tests/benchmarks/`; `tools/bench.py` pins `-ffp-contract=off -std=c11` for GCC so floating-point results match the VM, and checks that every configuration prints the same output.
