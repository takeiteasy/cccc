# CCCC Interactive Debugger

The CCCC VM includes an interactive, source-level debugger for step-by-step program execution and inspection.

**Enable with:** `-g` or `--debug` flags

When enabled, the debugger provides a powerful GDB-like interface for controlling program flow and inspecting state.

## Features

- **Source-Level Debugging**: The debugger maps bytecode instructions to their original source code locations with precise column tracking. When you step through the code, it displays the current file, line number, column number, and the corresponding source line, providing a seamless debugging experience. Column numbers are UTF-8 aware for correct positioning in multi-byte character source files.
- **Advanced Breakpoints**: Set breakpoints using multiple formats:
    - By line number in the current file (`break 42`).
    - By file and line number (`break test.c:42`).
    - At the entry point of a function (`break main`).
    - At a raw bytecode offset (legacy support).
- **Conditional Breakpoints**: Set breakpoints that only trigger when a specific condition is met. The expression can use local and global variables, arithmetic, comparison, logical operators, casts, assignments, member and pointer access, ternary expressions, comma expressions, and scalar integer/pointer direct function calls. Assignments and supported function calls are evaluated normally and can change program state.
    - Syntax: `break <location> if <expression>`
    - Example: `break 22 if x > 5`
    - Full function-call ABI support is not yet available in conditions: floating-point arguments or returns, struct/union returns, variadic calls, indirect calls, nested-function static links, and stack-passed FFI arguments are rejected with diagnostics.
- **Watchpoints (Data Breakpoints)**: Break execution when memory is read or written. Watchpoints can be set on variables by name or on raw memory addresses.
    - `watch <var|addr>`: Break on write.
    - `rwatch <addr>`: Break on read.
    - `awatch <addr>`: Break on read or write.
- **Execution Control**: Full control over program flow with commands to step into (`step`), step over (`next`), and step out of (`finish`) functions.
- **State Inspection**: Inspect VM registers, the call stack, and raw memory at any address. The debugger tracks local and global variable names, allowing them to be used in expressions.

## Debugger Commands

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

## Example Debugging Session

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

## Programmatic Break-in

When the debugger is enabled (`-g`), several mechanisms drop execution into the interactive REPL from within the running program.

### `__builtin_debugtrap()`

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

### `__builtin_trap()`

Like `__builtin_debugtrap()` but semantically unconditional: it is never expected to continue.  Under `-g` it still breaks into the REPL (you can continue), but in production builds it aborts.  Use it to guard unreachable paths that should never execute.

### `raise(SIGTRAP)` via VM signal handling

When a VM signal handler is registered for `SIGTRAP` and `-g` is active, `raise(SIGTRAP)` also enters the debugger REPL.  This matches the behaviour of native debugger trap instructions on AArch64 Apple platforms.

```c
#include <signal.h>

int main(void) {
    raise(SIGTRAP);   /* breaks into debugger REPL under -g */
    return 0;
}
```

## Auto-Debug-on-Crash

CCCC will automatically drop into the interactive debugger when a running
program hits a fatal VM error — a bad memory access, an out-of-bounds array
access, a stack overflow, an invalid indirect call/jump target, an unknown
instruction, or any other condition that would otherwise just print an error
and exit. This works the same way for normal program execution and for
compile-time (`#pragma comptime`) evaluation, since both run through the same
VM dispatch loop.

This is analogous to debugging-on-`SIGSEGV` in a native debugger: execution
stops with the faulting instruction still current, so registers, the call
stack, and local/global variables can be inspected exactly as they were at
the point of failure. Typing `continue` retries the faulting instruction (the
same way you'd continue past a breakpoint after fixing up state by hand);
typing `quit` exits.

**This only activates when both stdin and stdout are attached to a TTY** —
running CCCC under a pipe, redirect, or non-interactive harness always falls
back to printing the error and exiting, so scripted/batch usage is
unaffected.

**This traps VM-detected fatal errors, not raw host signals.** With the
default flags (no `-S`/`--safety`/`--pointer-sanitizer`/individual safety
flags), an out-of-bounds or NULL-pointer access in the guest program is not
checked by the VM and can fault as a genuine host `SIGSEGV`/`SIGBUS`, which
still terminates the process rather than dropping into the debugger. Enable
bounds/pointer safety checking (e.g. `--pointer-sanitizer`, `-S2`) to get
clean, debugger-trappable `NULL POINTER DEREFERENCE` / `USE-AFTER-FREE` /
`ALIGNMENT ERROR` diagnostics for those cases instead of a raw crash. Trapping
true host-level signals is tracked separately (see ticket #455).

You do not need to pass `-g`/`--debug` to get this behaviour — it is enabled
automatically for interactive sessions. Passing `-g` explicitly still works
as before (stop at the entry point of `main`), and additionally now also
traps on later fatal errors during the run.

**Disable with:** `--no-debug-on-crash`. Use this when invoking `cccc`
directly from an external test harness or script that happens to run
attached to a TTY (e.g. inside a `script`/`tmux` session) but should still
just see a printed error and a non-zero exit code on failure. CCCC's own
`--testing`/`--test`/`--test-suite` runners disable the auto-detection
internally for the same reason (they fork child processes that would
otherwise inherit the parent's TTY and hang waiting on debugger input).

## Source Map API

CCCC provides a programmatic API for accessing source location information, which is useful for building custom debugging tools or IDE integrations.

### Getting Source Location

```c
// Get source location for a given PC address
File *file = NULL;
int line_no = 0;
int col_no = 0;

if (cc_get_source_location(&vm, vm.pc, &file, &line_no, &col_no)) {
    printf("At %s:%d:%d\n", file->name, line_no, col_no);
}
```

### Exporting Source Maps

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

### Bytecode File Format

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
