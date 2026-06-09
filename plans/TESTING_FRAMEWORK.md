# CCCC Built-In Test Framework

> **Status:** Design draft — not implemented yet. Open questions are flagged
> with **[Q:]**. This document is intended to be reviewed before any code is
> written. See also [BUILD_MODE.md](BUILD_MODE.md) for the build system this
> plan assumes exists.

A built-in test framework that uses the same attribute-driven machinery as
`[[cccc::comptime]]` and the planned `[[cccc::build]]` / `[[cccc::build_target]]`
attributes. Tests are first-class declarations in the source; the test
runner is invoked with `--testing` and replaces the current Python-based
`tools/tests.py` for day-to-day use.

The existing 577 test files in `tests/`, `tests/macros/`, and
`tests/failures/` are migrated in place, with one mechanical rewrite per
file (replace `int main()` with `[[cccc::test]] int test_<filename>()`,
replace `EXPECT_*` comments with `[[cccc::negative_test(...)]]`).

## Goals

- **First-class tests** — a test is a tagged function, not a filename
  convention plus a header comment. Reflection tooling (`--ffi-decls`)
  lists tests alongside other declarations.
- **One binary, one command** — `./cccc --testing tests.c` is the only
  command you need to run the suite. No external runner script.
- **Fast iteration** — VM-mode tests run in a single VM by default;
  the per-test overhead is a function call, not a fork+exec.
- **Covers both worlds** — the build system is native-only
  (see [BUILD_MODE.md](BUILD_MODE.md)), so the testing framework
  is the only place bytecode / VM-execution is exercised. A test
  can run in the VM (default), be built natively via `-c=native`
  and run, or both — a test marked `mode = "both"` is a
  compatibility test that must pass in either world.
- **CCCC options live in source** — test files declare their
  VM-configuration needs (safety level, debug, profiling) via
  `#pragma cccc ...`. The runner collects pragmas from each test
  file and applies them when the test runs in the VM. This
  mirrors how production code is self-describing; the test
  framework just reads what the source already says.
- **No loss of coverage** — the migration to the framework preserves
  every existing assertion, including the negative-test markers.
- **Pluggable execution granularity** — v1 runs all positive tests
  in one VM and each negative test in its own process; the runner
  is structured so that per-test, per-suite, and hybrid modes can
  be added later without rewriting test code. This is the one
  explicit requirement of the design.
- **Output the existing tooling can consume** — TAP by default, with a
  `--format=json` option that the existing `tools/tests.py` summary
  code can ingest for backwards compatibility.

## Non-Goals (v1)

- **Source-level coverage measurement.** Out of scope; a `gcov`/LLVM
  integration would come later.
- **Property-based / fuzz tests.** The framework is for hand-written
  assertions. The AFL/libFuzzer harnesses stay as separate targets.
- **Distributed / remote tests.** Local only.
- **Timeouts per test.** A hanging test hangs the whole v1 runner. v2
  can add per-test timeouts when the per-test-process mode lands.
- **Setup/teardown fixtures.** Test functions are plain C functions.
  Shared state is set up by a test helper called from each test, or by
  a `[cccc::before_all]` / `[cccc::after_all]` pair on a suite (v2).

## CLI

```
./cccc --testing tests.c                       # run every test in tests.c
./cccc --testing tests.c --test-suite=pointers # filter to one suite
./cccc --testing tests.c --test=pointer_*      # filter to a name glob
./cccc --testing tests.c --list                # list, don't run
./cccc --testing tests.c --format=tap          # TAP (default)
./cccc --testing tests.c --format=json         # machine-readable
./cccc --testing tests.c --format=plain        # original human format
./cccc --testing tests.c -v                    # show each assertion
./cccc --testing tests.c --fail-fast           # stop on first failure
./cccc --testing tests.c --test-target=vm      # run only VM-mode tests
./cccc --testing tests.c --test-target=native  # run only native-build tests
./cccc --testing tests.c --test-target=both    # run only "both" tests
./cccc --testing tests.c --test-target=all     # run all (default)
./cccc --testing tests.c -j8                   # parallel (future)
./cccc --testing tests.c --seed=0xC0FFEE       # deterministic seeding
```

| Flag | Default | Meaning |
|------|---------|---------|
| `--testing` | (off) | Switch to test mode. The input file is treated as a test entry; `main()` is not required. |
| `--test-entry=NAME` | `test_main` | Symbol to invoke as the test entry. |
| `--test-suite=NAME` | (all) | Run only the named suite. Repeatable. |
| `--test=GLOB` | `*` | Run only tests whose name matches the glob. Repeatable. |
| `--test-target=MODE` | `all` | Which test targets to run: `vm`, `native`, `both`, or `all`. `all` runs VM-only tests in the VM, native-only tests natively, and `both` tests in both worlds. See [Test Targets (VM vs Native)](#test-targets-vm-vs-native). |
| `--list` | off | Print discovered tests and exit. |
| `--format=FMT` | `tap` | `tap`, `json`, or `plain`. |
| `-v` / `--verbose` | off | Print each assertion as it runs. |
| `--fail-fast` | off | Stop on the first failure. |
| `--test-seed=N` | time-based | Deterministic seed for any randomness the tests use. |
| `--test-mode=MODE` | `all-in-vm` | Granularity of *where* each test runs. v1: `all-in-vm`. Future: `per-test`, `per-suite`, `hybrid`. See [Test Execution Model](#test-execution-model). |
| `-j` / `--jobs=N` | nproc | Reserved for the future per-test/parallel modes. v1 ignores it. |

The runner rejects combining `--testing` with `--build` and
`--disassemble` (these are unrelated modes). It does **not**
reject `-c=native` style flags when running individual native
tests — those flags apply to the per-test native compile, not to
the test script itself. `--vm-profile`, `--debug`, and the
bytecode-output modes remain rejected at the top level (the
runner doesn't need them); per-test pragmas in test sources are
the way to opt into them for specific tests.

## Attributes

All four attributes are intercepted by the preprocessor the same way
`[[cccc::comptime]]` is — they never reach the general attribute
parser.

### `[[cccc::test]]`

A positive test. The function is run; passing means the function
returns `0` and no recorded assertion failed during its execution.

```c
[[cccc::test]]
int test_fortytwo(void) {
    return 42;  // CCCC convention: a program "passes" by returning 42
                // becomes a test that "passes" by returning 0
}
```

Accepted forms:

| Form | Meaning |
|------|---------|
| `[[cccc::test]]` | Plain test, runs in the VM (default). |
| `[[cccc::test(mode = "vm")]]` | Same as the plain form. |
| `[[cccc::test(mode = "native")]]` | The test is built natively (`-c=native`) and run as a child process. The C source must compile as a standalone program with `main()`. |
| `[[cccc::test(mode = "both")]]` | The test is run twice — once in the VM, once built natively. Both must pass. This is the "I want to make sure this works in both worlds" mode. |
| `[[cccc::test(suite = "name")]]` | Test belongs to suite `name`. |
| `[[cccc::test(name = "foo")]]` | Override the auto-derived test name. |
| `[[cccc::test(flags = "--foo")]]` | Extra CCCC flags applied when this test runs in the VM (e.g. `--bounds-checks`, `--optimize=2`). For native tests, forwarded to the underlying `cc`. |
| `[[cccc::test(timeout = 1000)]]` | Reserved. v1 ignores. |

The test's *name* is derived from the C function name by default, with
optional `name = "..."` to override. The `--test=*_foo*` glob matches
against the C name, not the source file.

For `mode = "native"` and `mode = "both"`, the test source is built
with the same flags the existing `-c=native` path uses (see
[README.md](../README.md)). The pragma mechanism in
[CCCC Pragma Extensions](#cccc-pragma-extensions) is the
source-driven way to express CCCC options; the `flags = "..."` form
on the attribute is the inline equivalent for one-off overrides.

### `[[cccc::negative_test(...)]]`

A test that is expected to fail — either at compile time or at
runtime. The function body is *not* invoked; the runner compiles the
test file in isolation and asserts the failure mode.

```c
[[cccc::negative_test(compile)]]
int test_undeclared_var(void) {
    return undeclared_var;  // compile error expected
}

[[cccc::negative_test(runtime, code = 139)]]
int test_segfault(void) {
    int *p = (int *)0xdead;
    return *p;  // SIGSEGV (139) expected
}
```

Accepted forms:

| Form | Meaning |
|------|---------|
| `[[cccc::negative_test(compile)]]` | The function body must fail to compile. |
| `[[cccc::negative_test(runtime)]]` | The function compiles; running it must fail. |
| `[[cccc::negative_test(compile, msg = "...")]]` | The compile error output must contain `...`. |
| `[[cccc::negative_test(runtime, code = N)]]` | The runtime exit code must be `N`. Default: any non-zero. |

Compile-mode negative tests are compiled in a child CCCC process with
the test function enabled; the test passes if the compile fails
(optionally: with a specific message). The function body is never
called.

### `[[cccc::suite("name")]]`

A grouping marker. Applied to a function — either a regular test
function or a *suite descriptor* function that groups other tests
by name.

```c
[[cccc::suite("pointers")]]
[[cccc::test]]
int test_pointer_basic(void) { return 0; }

[[cccc::suite("pointers")]]
[[cccc::test]]
int test_pointer_advanced(void) { return 0; }
```

Equivalently, as a descriptor function:

```c
[[cccc::suite("pointers")]]
void pointers_suite(cccc_test_ctx_t *ctx) {
    CCCC_RUN_TEST(ctx, test_pointer_basic);
    CCCC_RUN_TEST(ctx, test_pointer_advanced);
}
```

The descriptor form is a convenience for shared setup or for
re-exporting tests from a generated table. v1 supports both.

### `[[cccc::test_main]]`

Marks the test entry. The runner invokes this function to drive the
suite. Identified the same way `[[cccc::build]]` is:

1. `--test-entry=NAME` (highest precedence)
2. `[[cccc::test_main]]` attribute (must be unique)
3. Default name `test_main`

GNU attribute syntax is accepted everywhere:

```c
[[cccc::test]]                       // C23
__attribute__((cccc::test))          // GNU namespace
__attribute__((test))               // GNU short form (cccc namespace prefix is optional)
```

## CCCC Pragma Extensions

CCCC-specific options (VM optimisation, safety levels, debug, profiling,
etc.) can be declared from **inside C source** with
`#pragma cccc ...`. The pragma is the source-level counterpart to the
CLI flag it mirrors.

```c
// This test needs the VM to run with safety level 2
#pragma cccc safety(2)

[[cccc::test]]
int test_bounds_check(void) {
    int arr[3] = {1, 2, 3};
    $assert_eq(arr[0], 1);
    return 0;
}
```

When the testing framework runs this test in the VM, the runner
**parses the file's pragmas once** and applies the equivalent CLI
flags to the in-VM test process. The test runs as if the developer
had invoked `./cccc --testing -2 tests.c`, but the source itself
states the requirement.

Full list of pragmas is in
[BUILD_MODE.md → CCCC Pragma Extensions](BUILD_MODE.md#cccc-pragma-extensions).
The set of pragmas the testing framework understands is identical
to the build-mode / CLI list — there's only one pragma namespace.

### Precedence

CLI flags override pragmas:

```sh
# File says safety(2), CLI says -3. CLI wins. Test runs at -3.
$ ./cccc --testing -3 tests.c
```

This is the right precedence — the developer / CI run's flag is
the authority. Pragmas are for the *common case*; flags are for
the override.

### Scope

Pragmas are file-scope only in v1. A pragma applies to the rest
of the file from its point of occurrence; later pragmas supersede
earlier ones. **[Q: do we want `#pragma cccc push` /
`#pragma cccc pop` for block-scope overrides? My recommendation:
defer to v2 — none of the existing tests need it.]**

### Native-mode tests

Pragmas are ignored for `mode = "native"` and for the native
half of `mode = "both"` tests. A native test is built by the
system toolchain, which has no concept of `-2` or `--vm-profile`.
The test source's `int main()` runs against the real toolchain;
any CCCC-specific option it would have needed in the VM half
either has no native equivalent (debug, profiling) or is
implicit in the toolchain's defaults (optimisation).

## Function Shapes

```c
// Positive test
[[cccc::test]]
int test_simple(void) {
    $assert(1 + 1 == 2);
    return 0;
}

// Negative test (compile)
[[cccc::negative_test(compile)]]
int test_undeclared(void) {
    return undeclared;
}

// Negative test (runtime)
[[cccc::negative_test(runtime, code = 139)]]
int test_segfault(void) {
    int *p = (int *)0xdead;
    return *p;
}

// Suite descriptor
[[cccc::suite("basic")]]
void basic_suite(cccc_test_ctx_t *ctx) {
    CCCC_RUN_TEST(ctx, test_simple);
    CCCC_RUN_TEST(ctx, test_undeclared);
    CCCC_RUN_TEST(ctx, test_segfault);
}

// Test main
[[cccc::test_main]]
int test_main(cccc_test_ctx_t *ctx) {
    return cccc_run_all_tests(ctx);
}
```

## Assertion API

`tests.h` is a private header auto-injected into every
`[[cccc::test]]` and `[[cccc::test_main]]` translation unit the same
way `reflection.h` is auto-injected into `[[cccc::comptime]]` code.
It is not on the public include path.

```c
// Hard assertion — record failure and abort the current test
$assert(cond)
$assert_eq(a, b)
$assert_ne(a, b)
$assert_lt(a, b)
$assert_le(a, b)
$assert_gt(a, b)
$assert_ge(a, b)
$assert_streq(a, b)
$assert_not_null(p)

// Soft assertion — record failure, continue
CCCC_EXPECT(cond)
CCCC_EXPECT_EQ(a, b)
// ... (the same family)

// Unconditional fail
CCCC_FAIL("message")

// Run a sub-test from a suite descriptor
CCCC_RUN_TEST(ctx, test_fn)
```

### Mechanism

The macros expand to bookkeeping calls plus, on failure, a
`goto`-based early return out of the *current test function only* — a
test that calls `$assert` does not abort its sibling tests.

Implementation sketch:

```c
// generated prologue for every test function
#define $assert(cond) \
    do { \
        if (!(cond)) { \
            __cccc_test_record_failure(__FILE__, __LINE__, #cond); \
            return 1; \
        } \
    } while (0)
```

The framework stores the failure record in a per-test scratch buffer
attached to the test context. The dispatcher reads it after the test
returns to populate the TAP / JSON output.

This is the lightest mechanism that works in standard C and matches
the rest of CCCC's "no hidden language features" philosophy. A
`setjmp`/`longjmp` flavor is possible later for "skip remaining
assertions in this test" semantics, but is not needed in v1.

## Test Targets (VM vs Native)

The build system is native-only (see [BUILD_MODE.md](BUILD_MODE.md));
VM execution is exercised in the testing framework. A test carries
a `mode` that says which world it belongs to.

| Mode | Where it runs | How it's built |
|------|---------------|----------------|
| `vm` (default) | CCCC VM, in-process with the runner | Compiled by the runner; pragmas applied. |
| `native` | A child process forked by the runner | Compiled with `cc` via the same `-c=native` path the build system uses; CCCC pragmas are ignored. |
| `both` | Both worlds, sequentially | VM pass + native pass; both must pass. |

A test marked `mode = "native"` looks like a normal C program from
the system's perspective — it has an `int main(void)`, gets compiled
and linked by `cc`, and is run as a regular native process. The
runner treats it as a black box: build with `-c=native`, run, check
the exit code.

A test marked `mode = "both"` is the most demanding form — it's a
compatibility assertion that the code is correct in both the VM
and the real toolchain. These are useful for:

- Sanity-checking that the VM and `cc` agree on the language
  semantics.
- Catching features that work in the VM but not natively (or vice
  versa) as the toolchain drifts.
- Validating that the macros and reflection system are
  macro-expanding into C the system compiler accepts.

The default is `vm` because the existing test suite is
overwhelmingly VM-execution-shaped (the 577 test files in
`tests/`, `tests/macros/`, and `tests/failures/` all assume
`./cccc test.c` runs them in the VM). Migration to native-mode is
opt-in: a test gets `mode = "native"` if and only if the developer
wants it built by the real toolchain.

### Pragmas and test targets

For VM-mode tests, pragmas configure the VM (see
[CCCC Pragma Extensions](#cccc-pragma-extensions)). For native-mode
tests, pragmas are **ignored** — the system toolchain has no
concept of `-2` or `--vm-profile`. The `flags = "..."` form on
`[[cccc::test]]` is the way to pass extra flags to either side.

## Test Execution Model

This is the section that locks in the future-proofing. The runner is
designed around a small abstraction:

```c
typedef struct cccc_test_runner cccc_test_runner_t;

// vtable
typedef struct {
    int (*run)(cccc_test_runner_t *r, cccc_test_ctx_t *ctx, cccc_test_t *t);
    void (*discover)(cccc_test_runner_t *r, cccc_test_ctx_t *ctx);
} cccc_test_runner_ops_t;

cccc_test_runner_t *cccc_runner_in_vm(void);      // v1: positive tests
cccc_test_runner_t *cccc_runner_native(void);     // v1: native-mode tests
cccc_test_runner_t *cccc_runner_per_test(void);    // future
cccc_test_runner_t *cccc_runner_per_suite(void);   // future
cccc_test_runner_t *cccc_runner_hybrid(void);      // future
```

Each runner is selected by `--test-mode=MODE` (default: `all-in-vm`).
The `discover` step populates the test list; the `run` step executes
one test and returns its status. The dispatcher, output formatter,
and filtering logic are runner-agnostic.

### v1: `--test-mode=all-in-vm` (default)

```
./cccc --testing tests.c
                       │
                       ▼
   ┌──────────────────────────────────────────────┐
   │  1. Preprocess tests.c                       │
   │  2. Parse; cccc_test/cccc_negative_test attrs  │
   │     are intercepted by the preprocessor      │
   │  3. Find test_main (attr/name/flag)          │
   │  4. Compile test_main into the CCCC VM        │
   │  5. Discover all [[cccc::test]] and           │
   │     [[cccc::negative_test]] functions by      │
   │     scanning the program list, partitioned   │
   │     by mode (vm / native / both)             │
   │  6. Invoke test_main inside the VM           │
   │     ├─ test_main calls cccc_run_all_tests    │
   │     │   (or cccc_run_suite / cccc_run_matching)│
   │     ├─ For each mode=vm positive test:       │
   │     │   - parse the test file's pragmas      │
   │     │   - reset failure buffer for the test  │
   │     │   - call the test function (pragmas    │
   │     │     applied to the in-VM process)      │
   │     │   - record pass/fail from return value │
   │     │     and any recorded assertion failures│
   │     ├─ For each mode=native positive test:   │
   │     │   - fork a child CCCC process with the  │
   │     │     test source compiled via -c=native │
   │     │   - run the produced native executable │
   │     │   - exit 42 == pass (the existing      │
   │     │     convention from the Python runner) │
   │     ├─ For each mode=both positive test:     │
   │     │   - run as mode=vm, then as mode=native│
   │     │   - both must pass                     │
   │     ├─ For each compile-mode negative test:  │
   │     │   - fork a child CCCC process           │
   │     │   - feed it the function body          │
   │     │   - expect non-zero exit               │
   │     │   - optionally check the error message │
   │     ├─ For each runtime-mode negative test:  │
   │     │   - fork a child CCCC process           │
   │     │   - run it                             │
   │     │   - expect the configured exit code    │
   │  7. Aggregate, print TAP/JSON/plain, exit    │
   └──────────────────────────────────────────────┘
```

A single VM instance drives all `mode=vm` positive tests; each
`mode=native` test, each `mode=both` test, and each negative
test gets its own fork for isolation (compile failure can't be
caught inside a running VM, and runtime failures often segfault).
The exit-code convention is mode-specific: VM-mode tests pass on
`return 0` (the new convention), while native-mode tests pass on
the legacy `return 42` (the existing Python runner's convention)
— the runner handles the difference transparently.

### Future: per-test and per-suite runners

The vtable pattern means these can be added without touching test
code or the dispatch loop:

- **`per-test`** — every test, positive or negative, gets its own
  process. Slower but fully isolated. Useful for CI.
- **`per-suite`** — one VM per suite, but compile and runtime
  negatives still get their own process.
- **`hybrid`** — positives in a VM, negatives in their own
  processes. This is what `--test-mode=all-in-vm` v1 effectively is
  for the negative side, but the per-test version is
  `hybrid + per-positive-test`.

All runners must produce the same TAP / JSON / plain output. The
dispatcher, formatter, and `--test=*foo*` filtering operate on the
discovered test list, not the runtime representation.

## Negative Test Mechanics

### Compile mode

The runner forks a child CCCC process and asks it to compile just the
negative test function. The body must produce a compile error. The
runner:

1. Writes a tiny C file that `#include`s the test source with a
   guard macro defined, so only the negative test function's body is
   visible.
2. Invokes `./cccc --no-run -I./include tmp.c`.
3. Checks the exit code is non-zero.
4. If `msg = "..."` is set, greps stderr for that substring.

For tests that mix a negative test with positive tests in the same
file, the test file must gate the negative test behind a macro that
the runner defines during the compile check:

```c
#ifdef CCCC_NEGATIVE_COMPILE_test_undeclared
[[cccc::negative_test(compile)]]
int test_undeclared(void) {
    return undeclared_var;
}
#endif
```

For the file's normal `tests.c` compilation, `CCCC_NEGATIVE_COMPILE_*`
is undefined, so the function is omitted. The runner defines the
right macro when it forks.

This gating is the price of mixing positive and negative tests in
the same file. The existing `tests/failures/` directory convention
(negative tests in their own file) avoids the need for gating for
all current tests.

### Runtime mode

The runner forks `./cccc --no-stdlib -I./include tmp.c` and runs it.
The exit code must match `code = N` (default: any non-zero). Stdout
and stderr are captured for the `--verbose` output.

## Test Discovery And Entry Point

### Single-file tests

A test file declares its own tests and a test main:

```c
// tests/test_pointer_basic.c
[[cccc::test(suite = "pointers")]]
int test_pointer_basic(void) {
    int x = 42;
    int *p = &x;
    $assert_eq(*p, 42);
    return 0;
}

[[cccc::test_main]]
int test_main(cccc_test_ctx_t *ctx) {
    return cccc_run_all_tests(ctx);
}
```

The user can run this file directly: `./cccc --testing
tests/test_pointer_basic.c`.

### Multi-file tests (the migration shape)

A `tests.c` entry point `#include`s the migrated test files and
provides a single test main:

```c
// tests.c
#include "tests/test_fortytwo.c"
#include "tests/test_simple.c"
#include "tests/test_pointer_basic.c"
// ...

[[cccc::test_main]]
int test_main(cccc_test_ctx_t *ctx) {
    return cccc_run_all_tests(ctx);
}
```

Each migrated test file no longer contains `int main()`. Instead it
contains one or more `[[cccc::test]]` (or `[[cccc::negative_test]]`)
functions. Names are derived from the filename: `test_fortytwo.c` →
`test_fortytwo`.

This is the same model the build system uses — the entry point is
plain C, the tests are plain C, the runner is a header injected by
the framework.

### Per-file generation

For migration convenience, the build system (once it exists) can
auto-generate `tests.c` from the contents of `tests/`. The plan is
for `build.c` to include a target that scans the directory and
emits an aggregated `tests.c`. v1 of the test framework doesn't
require this — a hand-written `tests.c` works.

## Output Formats

### TAP (default)

TAP version 13, the same format Rust, Perl, and many other test
harnesses use:

```
TAP version 13
1..5
ok 1 - test_fortytwo
ok 2 - test_pointer_basic
ok 3 - test_simple
not ok 4 - test_div_by_zero
  ---
  file: tests/test_arith.c
  line: 17
  message: assertion failed: b != 0
  ---
ok 5 - test_undeclared_var (negative: compile)
1..5
# tests 5
# pass  4
# fail  1
```

### JSON

```json
{
  "version": 1,
  "summary": { "total": 5, "passed": 4, "failed": 1 },
  "tests": [
    { "name": "test_fortytwo", "status": "pass", "elapsed_ms": 0.1 },
    { "name": "test_div_by_zero", "status": "fail",
      "file": "tests/test_arith.c", "line": 17,
      "message": "assertion failed: b != 0" }
  ]
}
```

### Plain

The current human-readable format that `tools/tests.py` already
emits, kept around for backwards compatibility with anything that
greps test output.

## Migration Plan

485 files in `tests/`, 58 in `tests/macros/`, 34 in `tests/failures/`
— 577 files in total. The migration is mechanical and can be done
per-file with a script plus a human review pass.

### Per-file rewrites

| Original shape | New shape |
|----------------|-----------|
| `int main(void) { return 42; }` | `[[cccc::test]] int test_<filename>(void) { return 0; }` |
| `int main(void) { ... return 42; }` (positive, complex body) | `[[cccc::test]] int test_<filename>(void) { ... return 0; }` |
| `/* EXPECT_COMPILE_ERROR */` header + `int main(void) { ... }` | `[[cccc::negative_test(compile)]] int test_<filename>(void) { ... }` |
| `/* EXPECT_RUNTIME_ERROR */` header + `int main(void) { ... }` | `[[cccc::negative_test(runtime)]] int test_<filename>(void) { ... }` |
| `// CCCC_FLAGS: --foo` in header | `[[cccc::test(flags = "--foo")]]` (or `[[cccc::negative_test(compile, flags = "--foo")]]`) — runner injects the flag when running this test |
| `CCCC_EXPECT_STDERR: ...` / `CCCC_REJECT_STDERR: ...` | `[[cccc::test(stderr_match = "...")]]` / `[[cccc::test(stderr_no_match = "...")]]` |
| Multiple test functions in one file | Each gets its own `[[cccc::test]]` attribute. Names come from the function names. |

The return convention also flips: tests return `0` to pass (the
runner treats any non-zero as a fail), instead of `42`. The
migration script does this swap mechanically.

The script can also auto-derive the test name from the filename, so
`tests/test_fortytwo.c` produces `test_fortytwo` (or the filename
minus the `.c` and `test_` prefix — convention to be decided).

### `tests.c` entry file

Generated or hand-written. It `#include`s every test file and
declares a single `[[cccc::test_main]]` function.

### Fallback: keep `tools/tests.py` as a wrapper

During the migration, `make test` (and the eventual `cccc build`)
should call `./cccc --testing tests.c` for the new path but keep
`tools/tests.py` available as `make test-py` for verifying the
migration. Once the new framework's test count matches the
existing one, `tools/tests.py` is deleted.

## Makefile Conversion

The Makefile is the existing build system; the new build mode
(see [BUILD_MODE.md](BUILD_MODE.md)) replaces it. The test
framework depends on the build system only in the sense that
`make test` is the entry point today — once `build.c` exists, the
`test` target is `./cccc --testing tests.c` invoked from inside
the build entry.

The full target-by-target mapping of `Makefile` → `build.c`, the
chicken-and-egg migration order, and the platform-branch
conversions all live in
[BUILD_MODE.md → Makefile Replacement](BUILD_MODE.md#makefile-replacement).
This doc is for tests, not for the build system.

## Constraints

- Test functions are plain C and run in the CCCC VM (or a child
  process for negatives). The C surface available to them is the
  same as any other CCCC program (see [COVERAGE.md](../docs/COVERAGE.md)).
- `tests.h` is private. It is injected into test code automatically
  the way `reflection.h` is injected into macro code. It is not on
  the public include path.
- The `$assert` macros abort the *current* test only; they do not
  halt the runner. The runner always continues to the next test
  unless `--fail-fast` is set.
- A test that returns non-zero without recording an assertion is
  reported as `fail: returned N` — the test function is allowed to
  signal failure with its return value alone.
- A test that runs forever hangs the runner in v1. There is no
  per-test timeout. This is a v1 limitation; v2 can add it via the
  per-test-process runner.
- `--testing` is mutually exclusive with `--build` and
  `--disassemble` (unrelated modes). `--vm-profile` and `--debug`
  are rejected at the top level because the runner itself does not
  need them; per-test pragmas are the way to opt a specific test
  into them. `-c=native` is **not** rejected at the top level —
  it applies to the per-test native compile when the test's
  `mode = "native"` or the native half of `mode = "both"`.

## Open Questions

**[Q:1]** Should the auto-derived test name be the C function name
(`test_foo` from `int test_foo(void) { ... }`) or the filename
(`test_foo` from `tests/test_foo.c`)? The migration wants filename-
based naming for stability when the function gets renamed. My
recommendation: filename-based, with `name = "..."` override.

**[Q:2]** Should `$assert` early-return from the *test* or from
the *whole test_main*? v1 wants test-local early return. This is
implemented as `return 1` from the test function, which means
asserts work in tests but **not** in `test_main` (which is fine —
test_main should not contain asserts). v2 can add a longjmp-based
"abort test only" mechanism if real-world use wants it.

**[Q:3]** Should the existing `CCCC_FLAGS: ...` per-test flags be
preserved as `[[cccc::test(flags = "...")]]`? **Resolved:** yes.
The migration uses `flags = "..."` on the attribute for the
short term, with the pragma mechanism ([CCCC Pragma
Extensions](#cccc-pragma-extensions)) as the recommended
long-term form for CCCC-specific options.

**[Q:4]** How should multiple `[[cccc::test]]` functions in the same
file share setup? v1: just write a helper. v2: a `[cccc::before_all]`
attribute on a setup function that's called once per suite. I
recommend deferring `before_all` to v2.

**[Q:5]** Should the test framework be available without the build
system? **Resolved:** yes — the test framework and the build
system are independent. The Makefile can keep using
`tools/tests.py` for the test run while the build system is being
designed, then switch cleanly when both are ready.

**[Q:6]** Should `#pragma cccc push` / `#pragma cccc pop` for
block-scope CCCC-option overrides ship in v1 or v2? My
recommendation: v2 — none of the existing tests need it.

## Implementation Sketch

Roughly in order of dependency:

1. **`[[cccc::test]]` / `[[cccc::negative_test]]` / `[[cccc::suite]]` /
   `[[cccc::test_main]]` attribute parsing** — same machinery as
   `[[cccc::comptime]]`. The preprocessor intercepts them and tags
   declarations in the symbol table.
2. **`tests.h` private header** — prototypes for
   `cccc_test_ctx_t`, `cccc_test_t`, the assertion macros, the runner
   API (`cccc_run_all_tests`, `cccc_run_suite`, `cccc_run_matching`,
   `CCCC_RUN_TEST`), and the `mode` argument parsing.
3. **Test discovery** — symbol-table scan for functions tagged with
   the test attributes; build the in-memory test list, partitioned
   by `mode` (vm / native / both).
4. **The `all-in-vm` runner** — call each `mode=vm` test function,
   record pass/fail, format output. The runner is a vtable so the
   `per-test` / `per-suite` / `hybrid` modes are drop-in.
5. **The `native` runner** — for each `mode=native` and the
   native half of `mode=both` tests, fork a child CCCC process with
   the test source compiled via the existing `-c=native` path, run
   the produced native executable, check the exit code (42 == pass,
   the existing Python-runner convention).
6. **CCCC pragma collection** — a small parser pass over each
   `mode=vm` test's source file that lifts `#pragma cccc ...` lines
   into the equivalent CLI flag list, applied to the in-VM
   process for that test.
7. **Negative-test forking** — the runner forks a child CCCC for
   compile-mode and runtime-mode negative tests. This uses the
   existing fork / exec / waitpid primitives from `src/main.c`.
8. **`--testing` CLI mode** — new branch next to the build mode in
   `src/main.c`. Locates the test entry, discovers tests, runs them,
   formats output, propagates exit code. Handles `--test-target=...`
   filtering.
9. **TAP / JSON / plain formatters** — three small output functions.
10. **Migration script** — a one-shot Python or shell script that
    walks `tests/`, `tests/macros/`, `tests/failures/`, applies the
    per-file rewrite rules from [Migration Plan](#migration-plan),
    and writes the new files. Human review pass for anything that
    looks odd.
11. **`tests.c` entry file** — hand-written or auto-generated; lives
    in the repo root or under `tests/`.
12. **Side-by-side CI run** — `make test` runs both the new
    framework and `tools/tests.py` for one release; counts must
    match. Then `tools/tests.py` is removed.
13. **Doc updates** — `README.md` adds a "Testing" section.
    `AGENTS.md` reference table gets a row for this doc.

## Future Plans

The v1 design is deliberately forward-looking. The runner
vtable, the partitioned test list, and the pragma mechanism
together support a set of v2 enhancements without rewriting
test code:

- **Per-test, per-suite, and hybrid execution modes.** The
  vtable is in place; the v1 `all-in-vm` runner is one
  implementation. v2 adds the others as `cccc_runner_per_test`,
  `cccc_runner_per_suite`, `cccc_runner_hybrid`. The dispatch loop
  and formatters do not change.
- **Per-test timeouts.** Available the moment the `per-test`
  runner lands — a child process can be killed after a deadline.
  v1's `all-in-vm` runner cannot enforce timeouts.
- **Setup / teardown fixtures.** A `[cccc::before_all]` /
  `[cccc::after_all]` attribute on a suite descriptor, plus
  `[cccc::before_each]` / `[cccc::after_each]` for per-test
  fixtures. v2.
- **Block-scope pragmas.** `#pragma cccc push` / `#pragma cccc pop`
  for local CCCC-option overrides inside a test function. v2.
- **Source-level coverage.** A `gcov`-style integration (or the
  LLVM equivalent via the planned bytecode-to-LLVM backend)
  wired into the runner. Coverage is a flag on the test mode
  and an additional output format.
- **Property-based / fuzz test helpers.** The framework stays
  for hand-written assertions; the AFL/libFuzzer harnesses stay
  as separate build targets. A v2 helper API could let
  property-based tests share the test-runner plumbing without
  becoming AFL targets.
- **Distributed test runs.** Partition the discovered test list
  across machines; the runner is already a pure dispatch +
  aggregate operation. A `--shard=N/M` flag would be the
  smallest possible change.

The build system has its own future plan
([BUILD_MODE.md → Native Backends](BUILD_MODE.md#native-backends));
the test framework is the path the testing surface takes when
the v2 work lands, not the build system.

## See Also

- [BUILD_MODE.md](BUILD_MODE.md) — the build system that
  replaces the Makefile (see
  [Makefile Replacement](BUILD_MODE.md#makefile-replacement)).
  The testing framework covers VM and native execution; the
  build system is native-only and produces executables /
  libraries. The two systems share the CCCC pragma namespace —
  the full list is in
  [BUILD_MODE.md → CCCC Pragma Extensions](BUILD_MODE.md#cccc-pragma-extensions).
- [MACROS.md](../docs/MACROS.md) — the `[[cccc::comptime]]` system
  whose attribute-parsing and auto-injection machinery the test
  framework reuses.
- [COVERAGE.md](../docs/COVERAGE.md) — the C surface available
  to test code.
- [DEBUGGER.md](../docs/DEBUGGER.md) — the `-g` interactive
  debugger, which is mutually exclusive with `--testing` at
  the top level. A v2 test could opt into the debugger via
  `#pragma cccc debug` for interactive debugging of a single
  test.
- [SAFETY.md](../docs/SAFETY.md) — the safety levels
  (`-0` ... `-3`) that VM-mode tests opt into via
  `#pragma cccc safety(N)`.
- [OPTIMIZATION.md](../docs/OPTIMIZATION.md) — the bytecode
  optimisation passes that `#pragma cccc optimise(N)` activates
  for VM-mode tests.
