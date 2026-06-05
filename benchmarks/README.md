# JCC Benchmark Suite

Cross-compiler micro-benchmarks for comparing **JCC** (across all `--optimize` levels, in both parse+exec and precompiled-bytecode modes) against **GCC** (across `-O0..-O3`).

Each program is plain C99/C11, prints a single `result: …` line on stdout, and exits with code `42`. They are auto-discovered by `bench.py` and the standard `tests.py` smoke-runs them.

## Programs

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

## Running

```bash
# From the repo root
make bench-compare            # full run (~10 min)
make bench-compare-quick      # 2 iterations (~5 min)
python3 bench.py --filter fib.c             # one benchmark only
python3 bench.py --no-jbc --filter fib.c    # skip the jcc-jbc* (precompiled bytecode) columns
```

See [docs/BENCHMARKS.md](../docs/BENCHMARKS.md) for the full guide — how the runner works, how to read the table, and how to add new benchmarks.

## Results

Each run writes a JSON report to `results/run-<UTC>.json` with raw timings, compiler versions, host info, and per-`jcc-jbc*` `compile_ms` (the one-time cost of producing the bytecode file). All of this is included so results are reproducible and comparable across machines.

The VM dispatch loop was converted to a fully-inlined threaded interpreter in ticket #227 — a **1.2–1.7× speedup** on VM-bound workloads at `-O2` (fib: 1.21×, nqueens: 1.50×, sieve: 1.69×). Any stored results predating this change will not reflect the improvement.
