# Interactive REPL

CCCC includes an interactive top-level read-eval-print loop for typing C
declarations and expressions directly at a prompt, distinct from the
breakpoint-time [interactive debugger](DEBUGGER.md) (which
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

## How it works

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
  available at build time (see [Optional readline support](#optional-readline-support)
  below); otherwise input falls back to plain, non-editing line reads.

## Session commands

| Command | Description |
|---|---|
| `:help` | Show session commands. |
| `:type <expr>` | Print the type of `<expr>` without evaluating it. |
| `:load <file>` | Read declarations/expressions from `<file>`, one logical unit at a time. |
| `:quit` | Exit the REPL. |

## Result formatting

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

## Known limitations

- The REPL does not run the full preprocessor: `#include`, `#define`, and
  other directives are not available at the prompt (only tokenization and
  semantic parsing/type-checking run on each line).
- Every evaluated *expression* compiles a small one-shot wrapper function
  whose bytecode is not reclaimed after use, so a very long session slowly
  grows the VM's text/data segments. This does not affect
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

## Optional readline support

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
