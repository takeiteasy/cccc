# Debugging Macros

Diagnostics, expansion backtraces, `macroexpand` and profiling.

## Diagnostics And Debugging

Use source-located diagnostics when rejecting a macro argument:

```c
[[cccc::comptime]]
Node *require_nonzero(Node *value) {
    if (!value)
        MacroErrorAt(value, "expected an expression");
    return value;
}
```

Builder-created nodes automatically use the macro invocation as their source
location. When a diagnostic should point somewhere else, use the location
helpers explicitly:

```c
[[cccc::comptime]]
Node *checked_double(Node *value) {
    Node *expr = MakeBinary(NK_ADD, value, value);
    return CopyLocation(expr, value);
}
```

Use `SyntheticToken(label)` for diagnostics that belong to deliberately
generated code rather than the call site or an input expression:

```c
[[cccc::comptime]]
Node *generated_error(void) {
    Node *expr = MakeIntLiteral(0);
    SetToken(expr, SyntheticToken("generated expression"));
    MacroErrorAt(expr, "generated expression is invalid here");
    return expr;
}
```

AST dump helpers are available while developing macros:

| Helper | Use |
|--------|-----|
| `DumpTree(node)` | Print a readable tree |
| `DumpTreeToString(node)` | Render a tree into a string |
| `DumpAstGen(node)` | Print builder calls for a node |
| `DumpAstGenToString(node)` | Render builder calls into a string |

The interactive VM debugger (`-g`) does not currently stop inside macro
execution. Macro bytecode runs during compilation, before the final program is
started under the debugger, and CCCC suppresses VM debug tracing while invoking
macro functions. Use `$dump_*` helpers and source-located diagnostics for macro
debugging.

### Expansion backtrace

A diagnostic raised against generated code carries a note chain back to the
source that produced it, so a bug several macro/attribute frames deep does
not just point at an anonymous `<cccc macro: ...>` location:

```
<quote>:1: undefined_thing
           ^ error: undefined variable 'undefined_thing'
  note: in expansion of macro 'inner' at foo.c:12:5
  note: while handling attribute '@serialize' on 'struct Point' at foo.c:4:1
```

Each frame names one of: an ordinary macro call (`in expansion of macro
'NAME'`), an `@attr` handler running against its target decl (`while
handling attribute '@NAME' on 'DESC'`), or pre-parse file-scope generation
(`in file-scope generation by 'NAME'`) — reported innermost call first. The
chain is capped at 8 frames; a deeper stack (e.g. one that hit
`macro_recursion_limit`) prints a `... N more expansion frames` summary
line instead of the rest. Under `-j` (JSON diagnostics) the same chain is
the diagnostic object's `"notes"` array, one `{"file","line","column",
"message"}` object per frame.

The chain follows automatically from the location helpers above —
`SetToken`, `CopyLocation`, `SyntheticToken`, and the default
macro-invocation location every builder-created node gets — with no extra
call needed to opt in.

### macroexpand — macro expansion

Two functions are provided, matching Lisp's `macroexpand-1` / `macroexpand`
pair.

**`MacroExpand1(node)`** expands a single macro call node exactly once,
without splicing the result into the AST or recursing. Useful when you need
to observe one expansion step at a time or write meta-macros that inspect
intermediate forms.

**`MacroExpand(node)`** repeatedly calls `MacroExpand1` on the top-level
node until it is no longer a macro call (the form is *stable*). It does not
recurse into child nodes — only the outermost call is expanded. The VM's
`macro_recursion_limit` applies; exceeding it is a compile error.

```c
[[cccc::comptime]]
Node *make_answer(void) { return MakeIntLiteral(42); }

[[cccc::comptime]]
Node *wrap_answer(void) { return Quote("make_answer()"); }

[[cccc::comptime]]
void debug_macro(void) {
    Node *call = Quote("wrap_answer()");

    // One step: wrap_answer() -> make_answer() (still a macro call)
    Node *step1 = MacroExpand1(call);
    DumpTree(step1);

    // Full expansion: wrap_answer() -> make_answer() -> 42
    Node *full = MacroExpand(call);
    DumpTree(full);
}
```

If `node` is not a macro call, both functions return `node` unchanged
(identity). If the named macro is not found or has not compiled, `node` is
returned unchanged.

Underlying functions: `__builtin_macroexpand_1(Node *node)` and
`__builtin_macroexpand(Node *node)`.

### Profiling comptime code

`--vm-profile` counts the VM opcodes that comptime code executes and prints
them as a separate section before the program's own profile. It works in
every compile mode:

| Mode | Report |
|------|--------|
| Default (compile and run) | Comptime section, then the run section |
| `-c=native`, `-c=generated`, `-m` | Comptime section only |

```sh
cccc --vm-profile -c=native -o app app.c
```

```text
VM comptime opcode profile
total_opcodes: 7
LDR_LOCAL_D             2  28.57%
ZX4                     1  14.29%
CALLF                   1  14.29%
...
```

With `--json`, the comptime profile is a nested `"comptime"` object.[^profile-json]

[^profile-json]: In the default mode the JSON goes to stdout, with the run
profile's keys at the top level. Under `-c=native`, `-c=generated` and `-m`,
which may write C to stdout, the JSON replaces the text report on stderr and
holds only `"comptime"`. `--json` also prints diagnostics as JSON on stderr, so
a compile with warnings puts more than one document there.
