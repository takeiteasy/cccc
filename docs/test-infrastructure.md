# Test Infrastructure

Contributor notes: the test layout, suites, loose tests and harnesses.

> Covers the test directory layout, the build.c test targets, the host and smoke harnesses,
> per-architecture build/test workflows, and CI. For *writing* tests with
> `[[cccc::test]]` and the loose-test header directives, see
> [testing.md](testing.md).

## Gist

The 72 `[[cccc::test]]` suites in `tests/suites/` are the main framework.
`./cccc --build build.c --build-target=test` runs each suite once in VM mode
and once in `-c=native` mode, one build target per suite. Loose single-file
tests run by area target and are secondary. Everything is driven by
`build.c`; there is no Python test runner.

```bash
./cccc --build build.c --build-target=test --build-jobs=8 --build-keep-going
```

(`make` builds `./cccc`; `build.c` builds `build/cccc` and every test target
builds its own fresh copy, so `make` is not required first. See
[install.md](install.md).)

## Test directory layout

| Directory | Contents | Pass condition |
|-----------|----------|----------------|
| `tests/suites/` | `[[cccc::test]]` suites (`test_*.c`) | `--testing` run exits 0 (VM and native) |
| `tests/` | Loose single-file tests (`test_*.c`) | Exit 42, unless the header says otherwise |
| `tests/macros/` | Loose macro/comptime tests (`test_*.c`) | Same as `tests/` |
| `tests/host/` | Host-side harnesses linked against the compiler sources | Exit 0 |
| `tests/fuzz/corpus/` | Fuzz regression corpus (currently empty) | Compiles without crashing |
| `tests/benchmarks/` | Benchmarks (`tools/bench.py`, `bench` target) | n/a |

## Suites (main framework)

| Target | Runs |
|---|---|
| `test` | Every suite in VM mode, then in `-c=native` mode (`--testing=native`) |
| `test_vm` / `test_native` | One pass only |
| `test_release` | `test` against the `-O2` `cccc-release` binary |
| `test_msan` | `test` against an MSan build (Linux + clang) |
| `test_ubsan` / `test_asan` / `test_tsan` | `test` against a UBSan, ASan+UBSan or TSan build |
| `opt_test_O1` / `_O2` / `_O3` | `test` against cccc built at that host `-O` level |

Each suite is its own build target, so `--build-jobs=8` parallelises across
suites and `--build-keep-going` reports every failing suite in one run.

A suite's leading comment takes two optional lines (full table in
[testing.md](testing-suites.md#test-file-header-directives)):

- `// CCCC_FLAGS: <flags>` — flags for the suite (default `--testing`); in the
  native pass `--testing` becomes `--testing=native`.
- `// CCCC_NATIVE_SKIP: <reason>` — leave the suite out of the native pass.

There are no skip tables and no platform or compiler-family keyed skips: a
suite is either in the native pass or carries `CCCC_NATIVE_SKIP`.

`--testing=native` serializes a standalone TAP harness into the generated C
(a table of the program's `[[cccc::test]]` records and a `main()` that forks
each test, giving per-test isolation, `exit_code=` support, stdout/stderr
regex capture and timeouts). Not supported natively, refused by the compiler
with a diagnostic: `test_setup`/`test_teardown` hooks and negative tests
(`error=`/`expect_compile_error=`). A per-test `flags=` delta and `RET_STRUCT`
return assertions are individually `SKIP`ped in the TAP output. A suite using
any of these carries `CCCC_NATIVE_SKIP` or accepts the skips. Known
VM-versus-native divergences are in
[native.md](native-differences.md#behaviour-differences).

To reproduce a native divergence against a specific host compiler, set
`CCCC_NATIVE_CC` (a real gcc on macOS needs installing separately, e.g.
`/opt/homebrew/bin/gcc-16`):

```bash
CCCC_NATIVE_CC=/opt/homebrew/bin/gcc-16 ./cccc --build build.c --build-target=test_native
```

## Loose tests (area targets)

Loose tests are checked by `tools/testcheck.c`, which `build.c` builds as
`build/testcheck`:

```bash
build/testcheck <cccc> <test.c>      # run one test
build/testcheck --native <cccc> <test.c>  # build it with -c=native, run the binary
build/testcheck --lint <files>       # check every directive pattern compiles
```

Directives (`EXPECT_COMPILE_ERROR`, `EXPECT_RUNTIME_ERROR`, `CCCC_FLAGS`,
`CCCC_RUN_ARGS`, `CCCC_EXPECT_STDOUT`/`REJECT_STDOUT`/`EXPECT_STDERR`/
`REJECT_STDERR`) are documented in
[testing.md](testing-suites.md#test-file-header-directives). Patterns are POSIX
ERE and every line applies; an unknown `CCCC_*:` directive is an error. A test
passes on exit 42 (also 0 for `--testing` or when `CCCC_EXPECT_STDOUT` is
set; non-zero is accepted for `--testing`/`--build` with
`CCCC_EXPECT_STDERR`). `ok` patterns must be anchored (`^ok 1 - name`), and a
`--testing` run fails on any `not ok` / `✗` / JSON fail line that no expect
pattern matches (`testcheck` reports `unexpected failure line`).

| Target | Files |
|---|---|
| `test_warnings` | `test_warning_*` |
| `test_serialize` | `test_serialize_*`, `test_native_*` |
| `test_build_mode` | `test_build_*` |
| `test_safety` | `test_checked_*`, `test_dangling_*`, `test_heap_*` |
| `test_parallel` | `test_kernel_*`, `test_opencl_*`, `test_openmp_*` |
| `test_macros` | `tests/macros/` |
| `test_misc` | everything else |
| `test_native_loose` | every loose test again, built with `-c=native` |
| `test_smoke` | `tests/smoke/*.sh` multi-step checks |
| `test_extra` | all of the above plus `test_smoke` |

These are secondary: run the matching target when work touches that area, not
on every push. No sharding — use `--build-jobs`.

The native pass (`testcheck --native`) compiles each test with `-c=native`
and runs the binary with `CCCC_RUN_ARGS`; a `--testing` test runs as
`--testing=native`. Compile and run output are checked together against the
test's patterns. It skips (prints `SKIP`, passes) a test that:

- carries `// CCCC_NATIVE_SKIP: <reason>` — VM-only semantics, or a native bug
  with its open ticket;
- expects a compile error (the front end is shared, so it adds nothing);
- picks its own output mode: `-c`, `-m`/`--dump-expanded`, `-o`, `-E`,
  `--build*`, `--watch`, `--emit-cccc`.

### Leak pass

`test_leaks` runs every suite and loose test through `testcheck --leaks`: a
passing test is rerun under `leaks -atExit` (macOS) or valgrind (elsewhere),
and a leak in cccc's own process fails it. The children a `--testing` run
forks are not followed.

On macOS a forked child that `exit()`s stops itself under `leaks -atExit` and
never resumes, so a leak-pass timeout is reported as `SKIP` (the Linux valgrind
pass covers those tests). The report is read with NUL bytes blanked, and a
valgrind error that is not a leak (an uninitialised read) is labelled
`valgrind error (not a leak)`. Tests expecting a compile error are left out of
the pass because `error()` exits without teardown. cccc frees its own
exit-time state through `free_compiler_exit_state` in `src/main.c`.

Guest `malloc` is made visible to the tool with `-V`, except for tests that
use `--testing`, a safety flag, or carry `CCCC_LEAKS_KEEP_VM_HEAP`.

| Directive | Effect |
|---|---|
| `CCCC_EXPECT_LEAK[: reason]` | a leak is not a failure (a deliberate one, or a known compiler leak) |
| `CCCC_LEAKS_KEEP_VM_HEAP` | do not inject `-V` (the test needs the VM heap) |
| `CCCC_LEAKS_SKIP: reason` | left out of the pass; the reason cites the open ticket for a known compiler bug |

A suite takes `CCCC_LEAKS_SKIP` in its header like the other two lines.

### Not covered

| Gap | Follow-up |
|---|---|
| No sanitizer-binary selection, `--vm-profile`/`--perf` or per-pass optimisation matrix flags on loose tests (`opt_test_O*` and the `test_*san` targets cover the suites) | — |

## Host-side test harnesses

`tests/host/` holds regression tests that cannot be expressed as a single
`cccc`-compiled guest source file — for example, constructing multiple
`VirtualMachine` instances (or threads) in one process to catch cross-instance
static-state corruption. Each `tests/host/test_*.c` links directly against the
compiler sources (`#include "internal.h"`, like `src/fuzzing.c`), builds with
the same flags as the main binary, and `return 0`s on success (not the guest
exit-code-42 convention). They are chained `&&` in one command, so the first
failure stops the run. The `test` target runs them too; to run only them:

```bash
./cccc --build build.c --build-target=host_tests
```

## Fuzz replay

```bash
./cccc --build build.c --build-target=fuzz_replay
```

Runs `tools/fuzz_replay.py --binary PATH`, compiling every
`tests/fuzz/corpus/*.c` (compile-only). The corpus is currently empty.

## Native-backend serializer tests

The serializer that reconstructs a runtime translation unit only runs under
`-m`/`-c=generated`/`-c=native`, so suites run through the VM never touch it.
Three kinds of loose test cover it:

| Kind | Where | How |
|---|---|---|
| Round trip | `tests/test_native_*.c`, most `test_serialize_*.c` | plain exit-42 programs; the VM pass and the `-c=native` pass of `test_native_loose` both run them |
| Output shape | `tests/test_serialize_*_m.c` | `CCCC_FLAGS: -m` (or `-c=generated -o /dev/stdout`) with `CCCC_EXPECT_STDOUT`/`CCCC_REJECT_STDOUT` on the text |
| Multi-step | `tests/smoke/*.sh` | one script per scenario, run by `test_smoke` |

Helper sources shared by several tests (headers, extra translation units)
live in `tests/fixtures/`. A multi-TU test lists the helper `.c` files in
`CCCC_FLAGS`; the test file is the last TU.

**Directives for these tests.**

| Directive | Effect |
|---|---|
| `CCCC_HOST_CC[: flags]` | the stdout must also compile with the host `cc` (`$CC`); use `-Itests` when it replays an include of a fixture |
| `CCCC_ENV: NAME=value` | environment variable for cccc and the program (repeatable) |
| `CCCC_VM_SKIP: reason` | only the `--native` pass runs it (extended asm, VM-divergent `errno`) |
| `CCCC_NO_INCLUDE_DIR` | do not pass `-I./include`, so headers come from the embedded table |

**Writing a test.** Prefer an end-to-end round trip whose exit code depends on
the behaviour being right, plus a `_m` test for the textual shape. Header-
sourced declarations must not get a serialized prototype (the replayed
`#include` supplies it).

**Smoke scripts.** `tests/smoke/lib.sh` gives each script a scratch directory
and helpers (`expect_exit`, `expect_fail`, `logging_wrapper`, `find_family`).
The scripts cover what one `testcheck` run cannot: the argv handed to the host
compiler through a `CCCC_NATIVE_CC` wrapper, artifacts written or refused by
`--test-run`/`--testing`, default output names (`a.out`, `a.gen.c`), linking
`-c=generated` output, and the width-0 bit-field layout guard against a real
clang and gcc.

**Known gap.** `test_native_ctor_dtor_native_1020` fails when `CCCC_NATIVE_CC`
is gcc on macOS: gcc on Darwin rejects `__attribute__((constructor(150)))`.

## Guide pages

| Page | Covers |
|---|---|
| [selfhosting.md](selfhosting.md) | The self-hosting gate |
| [ci.md](ci.md) | Per-architecture workflows and continuous integration |
