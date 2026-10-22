# Assertion Macros

The `Assert*` family, and how asserts behave in threads and callbacks.

## Assertion Macros

All assertion macros use the `$` prefix and are injected automatically in `--testing` mode, or whenever the file itself carries a `[[cccc::test]]` attribute — see [Tests alongside `main()`](testing.md#tests-alongside-main).

### Basic Validity

| Macro | Description |
|-------|-------------|
| `Assert(cond)` | Fails if `cond` is false |
| `AssertTrue(cond)` | Alias for `Assert(cond)` |
| `AssertFalse(cond)` | Fails if `cond` is true |
| `AssertFail()` | Always fails |
| `AssertFailMsg(msg)` | Always fails with a custom message |

### Integer Comparisons

| Macro | Description |
|-------|-------------|
| `AssertEq(a, b)` | Fails if `a != b` |
| `AssertNeq(a, b)` | Fails if `a == b` |
| `AssertGt(a, b)` | Fails if `a <= b` |
| `AssertLt(a, b)` | Fails if `a >= b` |
| `AssertGe(a, b)` | Fails if `a < b` |
| `AssertLe(a, b)` | Fails if `a > b` |
| `AssertWithin(d, e, a)` | Fails if `\|e - a\| > d` |

### Bitwise

| Macro | Description |
|-------|-------------|
| `AssertBits(m, e, a)` | Fails if `(a & m) != (e & m)` |
| `AssertBitHigh(b, a)` | Fails if bit `b` of `a` is low |
| `AssertBitLow(b, a)` | Fails if bit `b` of `a` is high |

### Floating Point

| Macro | Description |
|-------|-------------|
| `AssertFloatWithin(d, e, a)` | Fails if `\|e - a\| > d` (float) |
| `AssertDoubleWithin(d, e, a)` | Fails if `\|e - a\| > d` (double) |
| `AssertFloatEq(e, a)` | Fails if `\|e - a\| > 1e-6` |
| `AssertDoubleEq(e, a)` | Fails if `\|e - a\| > 1e-15` |

### Pointers

| Macro | Description |
|-------|-------------|
| `AssertNull(p)` | Fails if `p` is not null |
| `AssertNotNull(p)` | Fails if `p` is null |

### Strings

| Macro | Description |
|-------|-------------|
| `AssertStrEq(a, b)` | Fails if strings `a` and `b` differ |
| `AssertStrEqLen(a, b, len)` | Fails if first `len` chars differ |

### Memory

| Macro | Description |
|-------|-------------|
| `AssertMemEq(e, a, len)` | Fails if `memcmp(e, a, len) != 0` |

### Arrays

| Macro | Description |
|-------|-------------|
| `AssertArrayEq(e, a, cnt)` | Fails if `e[0..cnt-1] != a[0..cnt-1]` (memcmp) |
| `AssertEachEq(e, a, cnt)` | Fails if any `a[i] != e` (element-wise) |

### Message-appending variants

Append `_msg` to any assertion to add a custom message string to the failure diagnostics:

| Macro | Description |
|-------|-------------|
| `AssertMsg(cond, msg)` | `Assert` with custom message |
| `AssertTrueMsg(cond, msg)` | `AssertTrue` with custom message |
| `AssertFalseMsg(cond, msg)` | `AssertFalse` with custom message |
| `AssertEqMsg(a, b, msg)` | `AssertEq` with custom message |
| `AssertStrEqMsg(a, b, msg)` | `AssertStrEq` with custom message |
| `AssertNullMsg(p, msg)` | `AssertNull` with custom message |
| `AssertNotNullMsg(p, msg)` | `AssertNotNull` with custom message |
| `AssertBitsMsg(m, e, a, msg)` | `AssertBits` with custom message |

When an assertion fails, the test is marked `not ok` and a diagnostic block is printed with the condition and source location. The remaining tests continue to run.


## Asserts in Threads and Callbacks

A failed `Assert*` ends the test wherever it runs; later tests are unaffected.

| Where the assert fails | Effect |
|---|---|
| Test function or a function it calls | Test stops at the assert and reports `not ok`. |
| Callback invoked by a host function (a `qsort` comparator, `bsearch`, `scandir`) | Same. Debugger flags, GIL and VM state are restored for the next test. |
| Thread started with `pthread_create` / `thrd_create` | That thread ends as if it called `pthread_exit` (its TSS destructors run). The test reports `not ok` once the test function returns. |
| Threads still running when a test fails or times out | They end at their next instruction, or as soon as they wake from a blocking call, without running TSS destructors. See [Threads left behind](#threads-left-behind). |
| Callback on such a thread (a `qsort` comparator) | The whole thread ends, not just the callback; code after the `qsort` call never runs. `pthread_exit` / `thrd_exit` inside a callback behaves the same. |
| Body of an `exit_code =` test | Test reports `not ok` with the assert's message. |

When several asserts fail in one test, the first message is the one
reported:

```c
static void *worker(void *arg) {
    AssertMsg(0, "worker failed first");   // reported
    return arg;
}

[[cccc::test]]
void both_fail(void) {
    pthread_t t;
    pthread_create(&t, 0, worker, 0);
    pthread_join(t, 0);
    AssertMsg(0, "main failed second");    // ignored
}
```

### Threads left behind

A test that fails or times out stops the threads it started and did not join.
They never run again, so they cannot write into later tests' state or hold the
interpreter lock against them. A thread blocked for good (on a condition
nobody signals) cannot be stopped; the harness waits up to 200 ms, prints
`# test N: K guest thread(s) still blocked; they end when they wake` and moves
on. Threads of a passing test, and threads started by `once` setup hooks, are
left alone: they keep running, and what they write to globals lands in the state
of whichever test is running then. Join them before the test returns to avoid it.
