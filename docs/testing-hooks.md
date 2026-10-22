# Setup, Teardown and State Reset

Hooks that run around tests, suites and the whole run.

## Setup and Teardown

The framework supports lifecycle hooks that run before and/or after tests. Use
`[[cccc::test_setup]]`, `__attribute__((test_setup))`, or `@test_setup` to mark
setup hooks (and the equivalent `test_teardown` forms for teardown). Hook
functions must have signature `void name(void)`.

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
    // runs before each test in the "network" suite — exact match only
}
```

### Suite hook inheritance

By default, a hook scoped to `suite = "math"` runs only for tests whose suite is
exactly `"math"`. Add the `inherit` keyword to also cover all sub-suites
(`"math/trig"`, `"math/trig/advanced"`, etc.):

```c
[[cccc::test_setup(suite = "math", inherit)]]
void math_setup(void) {
    // runs before each test in "math" AND any "math/*" sub-suite
}
```

The `inherit` flag uses the same `/`-boundary prefix rule as `--test-suite` filtering,
so a hook on `"math"` does **not** accidentally match `"mathematics"`.

`inherit` can be combined with `once` to wrap an entire subtree with a single
setup/teardown pair:

```c
[[cccc::test_setup(suite = "math", once, inherit)]]
void math_once_setup(void) {
    // fires once before the first "math" or "math/*" test
}

[[cccc::test_teardown(suite = "math", once, inherit)]]
void math_once_teardown(void) {
    // fires once after the last "math" or "math/*" test leaves the subtree
}
```

The once-hook lifecycle is **streaming**: as tests move from one sub-suite to another,
the framework closes hooks that no longer cover the current test (innermost-first)
and opens hooks that newly cover it (outermost-first). A parent suite hook stays
open across a dip into a child block — it does **not** re-fire when the suite
re-enters the parent scope after visiting a child.

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
    AssertEq(g_initialised, 1); // lazy_init fired before this test
}

[[cccc::test(name = "needs_init_b")]]
void test_b(void) {
    // g_initialised is still 1 — the data snapshot was taken after
    // lazy_init, so its state is visible to all subsequent tests.
    AssertEq(g_initialised, 1);
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

If a setup hook fails (via `Assert`), the test is skipped and the test is marked as failed. Teardown hooks still run after the failed setup.

If the test itself fails, teardown hooks still run. Teardown is only skipped on timeout, because the VM state is unknown after `SIGALRM`.

## Global State Reset

Global variables are automatically reset to their initial (compile-time) values before each positive test runs. This means tests can safely modify global state without affecting each other:

```c
static int g_count = 0;

[[cccc::test]]
void test_a(void) {
    g_count = 99;   // modifies g_count
    AssertEq(g_count, 99);
}

[[cccc::test]]
void test_b(void) {
    // g_count is reset to 0 before this test; test_a's modification is gone
    AssertEq(g_count, 0);
}
```

The reset happens after per-test setup hooks are scheduled but before they run — each test (including its setup hooks) sees the initial snapshot.

For suites with `once` setup hooks, each test in the suite restores the post-once-setup snapshot rather than the original initial snapshot, so the shared state established by `once` setup persists across the suite's tests.
