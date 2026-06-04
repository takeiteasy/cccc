# JCC Profiling Scripts

Helper scripts and Makefile targets for CPU and memory profiling of JCC.

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
```

## Cross-Compiler Benchmarks (JCC vs GCC)

For a higher-level view — comparing JCC to a real C compiler on the same portable workloads — see [BENCHMARKS.md](BENCHMARKS.md). The benchmark suite runs each workload under JCC × {none,O1,O2,O3} and GCC × {O0,O1,O2,O3}, verifies identical output, and reports per-config wall-clock timings plus speedup ratios.

```bash
make bench-compare            # full run
make bench-compare-quick      # quick run
python3 bench.py --filter fib.c   # one benchmark
```

The hyperfine-based `make bench` and the cross-compiler `make bench-compare` are complementary: `make bench` profiles a single workload in depth (with shell-startup variation), while `make bench-compare` produces a side-by-side matrix of how JCC stacks up against GCC.

## Output Files

All profiling output is written to `profile/`:

| File | Tool | Content |
|------|------|---------|
| `profile/bench.json` | hyperfine | Benchmark results in JSON |
| `profile/cpu.prof` | gperftools | Raw CPU profile |
| `profile/cpu.txt` | gperftools | Text CPU profile summary |
| `profile/mem.massif` | valgrind | Memory allocation timeline (Linux) |
