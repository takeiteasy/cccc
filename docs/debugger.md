# Debugger

A GDB-like source-level debugger, started with `-g` (or `--debug`).

It maps bytecode to source with UTF-8-aware columns, and also provides auto-debug-on-crash and a source-map API. For the read-eval-print loop see [repl.md](repl.md).

## Features

| Feature | Detail |
|---|---|
| Source-level | Shows file, line, column and the source line at each stop |
| Breakpoints | `break 42`, `break test.c:42`, `break main`, or a raw bytecode offset |
| Conditional breakpoints | `break 22 if x > 5` |
| Watchpoints | `watch <var\|addr>` (write), `rwatch <addr>` (read), `awatch <addr>` (either) |
| Execution control | `step`, `next`, `finish`, `continue` |
| Inspection | Registers, call stack, raw memory, locals and globals by name |
| Edit and continue | [`fix` and `reload`](debugger-edit.md) |

`print <variable>` resolves a live local or global and formats it like the [REPL](repl.md#result-formatting): scalars directly, structs, unions, arrays and vectors field by field in lldb-style braces. Every address is validated against the live text, data, heap and stack segments first, so a garbage member prints as a bare hex address rather than crashing the session.

### Conditions

A condition is compiled into a small wrapper function the first time its breakpoint hits, then reused (and shared by breakpoints in the same function with the same condition). It supports the full C expression grammar and call ABI, so this works:

```
break 22 if half(fx) > 2.0 && sum_point(pt) == 7
```

| Rule | Detail |
|---|---|
| Evaluation | Assignments and calls run normally and can change program state, including the paused frame's locals |
| Type | The controlling expression must be scalar, as in an `if`; the whole text must be one expression (`x == 1 garbage` is an error) |
| Blocks (`^{ ... }`) | May use the paused frame's locals, copied when evaluated; capturing an array or a `__block` local is an error |
| `__block` locals, VLAs, struct or union parameters | Read as in the source |

## Commands

| Command | Short | Description |
|---|---|---|
| `continue` | `c` | Run to the next breakpoint or watchpoint |
| `step` | `s` | Step into calls |
| `next` | `n` | Step over calls |
| `finish` | `f` | Run until the current function returns |
| `break <loc>` | `b` | Breakpoint at `<line>`, `<file:line>`, `<func>` or `<offset>` |
| `break <loc> if <expr>` | | Conditional breakpoint |
| `watch`, `rwatch`, `awatch` | `w` | Watchpoints |
| `info watch` | | List watchpoints |
| `delete <num>` | `d` | Delete a breakpoint |
| `list` | `l` | List breakpoints |
| `backtrace` | `bt` | Frames, innermost first |
| `frame [n]`, `up`, `down` | `fr` | Select a frame |
| `return [-n] [expr]` | | Pop the selected frame as if it returned `expr` |
| `restart [-n]` | | Re-enter the selected frame's function |
| `fix <function>` | | Replace a function ([debugger-edit.md](debugger-edit.md)) |
| `reload <file>` | | Re-read a source file ([debugger-edit.md](debugger-edit.md#reloading-a-file)) |
| `registers` | `r` | Print registers |
| `stack [count]` | `st` | Top `count` stack entries (default 10) |
| `disasm` | `dis` | Disassemble the current instruction |
| `memory <addr>` | `m` | Inspect memory |
| `print <var>` | `p` | Print a value; `print <hex_addr>` also works |
| `help` | `h`, `?` | Help |
| `quit` | `q` | Exit and terminate the program |

## Call frames

`bt` lists frames, `frame n`, `up` and `down` select one, and `print` reads that frame's locals.

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

`return` and `restart` act on the selected frame and stop again at the resulting pc.

| Command | Effect |
|---|---|
| `return 7` | Pops the frame and every frame inside it; the caller sees `7`, converted to the return type. `expr` is evaluated in the frame's scope. |
| `return` | Pops the frame and leaves the return registers as they are |
| `restart` | Pops the frames inside the selected one and re-enters its function with its original arguments. Needs an explicit `-g`[^snapshot] |
| `return -n`, `restart -n` | As above, without running `[[cleanup]]` handlers |

Popping frames runs the `[[cleanup]]` handlers they still owe, innermost frame first and latest declared first, as the frames' own exits would.[^cleanup] A variable owes its handler from the end of its initializer to scope exit; `restart` runs the restarted frame's handlers too. If a handler fails, the command is rejected and the program stays paused.

```
(cccc-dbg) return -n 0    # pop leaf() without releasing what it holds
warning: skipped [[cleanup]] handlers in leaf
```

A function stopped at `break <function>` has no frame yet: `restart` jumps to its entry again (the newest body, if redefined), and `return` returns from the call about to start.

```
(cccc-dbg) break leaf
(cccc-dbg) continue
(cccc-dbg) return 0       # leaf() yields 0 without running
```

| Limit | |
|---|---|
| Side effects of popped frames (output, heap and global writes) | Stay |
| Handlers | Run without dangling-pointer checks (`-3`) |
| A frame that replaced its caller by a tail call | Appears once |
| Frames | Stop at the root of the run: `main`, a host callback or a signal handler |
| After a host signal | The debugger is inspection-only; both commands are rejected |

## Example session

```
$ ./cccc -g test_debugger_enhanced.c
(cccc-dbg) break 20            # breakpoint at line 20
Breakpoint #0 set at test_debugger_enhanced.c:20

(cccc-dbg) continue
Breakpoint #0 hit at test_debugger_enhanced.c:20
    20:     int x = 10;

(cccc-dbg) watch x
Watchpoint #0: watch x

(cccc-dbg) step                # the assignment to x
Watchpoint #0 hit: write to x at 0x7ffeea28d3f8
Old value: 0
New value: 10

(cccc-dbg) stack 3
=== Stack (top 3 entries) ===
  sp[0] = 0x000000000000000a  (10)

(cccc-dbg) continue
```

## More

| Page | Covers |
|---|---|
| [debugger-edit.md](debugger-edit.md) | `fix` and `reload` |
| [debugger-crash.md](debugger-crash.md) | Programmatic break-in, auto-debug-on-crash and host backtraces |
| [debugger-source-map.md](debugger-source-map.md) | The source-map API for IDEs and embedders |

[^snapshot]: Under `-g` every function entry copies the argument registers (about 200 bytes), so `restart` never depends on parameters the body has since overwritten. Sessions that enabled the debugger only for crash trapping take no snapshots.
[^cleanup]: The compiler records, per function, the code ranges in which each cleanup variable is live, and the debugger looks up the pc each popped frame resumes at. A frame paused inside one of its own handlers does not run that handler, or any that ran before it, again.
