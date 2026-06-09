# CCCC Testing Framework

CCCC includes a built-in test framework for writing tests directly in C, using the `[[cccc::test]]` attribute to mark test functions and `CCCC_ASSERT*` macros for assertions.

## Writing Tests

Mark a function as a test with `[[cccc::test]]`. Test functions must take no arguments and have a `void` return type:

```c
[[cccc::test]]
void test_addition(void) {
    CCCC_ASSERT_EQ(1 + 1, 2);
}
```

Multiple test functions can coexist in the same file:

```c
[[cccc::test]]
void test_strings(void) {
    CCCC_ASSERT_STREQ("hello", "hello");
}

[[cccc::test]]
void test_pointers(void) {
    int x = 42;
    CCCC_ASSERT_NOT_NULL(&x);
}
```

No `#include` is required — the assertion macros and their backing declarations are injected automatically when running with `--testing`.

## Test Suites

Tests can be grouped into named suites. Two syntaxes are supported.

### Attribute argument

Specify the suite directly on the test attribute:

```c
[[cccc::test(suite = "math")]]
void test_add(void) {
    CCCC_ASSERT_EQ(1 + 1, 2);
}

[[cccc::test(suite = "math")]]
void test_mul(void) {
    CCCC_ASSERT_EQ(6 * 7, 42);
}
```

### Pragma block

Wrap a run of test functions in a pragma block to assign them all to the same suite:

```c
#pragma cccc suite begin "strings"

[[cccc::test]]
void test_equality(void) {
    CCCC_ASSERT_STREQ("foo", "foo");
}

[[cccc::test]]
void test_empty(void) {
    CCCC_ASSERT_STREQ("", "");
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

Standard glob wildcards (`*`, `?`, `[...]`) are supported.

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

| Macro | Description |
|-------|-------------|
| `CCCC_ASSERT(cond)` | Fails if `cond` is false |
| `CCCC_ASSERT_EQ(a, b)` | Fails if `a != b` (integer comparison) |
| `CCCC_ASSERT_NEQ(a, b)` | Fails if `a == b` (integer comparison) |
| `CCCC_ASSERT_NULL(p)` | Fails if `p` is not null |
| `CCCC_ASSERT_NOT_NULL(p)` | Fails if `p` is null |
| `CCCC_ASSERT_STREQ(a, b)` | Fails if strings `a` and `b` differ |

When an assertion fails, the test is marked `not ok` and a diagnostic block is printed with the condition and source location. The remaining tests continue to run.

## Limitations

- **Global state is not reset between tests.** Global variables keep their values from previous tests. Tests must be self-contained and must not rely on initial global state being clean.
- Test functions must have signature `void name(void)` — no parameters, void return.
- Calling `exit()` directly in a test terminates the entire process rather than failing just that test. Use `CCCC_ASSERT` macros instead.
- `--testing` cannot be combined with `-c`, `-o`, or other output flags.
- Suite blocks (`#pragma cccc suite begin/end`) cannot be nested.
- **Negative test bodies are matched against error substrings.** Use a substring that is specific enough to avoid false matches but not so specific that it breaks with minor message wording changes.
- `--test-timeout` uses `SIGALRM`; test code that also uses `alarm()` or installs a `SIGALRM` handler will interfere with the timeout mechanism.
