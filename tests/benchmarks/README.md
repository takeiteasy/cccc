# Benchmark Suite

Micro-benchmarks comparing the **cccc** VM with **Lua** (a reference interpreter) and **GCC** (`-O0`, `-O2`). Results and how to read them are in [docs/benchmarks.md](../../docs/benchmarks.md).

Each C program is plain C99/C11, prints a single `result: …` line on stdout and exits with code `42`. `lua/<name>.lua` is the matching port. `tools/bench.py` discovers the programs; the test targets do not run them.

| File | Workload | Default size |
|------|----------|--------------|
| `fib.c` | Recursive Fibonacci | `n=30` |
| `sieve.c` | Sieve of Eratosthenes | `limit=10,000,000` |
| `nqueens.c` | 10-queens backtracking | `N=10` |
| `matrix_mul.c` | 200×200 double matrix multiply | `N=200` |
| `quicksort.c` | Quicksort 100k random ints | `N=100,000` |
| `mandelbrot.c` | 400×400 mandelbrot, 200 iter | `400×400, 200` |
| `binary_tree.c` | BST insert + inorder traversal | `N=100,000` |
| `ackermann.c` | `ack(3, 8)` | `M=3, N=8` |
| `dispatch.c` | Accumulator machine dispatched with computed goto | `N=1,000,000` |

## Running

```sh
./cccc --build build.c --build-target=bench_compare         # builds build/cccc-release, then runs everything
./cccc --build build.c --build-target=bench_compare_quick   # 2 timed runs
python3 tools/bench.py --cccc build/cccc-release --filter fib.c   # one benchmark
python3 tools/bench.py --cccc build/cccc-release --vm-profile     # also write VM opcode profiles
```

`bench.py` skips the Lua column when `lua` is not on `PATH` (`--lua PATH` picks another interpreter), and uses a real GCC rather than Apple's `gcc` alias for clang (`--gcc PATH` overrides).

## Results

Each run writes a JSON report to `profile/bench-results/run-<UTC>.json` with raw timings, compiler versions and host info. `--vm-profile` adds dynamic opcode counts under `profile/bench-results/vm-profile-<UTC>/`.

## Changing a program

A change to a C program needs the same change in its Lua port; `bench.py` fails the run when the outputs differ.
