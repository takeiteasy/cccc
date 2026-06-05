# JCC Profiling Scripts

Helper scripts and Makefile targets for CPU, memory, and VM opcode profiling
of JCC.

## Prerequisites

- **hyperfine** — `brew install hyperfine` (macOS) or `cargo install hyperfine`
- **gperftools** — `brew install gperftools` (macOS) or `apt-get install google-perftools` (Linux)
  - Needed for CPU profiling with `libprofiler`

## Makefile Targets (from project root)

### Benchmarking

```bash
make bench                    # Hyperfine benchmark on a representative test
make bench TEST=foo.c         # Benchmark a specific test file
```

### CPU Profiling

```bash
make profile-cpu              # Profile compilation of a representative test
make profile-cpu TEST=foo.c   # Profile a specific test
```

This produces `profile/cpu.prof` (raw gperftools profile data) and a text summary.
On macOS you can also open the result in `pprof` (Go tool) or Instruments.

### VM Opcode Profiling

```bash
./jcc --vm-profile -I./include tests/test_comprehensive.c
./jcc --vm-profile-json profile/vm-opcodes/comprehensive.json -I./include tests/test_comprehensive.c
./jcc --profile-opcodes build/fib.jbc
```

`--vm-profile` prints a compact dynamic opcode count table to stderr after the
program exits. `--vm-profile-json <file>` writes the same data as JSON without
printing the table, which keeps benchmark stdout checks clean. The JSON includes
the execution mode (`source` or `jbc`), selected `--optimize` level, total VM
cycles, total profiled opcodes, and per-op counts and percentages.

### Memory Profiling

```bash
make profile-mem              # Memory profile a representative test
make profile-mem TEST=foo.c   # Profile a specific test
```

On macOS this uses `heap` (Xcode CLI tool) if available.
On Linux this uses `valgrind --tool=massif`.

## Manual Usage

### Hyperfine (cross-platform benchmarking)

```bash
# Single test
hyperfine --warmup 3 './jcc -I./include tests/test_comprehensive.c'

# Compare two versions
hyperfine --warmup 3 \
  -n 'main' './jcc -I./include tests/test_comprehensive.c' \
  -n 'branch' './jcc-branch -I./include tests/test_comprehensive.c'
```

### macOS `sample` (built-in CPU profiler, no install needed)

```bash
# Run compiler and sample it
./jcc -I./include tests/test_comprehensive.c &
PID=$!
sample $PID -mayDie -file profile/sample.txt
```

### macOS `heap` (built-in heap profiler)

```bash
# Requires Xcode command-line tools
heap -s -guessNonObjects ./jcc -I./include tests/test_comprehensive.c
```

### gperftools CPU profiler (cross-platform)

```bash
# Build profiling binary
make profile-cpu-build

# Run with profiling enabled
CPUPROFILE=profile/out.prof ./jcc-prof -I./include tests/test_comprehensive.c

# View raw profile (symbolized)
CPUPROFILE_FREQUENCY=1000 ./jcc-prof -I./include tests/test_comprehensive.c
```

## tests.py Integration

```bash
python3 tests.py --bench                        # Benchmark all tests
python3 tests.py --bench --match "*compre*"     # Benchmark matching tests
python3 tests.py --profile-cpu --match "*compre*"  # CPU profile matching tests
python3 tests.py --profile-mem --match "*malloc*"  # Memory profile matching tests
python3 tests.py --vm-profile --match "*profile*"  # VM opcode JSON profiles
python3 tests.py --jbc --vm-profile --match "*profile*"  # Profile .jbc execution
```

`tests.py --vm-profile` writes one JSON file per test under
`profile/vm-opcodes/`. In `--jbc` mode it profiles the bytecode execution phase,
not the source-to-bytecode save step.

## Cross-Compiler Benchmarks (JCC vs GCC)

For a higher-level view — comparing JCC to a real C compiler on the same portable workloads — see [BENCHMARKS.md](BENCHMARKS.md). The benchmark suite runs each workload under JCC × {none,O1,O2,O3} and GCC × {O0,O1,O2,O3}, verifies identical output, and reports per-config wall-clock timings plus speedup ratios.

```bash
make bench-compare            # full run
make bench-compare-quick      # quick run
python3 bench.py --filter fib.c   # one benchmark
python3 bench.py --filter fib.c --vm-profile   # include opcode profile JSON
```

The hyperfine-based `make bench` and the cross-compiler `make bench-compare` are complementary: `make bench` profiles a single workload in depth (with shell-startup variation), while `make bench-compare` produces a side-by-side matrix of how JCC stacks up against GCC.

When `bench.py --vm-profile` is enabled, JCC and JCC-JBC configs write per
benchmark/config opcode profiles under `benchmarks/results/vm-profile-<UTC>/`.
Each timing record in the benchmark JSON includes its `vm_profile_json` path.

## Output Files

All profiling output is written to `profile/`:

| File | Tool | Content |
|------|------|---------|
| `profile/bench.json` | hyperfine | Benchmark results in JSON |
| `profile/cpu.prof` | gperftools | Raw CPU profile |
| `profile/cpu.txt` | gperftools | Text CPU profile summary |
| `profile/mem.massif` | valgrind | Memory allocation timeline (Linux) |
| `profile/vm-opcodes/*.json` | JCC VM profiler | Dynamic opcode counts per test |
| `benchmarks/results/vm-profile-*/` | JCC VM profiler | Opcode profiles for benchmark configs |
