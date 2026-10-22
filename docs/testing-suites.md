# Test Suites

Group tests and configure a test file with header directives.

## Test Suites

Tests can be grouped into named suites. Two syntaxes are supported.

### Attribute argument

Specify the suite directly on the test attribute:

```c
[[cccc::test(suite = "math")]]
void test_add(void) {
    AssertEq(1 + 1, 2);
}

[[cccc::test(suite = "math")]]
void test_mul(void) {
    AssertEq(6 * 7, 42);
}
```

Suite names can include `/` to form a hierarchical path (e.g. `suite = "math/trig"`).

### Pragma block

Wrap a run of test functions in a pragma block to assign them all to the same suite:

```c
#pragma cccc suite begin "strings"

[[cccc::test]]
void test_equality(void) {
    AssertStrEq("foo", "foo");
}

[[cccc::test]]
void test_empty(void) {
    AssertStrEq("", "");
}

#pragma cccc suite end
```

### Nested suite blocks

Pragma blocks can be nested to form a hierarchical suite path. The separator is `/`.

```c
#pragma cccc suite begin "math"

[[cccc::test]]
void test_basic_add(void) { AssertEq(1 + 1, 2); }  // suite: "math"

#pragma cccc suite begin "trig"

[[cccc::test]]
void test_sin(void) { ... }  // suite: "math/trig"

[[cccc::test]]
void test_cos(void) { ... }  // suite: "math/trig"

#pragma cccc suite end  // end "trig"

[[cccc::test]]
void test_basic_mul(void) { AssertEq(3 * 3, 9); }  // suite: "math"

#pragma cccc suite end  // end "math"
```

Every `#pragma cccc suite begin` must have a matching `#pragma cccc suite end`. An unclosed block at end-of-file is a compile error.

Tests outside any block or attribute have no suite.

### Filtering by suite

Use `--test-suite=NAME` to run only tests whose suite matches `NAME`:

- **Exact match:** `--test-suite=math` runs tests with suite exactly `math`.
- **Prefix/sub-suite match:** `--test-suite=math` also runs `math/trig`, `math/algebra`, and any deeper nesting — anything whose suite path starts with `math/`.
- **Glob:** If `NAME` contains glob metacharacters (`*`, `?`, `[`), it is matched with `fnmatch` against the full suite path. For example, `--test-suite='math/*'` selects all direct children of `math`.

```bash
./cccc --testing --test-suite=math       myfile.c  # math + all sub-suites
./cccc --testing --test-suite=math/trig  myfile.c  # only math/trig (and deeper)
./cccc --testing --test-suite='math/*'   myfile.c  # glob: direct children only
```

> **Note:** Setup/teardown hooks are matched against suites by exact name by default.
> A hook scoped to `math` does **not** automatically run for `math/trig` tests
> unless the hook is declared with the `inherit` keyword (see [Setup and Teardown](testing-hooks.md#setup-and-teardown)).

## Test-File Header Directives

The `[[cccc::test]]` suites in `tests/suites/` are the main test framework:
`./cccc --build build.c --build-target=test` runs each one in VM and
`-c=native` mode. A suite's leading comment block takes two optional lines:

| Directive | Meaning |
|---|---|
| `// CCCC_FLAGS: <flags>` | Flags for this suite (default `--testing`). |
| `// CCCC_NATIVE_SKIP: <reason>` | Leave the suite out of the `-c=native` pass. |

Single-file tests (`tests/test_*.c`, `tests/macros/test_*.c`) cover what a
suite can't express: warnings, diagnostics, `-m` / `-c=generated` output,
`--build` scripts. Each file is compiled with `cccc -I./include <flags> file.c`
and passes when it exits 42, unless its header says otherwise. They run by
area:

| Target | Files |
|---|---|
| `test_warnings` | `test_warning_*` |
| `test_serialize` | `test_serialize_*`, `test_native_*` |
| `test_build_mode` | `test_build_*` |
| `test_safety` | `test_checked_*`, `test_dangling_*`, `test_heap_*` |
| `test_parallel` | `test_kernel_*`, `test_opencl_*`, `test_openmp_*` |
| `test_macros` | `tests/macros/` |
| `test_misc` | everything else |
| `test_smoke` | `tests/smoke/*.sh` multi-step checks |
| `test_extra` | all of the above plus `test_smoke` |

```bash
./cccc --build build.c --build-target=test_warnings --build-jobs=8
```

Directives, one per comment line, anchored as the first token:

| Directive | Meaning |
|---|---|
| `EXPECT_COMPILE_ERROR` | `cccc` must fail with a diagnostic (see [Negative Tests](testing-negative.md#negative-tests)). |
| `EXPECT_RUNTIME_ERROR` | The program must exit 255 via the runtime-safety trap (see [Runtime Safety Violations](testing-negative.md#runtime-safety-violations)). |
| `CCCC_FLAGS: <flags>` | Extra `cccc` flags. |
| `CCCC_RUN_ARGS: <args>` | Arguments for the program, after `--`. |
| `CCCC_EXPECT_STDOUT: <regex>` | stdout must match. |
| `CCCC_REJECT_STDOUT: <regex>` | stdout must not match. |
| `CCCC_EXPECT_STDERR: <regex>` | stdout + stderr must match. |
| `CCCC_REJECT_STDERR: <regex>` | stdout + stderr must not match. |

Example:

```c
// CCCC_FLAGS: -Wextra
// CCCC_EXPECT_STDERR: old-style \(K&R\).*\[-Wold-style-definition\]
```

A directive can repeat; every line applies. `EXPECT_*_ERROR` may share a line
with one following directive (`// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2`). A
`--testing` test also passes on exit 0, and a test with `CCCC_EXPECT_STDOUT`
on exit 0 or 42. A `--testing` or `--build` test with `CCCC_EXPECT_STDERR`
may exit non-zero.[^regex]

Two rules keep a `--testing` file from passing on the wrong line:

- An `ok` pattern is written `^ok 2 - name`. An unanchored `ok 2 - name` also
  matches inside `not ok 2 - name`, so `testcheck` rejects it.
- Every failure line (`not ok`, `✗`, a JSON `"status":"fail"` or
  `"timeout"`) must match some `CCCC_EXPECT_STDOUT` / `CCCC_EXPECT_STDERR`
  pattern. A file that expects `not ok 1 - fails` needs a line saying so:

```c
// CCCC_EXPECT_STDERR: ^not ok 1 - fails
// CCCC_EXPECT_STDERR: ^ok 2 - after
```

[^regex]: Patterns are POSIX extended regular expressions. `\s \S \d \w \n \t`
    work inside and outside brackets, `(?:...)` is accepted, and `[\s\S]`
    means any character. Stdout patterns match with `.` crossing newlines and
    `^`/`$` at line boundaries; stderr patterns use `REG_NEWLINE`. Lookahead,
    `\b` and `(?s)` are rejected, so write one directive line per condition.
    A directive sits on one physical line (`.clang-format` keeps it from
    wrapping), and the header ends at the first code line. `tools/testcheck.c`
    implements all of this; `build/testcheck --lint <files>` checks that every
    pattern compiles.
