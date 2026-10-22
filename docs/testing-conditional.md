# Conditional Test Code

Mode predefined macros, `@test` directives and the optional-feature build matrix.

## Mode predefined macros

When CCCC starts it defines exactly one of three mutually-exclusive macros
depending on the active mode:

| Macro | Defined when |
|---|---|
| `__CCCC_TEST_MODE__` | `--testing` is active |
| `__CCCC_BUILD_MODE__` | `--build` is active |
| `__CCCC_COMP_MODE__` | Neither (normal compilation) |

Use `#ifdef` / `#ifndef` to branch at compile time:

```c
#ifdef __CCCC_TEST_MODE__
#include "tests/shared_fixtures.h"
#endif
```

## Conditional directives (`@test`)

Any preprocessor directive can be gated on `--testing` mode by prefixing it
with a `@test` route attribute.  When `--testing` is active the route token is
stripped and the directive is processed normally; when it is inactive the
directive is silently skipped.  All three attribute spellings are accepted:

```c
#define @test            TEST_TIMEOUT_MS 5000   // @-prefix
#define [[cccc::test]]   TEST_TIMEOUT_MS 5000   // C23
#define __attribute__((test)) TEST_TIMEOUT_MS 5000 // GNU
```

This extends to `#include`, `#ifdef`, `#ifndef`, `#undef`, and every other
preprocessor directive:

```c
#include @test "tests/shared_fixtures.h"

#define @test TEST_TIMEOUT_MS 5000

#ifdef @test TEST_TIMEOUT_MS
#  define TIMEOUT TEST_TIMEOUT_MS
#endif                              // plain #endif — no route needed
```

When `--testing` is inactive, an `@test #ifdef`/`#ifndef`/`#if` is rewritten
as `#if 0` internally so the conditional stack stays balanced.  A plain
`#endif` (without `@test`) closes it correctly.  The `#else` branch under an
inactive `@test` conditional **runs** normally.

These are the first-class equivalents of the `__CCCC_TEST_MODE__` guard style:

```c
// Old style                            // New style
#ifdef __CCCC_TEST_MODE__               #define @test TEST_TIMEOUT_MS 5000
#define TEST_TIMEOUT_MS 5000
#endif

#ifdef __CCCC_TEST_MODE__               #include @test "tests/shared_fixtures.h"
#include "tests/shared_fixtures.h"
#endif
```

## Optional feature build matrix

Tests that exercise an opt-in feature must pass in **both** configurations.
Decimal floating-point (#402, `CCCC_HAS_DECIMAL=1`) is the current example:
`tests/suites/test_suite_decimal.c` and `test_suite_c23.c`'s
`test_c23_decimal` guard everything except size/alignment/struct-layout
checks behind `#ifdef __STDC_IEC_60559_DFP__`, so the same file returns 42
in the default build (decimal is a compile error there) and exercises real
BID-backed arithmetic when the flag is on:

```bash
./cccc --build build.c --build-target=test               # default build
tools/fetch_intel_bid.sh && make CCCC_HAS_DECIMAL=1
./cccc --build build.c --build-target=test               # decimal-enabled build
```

Negative tests that only make sense in one configuration (e.g. a decimal
literal being a diagnostic *because* the library isn't linked) aren't
encoded as checked-in `EXPECT_COMPILE_ERROR` files, since the test targets
run every test against whichever single `cccc` binary already exists —
there's no per-test build-config selector yet. Verify those by hand against
each build instead.

`tests/test_decimal_fold_clean_fenv.c` (#832) is deliberately a standalone
file with a plain `main()`, not a `[[cccc::test]]` case inside
`test_suite_decimal.c`: it asserts `fetestexcept(FE_ALL_EXCEPT) == 0` as the
very first statement, guarding against compile-time decimal constant folding
(or comptime macro execution) leaving the host FP environment dirty before
the guest program starts. Folding that assertion into the ~30-case suite
would make it order-dependent on sibling tests that legitimately divide or
`printf` a decimal value — either could set a flag the assertion would then
misattribute.
