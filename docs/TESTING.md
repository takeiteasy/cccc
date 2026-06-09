# JCC Testing Framework

JCC includes a built-in test framework for writing tests directly in C, using the `[[jcc::test]]` attribute to mark test functions and `JCC_ASSERT*` macros for assertions.

## Writing Tests

Mark a function as a test with `[[jcc::test]]`. Test functions must take no arguments and have a `void` return type:

```c
[[jcc::test]]
void test_addition(void) {
    JCC_ASSERT_EQ(1 + 1, 2);
}
```

Multiple test functions can coexist in the same file:

```c
[[jcc::test]]
void test_strings(void) {
    JCC_ASSERT_STREQ("hello", "hello");
}

[[jcc::test]]
void test_pointers(void) {
    int x = 42;
    JCC_ASSERT_NOT_NULL(&x);
}
```

No `#include` is required — the assertion macros and their backing declarations are injected automatically when running with `--testing`.

## Running Tests

```
./jcc --testing myfile.c
```

Output is in [TAP version 13](https://testanything.org/) format:

```
TAP version 13
1..3
ok 1 - test_addition
ok 2 - test_strings
not ok 3 - test_pointers
  ---
  message: &x is null (myfile.c:15)
  ...
```

The process exits with code `0` if all tests pass, `1` if any fail.

## Assertion Macros

| Macro | Description |
|-------|-------------|
| `JCC_ASSERT(cond)` | Fails if `cond` is false |
| `JCC_ASSERT_EQ(a, b)` | Fails if `a != b` (integer comparison) |
| `JCC_ASSERT_NEQ(a, b)` | Fails if `a == b` (integer comparison) |
| `JCC_ASSERT_NULL(p)` | Fails if `p` is not null |
| `JCC_ASSERT_NOT_NULL(p)` | Fails if `p` is null |
| `JCC_ASSERT_STREQ(a, b)` | Fails if strings `a` and `b` differ |

When an assertion fails, the test is marked `not ok` and a diagnostic block is printed with the condition and source location. The remaining tests continue to run.

## Limitations

- **Global state is not reset between tests.** Global variables keep their values from previous tests. Tests must be self-contained and must not rely on initial global state being clean.
- Test functions must have signature `void name(void)` — no parameters, void return.
- Calling `exit()` directly in a test terminates the entire process rather than failing just that test. Use `JCC_ASSERT` macros instead.
- `--testing` cannot be combined with `-c`, `-o`, or other output flags.
