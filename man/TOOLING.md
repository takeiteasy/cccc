# Developer Tooling

Reference for CCCC's interactive debugger, REPL, warning system, profiling, and fuzzing harnesses. Benchmark methodology/results live in [BENCHMARKS.md](BENCHMARKS.md); `--use-system-headers` and related header-resolution flags live in [HEADERS.md](HEADERS.md).

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
- **State Inspection**: Inspect VM registers, the call stack, and raw memory at any address. The debugger tracks local and global variable names, allowing them to be used in expressions. `print <variable>` resolves a live local or global by name and formats its value the same way the [Interactive REPL](#result-formatting)'s expression results are formatted -- scalars directly, and struct/union/array/vector values recursively, field by field, in lldb-style multi-line braces (both share the `cc_dump_value` formatter in `src/dump.c`).

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
| `print <var>` | `p <var>` | Print a variable's value, recursively formatting structs/unions/arrays/vectors. `print <hex_addr>` also works for a raw address (like `memory`, but by expression). |
| `help` | `h`, `?` | Show the help message. |
| `quit` | `q` | Exit the debugger and terminate the program. |

### `print` and live-stack address validation

`print`/`p` (and the recursive struct/union/array/vector formatting it uses)
shares the same pointer-validation check the REPL's
[result formatting](#result-formatting) relies on: every address is checked
against the VM's live text/data/heap/stack segments before being
dereferenced, so an uninitialized or garbage member prints as a bare hex
address instead of crashing the debugger session.

The stack half of that check used to validate against the wrong end of the
stack's reserved address range -- close to `stack_seg` itself, which never
actually holds committed data -- so it rejected every real local variable's
address. This only went unnoticed because the REPL, the check's original
caller, never dereferences a live stack address (its results always live in
the RETBUF pool or the data segment). It is fixed for `print`'s live-local
case: the check now validates against the stack's actual committed range,
which tracks growth as the program's call depth increases.

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

This pipeline is **VM only**. `__builtin_pc_function_name` and
`__builtin_pc_source_location` both lower to a call into a VM-internal FFI
shim that resolves a VM bytecode offset through the VM's own symbol/source-map
table — neither exists natively, so both are a hard compile error under
`-c=native`/`-m`/`-c=generated`. `__builtin_return_address` alone still
serializes and runs there, mapping to a real host return address instead of a
VM bytecode offset — see [NATIVE.md § Serialized-output
divergences](NATIVE.md#serialized-output-divergences).

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

#### Generated Code

Code produced by a comptime macro, an `@attr` handler, or file-scope
generation carries its own synthetic file/line (e.g. `<cccc macro:
label>:1:1` for `SyntheticToken`, or the macro's call-site location by
default) rather than a location in the real source file — there is no
line-for-line mapping back to a template the way a preprocessor macro
expansion has one. What generated code does carry is an **expansion
origin**: the chain of macro calls / attribute handlers / file-scope
generation that produced it, walked by the diagnostic printer to render
the `note: in expansion of ...` chain described in
[MACROS.md § Expansion backtrace](MACROS.md#expansion-backtrace). This is
compiler-diagnostics plumbing, not part of the source-map/PC-symbolization
API above — it isn't queryable through `cc_pc_to_source`/
`cc_get_source_location`, only surfaced on a compile error or warning.

#### Instruction Encoding

The VM text segment uses 32-bit instruction words. Operands that need 64 bits,
such as C integer immediates and text/data byte offsets, are encoded as two
consecutive 32-bit words. Direct branch and call targets are instruction
indexes, while C-visible function and label values are byte offsets from the
text segment base.

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

### Result formatting

Integers, `enum` (printed by value), `bool`, `float`/`double`, pointers (hex),
and `char *`/`char[]` (also printed as a string) are supported. Struct,
union, array, and vector results are formatted recursively, field by field,
in an lldb-style multi-line brace form:

```
cccc> struct Point { int x; int y; };
cccc> struct Outer { int id; struct Point pt; char *name; };
cccc> struct Outer o = {7, {1, 2}, "bob"};
cccc> o
(struct Outer) {
  id = 7
  pt = {
    x = 1
    y = 2
  }
  name = 0x1042 "bob"
}
```

Notes on the formatting rules:

- Each level of nesting indents by two spaces.
- A `char[]`/`char *` value prints as a double-quoted, C-escaped string,
  stopping at the first NUL byte (or the array length, whichever comes
  first).
- To keep large results readable, nested aggregates stop expanding past 8
  levels of depth (`{...}`), and arrays/vectors stop listing elements past
  32 (`...`).
- Bitfield members print their extracted (sign-extended if signed) integer
  value.
- Every pointer is validated against the VM's live text/data/heap/stack
  segments before being dereferenced (the same check the debugger's memory
  inspection commands use) -- an uninitialized or garbage pointer, including
  one nested inside a struct/union member, prints as a bare hex address
  instead of crashing the REPL.
- `_BitInt` values wider than 64 bits (returned via a multi-word buffer
  rather than a single register) and variably-modified (VLA) results are not
  formatted; both print a placeholder. In practice a VLA-typed result cannot
  occur at the REPL's top level, since a variably modified type at file
  scope is a compile error (see below).

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
  `--testing`, or any frontend/output mode
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
GCC/clang forms are passed through to `-c=generated` output so downstream compilers also
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
- `implicit-function-declaration` (warning-only at `--std=c89`/`gnu89`; a hard,
  unsuppressible error at C99 and later — CCCC's own default — and always
  under `-c=native` regardless of `--std=`; see below)
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
- `override-init` — warns when a later designator in a compound initializer overrides an earlier one (e.g. `{.x=1, .x=2}`), naming the overridden field; part of `-Wall`. For a union, this also fires when a *different* member's designator overrides one already set (e.g. `{.i=1, .f=2}`, since union members alias — matching gcc/clang), and covers a field reached through an anonymous or named struct/union member (e.g. `{.i=1, .i=2}` where `i` is a field of an anonymous union member, or `{.u.i=1, .u.i=2}` for a named nested union)
- `unused-macros` — warns when a `#define` that is not in a system header is never expanded anywhere in the translation unit; standalone only (not part of `-Wall` or `-Wextra`)
- `nonnull` — warns when a null argument is passed to a parameter marked `nonnull`/`nonnull(N,...)`, or when null is returned from a function marked `returns_nonnull`. Catches both a literal/constant-folded null and, via a flow-sensitive pass, a value provably null on *every* live path that reaches the call through a local variable; part of `-Wall`. Also catches a call to a function whose whole-TU summary (see `maybe-nonnull` below) proves it returns null on *every* path — but only when `-Wmaybe-nonnull` is also passed, since that flag gates the interprocedural pass that discovers the fact in the first place
- `maybe-nonnull` — companion to `nonnull`: warns on a value that is null on only *some* live paths reaching a nonnull-marked parameter or `returns_nonnull` return (e.g. `int *p = 0; if (cond) p = &x; f(p);`), via real dataflow over `if`/ternary/`&&`/`||`, loops (a bounded back-edge fixpoint, with `break`/`continue` envs tracked and joined in at the right point), and `switch` (a per-case join against the switch's entry state, with `break` envs joined into the exit). A construct the fixpoint/join scheme can't safely model — a `goto`/label anywhere inside, a computed goto, or Duff's device (a `case` label reachable from a loop body without an intervening `switch` of its own) — falls back to the original conservative scheme (every local assigned anywhere inside is reset to unknown), so it never produces a false positive, only a possibly-missed warning. Also covers a limited interprocedural case: a whole-translation-unit summary, iterated to a fixpoint, flags a pointer-returning function with a provable null-returning path — either a literal `return 0;`/`return NULL;`, or a `return` of a call to another already-flagged function (a transitive chain of any depth, converging regardless of source order). A direct call to a flagged function is treated as maybe-null at its call sites — whether assigned to a local first or used inline as the argument/return expression itself (e.g. `handle(maybe_null())`, `return maybe_null();`) — unless the summary proved the callee null on *every* path, in which case it's reported under `nonnull` instead (see above). An unannotated external/declaration-only callee is never assumed to maybe-return null. Higher false-positive rate on real code than plain `nonnull`, so standalone only — not part of `-Wall` or `-Wextra`
- `sentinel` — warns on a call to a function marked `sentinel`/`sentinel(N)` whose expected trailing variadic argument is not a literal, pointer-typed `NULL` (`NULL`/`(void*)0`/`nullptr`); a literal but non-pointer-typed `0` gets a distinct "bare 0 is not a pointer" message, and a fully-missing terminator gets "missing sentinel in function call". Also warns if the call doesn't supply enough variadic arguments for the sentinel position to exist. Purely syntactic (no flow analysis — a variable holding `NULL` still warns); part of `-Wall`. Applying `sentinel` to a non-variadic function is a separate, declaration-time warning under `attributes` (see above), not this flag
- `designated-init` — warns on a positional member initializer (`{1, 2}`, or the positional tail of a mixed literal like `{.a=1, 2}`) targeting a struct type marked `__attribute__((designated_init))`/`[[gnu::designated_init]]`. Purely syntactic, parse-time-only; a brace-less copy-initializer (`struct S a = b;`) and C23 empty-init `{}` are never flagged. Unlike GCC, **standalone only — not part of `-Wall` or `-Wextra`**, since CCCC enables no warnings by default
- `int-conversion` — warns on an implicit conversion between an integer and a pointer with no cast (e.g. `const char *p = 'a';` or `int n = some_ptr;`), covering assignment/scalar initialization, `return`, and prototyped call arguments. Suppressed when the source is the null pointer constant `0`, matching the standard exemption for `T *p = 0;`. Not checked for file-scope/global initializers (those take a separate constant-evaluation path). Part of `-Wall`
- `native-name-collision` — `-m`/`-c=native`/`-c=generated` only: warns when the serializer's rename passes (see [NATIVE.md § Serialized Output Divergences](NATIVE.md#serialized-output-divergences) for the full #1014/#1015/#1016 background) find a colliding name it cannot rename apart, so the generated C is left with a genuine collision for the host compiler to report. Currently covers one case: a header-exposed `enum`'s enumerator colliding with a plain file-scope identifier (a `static`, an `extern` global, or a function) declared in a translation unit that does not include that header — neither the enumerator (the replayed `#include` binds it textually) nor the Obj (renaming it would change an emitted symbol, or widen an existing "only rename dups" rule) can safely be renamed. Points at the colliding declaration and names the enumerator and the header that exposes it, so the user isn't left with only the host compiler's own diagnostic — which under `-c=native` names a temporary file that is deleted before the invocation returns. Part of `-Wall`
- `excess-init` — warns when a brace initializer supplies more elements than the target holds: a fixed-size array (`int a[1] = {1, 2, 3};`), a struct (`struct S s = {1, 2, 3};`, including the positional tail of a mixed literal past the last designator-reached member, e.g. `{.b = 1, 2}`), a GNU `vector_size` vector, or a string initializer too long for its destination array (`char c[3] = "abcd";`) — the last case names how many characters were supplied against how many were available, matching GCC's wording; an exact-fit string with the trailing NUL dropped (`char a[4] = "abcd";`) is legal C and never flagged. Warns once per initializer list, not once per surplus element (unlike GCC), so a nested excess (`int a[2][1] = {{1, 2}, {3}};`) warns once on the inner list that actually overflows. A flexible-size array/string (`int x[] = {...}`/`char c[] = "..."`) and a flexible array member are never flagged — their length comes from the initializer itself, so there is no fixed bound to exceed. Deliberately excludes VLA brace initialization (`int v[n] = {...}`): a VLA's bound isn't known until runtime, so excess elements there aren't statically checkable even in principle — the resulting out-of-bounds store is instead caught by the ordinary runtime bounds machinery at `-2`/`-3`. Part of `-Wall`

`implicit-function-declaration` is a hard error, not merely a warning, at
`--std=c99`/`c11`/`c17`/`c23` (and their `gnu*` variants) — matching ISO C99
6.5.2.2p1's constraint that a called function have a visible declaration, and
what every real host C compiler does at those standards. `-Wno-implicit-
function-declaration` has no effect there; it only silences the warning at
`--std=c89`/`gnu89`, where the call still resolves (as a variadic
`int f(...)`) exactly as it always has. Under `-c=native` it is always a hard
error, even at `--std=c89`: the guessed implicit signature is deliberately
never emitted into the generated C (it could collide with the real one from a
replayed header), so a real host compiler would reject the reference anyway
— CCCC now reports it as its own error up front instead (#1144).

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
```

`--vm-profile` prints a compact dynamic opcode count table to stderr after the
program exits. Combine it with `--json` to also write the same data as JSON to
stdout. The JSON includes the execution mode, total VM cycles, total profiled
opcodes, and per-op counts and percentages.

Alongside the opcode table, both the stderr report and the JSON output always
include three CHKT3 type-shadow counters (see SAFETY.md's `--type-checks`
section): `shadow_sweeps`/`shadow_pages_swept` count how many times, and how
many 64 KiB shadow pages, the page-reclamation sweep has freed; `shadow_pages_live`
is the number of currently-allocated shadow pages across the heap and globals
segments, sampled at print time. All three are zero when `--type-checks` isn't
enabled.

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
```

`tools/tests.py --vm-profile` writes one JSON file per test under
`profile/vm-opcodes/`.

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
- Flags: `-I./include -c=generated` (serialize-and-exit, no `main()` required, no host toolchain invoked)

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
afl-fuzz -i fuzz/seeds -o fuzz/out -m none -t 1000 -- ./cccc-afl -I./include -c=generated @@

# Run with ASan + AFL++ (slower but catches more bugs)
make afl-asan
afl-fuzz -i fuzz/seeds -o fuzz/out -m none -t 1000 -- ./cccc-afl-asan -I./include -c=generated @@

# Resume a stopped session
afl-fuzz -i - -o fuzz/out -m none -t 1000 -- ./cccc-afl -I./include -c=generated @@
```

### Corpus Tips

- The existing `tests/` suite provides excellent seeds — they cover many C constructs.
- AFL++ will mutate these; even removing `main()` is fine because
  `-c=generated` does not require an entry point.
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
