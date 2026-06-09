# CCCC Testing Framework

CCCC includes a built-in test framework for writing tests directly in C, using the `[[cccc::test]]` attribute to mark test functions and `$assert*` macros for assertions.

## Writing Tests

Mark a function as a test with `[[cccc::test]]`. Test functions must take no arguments and have a `void` return type:

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

`name`, `suite`, and `error` may all be combined in one attribute.

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

Output is in [TAP version 13](https://testanything.org/) format. Suite changes are emitted as TAP comments:

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

The process exits with code `0` if all tests pass, `1` if any fail.

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

`--test`, `--test-suite`, and `--list-tests` all imply `--testing`, so the flag can be omitted when using them.

### Stop after first failure

```
./cccc --testing --fail-fast myfile.c
```

Stops after the first failing test. Passes and failures already emitted remain in the TAP output; subsequent tests are not run.

### Per-test timeout

```
./cccc --testing --test-timeout=5 myfile.c
```

Kills any test that runs longer than 5 seconds. Timed-out tests are reported as:

```
not ok N - test_name # TIMEOUT
```

Remaining tests continue to run. Uses `SIGALRM` internally; test code that also installs `SIGALRM` handlers may interfere.

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

- Test functions must have signature `void name(void)` — no parameters, void return.
- Setup and teardown hook functions must also have signature `void name(void)`.
- Teardown hooks are skipped on test timeout (VM state is unknown after `SIGALRM`). They run in all other cases, including after test or setup failure.
- Calling `exit()` directly in a test terminates the entire process rather than failing just that test. Use `$assert*` macros instead.
- `--testing` cannot be combined with `-c`, `-o`, or other output flags.
- Suite blocks (`#pragma cccc suite begin/end`) cannot be nested.
- **Negative test bodies are matched against error substrings.** Use a substring that is specific enough to avoid false matches but not so specific that it breaks with minor message wording changes.
- `--test-timeout` uses `SIGALRM`; test code that also uses `alarm()` or installs a `SIGALRM` handler will interfere with the timeout mechanism.
