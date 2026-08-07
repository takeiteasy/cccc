# Developer Tooling

Reference for CCCC's interactive debugger, warning system, profiling, benchmarks, and fuzzing harnesses.

---

## Interactive Debugger

The CCCC VM includes an interactive, source-level debugger for step-by-step program execution and inspection.

**Enable with:** `-g` or `--debug` flags

When enabled, the debugger provides a powerful GDB-like interface for controlling program flow and inspecting state.

### Features

- **Source-Level Debugging**: The debugger maps bytecode instructions to their original source code locations with precise column tracking. When you step through the code, it displays the current file, line number, column number, and the corresponding source line, providing a seamless debugging experience. Column numbers are UTF-8 aware for correct positioning in multi-byte character source files.
- **Advanced Breakpoints**: Set breakpoints using multiple formats:
    - By line number in the current file (`break 42`).
    - By file and line number (`break test.c:42`).
    - At the entry point of a function (`break main`).
    - At a raw bytecode offset (legacy support).
- **Conditional Breakpoints**: Set breakpoints that only trigger when a specific condition is met. A condition is compiled -- not tree-walked -- into a small wrapper function the first time its breakpoint is hit, and that compiled wrapper is reused on every later hit, so it supports the full C expression grammar and the full call ABI: local and global variables, arithmetic, comparison, logical operators, casts, assignments, member and pointer access, ternary and comma expressions, and function calls with floating-point arguments/returns, struct/union returns, variadic calls, indirect calls, nested-function static links, and stack-passed arguments. Assignments and function calls are evaluated normally and can change program state (including the paused frame's own locals). The condition's controlling expression must have scalar type, exactly like a real `if` statement.
    - Syntax: `break <location> if <expression>`
    - Example: `break 22 if x > 5`
    - Example: `break 22 if half(fx) > 2.0 && sum_point(pt) == 7`
- **Watchpoints (Data Breakpoints)**: Break execution when memory is read or written. Watchpoints can be set on variables by name or on raw memory addresses.
    - `watch <var|addr>`: Break on write.
    - `rwatch <addr>`: Break on read.
    - `awatch <addr>`: Break on read or write.
- **Execution Control**: Full control over program flow with commands to step into (`step`), step over (`next`), and step out of (`finish`) functions.
- **State Inspection**: Inspect VM registers, the call stack, and raw memory at any address. The debugger tracks local and global variable names, allowing them to be used in expressions.

### Debugger Commands

| Command | Short | Description |
|---|---|---|
| `continue` | `c` | Continue execution until next breakpoint or watchpoint. |
| `step` | `s` | Single step, stepping into function calls. |
| `next` | `n` | Step over, executing function calls without stopping. |
| `finish` | `f` | Run until the current function returns. |
| `break <loc>` | `b <loc>` | Set a breakpoint at a specified location (`<line>`, `<file:line>`, `<func>`, or `<offset>`). |
| `break <loc> if <expr>` | `b <loc> if <expr>` | Set a conditional breakpoint. |
| `watch <var/addr>` | `w <var/addr>` | Set a watchpoint to break on writes to a variable or address. |
| `rwatch <addr>` | | Set a watchpoint to break on reads from an address. |
| `awatch <addr>` | | Set a watchpoint to break on reads or writes to an address. |
| `info watch` | | List all active watchpoints. |
| `delete <num>` | `d <num>` | Delete a breakpoint by its number. |
| `list` | `l` | List all breakpoints. |
| `registers` | `r` | Print all register values. |
| `stack [count]` | `st [count]` | Print the top `count` entries of the stack (default 10). |
| `disasm` | `dis` | Disassemble the current instruction. |
| `memory <addr>` | `m <addr>` | Inspect memory at a given address. |
| `help` | `h`, `?` | Show the help message. |
| `quit` | `q` | Exit the debugger and terminate the program. |

### Example Debugging Session

```bash
$ ./cccc -g test_debugger_enhanced.c

========================================
    CCCC Debugger
========================================
Starting at entry point...
Type 'help' for commands, 'c' to continue

(cccc-dbg) break 20            # Set breakpoint at line 20
Breakpoint #0 set at test_debugger_enhanced.c:20

(cccc-dbg) continue            # Run to breakpoint
Breakpoint #0 hit at test_debugger_enhanced.c:20
At test_debugger_enhanced.c:20:5
    20:     int x = 10;
0xc33400018 (offset 24): LEA -4

(cccc-dbg) watch x             # Watch for writes to variable 'x'
Watchpoint #0: watch x

(cccc-dbg) step                # Execute one instruction (the assignment to x)
Watchpoint #0 hit: write to x at 0x7ffeea28d3f8
Old value: 0
New value: 10
At test_debugger_enhanced.c:21:5
    21:     int y = factorial(4);

(cccc-dbg) stack 3             # Check the stack
=== Stack (top 3 entries) ===
  sp[0] = 0x000000000000000a  (10)
  ...

(cccc-dbg) continue            # Run to completion
```

### Programmatic Break-in

When the debugger is enabled (`-g`), several mechanisms drop execution into the interactive REPL from within the running program.

#### `__builtin_debugtrap()`

Inserts a hard-coded break-in point in source code.  The BTRAP opcode it emits enters the debugger REPL immediately; execution continues when you type `continue`.

```c
#include <stdio.h>

int main(void) {
    int x = compute();
    __builtin_debugtrap();   /* execution pauses here under -g */
    printf("x = %d\n", x);
    return 0;
}
```

Without `-g`, `__builtin_debugtrap()` (and `__builtin_trap()`) terminate the program.

#### `__builtin_trap()`

Like `__builtin_debugtrap()` but semantically unconditional: it is never expected to continue.  Under `-g` it still breaks into the REPL (you can continue), but in production builds it aborts.  Use it to guard unreachable paths that should never execute.

#### `raise(SIGTRAP)` via VM signal handling

When a VM signal handler is registered for `SIGTRAP` and `-g` is active, `raise(SIGTRAP)` also enters the debugger REPL.  This matches the behaviour of native debugger trap instructions on AArch64 Apple platforms.

```c
#include <signal.h>

int main(void) {
    raise(SIGTRAP);   /* breaks into debugger REPL under -g */
    return 0;
}
```

### Auto-Debug-on-Crash

CCCC automatically drops into the interactive debugger when a running program
hits either a fatal VM error or a native host `SIGSEGV`, `SIGBUS`, `SIGFPE`,
`SIGILL`, or `SIGABRT`. This covers unchecked NULL/out-of-bounds accesses with
the default safety flags as well as checked memory errors, stack overflow,
invalid control-flow targets, and unknown instructions. Runtime and compile-time
(`[[cccc::comptime]]` / `#pragma cccc comptime`) execution use the same path.

For VM-detected errors, execution stops with the faulting instruction current;
`continue` retries it after state has been inspected or changed. A true host
signal first unwinds the faulting native interpreter frame with `siglongjmp`,
then opens an inspection-only debugger at the recorded bytecode instruction.
Registers, VM stack, source, disassembly, and valid memory remain inspectable,
but `continue`, `step`, `next`, and `finish` are rejected because the native
frame cannot be resumed safely. `quit` or EOF exits with `128 + signal`.

**This only activates when both stdin and stdout are attached to a TTY** —
running CCCC under a pipe, redirect, or non-interactive harness always falls
back to printing the error and exiting, so scripted/batch usage is
unaffected.

Guest `signal()` dispositions remain authoritative. A registered VM handler or
`SIG_IGN` is delivered from the normal dispatch safe point and is not replaced
by crash debugging; restoring `SIG_DFL` restores the debugger trap while it is
active. Signal-handler code never runs directly in native signal context.

You do not need to pass `-g`/`--debug` to get this behaviour — it is enabled
automatically for interactive sessions. Passing `-g` explicitly still works
as before (stop at the entry point of `main`), and additionally now also
traps on later fatal errors during the run when stdin and stdout are TTYs.

**Disable with:** `--no-debug-on-crash`. Use this when invoking `cccc`
directly from an external test harness or script that happens to run
attached to a TTY (e.g. inside a `script`/`tmux` session) but should still
just see a printed error and a non-zero exit code on failure. CCCC's own
`--testing`/`--test`/`--test-suite` runners disable the auto-detection
internally for the same reason (they fork child processes that would
otherwise inherit the parent's TTY and hang waiting on debugger input).

### Source Map API

CCCC provides a programmatic API for accessing source location information, which is useful for building custom debugging tools or IDE integrations.

#### Symbolizing a PC from User Code

Two builtins compose with `__builtin_return_address` to produce human-readable symbolization from within bytecode programs:

```c
#include <stdio.h>

void print_caller_info(void) {
    // Capture the return address (= a VM bytecode offset, not a host address).
    void *ra = __builtin_return_address(0);

    // Map to the enclosing function name — always available, no -g required.
    const char *fn = __builtin_pc_function_name(ra);

    // Map to file/line — requires -g; gracefully returns NULL/0 without it.
    const char *file = NULL;
    int line = 0;
    __builtin_pc_source_location(ra, &file, &line);

    if (file)
        printf("called from %s at %s:%d\n", fn ? fn : "?", file, line);
    else
        printf("called from %s\n", fn ? fn : "?");
}
```

#### Embedder C API

The same lookups are available to embedders through `cccc.h`:

```c
// Map a bytecode PC to the enclosing function name.
// Does NOT require -g. Returns NULL if not found.
const char *cc_pc_to_name(VirtualMachine *vm, Pc pc);

// Map a bytecode PC to a source file name and 1-based line number.
// Requires -g. Returns 1 on success (sets *out_file, *out_line);
// returns 0 and zeros the outputs on failure.
int cc_pc_to_source(VirtualMachine *vm, Pc pc, const char **out_file, int *out_line);
```

Example:

```c
// Symbolize the current PC from the embedder side.
const char *name = cc_pc_to_name(&vm, vm.pc);
const char *file = NULL;
int line = 0;
cc_pc_to_source(&vm, vm.pc, &file, &line);
if (name) printf("in %s", name);
if (file) printf(" (%s:%d)", file, line);
putchar('\n');
```

#### Getting Source Location (low-level)

```c
// Get source file location for a given PC address (requires -g).
File *file = NULL;
int line_no = 0;
int col_no = 0;

if (cc_get_source_location(&vm, vm.pc, &file, &line_no, &col_no)) {
    printf("At %s:%d:%d\n", file->name, line_no, col_no);
}
```

#### Exporting Source Maps

Source maps can be exported to JSON format for use with external tools:

```c
// Export source map to JSON
FILE *f = fopen("sourcemap.json", "w");
cc_output_source_map_json(&vm, f);
fclose(f);
```

The JSON output includes:
- **pc**: Bytecode offset
- **file**: Source file name
- **line**: Line number (1-based)
- **col**: Starting column number (1-based, UTF-8 aware)
- **end_col**: Ending column number (1-based)

Example output:
```json
{
  "version": 3,
  "mappings": [
    {"pc": 8, "file": "test.c", "line": 3, "col": 5, "end_col": 8},
    {"pc": 16, "file": "test.c", "line": 4, "col": 9, "end_col": 14}
  ]
}
```

#### Bytecode File Format

Binary format (little-endian):
```
[Magic: "CCCC\0" (4 bytes)]
[Version: 4 (4 bytes)]
[Flags: CCCCFlags bitfield (4 bytes)]
[Text size: bytes (8 bytes)]
[Data size: bytes (8 bytes)]
[Main offset: instruction offset (8 bytes)]
[Data relocation count (8 bytes)]
[Text segment: 32-bit bytecode instruction words]
[Data segment: global variables and constants]
[Data relocations]
[Return buffer count (8 bytes)]
[Return buffer size (8 bytes)]
[Return buffer data offsets]
[FFI table]
```

The VM text segment uses 32-bit instruction words. Operands that need 64 bits,
such as C integer immediates and text/data byte offsets, are encoded as two
consecutive 32-bit words. Direct branch and call targets are instruction
indexes, while C-visible function and label values are byte offsets from the
text segment base. Saved bytecode stores data relocations and aggregate
return-buffer offsets separately from raw segment bytes so `cc_load_bytecode()`
can rebuild process-local VM pointers after loading.

Floating-point bytecode uses a tagged register file. `FLDR`/`FSTR` load and
store `double` values tagged as f64, while `FLDR_F32`/`FSTR_F32` load and store
C `float` values tagged as f32. Arithmetic and comparison opcodes without a
suffix operate as f64; `_F32` opcodes operate in f32 precision. `FROUND_F32`
converts a floating register to f32 and tags the destination as f32. Raw-bit
moves are typed: `FR2R`/`R2FR` move f64 payloads, and `FR2R_F32`/`R2FR_F32`
move f32 payloads.

### Host C Backtrace on Crash

When CCCC itself crashes — during parsing, codegen, or VM dispatch — it prints
a symbolic host C stack trace to stderr before the process exits. This works
during **any** phase, not only inside the VM dispatch loop. Example output when
a null dereference occurs inside the CCCC VM opcode handler:

```
Host C crash (SIGSEGV):
  #0   <0x191c256a3>
  #1   op_LDR_W_fn (src/ops.c:1130)
  #2   cccc_vm_eval_dispatch (/src/vm.c:617)
  #3   vm_eval (/src/host_signal.c:183)
  #4   cc_run_at (/src/vm.c:1873)
  #5   cc_run (/src/vm.c:1881)
  #6   main (/src/main.c:2215)
```

The handler is installed early in `main()` for SIGSEGV, SIGBUS, SIGFPE, and
SIGILL. It uses [libbacktrace](https://github.com/ianlancetaylor/libbacktrace)
(vendored in `src/backtrace/`, BSD-licensed) and is on by default.

After printing the trace the process dies with the original signal and exit
code, so the test runner's exit-code semantics and negative-test failures are
unaffected.

When the interactive debugger is active (`-g` and both stdin/stdout are a TTY),
the host C backtrace is also printed when the fault handler hands control to the
debug REPL — before the native frame is unwound by `siglongjmp` — so you see
both the host C call chain and the guest VM state.

#### File:line resolution on macOS

On macOS, CCCC is linked in a single compiler invocation that deletes temp `.o`
files. The binary carries only a debug map (N_OSO stabs) rather than inline
DWARF, so libbacktrace resolves **function names** but not `file:line` for
uninstrumented frames. To unlock full `file:line`:

```bash
make dsym      # runs dsymutil, produces cccc.dSYM
```

After that, re-running the same crash shows complete file and line numbers.
`cccc.dSYM` is gitignored.

On Linux, the `-g` build embeds DWARF inline in the ELF binary, so `file:line`
works without any extra step.

#### Disabling

Pass `CCCC_HAS_BACKTRACE=0` to `make` to build without libbacktrace:

```bash
make CCCC_HAS_BACKTRACE=0
```

---

## Interactive REPL

CCCC includes an interactive top-level read-eval-print loop for typing C
declarations and expressions directly at a prompt, distinct from the
breakpoint-time [Interactive Debugger](#interactive-debugger) above (which
inspects an already-running program, not a persistent session of its own).

**Enable with:** `-r` or `--repl` (takes no input file). Note that `-r` was
previously the short form of `--macro-recursion-limit`; that option's short
form has been removed and only the long form remains.

```bash
$ ./cccc --repl
CCCC interactive REPL. Type :help for session commands, :quit to exit.
cccc> int x = 40;
cccc> x + 2
(int) 42
cccc> int square(int n) { return n * n; }
cccc> square(7)
(int) 49
cccc> :quit
```

### How it works

A single VM instance and a single persistent global scope live for the whole
session:

- **Declarations** (variables, typedefs, structs/unions/enums, functions)
  are parsed against that persistent scope and accumulate for the rest of
  the session, exactly as if you kept typing more source into one
  translation unit. They are compiled *incrementally* -- evaluating a later
  line never re-lays-out or re-generates code for anything declared earlier,
  so runtime mutations (`x = x + 1;`) and any addresses computed by earlier
  lines remain valid.
- **Expressions** are wrapped in a synthetic zero-argument function, compiled
  the same incremental way, executed once on the VM, and their typed result
  is printed (reusing the same execute-on-the-VM approach the compiler uses
  for `[[cccc::comptime]]` macro bodies).
- Each line is classified as a declaration or an expression by peeking its
  first token against the live typedef/keyword table, so `a * b` parses as a
  declaration when `a` is a typedef and as an expression otherwise.
- **Multi-line input**: an unterminated brace/paren/bracket (or a trailing
  `\`) prompts for more input (`  ... `) instead of erroring, similar to
  Python or GDB.
- **Error recovery**: a line that fails to parse or type-check prints its
  error and rolls the session back to its state before that line -- it does
  not corrupt subsequent declarations or expressions.
- **Line history/editing** is provided by linking against GNU readline when
  available at build time (see [Optional dependencies](#optional-readline-support)
  below); otherwise input falls back to plain, non-editing line reads.

### Session commands

| Command | Description |
|---|---|
| `:help` | Show session commands. |
| `:type <expr>` | Print the type of `<expr>` without evaluating it. |
| `:load <file>` | Read declarations/expressions from `<file>`, one logical unit at a time. |
| `:quit` | Exit the REPL. |

### Result formatting (current scope)

Integers, `enum` (printed by value), `bool`, `float`/`double`, pointers (hex),
and `char *` (also printed as a string) are supported. Struct, union, and
array-typed results print a placeholder rather than their contents --
recursive aggregate formatting is tracked as follow-up work (ticket #666).

### Known limitations

- The REPL does not run the full preprocessor: `#include`, `#define`, and
  other directives are not available at the prompt (only tokenization and
  semantic parsing/type-checking run on each line).
- Every evaluated *expression* compiles a small one-shot wrapper function
  whose bytecode is not reclaimed after use, so a very long session slowly
  grows the VM's text/data segments (ticket #667). This does not affect
  declarations, which compile once and are never touched again.
- Redefining an already-defined function or variable within a session (hot
  reload) is not supported -- this is explicitly out of scope for the
  initial REPL and is tracked separately.
- Certain internal compiler errors outside of parsing/type-checking (for
  example calling a forward-declared-but-never-defined function, or hitting
  a hard resource limit like data-segment overflow) still terminate the
  process rather than being caught and rolled back, matching the compiler's
  general error-handling model everywhere else. Only parse-time and
  type-checking diagnostics are recoverable in the REPL today.
- `-r`/`--repl` is VM-only: it cannot be combined with `-c=native`, `--build`,
  `--testing`, `--ngrams`/`--fusion-candidates`, or any frontend/output mode
  (`-c`, `-d`, `-E`, `-M`, `--ast`, `--vm-profile`).

### Optional readline support

Line history/editing is linked dynamically against GNU readline when it is
available at build time (probed via `pkg-config`, with a Homebrew-path
fallback on macOS since readline there is keg-only). When readline is not
found, the build falls back to plain unbuffered line reads (no history, no
in-line editing) rather than failing.

```bash
make                       # builds with readline if found, else falls back
brew install readline      # macOS: make readline discoverable
make clean && make
```

---

## Warnings and Diagnostics

CCCC supports gcc/clang-style warning category controls for suppressible
compiler diagnostics. Warnings are disabled by default; enable categories with
`-W<name>`, `-Wall`, or `-Wextra`.

### Controls

- `-W<name>` enables one warning category.
- `-Wno-<name>` disables one warning category.
- `-Wall` enables the common warning set.
- `-Wextra` enables extra checks beyond `-Wall`.
- `-Werror` or `--Werror` promotes all enabled warnings to errors.
- `-Werror=<name>` enables one category and promotes it to an error.
- `-Wno-error=<name>` keeps one enabled category as a warning, even after
  `-Werror`.

Options are processed left to right, so later flags override earlier flags.
Exception: `-Werror=<name>` is **sticky**. Once a category is promoted to an
error with `-Werror=<name>`, a subsequent `-Wno-error=<name>` has no effect.
Use `-Wno-<name>` to fully disable the category, including its error promotion.

### Per-Test Warning Flags (in `[[cccc::test]]` suites)

Warning flags can be applied per-test inside `[[cccc::test]]` suite files using
the `flags=` attribute. This triggers a lazy recompile of the whole program with
the specified warning configuration before running that test:

```c
// Compile this test with -Wpedantic active
[[cccc::test(return = 42, flags = "-Wpedantic")]]
int test_pedantic_clean(void) { return 42; }

// Promote conversion warnings to errors for this test
[[cccc::test(return = 42, flags = "-Werror=conversion")]]
int test_no_conversion(void) { return 42; }
```

All CLI warning flags are accepted: `-W<name>`, `-Wno-<name>`, `-Wall`,
`-Wextra`, `-Wpedantic`, `-Werror`, `-Werror=<name>`, `-Wno-error=<name>`.
See [TESTING.md](TESTING.md) for the full per-test flags reference.

### Pragma-Based Suppression

Within source files, `#pragma cccc diagnostic` controls warning state inline:

```c
#pragma cccc diagnostic push           // save current warning state
#pragma cccc diagnostic pop            // restore saved state
#pragma cccc diagnostic ignored "-Wunused"   // suppress a category
#pragma cccc diagnostic warning "-Wunused"   // enable a category as warning
#pragma cccc diagnostic error   "-Wunused"   // promote a category to error
```

`push` and `pop` nest: each `push` saves the current state onto a stack, and
the matching `pop` restores it. Unmatched `pop` emits a `-Wcpp` diagnostic.

`#pragma GCC diagnostic` and `#pragma clang diagnostic` are also accepted and
act identically — this allows headers that already use GCC-style pragmas to
suppress warnings without modification. Unlike `#pragma cccc diagnostic`, the
GCC/clang forms are passed through to `-G` output so downstream compilers also
see them.

Pragma state is per-token and takes effect immediately at the pragma's source
position, so it correctly suppresses warnings on variables declared or used
after the pragma, including the implicit-return warning at the end of a
function.

### Machine-Readable Output

Pass `--json` to emit one JSON object per diagnostic to
stderr instead of the human-readable format:

```json
{"severity":"warning","file":"foo.c","line":10,"column":5,"message":"unused variable 'x'","option":"-Wunused"}
{"severity":"error","file":"foo.c","line":20,"column":1,"message":"expected ';'","option":null}
```

Fields:

| Field | Type | Description |
|-------|------|-------------|
| `severity` | string | `"warning"` or `"error"` |
| `file` | string | Source file path |
| `line` | number | 1-based line number |
| `column` | number | 1-based column number |
| `message` | string | Diagnostic text |
| `option` | string or null | `-W` flag name, or `null` for hard errors |

The trailing summary line (`N warnings generated.`) is suppressed in JSON mode.

### Supported Warning Names

- `unused`
- `implicit-function-declaration`
- `implicit-int`
- `return-type`
- `shadow`
- `format` (see also `-F` / `--format-string-checks`)
- `conversion` (umbrella; enables `sign-conversion` and `float-conversion` too)
- `sign-conversion`
- `float-conversion`
- `sign-compare`
- `pointer-arith`
- `pedantic`
- `deprecated`
- `cpp`
- `extra-tokens`
- `large-file-embed`
- `cccc-macro`
- `comptime-block-leak` — warns when a `#pragma cccc comptime begin` block in an included header is left unclosed at EOF and is auto-closed (part of `-Wextra`)
- `ignored-features`
- `attributes` — general attribute-usage diagnostics, e.g. `sentinel`/`[[gnu::sentinel]]` applied to a non-variadic function ("sentinel attribute only applies to variadic functions")
- `nodiscard`
- `fallthrough`
- `strict-prototypes`
- `discarded-qualifiers`
- `null-dereference` — registered; no compile-time diagnostic emitted (covered by `-S2`/`-S3` runtime safety)
- `restrict` — registered; no compile-time diagnostic emitted (covered by runtime safety)
- `array-bounds` — registered; no compile-time diagnostic emitted (covered by `-S2`/`-S3` runtime safety)
- `stringop-overflow` — registered; no compile-time diagnostic emitted (covered by runtime safety)
- `stringop-truncation` — registered; no compile-time diagnostic emitted (covered by runtime safety)
- `duplicated-branches` — warns when the `then` and `else` bodies of an `if` statement are structurally identical
- `duplicated-cond` — warns when a condition is repeated in an `if`/`else if` chain
- `unused-value` — warns when an expression whose result is discarded has no side effects (e.g. `x + y;` as a statement)
- `multichar` — warns on multi-character character constants such as `'ab'` or `'abc'`
- `main` — warns on suspicious `main()` signatures: non-`int` return type, wrong parameter count (not 0 or 2), wrong first parameter type, or wrong second parameter type
- `switch-default` — warns when a `switch` statement has no `default:` label
- `switch-bool` — warns when the controlling expression of a `switch` has boolean type (`_Bool` / `bool`)
- `switch` — for a `switch` on an enum-typed condition: warns when an enumerator has no matching `case` (unless a `default:` is present), and warns when a `case` label's value doesn't match any enumerator of that enum
- `switch-enum` — like `switch`'s missing-enumerator check, but fires even when a `default:` is present
- `float-equal` — warns on direct `==` or `!=` comparisons between floating-point operands
- `shift-negative-value` — warns when the shift amount is a negative integer constant
- `shift-overflow` — warns when the shift amount equals or exceeds the promoted type's bit-width
- `logical-op` — warns when a constant expression appears as an operand of `&&` or `||`
- `tautological-compare` — warns on self-comparisons and unsigned range checks that are always true or false
- `sizeof-pointer-memaccess` — warns when `sizeof(pointer)` is passed as the size argument to `memset`, `memcpy`, `memmove`, or `memcmp`
- `incompatible-pointer-types` — warns on implicit pointer assignments or argument passing where the pointee types are incompatible (excluding `void *`); part of `-Wall`
- `cast-qual` — warns when an explicit cast removes `const`, `volatile`, or `restrict` from the pointed-to type
- `cast-align` — warns when an explicit cast increases the alignment requirement of the pointer target type
- `missing-prototypes` — warns when a non-static function is defined without a prior full prototype declaration
- `missing-declarations` — warns when a non-static, non-inline function is defined without any prior declaration
- `redundant-decls` — warns when the same name is declared more than once with the same linkage in the same scope; part of `-Wextra`
- `override-init` — warns when a later designator in a compound initializer overrides an earlier one (e.g. `{.x=1, .x=2}`); part of `-Wall`
- `unused-macros` — warns when a `#define` that is not in a system header is never expanded anywhere in the translation unit; standalone only (not part of `-Wall` or `-Wextra`)
- `nonnull` — warns when a null argument is passed to a parameter marked `nonnull`/`nonnull(N,...)`, or when null is returned from a function marked `returns_nonnull`. Catches both a literal/constant-folded null and, via a flow-sensitive pass, a value provably null on *every* live path that reaches the call through a local variable; part of `-Wall`. Also catches a call to a function whose whole-TU summary (see `maybe-nonnull` below) proves it returns null on *every* path — but only when `-Wmaybe-nonnull` is also passed, since that flag gates the interprocedural pass that discovers the fact in the first place
- `maybe-nonnull` — companion to `nonnull`: warns on a value that is null on only *some* live paths reaching a nonnull-marked parameter or `returns_nonnull` return (e.g. `int *p = 0; if (cond) p = &x; f(p);`), via real dataflow over `if`/ternary/`&&`/`||`, loops (a bounded back-edge fixpoint, with `break`/`continue` envs tracked and joined in at the right point), and `switch` (a per-case join against the switch's entry state, with `break` envs joined into the exit). A construct the fixpoint/join scheme can't safely model — a `goto`/label anywhere inside, a computed goto, or Duff's device (a `case` label reachable from a loop body without an intervening `switch` of its own) — falls back to the original conservative scheme (every local assigned anywhere inside is reset to unknown), so it never produces a false positive, only a possibly-missed warning. Also covers a limited interprocedural case: a whole-translation-unit summary, iterated to a fixpoint, flags a pointer-returning function with a provable null-returning path — either a literal `return 0;`/`return NULL;`, or a `return` of a call to another already-flagged function (a transitive chain of any depth, converging regardless of source order). A direct call to a flagged function is treated as maybe-null at its call sites — whether assigned to a local first or used inline as the argument/return expression itself (e.g. `handle(maybe_null())`, `return maybe_null();`) — unless the summary proved the callee null on *every* path, in which case it's reported under `nonnull` instead (see above). An unannotated external/declaration-only callee is never assumed to maybe-return null. Higher false-positive rate on real code than plain `nonnull`, so standalone only — not part of `-Wall` or `-Wextra`
- `sentinel` — warns on a call to a function marked `sentinel`/`sentinel(N)` whose expected trailing variadic argument is not a literal, pointer-typed `NULL` (`NULL`/`(void*)0`/`nullptr`); a literal but non-pointer-typed `0` gets a distinct "bare 0 is not a pointer" message, and a fully-missing terminator gets "missing sentinel in function call". Also warns if the call doesn't supply enough variadic arguments for the sentinel position to exist. Purely syntactic (no flow analysis — a variable holding `NULL` still warns); part of `-Wall`. Applying `sentinel` to a non-variadic function is a separate, declaration-time warning under `attributes` (see above), not this flag
- `designated-init` — warns on a positional member initializer (`{1, 2}`, or the positional tail of a mixed literal like `{.a=1, 2}`) targeting a struct type marked `__attribute__((designated_init))`/`[[gnu::designated_init]]`. Purely syntactic, parse-time-only; a brace-less copy-initializer (`struct S a = b;`) and C23 empty-init `{}` are never flagged. Unlike GCC, **standalone only — not part of `-Wall` or `-Wextra`**, since CCCC enables no warnings by default

`conversion` is an umbrella name: `-Wconversion` enables `sign-conversion` and
`float-conversion` as well as the integer-narrowing check.

`all` and `extra` are group names used by `-Wall`, `-Wno-all`, `-Wextra`, and
`-Wno-extra`.

---

## Profiling

Helper scripts and Makefile targets for CPU, memory, and VM opcode profiling
of CCCC.

### Prerequisites

- **hyperfine** — `brew install hyperfine` (macOS) or `cargo install hyperfine`
- **gperftools** — `brew install gperftools` (macOS) or `apt-get install google-perftools` (Linux)
  - Needed for CPU profiling with `libprofiler`

### Makefile Targets

```bash
make bench                    # Hyperfine benchmark on a representative test
make bench TEST=foo.c         # Benchmark a specific test file
make profile-cpu              # Profile compilation of a representative test
make profile-cpu TEST=foo.c   # Profile a specific test
make profile-mem              # Memory profile a representative test
make profile-mem TEST=foo.c   # Profile a specific test
```

CPU profiling produces `profile/cpu.prof` (raw gperftools profile data) and a
text summary. On macOS you can also open the result in `pprof` (Go tool) or
Instruments.

On macOS, `make profile-mem` uses `heap` (Xcode CLI tool) if available.
On Linux it uses `valgrind --tool=massif`.

### VM Opcode Profiling

```bash
./cccc --vm-profile -I./include tests/benchmarks/mandelbrot.c
./cccc --vm-profile --json -I./include tests/benchmarks/mandelbrot.c > profile/vm-opcodes/mandelbrot.json
./cccc -Y build/fib.c4
```

`--vm-profile` prints a compact dynamic opcode count table to stderr after the
program exits. Combine it with `--json` to also write the same data as JSON to
stdout. The JSON includes the execution mode (`source` or `c4`), selected
`--optimize` level, total VM cycles, total profiled opcodes, and per-op counts
and percentages.

Alongside the opcode table, both the stderr report and the JSON output always
include three CHKT3 type-shadow counters (see SAFETY.md's `--type-checks`
section): `shadow_sweeps`/`shadow_pages_swept` count how many times, and how
many 64 KiB shadow pages, the page-reclamation sweep has freed; `shadow_pages_live`
is the number of currently-allocated shadow pages across the heap and globals
segments, sampled at print time. All three are zero when `--type-checks` isn't
enabled.

### Static Bytecode Analysis

For understanding *static* instruction patterns in `.c4` files (independent of
any execution), cccc has two in-process analyses:

```bash
# Static n-gram mining on a pre-compiled .c4
./cccc -o /tmp/sieve.c4 -I./include tests/benchmarks/sieve.c
./cccc --ngrams=2 --ngrams-top=15 /tmp/sieve.c4
./cccc --ngrams=3 --ngrams-top=15 /tmp/sieve.c4
./cccc --ngrams=2 --ngrams-per-file /tmp/sieve.c4

# Same analysis directly on .c source — compiles in-process first
./cccc --ngrams=2 --ngrams-top=15 -I./include tests/benchmarks/sieve.c

# Use-def fusion candidate detection
./cccc --fusion-candidates=50 /tmp/sieve.c4
./cccc --fusion-candidates=50 --json /tmp/sieve.c4
```

`--ngrams[=N]` walks the text segment of one or more `.c4` files and ranks
2-grams (`N=2`, default) or 3-grams (`N=3`) by occurrence. `--ngrams-per-file`
also prints a per-input section in addition to the aggregate. `--ngrams-top=N`
limits the rows per section.

`--fusion-candidates[=N]` walks the text segment, tracks register defs/uses
per instruction, and reports adjacent `def -> use` pairs where the defining
instruction has a single reader. Add `--json` to get JSON output for scripting.

These two analyses are mutually exclusive with `--vm-profile*`, `-g/--debug`,
`-d/--disassemble`, `-c=native`, and any safety / execution / output flags.

`--ngrams`/`--fusion-candidates` with one or more prebuilt `.c4` inputs rejects
`--link` on the same command line — a prebuilt `.c4` has no fresh codegen output
in that process for the linker pass to resolve relocations against, so `--link`
there would otherwise be a silent no-op (same reasoning as `--link` against a
single prebuilt `.c4` in the run/`--testing`/`--disassemble` dispatch). Analysing
a **source** input still links normally: `--link lib.c4a` runs the same
compile-time linker pass first, and the analysis then walks the fully linked
text segment.

See [VM.md](VM.md) for how to combine the static counts with the dynamic bigram
profile to surface the strongest fusion candidates.

### Manual Profiling

#### Hyperfine

```bash
# Single test
hyperfine --warmup 3 './cccc -I./include tests/benchmarks/mandelbrot.c'

# Compare two versions
hyperfine --warmup 3 \
  -n 'main' './cccc -I./include tests/benchmarks/mandelbrot.c' \
  -n 'branch' './cccc-branch -I./include tests/benchmarks/mandelbrot.c'
```

#### macOS `sample` (built-in, no install needed)

```bash
./cccc -I./include tests/benchmarks/mandelbrot.c &
PID=$!
sample $PID -mayDie -file profile/sample.txt
```

#### macOS `heap` (built-in heap profiler)

```bash
heap -s -guessNonObjects ./cccc -I./include tests/benchmarks/mandelbrot.c
```

#### gperftools CPU profiler

```bash
make profile-cpu-build

CPUPROFILE=profile/out.prof ./cccc-prof -I./include tests/benchmarks/mandelbrot.c
CPUPROFILE_FREQUENCY=1000 ./cccc-prof -I./include tests/benchmarks/mandelbrot.c
```

### `tools/tests.py` Integration

```bash
python3 tools/tests.py --bench                        # Benchmark all tests
python3 tools/tests.py --bench --match "*compre*"     # Benchmark matching tests
python3 tools/tests.py --profile-cpu --match "*compre*"  # CPU profile matching tests
python3 tools/tests.py --profile-mem --match "*malloc*"  # Memory profile matching tests
python3 tools/tests.py --vm-profile --match "*profile*"  # VM opcode JSON profiles
python3 tools/tests.py --c4 --vm-profile --match "*profile*"  # Profile .c4 execution
```

`tools/tests.py --vm-profile` writes one JSON file per test under
`profile/vm-opcodes/`. In `--c4` mode it profiles the bytecode execution phase,
not the source-to-bytecode save step.

### Output Files

All profiling output is written to `profile/`:

| File | Tool | Content |
|------|------|---------|
| `profile/bench.json` | hyperfine | Benchmark results in JSON |
| `profile/cpu.prof` | gperftools | Raw CPU profile |
| `profile/cpu.txt` | gperftools | Text CPU profile summary |
| `profile/mem.massif` | valgrind | Memory allocation timeline (Linux) |
| `profile/vm-opcodes/*.json` | CCCC VM profiler | Dynamic opcode counts per test |
| `profile/bench-results/vm-profile-*/` | CCCC VM profiler | Opcode profiles for benchmark configs |

---

## Benchmarks

A focused cross-compiler benchmark suite that measures the cost of the **CCCC bytecode VM** by comparing it against **GCC** (across `-O0` through `-O3`). Every benchmark is plain C99/C11, so the comparison is apples-to-apples. The suite also times CCCC's precompiled-bytecode mode (`cccc-c4*`) so you can separate the bytecode VM cost from the source-to-bytecode compile cost.

For production builds, CCCC also offers a `-c=native` mode that hands macro-expanded C to `cc` / `clang` / `gcc` — that path bypasses the VM entirely and matches the `gcc*` columns below. The benchmarks in this document are deliberately scoped to the VM, because that is the part CCCC is responsible for.

### Quick Start

```bash
make bench-compare            # full run: 3 timed iterations per (bench, config), ~10 min
make bench-compare-quick      # 2 iterations, ~5 min, good for quick checks
python3 tools/bench.py --filter fib.c    # run a single benchmark
python3 tools/bench.py --no-c4 --filter fib.c   # skip the cccc-c4 columns
python3 tools/bench.py --filter fib.c --vm-profile   # also write opcode profile JSON
```

Sample output:

```
====================================================================================================
 CCCC vs GCC benchmark results (median ms, lower is better)
====================================================================================================
benchmark    cccc    cccc-O1  cccc-O2  cccc-O3  cccc-O4  cccc-c4  cccc-c4-O1  cccc-c4-O2  cccc-c4-O3  cccc-c4-O4  gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  ------  -------  -------  -------  -------  -------  ----------  ----------  ----------  ----------  ------  ------  ------  ------
ackermann    682.9   681.9    918.5    793.8    781.7    652.3    652.2       889.8       765.9       749.6       16.6    16.4    4.3     6.0   
binary_tree  868.6   859.5    963.1    824.1    809.9    821.1    823.5       922.9       788.2       777.2       20.6    20.3    19.4    22.9  
fib          573.7   597.4    674.6    580.0    577.3    550.2    543.8       651.5       545.8       550.3       7.5     8.8     4.1     7.3   
mandelbrot   6101.2  6065.3   4983.7   3970.9   3626.0   6064.4   6078.5      4992.8      3970.4      3610.3      62.0    30.4    29.4    28.4  
matrix_mul   5014.6  4997.7   4136.5   3747.4   3309.6   4980.4   5024.9      4131.1      3741.5      3318.6      24.6    5.7     4.0     3.8   
nqueens      1322.5  1234.3   1141.5   979.8    948.9    1297.3   1210.7      1121.7      953.7       921.3       14.4    5.1     8.1     9.2   
quicksort    1829.9  1772.4   1565.3   1498.8   1378.8   1808.9   1745.4      1531.0      1466.5      1343.3      18.5    9.1     12.5    13.0  
sieve        9660.6  9328.2   7331.3   7311.1   7075.3   9519.7   9295.3      7865.2      7292.1      7067.4      36.2    24.4    21.2    20.4  

Speedup vs gcc -O2 (>1.0x = slower than gcc -O2):
benchmark    cccc     cccc-O1  cccc-O2  cccc-O3  cccc-O4  cccc-c4  cccc-c4-O1  cccc-c4-O2  cccc-c4-O3  cccc-c4-O4  gcc-O0  gcc-O1  gcc-O2  gcc-O3
-----------  -------  -------  -------  -------  -------  -------  ----------  ----------  ----------  ----------  ------  ------  ------  ------
ackermann    158.4x   158.2x   213.0x   184.1x   181.3x   151.3x   151.3x      206.4x      177.6x      173.9x      3.9x    3.8x    1.0x    1.4x  
binary_tree  44.8x    44.3x    49.6x    42.5x    41.7x    42.3x    42.4x       47.6x       40.6x       40.1x       1.1x    1.0x    1.0x    1.2x  
fib          141.2x   147.1x   166.1x   142.8x   142.1x   135.4x   133.9x      160.4x      134.4x      135.5x      1.9x    2.2x    1.0x    1.8x  
mandelbrot   207.3x   206.1x   169.4x   135.0x   123.2x   206.1x   206.6x      169.7x      134.9x      122.7x      2.1x    1.0x    1.0x    0.97x 
matrix_mul   1265.2x  1260.9x  1043.6x  945.5x   835.0x   1256.6x  1267.8x     1042.3x     944.0x      837.3x      6.2x    1.4x    1.0x    0.97x 
nqueens      162.6x   151.8x   140.4x   120.5x   116.7x   159.5x   148.9x      137.9x      117.3x      113.3x      1.8x    0.63x   1.0x    1.1x  
quicksort    146.6x   142.0x   125.4x   120.1x   110.5x   144.9x   139.8x      122.7x      117.5x      107.6x      1.5x    0.73x   1.0x    1.0x  
sieve        455.0x   439.4x   345.3x   344.4x   333.3x   448.4x   437.8x      370.5x      343.5x      332.9x      1.7x    1.1x    1.0x    0.96x 
geomean      202.70x  199.76x  192.52x  170.37x  162.05x  197.64x  194.54x     190.65x     166.28x     158.25x     2.14x   1.27x   1.00x   1.15x 

Correctness: all benchmarks produce identical output across all configs
```

> **Note:** The `gcc*` columns use Homebrew GCC-15 (auto-detected by `bench.py` when the system `gcc` is Apple Clang). GCC-15 is substantially faster than Apple Clang on some workloads — notably `ackermann` (deep recursion) and `fib` — so the `×` ratios for those benchmarks are larger than they were when earlier runs used Clang. The `cccc*` absolute timings are directly comparable with older runs.

Key VM improvements reflected in these numbers:
- **#227 — inlined threaded dispatch**: opcode logic embedded directly at each computed-goto label (~1.2–1.7× on VM-bound workloads).
- **#250 — fused local load/store opcodes**: `LEA3+LDR/STR` two-opcode sequence replaced by a single `LDR_LOCAL_*`/`STR_LOCAL_*` (~23% geomean improvement).
- **#249 — scalar local promotion**: at `--optimize=2`+, hot eligible integer/pointer locals held in VM saved registers, flushed at exits — reduces repeated local load/store traffic in tight loops.
- **#251 — indexed load/store opcodes**: at `--optimize=2`+, `base + index * scale` patterns use `LDR_INDEX_*`/`STR_INDEX_*` fused opcodes — removes explicit MUL+ADD address calculation from array loops.
- **#261 — automatic opcode fusion**: at `--optimize=4` or with `--fuse-ops`, adjacent single-def/single-use arithmetic chains are rewritten to fused opcodes (`MULI3`, `MULADD3`, `MULADDI3`).
- **#415 — CSE for `[[gnu::const]]` + extended dead-call elimination**: at `--optimize=2`+, duplicate calls to const functions within a straight-line block are replaced by a register move. Dead-call elimination extended to indirect (CALLN) and FFI (CALLF) calls at `--optimize=1`+.
- **#461 — float/double local promotion**: at `--optimize=2`+, hot floating-point locals held in VM saved FP registers (`FREG_S0`–`FREG_S3`) — eliminates per-iteration `FLDR_LOCAL`/`FSTR_LOCAL` round-trips in FP-heavy loops. Notable improvement on mandelbrot (6412ms → 5034ms at `--optimize=3`).
- **#462 — fused FP multiply-add (`FMADD3`/`FMADD3_F32`)**: at `--optimize=4` or with `--fuse-ops`, adjacent `FMUL3+FADD3` chains are rewritten to a single `FMADD3` dispatch — one less opcode per multiply-accumulate iteration. Largest visible wins are matrix_mul (4007ms → 3444ms vs `--optimize=3`) and mandelbrot (5034ms → 4782ms). Add `--fma` to additionally enable single-rounding FMA (see correctness note below).
- **#478 — fused FP multiply-subtract (`FMSUB3`/`FMSUB3_F32`)**: extends the fusion pass to the minuend form (`a*b - c`) of `FMUL3+FSUB3` — emits `FMSUB3` when the multiply result is the left-hand operand of the subtraction.
- **#479 — fused negated multiply-subtract (`FNMSUB3`/`FNMSUB3_F32`) + dead-FMOV3 elimination**: adds the accumulating-subtract form (`sum -= a*b`, i.e. `rd = rs1 - rs2*rs3`) as `FNMSUB3`. The dead-FMOV3 elimination in copy-prop (sub-pass C global use-count scan) removes the promoted-register read copy that the float local promotion pass inserts between `FMUL3` and `FSUB3`, restoring adjacency so the fusion fires on loop patterns. Combined impact: mandelbrot `--optimize=3` 5034ms → 3990ms (−21%), `--optimize=4` 4782ms → 3643ms (−24%); matrix_mul `--optimize=4` 3444ms → 3325ms (−3%).

Validation run (2026-06-18, `--runs 2`, Homebrew GCC-15): all correctness checks passed. `--optimize=4` geomean is 162.05× slower than GCC-O2 (down from 182.61× before #479).

Re-run `make bench-compare` to get updated numbers for your machine.

JSON output is also written to `profile/bench-results/run-<UTC>.json` for tracking over time. Each `cccc-c4*` row includes a `compile_ms` field showing the one-time cost of producing the bytecode file (this cost is paid once, not in the timed median).

### The Benchmark Suite

All programs are portable C99/C11, exit with code `42` (so the standard `tools/tests.py` smoke-runs them for free), and print a single canonical `result: …` line on stdout. Each takes a single optional compile-time size via `-DBENCH_N=<value>` (default tuned for ~1-15s on `cccc` default).

| Benchmark | What it measures | Default size | Result |
|-----------|------------------|--------------|--------|
| `fib.c` | Recursive Fibonacci, call overhead, int math | `n = 30` | `result: 832040` |
| `sieve.c` | Sieve of Eratosthenes, array access, int math | `limit = 10,000,000` | prime count + sum |
| `nqueens.c` | 10-queens backtracking, branching, recursion | `N = 10` | `result: 724` |
| `matrix_mul.c` | 200×200 double matrix multiply, FP, cache | `N = 200` | `result: <checksum>` |
| `quicksort.c` | Quicksort 100k random ints, recursion, arrays | `N = 100,000` | sorted-array sum |
| `mandelbrot.c` | 400×400 mandelbrot, 200 iters, FP, branching | `400×400, 200` | total iter count |
| `binary_tree.c` | BST insert + inorder traversal, pointer chasing, malloc | `N = 100,000` | visit count + sum |
| `ackermann.c` | `ack(3, 8)`, deep recursion, stack pressure | `M=3, N=8` | `result: 2045` |

### How It Works

`tools/bench.py` does the following for each benchmark:

1. **Compile** the source with GCC at every optimization level (cached in `build/`).
2. **Compile** the source with CCCC at every `--optimize` level to a `.c4` bytecode file (cached in `build/`).
3. **Run** CCCC at every `--optimize` level on the source directly — this measures the full parse+execute cost.
4. **Run** the prebuilt `.c4` files — this measures just the bytecode VM cost.
5. **Run** the prebuilt GCC binaries.
6. **Time** each run with `time.perf_counter()`; discard `N` warmup runs, time `R` runs, take min/median/mean.
7. **Verify** that every config's stdout matches the CCCC reference. A mismatch is flagged and causes a non-zero exit.
8. **Report** as a human-readable table + a JSON file.

With `--vm-profile`, CCCC and CCCC-C4 configs also write dynamic opcode count
profiles to `profile/bench-results/vm-profile-<UTC>/`.

### What's Being Measured

- **`cccc*`** — end-to-end wall time: source on disk → bytecode compilation → VM startup → bytecode execution → exit.
- **`cccc-c4*`** — bytecode execution only: load a precompiled `.c4` from disk and run it. The compile cost is paid once (reported in `compile_ms`) and is not part of the timed median.
- **`gcc*`** — execution time of a prebuilt native binary.

The `cccc-c4*` columns are the cleanest apples-to-apples comparison with GCC: both are "compile once, run many times" measurements.

### Bytecode (.c4) Configs

```bash
./cccc --optimize=N -o build/fib.c4 tests/benchmarks/fib.c   # compile once
./cccc build/fib.c4                                    # run many times
```

The `.c4` files are cached in `build/` and rebuilt only when missing. The bytecode format self-resolves FFI symbols via `dlsym` on load, so `.c4` files built on one machine run on the same machine without bundling libc. Use `--no-c4` to skip these columns for faster iteration.

### Correctness

C11 leaves some leeway for floating-point contraction (FMA), which can produce bit-different results between `-O0` and `-O2`. To keep the comparison fair, `tools/bench.py` compiles GCC with `-ffp-contract=off -std=c11`. CCCC's default `FMADD3` opcode uses two separate roundings — semantically identical to the separate `FMUL3`+`FADD3` it replaces, so the benchmark outputs match GCC `-ffp-contract=off` exactly.

The optional `--fma` flag enables true single-rounding FMA. This can yield a few percent additional speedup on multiply-accumulate loops but **will diverge from GCC `-ffp-contract=off`** on inputs where the intermediate product has rounding error.

### Tips for Clean Numbers

- **Close other apps** to reduce noise.
- **Run multiple iterations** (`--runs 5` or more) for benchmarks under ~50ms.
- **Use `--filter`** to iterate on a single benchmark.
- **Use `--no-c4`** when iterating on parse/compile performance.
- **Use `--vm-profile`** when optimizing bytecode generation or VM dispatch.
- **Compare JSON files over time** — `profile/bench-results/run-*.json` includes compiler versions, host info, and run settings.

### Adding a New Benchmark

1. Drop a `<name>.c` in `tests/benchmarks/`.
2. The contract: plain C99/C11, optionally `#define BENCH_N <default>`, print `result: <value>`, `return 42`.
3. `python3 tools/bench.py --filter "<name>.c"` to verify it runs and matches GCC.
4. The standard `tools/tests.py` will pick it up automatically (exit code 42).

### Reading the Report

- **`median ms`** — middle value of the timed runs.
- **`min ms`** — fastest run; useful as a lower bound.
- **`stable`** — whether every run produced identical stdout.
- **`compile_ms`** — for `cccc-c4*` configs only, the one-time compile cost. Not part of the timed median.
- **Speedup vs gcc -O2** — `median_ms / median_gcc_O2_ms`. Above 1.0× means slower than gcc -O2.
- **`geomean`** — geometric mean of per-benchmark ratios; the right "overall" comparison number.

### Cross-Compiler Flag Notes

- `tools/bench.py` auto-detects when the system `gcc` is actually Apple Clang (on macOS) and switches to a Homebrew `gcc-15`/`gcc-14`/etc. if available. Pass `--gcc PATH` to override.
- Add clang to the matrix by editing `CCCC_CONFIGS` / `GCC_CONFIGS` in `tools/bench.py`.

---

## Fuzzing

Fuzzing harnesses and scripts for CCCC using AFL++ and libFuzzer.

### Quick Start

#### 1. Build the AFL++ instrumented binary

```bash
make afl
```

This produces `cccc-afl` in the project root, compiled with `afl-clang-fast`.

#### 2. Seed the corpus

```bash
make fuzz-seed
```

Copies the committed `tests/fuzz/corpus/` regression corpus and all
`tests/test_*.c` files into `fuzz/seeds/` as seed inputs.

#### 3. Run AFL++

```bash
make fuzz-run
```

Starts `afl-fuzz` in the background with sensible defaults:
- Input: `fuzz/seeds/`
- Output: `fuzz/out/`
- Timeout: 1000ms
- Memory: none (unlimited)
- Flags: `-I./include -c` (bytecode compile, no `main()` required)

#### 4. Inspect crashes

```bash
make fuzz-crashes    # list crash files
make fuzz-triage     # run each crash to see ASan/UBSan output
make fuzz-minimize   # minimize all crashes with afl-tmin
```

### Manual Usage

```bash
# Seed corpus from existing tests
cp tests/test_*.c fuzz/seeds/

# Run AFL++ (single instance)
afl-fuzz -i fuzz/seeds -o fuzz/out -m none -t 1000 -- ./cccc-afl -I./include -c @@

# Run with ASan + AFL++ (slower but catches more bugs)
make afl-asan
afl-fuzz -i fuzz/seeds -o fuzz/out -m none -t 1000 -- ./cccc-afl-asan -I./include -c @@

# Resume a stopped session
afl-fuzz -i - -o fuzz/out -m none -t 1000 -- ./cccc-afl -I./include -c @@
```

### Corpus Tips

- The existing `tests/` suite provides excellent seeds — they cover many C constructs.
- AFL++ will mutate these; even removing `main()` is fine because `-c`
  (bytecode compile) does not require an entry point.
- For deeper fuzzing, add hand-crafted seeds for edge cases:
  - Empty files
  - Very long identifiers
  - Deeply nested parentheses/braces
  - Unicode in comments/strings
  - Malformed preprocessor directives

### libFuzzer (optional)

A persistent-mode harness is available in `./src/fuzzing.c`.
Build and run it with:

```bash
make fuzz_harness
./fuzz_harness fuzz/seeds/
```

### Cleanup

```bash
make clean          # remove corpus, output, and crash directories
```

---

## System Headers

By default CCCC resolves all standard C library `#include` directives to its
own polyfill headers from `./include/`. This ensures portability and correct
VM-ABI types regardless of the host SDK. Three CLI flags let you point CCCC at
real macOS/Linux SDK headers instead:

### `--use-system-headers`

For standard headers that are **not** compiler-owned (see list below), search
`-isystem` / `--sysroot` directories **before** falling back to the CCCC
polyfill in `./include/`. Useful for testing CCCC against real SDK headers.

```bash
./cccc --use-system-headers -isystem /usr/include program.c
./cccc --use-system-headers --sysroot "$(xcrun --show-sdk-path)" program.c
```

### `--no-builtin-includes`

Combine with `--use-system-headers` to **disable the polyfill fallback** for
non-owned standard headers. The include fails with "cannot open file" if the
SDK copy is not present in any configured system include path. Compiler-owned
headers (see below) are still resolved from CCCC.

```bash
./cccc --use-system-headers --no-builtin-includes \
       --sysroot "$(xcrun --show-sdk-path)" program.c
```

### `--sysroot <path>`

Set the SDK root. Automatically adds `<path>/usr/include` (and
`<path>/usr/local/include` if present) to the system include path list and
implies `--use-system-headers`.

```bash
./cccc --sysroot "$(xcrun --show-sdk-path)" program.c
```

### Compiler-owned headers (never overridden)

These headers are tightly coupled to CCCC's VM ABI and are always resolved from
CCCC's own copies, even when `--use-system-headers` or `--no-builtin-includes`
is active:

| Header | Reason |
|--------|--------|
| `stdarg.h` | `va_list` layout matches CCCC's register-spill ABI |
| `setjmp.h` | `jmp_buf` layout matches CCCC's `SETJMP`/`LONGJMP` opcodes |
| `stdbool.h` | Authoritative C23 boolean type definitions |
| `stddef.h` | Authoritative `ptrdiff_t`, `size_t`, `nullptr_t` definitions |
| `stdint.h` | Authoritative fixed-width integer types |
| `inttypes.h` | Companion to `stdint.h` |
| `complex.h` | `creal`/`cimag`/`CMPLX` etc. lower to CCCC's `__cccc_*` builtins, not real `_Complex`-argument-passing ABI |
| `stdatomic.h` | `atomic_load`/`atomic_store`/`atomic_fetch_*` lower to CCCC's `__builtin_atomic_*` VM builtins |
| `stdckdint.h` | `ckd_add`/`ckd_sub`/`ckd_mul` lower to CCCC's checked-arithmetic VM builtins |

### Pragma suppression in system-header mode

When `--use-system-headers` is active (or a file is marked as a system header
via `is_system_header`), CCCC suppresses:

- "unknown pragma ignored" — e.g. `#pragma GCC system_header`,
  `#pragma clang assume_nonnull begin/end`
- "unknown warning option" — e.g. Clang-specific `-W` names in
  `#pragma clang diagnostic ignored`

These are common in real SDK headers and are informational hints to the native
compiler that have no meaning in CCCC's VM execution.

## Stdlib FFI Registration Audit

```bash
python3 tools/audit_ffi.py
```

Cross-checks every `cc_register_cfunc`/`cc_register_cfunc_ex` call in
`src/stdlib/*.c` against the declared signature of the wrapped function in
`include/*.h`, flagging `num_args`, `returns_double`, and `double_arg_mask`
mismatches, plus declarations that are never registered at all. Exists
because a handful of hand-typo'd registrations (wrong arg count or return
kind for functions with pointer out-params, like `frexp`/`modf`/`remquo`)
silently produced garbage at runtime with no test catching it. Run after
editing `include/*.h` or any `src/stdlib/*.c` registration table; exits
nonzero on any finding.

It's a regex-based scanner, not a real C parser: registrations wrapping a
local static helper (not itself declared in `include/`) are silently
skipped rather than flagged, so a clean run means "everything checkable was
consistent," not "everything is provably correct."
