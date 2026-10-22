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

**Struct / union**: use an inline compound literal to assert a struct or union return value. Fields are compared field-by-field using the same per-type rules as the scalar paths (epsilon for `float`/`double`, `strcmp` for `char *`, integer equality for all other types). Fields omitted from the compound literal are expected to be zero (C zero-initialisation), with one exception — see **Unions**, below.

```c
struct Point { int x; int y; };

[[cccc::test(return = (struct Point){.x = 1, .y = 2})]]
struct Point test_make_point(void) {
    return (struct Point){.x = 1, .y = 2};
}
```

Mixed field types are supported:

```c
struct Named { char *label; int code; };

[[cccc::test(return = (struct Named){.label = "ok", .code = 0})]]
struct Named test_result(void) {
    return (struct Named){.label = "ok", .code = 0};
}
```

Struct assertions support `=` and `!=` operators only; ordered comparisons (`<`, `<=`, `>`, `>=`) are not meaningful for structs and produce a `-Wattributes` warning with the assertion skipped.

If the assertion fails:

```
expected return value = (struct Point){.x = 1, .y = 2}, got {.x = 99, .y = 2}
```

**Nested fields**: a field that is itself a struct, union, or array can be asserted with a nested compound literal. The nested literal may repeat the `(struct|union TAG)` prefix or, more commonly, be a bare brace list — its type is inferred from the field being initialized. Nesting recurses to a maximum of 8 levels; a literal nested deeper than that produces a `-Wattributes` warning and the whole assertion is skipped.

```c
struct Inner { int a; int b; };
struct Outer { struct Inner p; int z; };

[[cccc::test(return = (struct Outer){.p = {.a = 1, .b = 2}, .z = 3})]]
struct Outer make_outer(void) {
    return (struct Outer){.p = {.a = 1, .b = 2}, .z = 3};
}
```

A field omitted from a nested literal is expected to be zero, recursively — an entirely omitted nested field means all of its own fields are expected to be zero.

**Anonymous struct/union members**: an anonymous member's fields are addressed directly by the outer struct's own designators, matching C's own anonymous-member lookup rules — there is no extra level of nesting in the literal.

```c
struct WithAnon {
    struct { int x; int y; }; // anonymous
    int z;
};

[[cccc::test(return = (struct WithAnon){.x = 1, .y = 2, .z = 3})]]
struct WithAnon make(void) {
    return (struct WithAnon){.x = 1, .y = 2, .z = 3};
}
```

**Arrays**: array-typed fields are asserted with a brace list of positional (undesignated) elements, matched left-to-right; elements past the end of the list are expected to be zero. A `char[]` field may instead be compared against a string literal, following C zero-initialisation semantics (bytes past the literal's terminating `NUL` are expected to be zero).

```c
struct Buf { int values[5]; char label[8]; };

[[cccc::test(return = (struct Buf){.values = {1, 2, 3}, .label = "hi"})]]
struct Buf make_buf(void) {
    struct Buf r = {0};
    r.values[0] = 1; r.values[1] = 2; r.values[2] = 3;
    r.label[0] = 'h'; r.label[1] = 'i';
    return r;
}
```

An array of structs takes a positional list of (optionally `(struct TAG)`-prefixed) nested literals: `.items = {(struct Pair){.a=1,.b=2}, (struct Pair){.a=3,.b=4}}`.

**Unions**: union arms alias the same storage, so — unlike a struct — an arm *not* named in the literal is not asserted on at all (it is not "expected zero", since reading it back would just reinterpret whichever arm actually was asserted). Only the arms named in the literal are compared.

```c
union Val { int i; float f; };

[[cccc::test(return = (union Val){.i = 0x3f800000})]]
union Val make_val(void) {
    return (union Val){.i = 0x3f800000}; // .f is not checked
}
```

**Scope limitations** — the following are not supported and are deferred to follow-up tickets:

- *Bitfields* — a bitfield member is compared as its whole storage unit, not its individual bit-width.
- *Non-`char *` pointer fields* — pointer fields other than `char *` are read as integers.
- *Pre-declared constants* — field values must be compile-time literals; named constants (enum names, `#define` values) are not resolved in this context (see [ticket #349](https://todo.sr.ht/~takeiteasy/cccc/349)).

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

`Assert*` macros and the `return` assertion are independent — both must pass for the test to pass.

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
