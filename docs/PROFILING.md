# CCCC Profiling Scripts

Helper scripts and Makefile targets for CPU, memory, and VM opcode profiling
of CCCC.

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
./cccc --vm-profile -I./include tests/test_comprehensive.c
./cccc --vm-profile --json -I./include tests/test_comprehensive.c > profile/vm-opcodes/comprehensive.json
./cccc -Y build/fib.jbc
```

`--vm-profile` prints a compact dynamic opcode count table to stderr after the
program exits. Combine it with `--json` to also write the same data as JSON to
stdout. The JSON includes the execution mode (`source` or `jbc`), selected
`--optimize` level, total VM cycles, total profiled opcodes, and per-op counts
and percentages.

### Static Bytecode Analysis

For understanding *static* instruction patterns in `.jbc` files (independent of
any execution), cccc has two in-process analyses:

```bash
# Static n-gram mining on a pre-compiled .jbc
./cccc -o /tmp/sieve.jbc -I./include profile/benchmarks/sieve.c
./cccc --ngrams=2 --ngrams-top=15 /tmp/sieve.jbc
./cccc --ngrams=3 --ngrams-top=15 /tmp/sieve.jbc
./cccc --ngrams=2 --ngrams-per-file /tmp/sieve.jbc

# Same analysis directly on .c source — compiles in-process first
./cccc --ngrams=2 --ngrams-top=15 -I./include profile/benchmarks/sieve.c

# Use-def fusion candidate detection
./cccc --fusion-candidates=50 /tmp/sieve.jbc
./cccc --fusion-candidates=50 --json /tmp/sieve.jbc
```

`--ngrams[=N]` walks the text segment of one or more `.jbc` files and ranks
2-grams (`N=2`, default) or 3-grams (`N=3`) by occurrence. `--ngrams-per-file`
also prints a per-input section in addition to the aggregate. `--ngrams-top=N`
limits the rows per section.

`--fusion-candidates[=N]` walks the text segment, tracks register defs/uses
per instruction, and reports adjacent `def -> use` pairs where the defining
instruction has a single reader. Add `--json` to get JSON output for scripting.

These two analyses are mutually exclusive with `--vm-profile*`, `-g/--debug`,
`-d/--disassemble`, `-c=native`, and any safety / execution / output flags.
See [PROFILING.md](PROFILING.md) and [VM.md](VM.md) for how to combine the
static counts with the dynamic bigram profile to surface the strongest
fusion candidates.

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
hyperfine --warmup 3 './cccc -I./include tests/test_comprehensive.c'

# Compare two versions
hyperfine --warmup 3 \
  -n 'main' './cccc -I./include tests/test_comprehensive.c' \
  -n 'branch' './cccc-branch -I./include tests/test_comprehensive.c'
```

### macOS `sample` (built-in CPU profiler, no install needed)

```bash
# Run compiler and sample it
./cccc -I./include tests/test_comprehensive.c &
PID=$!
sample $PID -mayDie -file profile/sample.txt
```

### macOS `heap` (built-in heap profiler)

```bash
# Requires Xcode command-line tools
heap -s -guessNonObjects ./cccc -I./include tests/test_comprehensive.c
```

### gperftools CPU profiler (cross-platform)

```bash
# Build profiling binary
make profile-cpu-build

# Run with profiling enabled
CPUPROFILE=profile/out.prof ./cccc-prof -I./include tests/test_comprehensive.c

# View raw profile (symbolized)
CPUPROFILE_FREQUENCY=1000 ./cccc-prof -I./include tests/test_comprehensive.c
```

## tools/tests.py Integration

```bash
python3 tools/tests.py --bench                        # Benchmark all tests
python3 tools/tests.py --bench --match "*compre*"     # Benchmark matching tests
python3 tools/tests.py --profile-cpu --match "*compre*"  # CPU profile matching tests
python3 tools/tests.py --profile-mem --match "*malloc*"  # Memory profile matching tests
python3 tools/tests.py --vm-profile --match "*profile*"  # VM opcode JSON profiles
python3 tools/tests.py --jbc --vm-profile --match "*profile*"  # Profile .jbc execution
```

`tools/tests.py --vm-profile` writes one JSON file per test under
`profile/vm-opcodes/`. In `--jbc` mode it profiles the bytecode execution phase,
not the source-to-bytecode save step.

## Cross-Compiler Benchmarks (CCCC vs GCC)

For a higher-level view — comparing CCCC to a real C compiler on the same portable workloads — see [BENCHMARKS.md](BENCHMARKS.md). The benchmark suite runs each workload under CCCC × {none,O1,O2,O3} and GCC × {O0,O1,O2,O3}, verifies identical output, and reports per-config wall-clock timings plus speedup ratios.

```bash
make bench-compare            # full run
make bench-compare-quick      # quick run
python3 tools/bench.py --filter fib.c   # one benchmark
python3 tools/bench.py --filter fib.c --vm-profile   # include opcode profile JSON
```

The hyperfine-based `make bench` and the cross-compiler `make bench-compare` are complementary: `make bench` profiles a single workload in depth (with shell-startup variation), while `make bench-compare` produces a side-by-side matrix of how CCCC stacks up against GCC.

When `tools/bench.py --vm-profile` is enabled, CCCC and CCCC-JBC configs write per
benchmark/config opcode profiles under `profile/benchmarks/results/vm-profile-<UTC>/`.
Each timing record in the benchmark JSON includes its `vm_profile_json` path.

## Output Files

All profiling output is written to `profile/`:

| File | Tool | Content |
|------|------|---------|
| `profile/bench.json` | hyperfine | Benchmark results in JSON |
| `profile/cpu.prof` | gperftools | Raw CPU profile |
| `profile/cpu.txt` | gperftools | Text CPU profile summary |
| `profile/mem.massif` | valgrind | Memory allocation timeline (Linux) |
| `profile/vm-opcodes/*.json` | CCCC VM profiler | Dynamic opcode counts per test |
| `profile/benchmarks/results/vm-profile-*/` | CCCC VM profiler | Opcode profiles for benchmark configs |
