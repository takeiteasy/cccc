# CCCC Testing Framework

CCCC includes a built-in test framework for writing tests directly in C, using the `[[cccc::test]]` attribute to mark test functions and `$assert*` macros for assertions.

## Writing Tests

Mark a function as a test with `[[cccc::test]]`. Test functions must take no arguments; they may return `void`, `int`, `double`, `float`, or `char *`:

```c
[[cccc::test]]
void test_addition(void) {
    $assert_eq(1 + 1, 2);
}
```

Multiple test functions can coexist in the same file:

```c
[[cccc::test]]
void test_strings(void) {
    $assert_streq("hello", "hello");
}

[[cccc::test]]
void test_pointers(void) {
    int x = 42;
    $assert_not_null(&x);
}
```

No `#include` is required — the assertion macros and their backing declarations are injected automatically when running with `--testing`.

### Custom display names

Give a test a human-readable name with the `name` option. The display name appears in TAP output and is used for glob filtering; the C function name is unchanged:

```c
[[cccc::test(name = "addition is commutative")]]
void test_add_commutative(void) {
    $assert_eq(1 + 2, 2 + 1);
}
```

`name`, `suite`, `error`, `timeout`, `error_count`, `return`, and `return_epsilon` may all be combined in one attribute.

## Test Suites

Tests can be grouped into named suites. Two syntaxes are supported.

### Attribute argument

Specify the suite directly on the test attribute:

```c
[[cccc::test(suite = "math")]]
void test_add(void) {
    $assert_eq(1 + 1, 2);
}

[[cccc::test(suite = "math")]]
void test_mul(void) {
    $assert_eq(6 * 7, 42);
}
```

### Pragma block

Wrap a run of test functions in a pragma block to assign them all to the same suite:

```c
#pragma cccc suite begin "strings"

[[cccc::test]]
void test_equality(void) {
    $assert_streq("foo", "foo");
}

[[cccc::test]]
void test_empty(void) {
    $assert_streq("", "");
}

#pragma cccc suite end
```

Suite blocks cannot be nested. Tests outside any block or attribute have no suite.

## Negative Tests

Mark a test with `error = "pattern"` to assert that the function body fails to compile with a diagnostic matching the given substring:

```c
[[cccc::test(error = "undefined variable")]]
void test_undefined_var(void) {
    int x = not_declared;
}
```

The test passes if compilation of the body produces at least one error whose message contains `pattern`. It fails if the code compiles without error, or if no error matches the pattern.

Both `suite` and `error` may be combined:

```c
[[cccc::test(suite = "negative", error = "undefined variable")]]
void test_combined(void) {
    int x = also_not_declared;
}
```

Negative test bodies are compiled in error-collection mode; their errors are absorbed and never propagate to the rest of the compilation. The test result is computed at compile time.

### Expected error count

Add `error_count` with a comparison operator to assert something about the number of compilation errors produced. The supported operators are `=` (or `==`), `!=`, `<`, `<=`, `>`, and `>=`. When no operator is written, `=` is assumed:

```c
[[cccc::test(error = "undefined variable", error_count = 1)]]
void test_exactly_one_error(void) {
    int x = not_declared;
}

[[cccc::test(error = "undefined", error_count > 1)]]
void test_more_than_one_error(void) {
    int a = x + y;  // two undeclared variables
}

[[cccc::test(error = "undefined", error_count != 1)]]
void test_not_exactly_one(void) {
    int a = x + y;  // two errors, so != 1 passes
}
```

The test passes only if: (1) at least one error matches the `error` pattern, and
(2) the `error_count` assertion holds.  A count mismatch is reported as a failed
negative test with a descriptive message.

### Negated pattern matching

Use `error != "pattern"` to assert that no compilation error contains the given substring. The function body must still produce at least one error (it is still a negative test):

```c
[[cccc::test(error != "cannot convert")]]
void test_no_type_error(void) {
    int x = undefined_var;  // undeclared error, but no type conversion error
}
```

The test passes if at least one error is produced and none of them contain the pattern. It fails if any error matches the pattern.

### Return value assertion

Use `return = value` to assert that a test function returns a specific value. The comparison type is inferred from the literal:

**Integer** (and `char`/`enum`): the function's return value is compared as a 64-bit integer. `char` and `enum` values must be written as their numeric equivalent — enum *names* (`return = GREEN`) are not resolved and will silently skip the assertion.

```c
[[cccc::test(return = 42)]]
int test_the_answer(void) {
    return 6 * 7;
}

[[cccc::test(return = -1)]]
int test_neg(void) {
    return -1;
}

[[cccc::test(return = 65)]]
int test_char_a(void) {
    return (int)'A';  // 'A' == 65; use the integer value, not the char literal
}

// enum: write the integer value, not the enum name
// [[cccc::test(return = 1)]]  ← correct
// [[cccc::test(return = GREEN)]]  ← warns and skips assertion (enum names not supported)
```

Passing an unrecognized operand (such as an enum name) emits a `-Wattributes` warning and skips the assertion rather than silently passing:

```
warning: unrecognized return= operand 'GREEN'; assertion skipped (enum names not supported, use the integer value)
```

**Float/double**: the return value is compared within an absolute tolerance of `1e-9` for `=`/`!=`. For ordered comparisons (`<`, `>`, etc.) no tolerance is applied.

```c
[[cccc::test(return = 3.14)]]
double test_pi(void) {
    return 3.14;
}
```

Use `return_epsilon` to override the tolerance for a specific test:

```c
[[cccc::test(return = 3.14159, return_epsilon = 1e-5)]]
double test_approx_pi(void) {
    return compute_pi();  // passes if within 1e-5 of 3.14159
}
```

`return_epsilon` only applies to `=` and `!=`; ordered comparisons use exact values regardless.

**String** (`char *`): the returned pointer is compared to the literal using `strcmp`.

```c
[[cccc::test(return = "ok")]]
const char *test_status(void) {
    return "ok";
}
```

**Comparison operators** (`=`, `!=`, `<`, `<=`, `>`, `>=`): use a comparison operator instead of equality. The default (no operator) is `=`.

```c
[[cccc::test(return > 0)]]
int test_positive(void) {
    return 42;
}

[[cccc::test(return != 0)]]
int test_nonzero(void) {
    return -1;
}
```

If the assertion fails, the test is reported as failed with a message showing the expected and actual values:

```
expected return value = 42, got 7
expected return string = "ok", got "err"
```

`return` may be combined with other options:

```c
[[cccc::test(return = 42, name = "answer is correct", suite = "math")]]
int test_the_answer(void) {
    return 6 * 7;
}
```

`$assert*` macros and the `return` assertion are independent — both must pass for the test to pass.

## Setup and Teardown

The framework supports lifecycle hooks that run before and/or after tests. Use `[[cccc::test_setup]]` and `[[cccc::test_teardown]]` to mark hook functions. Hook functions must have signature `void name(void)`.

### Global hooks (run around every test)

A hook with no arguments runs before (or after) every test in the file:

```c
[[cccc::test_setup]]
void global_setup(void) {
    // runs before each test
}

[[cccc::test_teardown]]
void global_teardown(void) {
    // runs after each test
}
```

### Name-pattern hooks

Use `name = "glob"` to run a hook only around tests whose display name matches the glob pattern:

```c
[[cccc::test_setup(name = "db_*")]]
void db_setup(void) {
    // runs before tests whose display name starts with "db_"
}
```

Standard glob wildcards (`*`, `?`, `[...]`) are supported. The pattern is matched against the display name (set via `name = "..."` on `[[cccc::test]]`) or the C function name if no display name is set.

### Suite per-test hooks

Use `suite = "name"` to run a hook before (or after) every test in the named suite:

```c
[[cccc::test_setup(suite = "network")]]
void network_setup(void) {
    // runs before each test in the "network" suite
}
```

### Suite once-hooks

Add `once` to run a hook exactly once at the start (or end) of a suite, rather than around each individual test. This is useful for expensive shared fixtures:

```c
[[cccc::test_setup(suite = "db", once)]]
void open_db(void) {
    // runs once before the first test in the "db" suite
}

[[cccc::test_teardown(suite = "db", once)]]
void close_db(void) {
    // runs once after the last test in the "db" suite
}
```

The global state modified by a `once` setup persists across all tests in the suite — each test restores the post-once-setup snapshot rather than the initial compiled state.

### Name-pattern once-hooks

The `once` keyword can also be combined with `name = "glob"` to run a hook
exactly once before the first test matching the glob, or after all tests
complete (for teardown):

```c
static int g_initialised = 0;

[[cccc::test_setup(name = "needs_init_*", once)]]
void lazy_init(void) {
    g_initialised = 1;
}

[[cccc::test(name = "needs_init_a")]]
void test_a(void) {
    $assert_eq(g_initialised, 1); // lazy_init fired before this test
}

[[cccc::test(name = "needs_init_b")]]
void test_b(void) {
    // g_initialised is still 1 — the data snapshot was taken after
    // lazy_init, so its state is visible to all subsequent tests.
    $assert_eq(g_initialised, 1);
}
```

The data segment is snapshotted after the once-setup runs, so its state is
visible to all subsequent tests — not just those matching the glob.  This
behaviour mirrors suite-level once-hooks.

### Execution order

For each positive test, hooks run in this order:

1. Per-test setup hooks (global, suite, and name-pattern that match, in declaration order)
2. The test itself
3. Per-test teardown hooks (global, suite, and name-pattern that match, in declaration order)

Suite `once` hooks run at suite boundaries:

- `once` setup: before the first test in the suite (before per-test hooks for that test)
- `once` teardown: after the last test in the suite (after per-test hooks for that test)

If a setup hook fails (via `$assert`), the test is skipped and the test is marked as failed. Teardown hooks still run after the failed setup.

If the test itself fails, teardown hooks still run. Teardown is only skipped on timeout, because the VM state is unknown after `SIGALRM`.

## Global State Reset

Global variables are automatically reset to their initial (compile-time) values before each positive test runs. This means tests can safely modify global state without affecting each other:

```c
static int g_count = 0;

[[cccc::test]]
void test_a(void) {
    g_count = 99;   // modifies g_count
    $assert_eq(g_count, 99);
}

[[cccc::test]]
void test_b(void) {
    // g_count is reset to 0 before this test; test_a's modification is gone
    $assert_eq(g_count, 0);
}
```

The reset happens after per-test setup hooks are scheduled but before they run — each test (including its setup hooks) sees the initial snapshot.

For suites with `once` setup hooks, each test in the suite restores the post-once-setup snapshot rather than the original initial snapshot, so the shared state established by `once` setup persists across the suite's tests.

## Running Tests

```
./cccc --testing myfile.c
```

Select the output format with `--test-format`:

| Format   | Flag                        | Use case              |
|----------|-----------------------------|-----------------------|
| TAP      | `--test-format=tap` (default) | CI systems, `prove` |
| Plain    | `--test-format=plain`       | Human-readable terminal |
| JSON     | `--test-format=json`        | Machine-readable, CI integration |

### TAP format

[TAP version 13](https://testanything.org/) output. Suite changes are emitted as comments:

```
TAP version 13
1..5
ok 1 - test_assert_true
# Suite: math
ok 2 - test_add
ok 3 - test_mul
# Suite: strings
ok 4 - test_equality
not ok 5 - test_empty
  ---
  message: "foo" != "" ("foo" != "") (myfile.c:30)
  ...
```

### Plain format

Human-readable output that mimics the Python test runner's style, with check/cross prefixes and a summary block:

```
Running 5 tests...
  ✓ test_assert_true
── math ──
  ✓ test_add
  ✓ test_mul
── strings ──
  ✓ test_equality
  ✗ test_empty ("foo" != "" ("foo" != "") (myfile.c:30))

=======================
Test Results Summary
=======================
Total:          5
Passed:         4
Failed:         1
```

### JSON format

Line-delimited JSON objects, one per test, wrapped in an array:

```json
[
  {"name":"test_assert_true","status":"pass"},
  {"name":"test_add","suite":"math","status":"pass"},
  {"name":"test_mul","suite":"math","status":"pass"},
  {"name":"test_equality","suite":"strings","status":"pass"},
  {"name":"test_empty","suite":"strings","status":"fail","message":"\"foo\" != \"\" (\"foo\" != \"\") (myfile.c:30)"}
]
```

The process exits with code `0` if all tests pass, `1` if any fail.

### Combining with `-c` (compile pre-pass)

`--testing` can be combined with `-c=bytecode` or `-c=native` to run tests as a pre-pass before compilation. If all tests pass the compile step proceeds; if any test fails the compile step is skipped and the process exits non-zero.

```
./cccc --testing -c=bytecode -o out.jbc myfile.c
```

This is useful in build scripts that want to guard bytecode or native compilation behind a passing test run.

## Native Execution

By default, `--testing` runs every test inside the CCCC VM. Tests can instead
run as **compiled native code** for speed, either for the whole file or on a
per-test basis.

### Run everything natively: `--testing -c=native`

```
./cccc --testing -c=native myfile.c
```

All non-negative (`error = "..."` is unaffected) tests in the file are
serialized to a single C source file, compiled with the system C compiler,
and run as a native binary. Each test runs in its own forked child process,
so global/static state mutated by one test does not leak into the next
(mirroring the VM's data-segment snapshot/reset behavior). Native test names
are suffixed `[native]` in the output:

```
TAP version 13
1..2
ok 1 - test_add [native]
ok 2 - test_mul [native]
```

### Per-test native mode: `mode = "native"`

```c
[[cccc::test(mode = "native")]]
void test_hot_path(void) {
    $assert_eq(compute(40, 2), 42);
}
```

Only tests with `mode = "native"` run natively; all other tests in the file
continue to run in the VM. TAP/PLAIN/JSON numbering is sequential across both
passes — VM tests are numbered first, followed by native tests, in a single
combined plan (`1..N`).

`mode = "native"` cannot be combined with `error = "..."` — negative tests
are evaluated at compile time, not by running native code, so this
combination is rejected with a parse-time error.

### How it works

- All `$assert*` macros work identically in native mode — they call into a
  small embedded native runtime (real C signatures, not VM FFI) that mirrors
  `include/cccc/tests.h`.
- `return = value` / `return_epsilon = ...` assertions are checked inline in
  the generated native harness.
- Each test forks; the child reports pass/fail/timeout back to the parent
  over a pipe. A crashing or signaled child is reported as a failure.
- `timeout = <ms>` (per-test) and `--test-timeout=<seconds>` (global) both
  work in native mode via `SIGALRM`/`setitimer`, same as in the VM.

### Setup/teardown hooks in native mode

All hook types described in [Setup and Teardown](#setup-and-teardown) — global,
name-pattern, suite per-test, suite `once`, and name-pattern `once` — also run
under `-c=native`. Per-test setup/teardown hooks run inside each test's forked
child process, in declaration order, following the same
[execution order](#execution-order) as the VM backend. `once` hooks run in the
parent process before the relevant test's `fork()`, so any global state they
set up is inherited by that test and all subsequent tests via copy-on-write —
this mirrors the VM's snapshot-persistence behavior for `once` hooks.

Two behaviors differ from the VM backend:

- **Cross-suite persistence**: the VM resets global state to a per-suite
  snapshot at each suite boundary, so a suite's `once`-setup effects don't
  leak into later suites. Native mode has no equivalent reset — global state
  mutated by a `once` hook (or by any test, via the parent/COW model) persists
  for the remainder of the run. Avoid relying on suite isolation for global
  state shared via `once` hooks when using `-c=native`.
- **Mixed VM+native runs**: when `-c=native` is *not* used but some tests use
  `mode = "native"`, a suite whose tests are split across the VM pass and the
  native pass could have its suite-`once` hooks fire once per pass (twice
  total). Using `--testing -c=native` (which forces every test native) avoids
  this, since only the native pass runs.

## Filtering Tests

Run a subset of tests without modifying the source file.

### By name (glob pattern)

```
./cccc --testing --test='test_assert_*' myfile.c
```

Standard glob wildcards (`*`, `?`, `[...]`) are supported. The pattern matches against the display name (or C function name if no display name is set).

### By suite

```
./cccc --testing --test-suite=math myfile.c
```

Only tests belonging to the named suite are run.

### List without running

```
./cccc --testing --list-tests myfile.c
```

Prints all test names (and their suites) without executing them:

```
# Tests (5 total):
test_assert_true
test_add                                 [suite: math]
test_mul                                 [suite: math]
test_equality                            [suite: strings]
test_empty                               [suite: strings]
```

`--test`, `--test-suite`, `--list-tests`, and `--test-format` all imply `--testing`, so the flag can be omitted when using them.

### Stop after first failure

```
./cccc --testing --fail-fast myfile.c
```

Stops after the first failing test. Passes and failures already emitted remain in the TAP output; subsequent tests are not run.

### Per-test timeout

Set a global timeout for all tests:

```
./cccc --testing --test-timeout=5 myfile.c
```

Kills any test that runs longer than 5 seconds. Timed-out tests are reported in the selected format (e.g. `not ok N # TIMEOUT` in TAP, `✗ name (TIMEOUT)` in plain, `"status":"timeout"` in JSON). Remaining tests continue to run. Uses `SIGALRM` internally; test code that also installs `SIGALRM` handlers may interfere.

### Per-function timeout

Override the global timeout for a specific test with `timeout = <ms>`:

```c
[[cccc::test(timeout = 200)]]
void test_fast_operation(void) {
    // fails if this runs longer than 200ms
}
```

The timeout value is in milliseconds. When set, it takes precedence over the
global `--test-timeout` for that test.  Uses `setitimer(ITIMER_REAL)` for
sub-second precision.

## Assertion Macros

All assertion macros use the `$` prefix and are injected automatically in `--testing` mode.

### Basic Validity

| Macro | Description |
|-------|-------------|
| `$assert(cond)` | Fails if `cond` is false |
| `$assert_true(cond)` | Alias for `$assert(cond)` |
| `$assert_false(cond)` | Fails if `cond` is true |
| `$assert_fail()` | Always fails |
| `$assert_fail_msg(msg)` | Always fails with a custom message |

### Integer Comparisons

| Macro | Description |
|-------|-------------|
| `$assert_eq(a, b)` | Fails if `a != b` |
| `$assert_neq(a, b)` | Fails if `a == b` |
| `$assert_gt(a, b)` | Fails if `a <= b` |
| `$assert_lt(a, b)` | Fails if `a >= b` |
| `$assert_ge(a, b)` | Fails if `a < b` |
| `$assert_le(a, b)` | Fails if `a > b` |
| `$assert_within(d, e, a)` | Fails if `\|e - a\| > d` |

### Bitwise

| Macro | Description |
|-------|-------------|
| `$assert_bits(m, e, a)` | Fails if `(a & m) != (e & m)` |
| `$assert_bit_high(b, a)` | Fails if bit `b` of `a` is low |
| `$assert_bit_low(b, a)` | Fails if bit `b` of `a` is high |

### Floating Point

| Macro | Description |
|-------|-------------|
| `$assert_float_within(d, e, a)` | Fails if `\|e - a\| > d` (float) |
| `$assert_double_within(d, e, a)` | Fails if `\|e - a\| > d` (double) |
| `$assert_float_eq(e, a)` | Fails if `\|e - a\| > 1e-6` |
| `$assert_double_eq(e, a)` | Fails if `\|e - a\| > 1e-15` |

### Pointers

| Macro | Description |
|-------|-------------|
| `$assert_null(p)` | Fails if `p` is not null |
| `$assert_not_null(p)` | Fails if `p` is null |

### Strings

| Macro | Description |
|-------|-------------|
| `$assert_streq(a, b)` | Fails if strings `a` and `b` differ |
| `$assert_streq_len(a, b, len)` | Fails if first `len` chars differ |

### Memory

| Macro | Description |
|-------|-------------|
| `$assert_mem_eq(e, a, len)` | Fails if `memcmp(e, a, len) != 0` |

### Arrays

| Macro | Description |
|-------|-------------|
| `$assert_eq_array(e, a, cnt)` | Fails if `e[0..cnt-1] != a[0..cnt-1]` (memcmp) |
| `$assert_each_eq(e, a, cnt)` | Fails if any `a[i] != e` (element-wise) |

### Message-appending variants

Append `_msg` to any assertion to add a custom message string to the failure diagnostics:

| Macro | Description |
|-------|-------------|
| `$assert_msg(cond, msg)` | `$assert` with custom message |
| `$assert_true_msg(cond, msg)` | `$assert_true` with custom message |
| `$assert_false_msg(cond, msg)` | `$assert_false` with custom message |
| `$assert_eq_msg(a, b, msg)` | `$assert_eq` with custom message |
| `$assert_streq_msg(a, b, msg)` | `$assert_streq` with custom message |
| `$assert_null_msg(p, msg)` | `$assert_null` with custom message |
| `$assert_not_null_msg(p, msg)` | `$assert_not_null` with custom message |
| `$assert_bits_msg(m, e, a, msg)` | `$assert_bits` with custom message |

When an assertion fails, the test is marked `not ok` and a diagnostic block is printed with the condition and source location. The remaining tests continue to run.

## Limitations

- Test functions must take no arguments and return `void`, `int`, `double`, `float`, or `char *`. Use `return = value` to assert on the return value.
- `return =` assertions support integer literals, float literals, and string literals. Enum names (`return = GREEN`) and character literals (`return = 'A'`) are not resolved — use the integer value instead (`return = 1`, `return = 65`). Unrecognized operands produce a `-Wattributes` warning and skip the assertion.
- Setup and teardown hook functions must also have signature `void name(void)`.
- Teardown hooks are skipped on test timeout (VM state is unknown after `SIGALRM`). They run in all other cases, including after test or setup failure.
- Calling `exit()` directly in a test terminates the entire process rather than failing just that test. Use `$assert*` macros instead.
- Suite blocks (`#pragma cccc suite begin/end`) cannot be nested.
- **Negative test bodies are matched against error substrings.** Use a substring that is specific enough to avoid false matches but not so specific that it breaks with minor message wording changes.
- `--test-timeout` uses `SIGALRM`; test code that also uses `alarm()` or installs a `SIGALRM` handler will interfere with the timeout mechanism.

### Native execution (`-c=native` / `mode = "native"`)

- Struct/union global initializers serialize as `/* init data */` and will
  fail native compilation — use scalar globals (int, float, `char *`) only
  in files with native-mode tests.
- In mixed mode (some tests `mode = "native"`, others VM), PLAIN and JSON
  output produce two separate summary blocks (one per pass). TAP output is a
  single sequential stream and is unaffected.
- `--list-tests` does not currently merge native and VM test listings into a
  single combined view.
- `[[cccc::test_setup]]` / `[[cccc::test_teardown]]` hooks (including `once`
  and name-pattern hooks) are not run for tests executed natively — they are
  silently skipped (tracked as
  [#362](https://todo.sr.ht/~takeiteasy/cccc/362)). Avoid relying on
  setup/teardown hooks for `mode = "native"` tests, or for any test under
  `--testing -c=native`, until this is implemented.
