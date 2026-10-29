# Benchmarks

The VM runs about 3x slower than Lua and about 40x slower than `gcc -O2`; compile speed is close to a host compiler's. Use `-c=native` when speed matters.

## Compiling cccc

Building cccc's own sources (about 100 files) on an Apple M1, 8 cores, macOS 15.

| Build | Wall time | Notes |
|---|---|---|
| Host `cc -O2` via `./cccc --build build.c --build-target=release` | 6 s | Parallel, one `cc` per file |
| `cccc -c=native` ([`ouroboros`](selfhosting.md#recipe)) | 2 m 10 s | One merged unit, single thread |

`cccc -c=native` parses every file into one program and writes it as C before the host compiler runs, so the frontend does not scale with `--build-jobs`.

Measure a build with the release binary (`./cccc --build build.c --build-target=release`, then `build/cccc-release`). The `make` binary is built at `-O0` and is about 3x slower.

## Running code

Median wall-clock milliseconds, lower is better. Each column runs the same workload end to end from source:

| Column | What runs |
|---|---|
| `cccc` | The VM: source to bytecode, then interpreted |
| `lua` | Lua 5.5.1: source to bytecode, then interpreted. A reference point only[^lua] |
| `gcc-O0`, `gcc-O2` | Prebuilt native binaries (Homebrew GCC 16.2.0) |

Apple M1, release-built `cccc`, 3 timed runs after 1 warmup.

| Benchmark | `cccc` | `lua` | `gcc-O0` | `gcc-O2` |
|---|---|---|---|---|
| ackermann | 197.3 | 48.1 | 18.4 | 3.7 |
| binary_tree | 259.0 | 122.7 | 18.3 | 18.0 |
| dispatch | 509.1 | 170.1 | 16.8 | 5.5 |
| fib | 178.1 | 52.2 | 6.5 | 11.0 |
| mandelbrot | 1032.9 | 388.4 | 63.8 | 27.8 |
| matrix_mul | 804.9 | 234.1 | 19.5 | 3.7 |
| nqueens | 201.6 | 58.8 | 11.8 | 9.8 |
| quicksort | 282.3 | 62.7 | 16.8 | 9.6 |
| sieve | 1154.6 | 325.8 | 33.3 | 19.0 |
| **Geometric mean** | **398.4** | **121.4** | **18.8** | **9.6** |

The VM is 2.1x to 4.5x slower than Lua (3.3x on the geometric mean) and 14x to 220x slower than `gcc -O2` (41x), worst on tight double-precision loops such as `matrix_mul`. Tiny native timings (`fib`, `binary_tree`) are dominated by process start-up, so `gcc-O0` can beat `gcc-O2` there.

`-c=native` hands the program to a host compiler and runs at native speed.

## Reproducing

```sh
./cccc --build build.c --build-target=bench_compare   # builds the release binary, runs everything
python3 tools/bench.py --cccc build/cccc-release --filter fib.c   # one benchmark
```

The C programs are in `tests/benchmarks/` and the Lua ports in `tests/benchmarks/lua/`. `tools/bench.py` pins `-ffp-contract=off -std=c11` for GCC so floating-point results match the VM, and checks that every configuration prints the same output. The Lua column is skipped when `lua` is not installed (`--lua PATH` picks another interpreter).

[^lua]: Lua is dynamically typed with tables instead of structs and arrays, and the ports are idiomatic rather than line for line (`dispatch` is an `if` chain where the C uses computed goto). Both interpreters' timings include parsing and compiling the source. Read the column as a rough yardstick for interpreter speed, not a contest.
