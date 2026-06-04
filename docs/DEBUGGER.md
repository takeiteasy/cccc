# JCC Interactive Debugger

The JCC VM includes an interactive, source-level debugger for step-by-step program execution and inspection.

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
    - Full function-call ABI support is not yet available in conditions: floating-point arguments or returns, struct/union returns, variadic calls, indirect calls, nested-function static links, and stack-passed arguments are rejected with diagnostics.
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
$ ./jcc -g test_debugger_enhanced.c

========================================
    JCC Debugger
========================================
Starting at entry point...
Type 'help' for commands, 'c' to continue

(jcc-dbg) break 20            # Set breakpoint at line 20
Breakpoint #0 set at test_debugger_enhanced.c:20

(jcc-dbg) continue            # Run to breakpoint
Breakpoint #0 hit at test_debugger_enhanced.c:20
At test_debugger_enhanced.c:20:5
    20:     int x = 10;
0xc33400018 (offset 24): LEA -4

(jcc-dbg) watch x             # Watch for writes to variable 'x'
Watchpoint #0: watch x

(jcc-dbg) step                # Execute one instruction (the assignment to x)
Watchpoint #0 hit: write to x at 0x7ffeea28d3f8
Old value: 0
New value: 10
At test_debugger_enhanced.c:21:5
    21:     int y = factorial(4);

(jcc-dbg) stack 3             # Check the stack
=== Stack (top 3 entries) ===
  sp[0] = 0x000000000000000a  (10)
  ...

(jcc-dbg) continue            # Run to completion
```

## Source Map API

JCC provides a programmatic API for accessing source location information, which is useful for building custom debugging tools or IDE integrations.

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
[Magic: "JCC\0" (4 bytes)]
[Version: 4 (4 bytes)]
[Flags: JCCFlags bitfield (4 bytes)]
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

Floating-point bytecode has separate 64-bit and 32-bit memory operations:
`FLDR`/`FSTR` load and store `double`, while `FLDR_F32`/`FSTR_F32` load and
store C `float`. `FROUND_F32` rounds a floating register through `float`
precision while keeping the VM register file represented as `double`.
