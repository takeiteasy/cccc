# Testing

Write tests in C with `@test` and run them with `cccc --testing`.

CCCC includes a built-in test framework for writing tests directly in C,
using the `[[cccc::test]]` attribute to mark test functions and `Assert*`
macros for assertions. This is a guide to writing and running them.

## Writing Tests

Mark a function as a test with `[[cccc::test]]`, `__attribute__((test))`,
`@test`, `__test`, or `__test__`. Test functions must take no arguments; they may return `void`, `int`,
`double`, `float`, or `char *`:

```c
[[cccc::test]]
void test_addition(void) {
    AssertEq(1 + 1, 2);
}

// Equivalent shorthands:
__attribute__((test))
void test_with_gnu_attr(void) { AssertEq(2 * 3, 6); }

@test
void test_with_at_prefix(void) { AssertEq(10 - 8, 2); }
```

Multiple test functions can coexist in the same file:

```c
[[cccc::test]]
void test_strings(void) {
    AssertStrEq("hello", "hello");
}

[[cccc::test]]
void test_pointers(void) {
    int x = 42;
    AssertNotNull(&x);
}
```

No `#include` is required — the assertion macros and their backing declarations are injected automatically when running with `--testing`, or when the file's own source carries a `[[cccc::test]]`/`@test`/`__attribute__((test))` attribute even without `--testing` (see [Tests alongside `main()`](#tests-alongside-main) below). Outside `--testing` the test functions still compile, but nothing calls them.

### Multi-file `--testing` invocations

`cccc --testing a.c b.c` compiles both files as independent translation
units, exactly like an ordinary multi-file build. The assertion macros and
`__builtin_assert_*` declarations are injected into *every* input file's own
parse stream, so `[[cccc::test]]` functions can live in any file regardless
of its position on the command line — `cccc -t helper.c tests.c` and
`cccc -t tests.c helper.c` behave identically.

A `--testing` run that collects zero `[[cccc::test]]` functions across every
input file is a hard error (`error: --testing: no [[cccc::test]] functions
found in any input file`, exit 1), not a silent `TAP version 13 / 1..0` pass
— this catches an invocation whose test file's declarations never reached
the parser. A `--test=GLOB`/`--test-suite=` filter that matches nothing is
unaffected and keeps exiting 0 (that's a filter miss, not a broken build).

### Tests alongside `main()`

A file does not need `--testing` to compile a `[[cccc::test]]` function next
to `main()` — `testing.h` (the `Assert*` macros and their
`__builtin_assert_*` backing declarations) is injected automatically whenever
the file's own source carries a `[[cccc::test]]`/`[[cccc::test_setup]]`/
`[[cccc::test_teardown]]` attribute (`@test`, `[[cccc::test]]`, or
`__attribute__((test))`, all recognized), the same way it always is under
`--testing`:

```c
int main(void) {
    return 0;
}

@test void it_works(void) {
    AssertEq(1 + 1, 2);
}
```

What each invocation does with that file:

| Command | Behaviour |
|---|---|
| `cccc demo.c` | Runs `main()` on the VM. `it_works` compiles but is never run. |
| `cccc -c=native -o demo demo.c` | Native binary of `main()`. `it_works` is emitted (inert) alongside the assert runtime — see [native.md](native.md). |
| `cccc --testing demo.c` | Discovers and runs `it_works`, exactly as if it were the whole file. |

This is demand-driven, not unconditional: a file with no `@test`/
`[[cccc::test...]]` attribute anywhere sees none of `testing.h`'s `Assert*`
macros, so an ordinary program is free to define its own function named
`Assert` (or any other assertion-macro name) without a collision. The scan
only looks at the file's own text, so an attribute produced by macro
expansion, or written only in an `#include`d header, is not enough by
itself — pass `--testing` explicitly in that case, or
`#include <cccc/testing.h>` directly.

### Programmatic test generation via emit blocks

`[[cccc::test]]` (and `[[cccc::test_setup]]` / `[[cccc::test_teardown]]`) inside
`#pragma cccc emit begin...end` blocks are scanned and registered, enabling
comptime macros to generate test functions programmatically:

```c
#pragma cccc comptime begin
#pragma cccc emit begin
[[cccc::test]]
void generated_test(void) { }   // registered and run under --testing
#pragma cccc emit end
#pragma cccc comptime end
```

Note that C preprocessor macros (`AssertEq`, `Assert`, etc.) are not expanded
inside emit blocks. Use the underlying `__builtin_assert_*` functions directly,
or structure the test body so that assertions live outside the emit block.

### Programmatic test registration via AST API

When building functions through the AST API (`MakeFunction` + `PublishNode`),
use `AddAttribute` to register and configure the function as a test entry.
Suite, display name, timeout, and all other test options are expressed inline
in the attribute string — the same options available in `[[cccc::test(...)]]`:

```c
[[cccc::comptime]]
void gen_tests(void) {
    Obj *fn = MakeFunction("my_generated_test", GetType("void"));
    WithFn(fn) { FunctionSetBody(fn, Quote("return;")); }
    PublishNode(fn);

    // All test options inline — same syntax as [[cccc::test(...)]]
    AddAttribute(fn, "cccc::test(suite=\"generated\", name=\"my test name\", timeout=5000)");
}
gen_tests();
```

| Helper | Effect |
|---|---|
| `AddAttribute(fn, "cccc::test")` | Register as `[[cccc::test]]` |
| `AddAttribute(fn, "cccc::test(suite=\"s\")")` | Register with suite |
| `AddAttribute(fn, "cccc::test(name=\"n\")")` | Register with display name |
| `AddAttribute(fn, "cccc::test(timeout=5000)")` | Register with timeout |
| `MarkAsTest(fn)` | Shorthand for `AddAttribute(fn, "cccc::test")` |

For error patterns, return assertions, and per-test compiler flags, use the
full `[[cccc::test(...)]]` attribute string inside `AddAttribute` — all options
from the attribute syntax are available. See [macros.md](macros.md) for details.

### Custom display names

Give a test a human-readable name with the `name` option. The display name appears in TAP output and is used for glob filtering; the C function name is unchanged:

```c
[[cccc::test(name = "addition is commutative")]]
void test_add_commutative(void) {
    AssertEq(1 + 2, 2 + 1);
}
```

`name`, `suite`, `error`, `exit_code`, `timeout`, `error_count`, `return`, `return_epsilon`, `flags`, `expect_stderr`, `reject_stderr`, `expect_stdout`, and `reject_stdout` may all be combined in one attribute (with the exception that `exit_code` is mutually exclusive with `error` and `return`).


## Limitations

- Test functions must take no arguments and return `void`, `int`, `double`, `float`, `char *`, or a flat struct/union. Use `return = value` to assert on the return value.
- `return =` assertions support integer literals, float literals, string literals, and compound struct/union literals (`(struct T){.f = v, ...}`). Enum names (`return = GREEN`) and character literals (`return = 'A'`) are not resolved — use the integer value instead (`return = 1`, `return = 65`). Unrecognized operands produce a `-Wattributes` warning and skip the assertion.
- Struct `return =` supports nested struct/union/array fields and anonymous struct/union members (recursing to a maximum of 8 levels), in addition to scalar and `char *` fields. Bitfield members and non-`char *` pointer fields are not supported.
- Setup and teardown hook functions must also have signature `void name(void)`.
- Teardown hooks are skipped on test timeout (VM state is unknown after `SIGALRM`). They run in all other cases, including after test or setup failure.
- Calling `exit()` directly in a normal test terminates the entire process rather than failing just that test. Use `Assert*` macros instead, or use `exit_code =` if testing that the function exits with a specific code.
- Setup/teardown hooks match suites by exact name by default. Add `inherit` to also cover sub-suites.
- **Negative test bodies are matched against error substrings.** Use a substring that is specific enough to avoid false matches but not so specific that it breaks with minor message wording changes.
- **`exit_code =` tests are skipped on non-POSIX platforms** where `fork(2)` is not available.
- `--test-timeout` uses `SIGALRM`; test code that also uses `alarm()` or installs a `SIGALRM` handler will interfere with the timeout mechanism.
- **`flags=` triggers a whole-program recompile.** The recompile is lazy (only when the required config changes), but setup/teardown hook once-snapshots taken before a recompile are discarded and re-taken under the new compile. If a once-setup hook leaves per-test state in global variables, that state may not survive across recompile boundaries.
- **`flags=` supports safety and warning flags.** Flags that affect tokenisation, preprocessing, or output format (e.g. `--std=`, `--include`, `-D`) cannot be per-test and will be rejected as unknown flags.

## Guide pages

| Page | Covers |
|---|---|
| [testing-suites.md](testing-suites.md) | Suites and test-file header directives |
| [testing-negative.md](testing-negative.md) | Negative tests, exit-code tests and runtime safety violations |
| [testing-returns.md](testing-returns.md) | `return = value` assertions |
| [testing-asserts.md](testing-asserts.md) | Assertion macros, and asserts in threads and callbacks |
| [testing-hooks.md](testing-hooks.md) | Setup/teardown hooks and global state reset |
| [testing-conditional.md](testing-conditional.md) | Mode macros, `@test` directives and the feature build matrix |
| [testing-running.md](testing-running.md) | Output formats, backends, filtering, timeouts |
| [testing-flags.md](testing-flags.md) | Per-test compiler flags and output assertions |
