# Debugger

A GDB-like source-level debugger, started with `-g`.

The CCCC VM includes an interactive, source-level debugger for step-by-step
program execution and inspection, plus an auto-debug-on-crash mode and a
programmatic source-map API. For the top-level read-eval-print loop, see
[repl.md](repl.md).

**Enable with:** `-g` or `--debug` flags

When enabled, the debugger provides a powerful GDB-like interface for controlling program flow and inspecting state.

## Features

- **Source-Level Debugging**: The debugger maps bytecode instructions to their original source code locations with precise column tracking. When you step through the code, it displays the current file, line number, column number, and the corresponding source line, providing a seamless debugging experience. Column numbers are UTF-8 aware for correct positioning in multi-byte character source files.
- **Advanced Breakpoints**: Set breakpoints using multiple formats:
    - By line number in the current file (`break 42`).
    - By file and line number (`break test.c:42`).
    - At the entry point of a function (`break main`).
    - At a raw bytecode offset (legacy support).
- **Conditional Breakpoints**: Set breakpoints that only trigger when a specific condition is met. A condition is compiled -- not tree-walked -- into a small wrapper function the first time its breakpoint is hit, and that compiled wrapper is reused on every later hit (and shared by breakpoints in the same function with the same condition; deleting the last of them gives its code and string literals back, and a later condition reuses what was freed below newer code and data; a condition that stores outside its own locals, calls a session-compiled function or passes pointers to a C function not known to keep none keeps them), so it supports the full C expression grammar and the full call ABI: local and global variables, arithmetic, comparison, logical operators, casts, assignments, member and pointer access, ternary and comma expressions, and function calls with floating-point arguments/returns, struct/union returns, variadic calls, indirect calls, nested-function static links, and stack-passed arguments. Assignments and function calls are evaluated normally and can change program state (including the paused frame's own locals). A block literal (`^{ ... }`) can use the paused frame's locals; it copies them when the literal is evaluated, as in C. A block cannot capture an array or a `__block` local of the paused frame: the condition reports an error. A `__block` local, a variable-length array and a struct or union parameter read as in the source. The condition's controlling expression must have scalar type, exactly like a real `if` statement, and the whole text must be one expression: `x == 1 garbage` is an error.
    - Syntax: `break <location> if <expression>`
    - Example: `break 22 if x > 5`
    - Example: `break 22 if half(fx) > 2.0 && sum_point(pt) == 7`
- **Watchpoints (Data Breakpoints)**: Break execution when memory is read or written. Watchpoints can be set on variables by name or on raw memory addresses.
    - `watch <var|addr>`: Break on write.
    - `rwatch <addr>`: Break on read.
    - `awatch <addr>`: Break on read or write.
- **Execution Control**: Full control over program flow with commands to step into (`step`), step over (`next`), and step out of (`finish`) functions.
- **State Inspection**: Inspect VM registers, the call stack, and raw memory at any address. The debugger tracks local and global variable names, allowing them to be used in expressions. `print <variable>` resolves a live local or global by name and formats its value the same way the [REPL](repl.md#result-formatting)'s expression results are formatted -- scalars directly, and struct/union/array/vector values recursively, field by field, in lldb-style multi-line braces (both share the `cc_dump_value` formatter in `src/dump.c`).

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
| `backtrace` | `bt` | List the call frames, innermost first. |
| `frame [n]` | `fr [n]` | Select frame `n` (default: show the selected one). |
| `up` / `down` | | Select the caller / callee frame. |
| `return [-n] [expr]` | | Pop the selected frame as if it returned `expr`, running its `[[cleanup]]` handlers (`-n` skips them). |
| `restart [-n]` | | Re-enter the selected frame's function from the start, running the abandoned frame's `[[cleanup]]` handlers (`-n` skips them). |
| `fix <function>` | | Replace a function with the definition typed next, or edited in `$EDITOR`. |
| `reload <file>` | | Re-read a source file, redefining the functions whose bodies changed. |
| `registers` | `r` | Print all register values. |
| `stack [count]` | `st [count]` | Print the top `count` entries of the stack (default 10). |
| `disasm` | `dis` | Disassemble the current instruction. |
| `memory <addr>` | `m <addr>` | Inspect memory at a given address. |
| `print <var>` | `p <var>` | Print a variable's value, recursively formatting structs/unions/arrays/vectors. `print <hex_addr>` also works for a raw address (like `memory`, but by expression). |
| `help` | `h`, `?` | Show the help message. |
| `quit` | `q` | Exit the debugger and terminate the program. |

## Call frames

`bt` lists the frames of the paused program; `frame n`, `up` and `down` select
one, and `print` reads that frame's locals.

```
(cccc-dbg) bt
*#0  leaf at dbg.c:6
 #1  mid at dbg.c:10
 #2  main at dbg.c:13
(cccc-dbg) up
*#1  mid at dbg.c:10
(cccc-dbg) print t
t = (int) 5
```

`return` and `restart` act on the selected frame, and the debugger stops again
at the resulting pc.

| Command | Effect |
|---|---|
| `return 7` | Pops the frame (and every frame inside it); its caller sees `7`, converted to the function's return type. `expr` is evaluated in the frame's scope. |
| `return` | Pops the frame and leaves the return registers as they are. |
| `restart` | Pops the frames inside the selected one and re-enters its function with the arguments it was called with. Needs an explicit `-g`.[^snapshot] |
| `return -n`, `restart -n` | As above, without running `[[cleanup]]` handlers. |

Popping frames runs the `[[cleanup]]` handlers they still owe, innermost frame
first and latest declared first within a frame, as the frames' own exits
would.[^cleanup] A variable owes its handler from the end of its initializer
until its scope exits. `restart` runs the restarted frame's handlers too, since
it abandons that activation. If a handler fails, the command is rejected and
the program stays paused where it was.

```
(cccc-dbg) return -n 0    # pop leaf() without releasing what it holds
warning: skipped [[cleanup]] handlers in leaf
```

A function stopped at a `break <function>` has no frame yet: `restart` jumps to
its entry again (the newest body, if it was redefined), and `return` returns
from the call that is about to start.

```
(cccc-dbg) break leaf
(cccc-dbg) continue
(cccc-dbg) return 0       # leaf() yields 0 without running
```

Limits:

- Side effects the popped frames already caused (output, heap and global
  writes) stay.
- Handlers run without dangling-pointer checks (`-3`).
- A frame that replaced its caller through a tail call appears once.
- Frames stop at the root of the current run: `main`, a host callback or a
  signal handler.
- After a host signal the debugger is inspection-only and both commands are
  rejected.

[^snapshot]: Under `-g`, every function entry copies the argument registers
    (about 200 bytes), so `restart` never depends on parameters the body has
    since overwritten. Sessions that only enabled the debugger for crash
    trapping take no snapshots.
[^cleanup]: The compiler records, per function, the code ranges in which each
    cleanup variable is live, and the debugger looks up the pc each popped
    frame resumes at. A frame paused inside one of its own handlers does not
    run that handler, or any that ran before it, again.

## Edit and continue

`fix <function>` replaces a function while the program is paused. Type the new
definition on the lines that follow; the debugger takes it once the braces
balance. New calls, function pointers taken earlier and host callbacks run the
new body.

```
(cccc-dbg) break leaf
(cccc-dbg) continue
Breakpoint hit at PC 98
(cccc-dbg) fix leaf
  ... int leaf(int x) {
  ...   return x * 3 + g;
  ... }
leaf replaced; new calls run the new body
(cccc-dbg) restart
(cccc-dbg) continue
```

With `$VISUAL` or `$EDITOR` set and a terminal on stdin and stdout, `fix`
opens the editor on a temporary file holding the function's current source and
compiles it when the editor exits. After a compile error it asks `re-edit?
[Y/n]` and reopens the file with the edits kept. Saving an empty or unchanged
file cancels. Without an editor or terminal, `fix` reads the definition from
the lines that follow.

A frame already running the old body finishes in it. `restart` re-enters the
frame's function from the start, which runs the new body.

| Rule | Detail |
|---|---|
| Signature | Return type, parameters and `static` must match the old definition. |
| Scope | Functions of the last file only; the new source sees that file's declarations. |
| Source lines | The new body belongs to the file `<repl>`: `break <repl>:2`. |
| Breakpoints | A `break <function>` follows the new body. A `file:line` breakpoint inside the old body is removed with a message; set it again on `<repl>:line`. |
| Macros | The last file's macros expand in the new source. `#` directives are rejected, and a macro the file `#undef`s later is not available. |
| Statics | A block-scope `static` keeps its storage when the new body declares one of the same name and type. |

Embedders call `cc_debug_fix(vm, name, source)`.

## Reloading a file

`reload <file>` re-reads a source file of the paused program after it was
edited, and redefines every function whose body changed. It works across the
whole file, headers included.

```
(cccc-dbg) break step
(cccc-dbg) reload prog.c
Breakpoint #0 moved to PC 258
reloaded prog.c: 2 changed, 1 unchanged, 0 new
(cccc-dbg) continue
```

| Part of the file | After a reload |
|---|---|
| Changed function | Replaced as by `fix`: new calls run the new body; a frame already in the old body finishes in it. |
| Unchanged function | Not compiled again. Its code, breakpoints and line numbers stay as they were. |
| New function or global | Added, and callable by name; a global runs its initializer. |
| Global, file-scope `static`, block-scope `static` | Keep their storage and current value. An edited initializer is ignored. |
| Definition removed from the file | Stays, with a note. |

A function counts as changed when its expanded tokens differ, so editing a
macro or header it uses changes it too. Macros and `#include` guards are read
afresh from the file; the macros of the session are unaffected.

The reload is refused, with nothing changed, if the file

- does not parse, or is not a file of the program;
- changes a function's signature (return type, parameters, `static`);
- changes the layout of a struct, union, enum or typedef it defines, or the
  type of a global;
- changes the value of a `constexpr` object;
- has a comptime function or variable that does not compile.[^comptime-reload]

### Comptime code

A new or changed function may call a comptime function, in `reload` and in
`fix`: the call is expanded when the change is applied. `reload` also accepts
a file that adds or edits comptime code:

| Comptime part | After a reload |
|---|---|
| New function or variable | Added and initialized. |
| Changed function | Replaced. Its `static` locals that still match by name and type keep their values. |
| Variable with an edited declaration | Initialized again. |
| Unchanged variable | Keeps its value. |
| Function that calls comptime code, in the reloaded file | Expanded again, even when its own text is unchanged. |
| Function that calls comptime code, in another file | Keeps its old expansion; reload that file. |
| Comptime code removed from the file | Stays, with no note. |
| Comptime call at file scope (`gen();`) | Runs again.[^file-scope-calls] |

`fix` does not accept comptime definitions. Like `fix`, the command reaches the
last file's scope. Embedders call `cc_debug_reload(vm, path)`.

`cccc --watch` runs this reload on every save, without the debugger; see
[watch.md](watch.md).

## `print` and live-stack address validation

`print`/`p` (and the recursive struct/union/array/vector formatting it uses)
shares the same pointer-validation check the REPL's
[result formatting](repl.md#result-formatting) relies on: every address is checked
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

## Guide pages

| Page | Covers |
|---|---|
| [debugger-crash.md](debugger-crash.md) | Programmatic break-in, auto-debug-on-crash and host backtraces |
| [debugger-source-map.md](debugger-source-map.md) | The source-map API for IDEs and embedders |
