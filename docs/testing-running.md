# Running and Filtering Tests

Output formats, backends, filters and timeouts.

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

### Testing backends (`--testing[=vm|native]`)

`--testing`/`-t` takes an optional backend selector. Bare `-t`/`--testing`
(no `=`) means `=vm` — the default, in-process behaviour described above.

`--testing=native` serializes the harness itself into a standalone native
binary, builds it with the host compiler, runs it and exits with its status
(`0` all passed, `1` any failed). Each test runs in a forked child, so
`exit_code=`, stdout/stderr capture and timeouts work as under `=vm`.

```
./cccc --testing=native suite.c               # build in a temp file, run, delete
./cccc --testing=native -o suite_bin suite.c  # same run, binary kept at suite_bin
```

The harness takes the same selection flags as the VM runner, so a kept binary
accepts them directly:

| Flag | Effect |
|---|---|
| `--test=GLOB` | run tests whose name matches |
| `--test-suite=NAME` | run a suite and its sub-suites (glob if `NAME` has `*?[`) |
| `--fail-fast` | stop after the first failure or timeout |
| `--test-timeout=N` | default per-test timeout in seconds |
| `--list-tests` | list the selected tests without running them |

```
./cccc --testing=native -o suite_bin suite.c
./suite_bin --test='math_*' --fail-fast
```

`--test-format=tap|plain|json` picks the output format, with the same output as
`--testing=vm`. The format is fixed when the binary is built, so a kept binary
does not take `--test-format`.

Not supported natively (refused with a diagnostic): `test_setup`/`test_teardown`
hooks and negative tests (`error=`, `expect_compile_error=`). A per-test
`flags=` delta is `SKIP`ped in the TAP output.

### Combining `--testing` with `-c` (compile pre-pass)

`--testing`/`-t` (the VM backend) can be combined with `-c=native`, where
the test suite acts as a pre-pass guard in front of the compile: the suite
runs first, and only if every collected `[[cccc::test]]` passes does the
compile step proceed, building an executable through the host toolchain.
Any test failure (or a run that hits its timeout) exits nonzero without
producing an artifact, so an artifact's existence certifies that the suite
was green at compile time:

```
./cccc --testing -c=native -o prog myfile.c       # tests, then host compile
```

The gate is independent of `--fail-fast`: that flag only stops the test
*run* after the first failure, while any failing test blocks the compile
regardless. The same guard applies to `--testing --build` — a build script
whose suite fails is refused before any target compiles.

### `--run`: run the program after the suite

`--run` runs `main()` on the VM, which is what a plain `cccc file.c` already
does. After `--testing` it makes the suite a gate in front of the run: `main()`
runs only if every test passes.

```
./cccc --testing --run myfile.c     # tests, then main() if all passed
./cccc --build --run myfile.c       # build recipe, then main() if it succeeded
```

A failing gate exits nonzero without running `main()`. `--list-tests --run`
lists the tests and stops. `--run` cannot be combined with `-c`,
`--testing=native`, `--repl`, `--test-run`, `-d`, `-E`, `-m`, `-p`, `--ast`,
`-j` or `-J`, and `--build --run` cannot be combined with `--watch`.

`--testing=native` is unaffected by all this (that combination is
`--testing`'s own compile-and-run-the-harness mode, not a VM pre-pass in
front of a separate `-c=native`). `-c=generated` also combines with
`--testing`, but is *not* suite-guarded: its portable-C serialization runs
before the test dispatch (the serialized output is emitted regardless of
the suite's result) — the combination exists so tooling can inspect
generated output with `--testing`-active preprocessing, e.g. the `@test`
route-token leak check.

### `--test-run[=LEVEL]`: smoke-test the program itself before compiling

Where `--testing -c=native` guards compilation behind a passing `[[cccc::test]]` suite, `--test-run[=LEVEL]` guards it behind a single VM execution of the program's own `main()` under safety instrumentation -- a "does this crash under CCCC's safety checks" smoke test, with no test functions involved.

```
./cccc --test-run program.c                          # VM run at safety=max, then compile native
./cccc --test-run=basic program.c                     # VM run at a lower safety preset
```

- `LEVEL` accepts the same values as `--safety=`: `none`/`basic`/`standard`/`max` or `0`/`1`/`2`/`3`. Bare `--test-run` (no `=LEVEL`) is `max`.
- The smoke-test run happens in a forked child process, so it never touches the compiled program's actual global/heap state -- the eventual native artifact still starts from the source's real compile-time initializers, not whatever the smoke test's `main()` left them as.
- Success is "ran to completion without a VM-detected safety violation (bounds/UAF/CFI/uninitialized-read/etc.), a real crash (signal), or a hang" -- capped at `--test-timeout` seconds (default 30s when unset). The program's own exit code is **not** checked: a CLI that legitimately returns nonzero on bad input is not a `--test-run` failure. A safety violation alone (no accompanying crash) still refuses to compile; a bare memory leak with no other violation does not.
- Implies `-c=native` when no `-c=FMT` is given (matching bare `-c`'s own default); an explicit `-c=native` still picks the format. `-o`'s default-filename behavior (`./a.out`) applies the same as plain `-c`.
- Not compatible with `--repl`, `--build`, `--testing`, `-d`, or the frontend output modes (`-E`/`-M`/`--ast`/`-j`/`-J`) -- none of these have a compile step for `--test-run` to guard.

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
