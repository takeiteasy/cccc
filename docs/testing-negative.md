# Negative and Exit-Code Tests

Tests that expect a compile error, an exit code or a runtime violation.

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

### Asserting any compile error

Use `expect_compile_error = true` when you want to assert that the function body fails to compile but do not need to match a specific error message:

```c
[[cccc::test(expect_compile_error = true)]]
void test_bad_bitfield(void) {
    struct S { int x : -1; };
}
```

The test passes if at least one compile error is produced. It fails if the code compiles without error.

This is more explicit than `error = ""` (empty string, which matches any message). When both `expect_compile_error = true` and `error = "pattern"` are set, `expect_compile_error` is redundant and a `-Wattributes` warning is emitted.

**Limitation:** only parse-time errors are catchable this way. Errors that fire during code generation — such as comptime macro expansion errors, unresolved external symbols, and global-initialiser type checks — occur after the function body is parsed and cannot be asserted with this mechanism. Use a legacy `// EXPECT_COMPILE_ERROR` test file for those cases.

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


## Exit Code Tests

Mark a test with `exit_code = N` to assert that the function produces a specific process exit code when run. This covers both explicit `exit()` calls and crash-induced signal deaths:

```c
// Test passes when the function exits with code 0 (normal return)
[[cccc::test(exit_code = 0)]]
int test_clean_exit(void) {
    return 0;
}

// Test passes when the function calls exit(42)
[[cccc::test(exit_code = 42)]]
void test_explicit_exit(void) {
    exit(42);
}

// Test passes when the function segfaults (SIGSEGV = signal 11 → exit code 139)
[[cccc::test(exit_code = 139)]]
int test_segfault(void) {
    volatile int *p = (volatile int *)0;
    return *p;
}
```

Exit codes follow the shell convention: a normal exit via `return` or `exit(N)` produces exit code `N` (0–127); a process killed by signal `S` produces exit code `128 + S`. Common signal exit codes:

| Signal | Number | Exit code |
|--------|--------|-----------|
| SIGSEGV | 11 | 139 |
| SIGABRT | 6 | 134 |
| SIGBUS | 10 | 138 |
| SIGFPE | 8 | 136 |
| SIGILL | 4 | 132 |

`exit_code` may be combined with `suite`, `name`, and `timeout`. It is **mutually exclusive** with `error =` (compile-time) and `return =` (can't read a crashed child's return register); combining them emits a `-Wattributes` warning and the conflicting argument is ignored.

When a test does not produce the expected exit code, the failure message shows both the expected and actual values:

```
expected exit_code 139, got 0
```

### Timeout interaction

`exit_code` tests respect the per-test `timeout =` and global `--test-timeout` settings. A test that hangs beyond its timeout is killed with `SIGKILL` and reported as `TIMEOUT`.

### Limitations

- `exit_code` tests are run in a forked subprocess. Setup hooks run in the parent (before the fork) so their state is visible to the child; teardown hooks run in the parent (after the child exits).
- A failed `Assert*` in a setup hook, a teardown hook or the test body fails the test and reports the assert's message, even when the exit code matches. Setup failures skip the fork.
- Global state changes made inside an `exit_code` test are not visible to subsequent tests (the child process exits and the parent restores its own snapshot as usual).
- `exit_code` tests are skipped on non-POSIX platforms where `fork` is unavailable.

## Runtime Safety Violations

A runtime safety violation inside an ordinary (non-`exit_code`) `[[cccc::test]]`
body — an uninitialized read, a bounds error, use-after-free, and the other
checks documented in [safety.md](safety.md) — aborts the test function
mid-body and reports `not ok`, with a message naming the opcode that
aborted:

```
not ok 1 - test_reads_uninitialized_local
  ---
  message: runtime safety violation (CHKI at pc 9) -- see the diagnostic above
  ...
```

The violation's own diagnostic banner (e.g. `UNINITIALIZED VARIABLE READ`)
is printed directly above this message, since it comes from the same
mechanism used outside `--testing` mode. Code after the trap point —
including any `Assert*` call or `printf` meant to prove non-completion —
never runs, exactly as it wouldn't outside `--testing`. A fault inside a
setup or teardown hook is attributed the same way. Use an `exit_code =`
test (above) instead if the point of the test is to assert on the specific
exit code a crash produces.
