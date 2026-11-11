# Benchmarks

The VM runs about 2x slower than Lua and about 30x slower than `gcc -O2`; compile speed is close to a host compiler's. Use `-c=native` when speed matters.

## Compiling cccc

Building cccc's own sources (about 100 files) on an Apple M1, 8 cores, macOS 15.

| Build | Wall time | Notes |
|---|---|---|
| Host `cc -O2` via `./cccc --build build.c --build-target=release` | 6 s | Parallel, one `cc` per file |
| `cccc --compile=object` per file ([`ouroboros`](selfhosting.md#recipe)) | 7 s | One cccc process per file, parallel |
| `cccc -c=native` ([`ouroboros_single`](selfhosting.md#recipe)) | 22 s | One merged unit, single thread front end |
| `cccc -m` over the 103 sources (the front end and serializer, no host compiler) | 15 s | Single thread |

`cccc -c=native` over every file parses them into one program and writes it as C before the host compiler runs, so the frontend does not scale with `--build-jobs`; per-file objects do.

Measure a build with the release binary (`./cccc --build build.c --build-target=release`, then `build/cccc-release`). The `make` binary is built at `-O0` and is about 3x slower.

## Repeated `@inst`

Parse time of a file with 20,000 repeat `@inst(...) f(1)` calls (`cccc -J`, release build, best of 3). A hit looks up the existing instance by name; the baseline is the same file calling a plain function.

| Template parameters | Wall time | Peak memory |
|---|---|---|
| none (plain function) | 0.08 s | 83 MB |
| `T` | 0.13 s | 112 MB |
| `T, int N` | 0.15 s | 124 MB |
| `T, double D` | 0.16 s | 125 MB |
| `T, int (*F)(int)` | 0.16 s | 131 MB |
| `T, int N, double D, int (*F)(int)` | 0.21 s | 156 MB |
| `T, T N` (type depends on `T`) | 0.16 s | 146 MB |

A constant parameter's type is parsed once per template unless it names an earlier parameter, which is why `T, T N` costs more than `T, int N`.

## Running code

Median wall-clock milliseconds, lower is better. Each column runs the same workload end to end from source:

| Column | What runs |
|---|---|
| `cccc` | The VM: source to bytecode, then interpreted |
| `lua` | Lua 5.5.1: source to bytecode, then interpreted. A reference point only[^lua] |
| `gcc-O0`, `gcc-O2` | Prebuilt native binaries (Homebrew GCC 16.2.0) |

Apple M1, release-built `cccc`, 5 timed runs after 1 warmup.

| Benchmark | `cccc` | `lua` | `gcc-O0` | `gcc-O2` |
|---|---|---|---|---|
| ackermann | 184.0 | 48.3 | 13.7 | 4.9 |
| binary_tree | 241.5 | 112.0 | 17.3 | 16.0 |
| dispatch | 230.4 | 171.6 | 13.2 | 6.5 |
| fib | 174.2 | 52.7 | 7.1 | 4.0 |
| mandelbrot | 759.9 | 388.9 | 61.3 | 28.1 |
| matrix_mul | 725.2 | 234.6 | 19.6 | 4.5 |
| nqueens | 118.9 | 59.0 | 8.6 | 5.7 |
| quicksort | 201.8 | 63.0 | 12.1 | 7.7 |
| sieve | 483.5 | 324.8 | 35.6 | 18.5 |
| **Geometric mean** | **282.3** | **120.6** | **16.7** | **8.4** |

The VM is 1.3x to 3.8x slower than Lua (2.3x on the geometric mean) and 15x to 161x slower than `gcc -O2` (34x), worst on tight double-precision loops such as `matrix_mul`. Tiny native timings (`fib`, `binary_tree`) are dominated by process start-up, so `gcc-O0` can beat `gcc-O2` there.

`-c=native` hands the program to a host compiler and runs at native speed.

## Memory

Peak memory, measured by `tools/bench.py` as the maximum resident set of each process (the footprint on macOS, which counts compressed pages).

Compiling cccc's own 103 sources, release-built `cccc`:

| Step | Peak |
|---|---|
| `cccc --ast` (parse and link every file) | 3.0 GB |
| `cccc -m` (plus the serializer that `-c=native` runs before the host compiler) | 3.4 GB |
| `gcc -c`, the largest single file | 45 MB |

`cccc` holds every file in one program, so its peak grows with the number of files (about 30 MB each); `gcc` compiles one file per process.

Running the benchmarks, in MB. The `cccc` column is compile and run in one process, `gcc` is the prebuilt binary alone, and `lua` is the interpreter.

| Benchmark | `cccc` | `lua` | `gcc-O0` | `gcc-O2` |
|---|---|---|---|---|
| ackermann | 6.9 | 1.8 | 1.2 | 1.2 |
| binary_tree | 18.6 | 11.7 | 4.2 | 4.2 |
| dispatch | 6.9 | 1.5 | 1.2 | 1.2 |
| fib | 7.0 | 1.4 | 1.2 | 1.2 |
| mandelbrot | 7.2 | 1.5 | 1.2 | 1.2 |
| matrix_mul | 8.5 | 3.8 | 2.1 | 2.1 |
| nqueens | 7.1 | 1.5 | 1.2 | 1.2 |
| quicksort | 7.9 | 3.6 | 1.6 | 1.6 |
| sieve | 16.9 | 238.1 | 10.7 | 10.7 |

The VM's baseline is about 7 MB, so it stays within a few megabytes of `gcc` on small programs. `sieve` is the exception: the C versions use a 10 MB byte array, while the Lua port keeps its 10 million flags in a table.

## Reproducing

```sh
./cccc --build build.c --build-target=bench_compare   # builds the release binary, runs everything
python3 tools/bench.py --cccc build/cccc-release --filter fib.c   # one benchmark
./cccc --build build.c --build-target=bench_memory   # peak memory, including compiling cccc itself
```

The C programs are in `tests/benchmarks/` and the Lua ports in `tests/benchmarks/lua/`. `tools/bench.py` pins `-ffp-contract=off -std=c11` for GCC so floating-point results match the VM, and checks that every configuration prints the same output. The Lua column is skipped when `lua` is not installed (`--lua PATH` picks another interpreter).

[^lua]: Lua is dynamically typed with tables instead of structs and arrays, and the ports are idiomatic rather than line for line (`dispatch` is an `if` chain where the C uses computed goto). Both interpreters' timings include parsing and compiling the source. Read the column as a rough yardstick for interpreter speed, not a contest.
