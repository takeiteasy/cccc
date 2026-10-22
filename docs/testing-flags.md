# Per-Test Flags and Output Assertions

Compile flags scoped to one test, and checks on what a test prints.

## Per-test flags

`flags = "..."` overrides compiler flags for one test:

```c
[[cccc::test(flags = "--bounds-checks")]]
void test_with_bounds(void) {
    int arr[3] = {1, 2, 3};
    AssertEq(arr[0], 1);
}

[[cccc::test(return = 42, flags = "-Wpedantic")]]
int test_pedantic_clean(void) { return 42; }
```

The value is a whitespace-separated list of CLI flags. An unknown flag is a compile error naming the test.

| Kind | Flags |
|---|---|
| Safety checks | `-b`/`--bounds-checks`, `--overflow-checks`, `--type-checks`, `--stack-canaries`, `--heap-canaries`, `--uaf-detection`, `--pointer-sanitizer`, `--memory-leak-detection`, `--memory-tagging`, `--uninitialized-detection`, `--stack-instrumentation`, `--alignment-checks`, `--provenance-tracking`, `--invalid-arithmetic`, `--format-string-checks`, `--random-canaries`, `--memory-poisoning`, `--thread-safety`, `--dangling-pointers`, `-V`/`--require-vm-heap`, `-C`/`--control-flow-integrity`, `--ffi-errors-fatal`, `--ffi-allow=NAME` (adds to any suite allow list) |
| Presets | `-0`…`-3`, `--safety=none\|basic\|standard\|max` |
| Warnings | `-W<name>`, `-Wno-<name>`, `-Wall`, `-Wextra`, `-Wpedantic`, `-Werror`, `-Werror=<name>`, `-Wno-error=<name>` |
| Not accepted | `-O<n>` (no optimiser; put it in the file's `CCCC_FLAGS:` for the native leg) |

`-V`/`--require-vm-heap` means "this test needs the VM heap" and forces it on, the opposite of the CLI's `-V`/`--no-vm-heap`. The CLI's disable form is not accepted here, because `flags="--no-vm-heap"` would read as off but force the heap on. A heap-dependent test with `-V` keeps the heap even under `--leaks`.

### Flags that stay at file scope

Parse-time flags cannot be applied per test, because the lazy recompile only re-runs code generation. Use a dedicated suite file with `CCCC_FLAGS:`:

| Flag | Suite file |
|---|---|
| `--std=c89`/`c99`/`c11`/`c17`/`c23` | `test_suite_std_c89.c`, `test_suite_std_c17.c`, ... |
| `--ffi-deny=NAME` | `test_suite_ffi_deny.c` (one file per deny set) |
| `--disable-ffi` | `test_suite_ffi_disable.c` |

### Semantics

| Topic | Rule |
|---|---|
| Recompile | `flags=` recompiles the whole program just before the test; adjacent tests with the same flags share one compile, and unflagged tests pay nothing |
| Precedence | Per-test flags beat CLI flags and `#pragma cccc config(...)` ([safety.md](safety.md)); unmentioned settings inherit |
| `--testing=native` | A test with `flags=` is marked `SKIP` in the TAP output (it would need its own binary) |

## Output assertions

Four keys check what the test (and its setup hooks) print:

| Key | Passes when |
|---|---|
| `expect_stdout = "re"` | stdout matches the POSIX ERE |
| `reject_stdout = "re"` | stdout does not match |
| `expect_stderr = "re"` | stderr matches |
| `reject_stderr = "re"` | stderr does not match |

Patterns are searched anywhere in the output, as with `re.search` and `MULTILINE`.

```c
[[cccc::test(flags = "-Wattributes",
             expect_stderr = "warning: unrecognized return= operand")]]
int test_unrecognized_operand(void) { return 42; }

[[cccc::test(reject_stderr = "fatal")]]
void test_no_fatal_output(void) { printf("hello\n"); }

[[cccc::test(expect_stdout = "result: 42")]]
void test_prints_result(void) { printf("result: %d\n", 6 * 7); }
```

The keys combine with each other and with all other test keys. With `flags=`, stderr capture includes diagnostics from the recompile.

| Limitation | |
|---|---|
| Parse-time warnings (during tokenising or attribute parsing) | Produced before capture starts: use a loose file with `CCCC_FLAGS:` and `CCCC_EXPECT_STDERR:` |
| `exit_code =` tests | No capture (they run in a forked child) |
| Non-POSIX platforms | No capture |
