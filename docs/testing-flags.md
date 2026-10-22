# Per-Test Flags and Output Assertions

Compile flags scoped to one test, and stdout/stderr checks.

## Per-test compiler flags

Override the compiler flags for a specific test with `flags = "..."`:

```c
[[cccc::test(flags = "--bounds-checks")]]
void test_with_bounds(void) {
    int arr[3] = {1, 2, 3};
    AssertEq(arr[0], 1);
}

[[cccc::test(flags = "--safety=1 --overflow-checks")]]
void test_safety(void) {
    AssertEq(1 + 1, 2);
}
```

The `flags` value is a whitespace-separated string of CCCC CLI flags. Any flag
accepted by `cccc` for safety or warnings can be used:

**Safety check flags:** `-b`/`--bounds-checks`, `--overflow-checks`, `--type-checks`,
`--stack-canaries`, `--heap-canaries`, `--uaf-detection`, `--pointer-sanitizer`,
`--memory-leak-detection`, `--memory-tagging`, `--uninitialized-detection`,
`--stack-instrumentation`, `--alignment-checks`, `--provenance-tracking`,
`--invalid-arithmetic`, `--format-string-checks`, `--random-canaries`,
`--memory-poisoning`, `--thread-safety`, `--dangling-pointers`, `-V`/`--require-vm-heap`,
`-C`/`--control-flow-integrity`, `--ffi-errors-fatal`,
`--ffi-allow=NAME` (additive; adds to any suite-level allow list)

Note: in the test-dialect, `-V`/`--require-vm-heap` means "this test requires
the VM heap" and forces it on — the opposite of the CLI's `-V`/`--no-vm-heap`
(which disables it). The dialect keeps `-V`/`--require-vm-heap` rather than the
CLI name because per-test flags take precedence over the CLI (see Semantics
below): a heap-dependent test declared with `-V` keeps the VM heap active for
itself even when the suite is run under `--leaks`, which injects the CLI `-V`
to expose guest allocations. The CLI's disable flag is deliberately not
accepted here — `flags="--no-vm-heap"` would force the heap *on*, the opposite
of what its name says.

**Safety presets:** `-0`/`-1`/`-2`/`-3` and `--safety=none|basic|standard|max`

`-O<n>` is **not** a per-test flag: the VM has no optimiser, so it would do
nothing. (A test that needs an optimising host build for its `-c=native`
round-trip leg carries `-O2` in its file-scope `CCCC_FLAGS:` comment, which
the runner forwards to the host cc.)

**Warning flags** (affect compilation diagnostics for this test's lazy recompile):
- `-W<name>` / `-Wno-<name>` — enable/disable a warning category
- `-Wall`, `-Wextra`, `-Wpedantic`, `-Wstrict-prototypes`, etc.
- `-Werror` — treat all enabled warnings as errors for this test
- `-Werror=<name>` — promote one warning category to an error
- `-Wno-error=<name>` — demote one category (reverses a previous `-Werror=<name>`)

```c
[[cccc::test(return = 42, flags = "-Wpedantic")]]
int test_pedantic_clean(void) { return 42; }

[[cccc::test(return = 42, flags = "--bounds-checks")]]
int test_bounds(void) { int a[1] = {42}; return a[0]; }
```

Unknown flags are a hard compile error referencing the test name.

### Flags that must stay at file scope

Some flags affect parsing or preprocessing at tokenisation time and cannot be
applied per-test because the lazy recompile only re-runs codegen/semantics.
Use a dedicated suite file with the flag in its `CCCC_FLAGS:` comment instead:

| Flag | Suite file pattern |
|------|--------------------|
| `--std=c89`/`c99`/`c11`/`c17`/`c23` | `test_suite_std_c89.c`, `test_suite_std_c17.c`, … |
| `--ffi-deny=NAME` | `test_suite_ffi_deny.c` (one file per deny set) |
| `--disable-ffi` | `test_suite_ffi_disable.c` |

### Semantics

Because safety flags are baked into bytecode generation (not applied at
runtime), `flags=` triggers a **lazy recompile** of the whole
program immediately before the test runs. Adjacent tests that share the same
flag set share one compile; only tests with a `flags=` attribute pay any
recompile cost — unflagged tests run on the initial compile.

Per-test flags take precedence over everything, including CLI-level flags
passed to `cccc` directly and file-scope `#pragma cccc config(...)` settings.
Bits not mentioned in `flags=` inherit the base (CLI + `#pragma config`)
configuration.

### Interaction with `#pragma cccc config(...)`

`flags=` is for one-off per-test overrides. `#pragma cccc config(...)` (see
[safety.md](safety.md)) is the file-scope mechanism for declaring common
options. Both can coexist: per-test `flags=` takes precedence over
`#pragma config` for that test.

### Native mode

`flags=` applies only to the bytecode/VM execution path. Under
`--testing=native` a test carrying a `flags=` delta is not
silently ignored — it is individually marked `SKIP` in the generated TAP
output, since a per-test safety/warning configuration would
need a separate native binary per test, which v1 doesn't attempt.

## Per-test output assertions

Four attribute keys let a suite test assert on the text printed to stdout or
stderr during the test function (and any setup hooks that run before it):

| Key | Passes when... |
|-----|----------------|
| `expect_stderr = "regex"` | stderr **matches** the POSIX ERE pattern |
| `reject_stderr = "regex"` | stderr does **not** match the POSIX ERE pattern |
| `expect_stdout = "regex"` | stdout **matches** the POSIX ERE pattern |
| `reject_stdout = "regex"` | stdout does **not** match the POSIX ERE pattern |

Matching uses `regcomp(REG_EXTENDED)` + `regexec()` — equivalent to Python's
`re.search()` with `re.MULTILINE` (pattern is searched anywhere in the output).

```c
// Test passes only if the warning appears on stderr.
[[cccc::test(flags = "-Wattributes",
             expect_stderr = "warning: unrecognized return= operand")]]
int test_unrecognized_operand(void) {
    return 42;
}

// Test passes only if "fatal" never appears in stderr.
[[cccc::test(reject_stderr = "fatal")]]
void test_no_fatal_output(void) {
    printf("hello\n");
}

// Test passes only if stdout contains the expected line.
[[cccc::test(expect_stdout = "result: 42")]]
void test_prints_result(void) {
    printf("result: %d\n", 6 * 7);
}
```

All four keys can be combined in one attribute and with all other
`[[cccc::test(...)]]` keys.

When `flags = "..."` is also present, `expect_stderr`/`reject_stderr` capture
output from both the lazy-recompile phase and the test-execution phase.
The recompile invokes `cc_compile()` (code generation only — not
preprocessing or parsing), so any gen-phase diagnostics are
also captured and prepended to the test-execution output before the pattern check.

**Note:** Parse-time diagnostics (such as `-W` warnings emitted during
tokenisation or attribute parsing) are produced before `cc_run_tests()` sets up
any capture pipes and cannot be matched with `expect_stderr` in a suite test.
Tests that assert on parse-time warnings must remain as legacy files using
`CCCC_FLAGS:` and `CCCC_EXPECT_STDERR:`.

**Limitations:**
- Output capture is not supported for `exit_code =` tests (the subprocess
  runs in a forked child; future ticket).
- Capture is not available on non-POSIX platforms.
