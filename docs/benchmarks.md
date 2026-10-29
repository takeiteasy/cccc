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
| ackermann | 578.2 | 17.9 | 14.5 | 14.0 | 15.7 |
| binary_tree | 768.0 | 22.5 | 18.5 | 19.4 | 18.4 |
| dispatch | 1574.7 | 19.1 | 5.3 | 10.2 | 7.8 |
| fib | 524.6 | 8.3 | 9.2 | 9.2 | 9.6 |
| mandelbrot | 2670.4 | 63.0 | 29.8 | 29.8 | 29.9 |
| matrix_mul | 2082.6 | 21.3 | 5.4 | 4.2 | 6.0 |
| nqueens | 524.0 | 14.1 | 7.3 | 10.2 | 12.2 |
| quicksort | 825.2 | 16.9 | 10.9 | 11.5 | 12.4 |
| sieve | 2991.3 | 44.5 | 20.2 | 18.0 | 17.7 |
| **Geometric mean** | **1113.8** | **21.3** | **11.5** | **12.4** | **13.0** |

The VM is roughly 40x to 496x slower than `gcc -O2`, worst on tight double-precision loops such as `matrix_mul`. Loop-heavy programs gain most from keeping loop locals in registers, from using those registers directly as instruction operands and from the per-function clean-up; recursion-heavy ones (`ackermann`, `fib`, `binary_tree`) gain least. `-c=native` hands the program to a host compiler and runs at native speed.

## Reproducing

```sh
python3 tools/bench.py                    # all benchmarks
python3 tools/bench.py --filter fib.c     # one benchmark
```

The programs are in `tests/benchmarks/`; `tools/bench.py` pins `-ffp-contract=off -std=c11` for GCC so floating-point results match the VM, and checks that every configuration prints the same output.
