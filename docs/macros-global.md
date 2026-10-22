# Global Generation

File-scope comptime calls that emit functions, globals and types.

## Comptime functions in global initializers

A `Node *`-returning comptime function whose result is an integer or float
constant literal can initialize a **scalar** global variable:

```c
[[cccc::comptime]]
Node *ct_val(void) { return MakeIntLiteral(42); }

[[cccc::comptime]]
Node *ct_add(Node *a, Node *b) { return MakeBinary(NK_ADD, a, b); }

int G = ct_val();           // G == 42
int H = ct_add(20, 22);     // H == 42
float F = ct_val();         // F == 42.0
int I = ct_add(1, 1) + 40;  // arithmetic on macro result: I == 42
```

The macro call is deferred until after the main parse completes (during
`cc_expand_macros`), so `$symbol` forward-references in **other** comptime
functions are not affected.

### Scope limitations

- Only **integer and float scalar** initializers. Pointer-valued or struct-valued
  macro results are not supported; they produce a "not a compile-time constant"
  error just as before.
- `constexpr` variables cannot use macro-call initializers. Use a regular
  (non-constexpr) global variable instead.
- The macro must return `Node *`. An `int`- or `void`-returning comptime function
  in a global initializer is rejected with "expression-position comptime functions
  must return Node*".

## Comptime functions in constant expressions

A `Node *`-returning comptime function call is expanded where the parser needs
a constant: `case` labels (including GNU ranges), enumerator values,
`_Static_assert` conditions and file-scope array lengths.

```c
[[cccc::comptime]]
Node *PLUS(Node *a, Node *b) { return MakeBinary(NK_ADD, a, b); }

enum { A = PLUS(1, 2) };
int table[PLUS(2, 3)];
_Static_assert(PLUS(40, 2) == 42, "ok");

switch (v) {
    case PLUS(20, 2): ...
}
```

The call runs during the parse, so the comptime program is compiled at the
first such use. A block-scope array length such as `int v[PLUS(n, 1)]` stays a
runtime (VLA) length.

## Global Generation

Use a non-inline macro with a file-scope call when generated functions should be
available to the whole parsed program. The call runs before the main parse, and
CCCC automatically synthesizes forward declarations for every generated function
and `extern` declarations for every generated global variable, so no manual
publication is needed. This also holds for `-c=generated`/`-m` output: a
generated function's C text gets a forward-declared prototype ahead of any
other generated function that calls it, regardless of the order the functions
were created or published in, so mutual recursion between generated functions
compiles under a plain downstream `cc`. A `FunctionPrototype()` published but
never given a body still reaches the output as a bare declaration.

```c
[[cccc::comptime]]
void generate_answer(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("answer", int_ty);
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
}

generate_answer();

int main(void) {
    return answer();
}
```

### File-scope arguments and variadic macro tails

Global-generation macros accept fixed parameters plus an unbounded trailing
`...` tail. Each fixed argument's token sequence is stringified and delivered
as a `char *` to the matching parameter: string literals pass their raw value,
while keywords, identifiers, and numbers pass their spelling. The variadic tail
is available through `VarargCount()` and
`VarargStrAt(i)`.

```c
[[cccc::comptime]]
void gen_types(...) {
    for (int i = 0; i < VarargCount(); i++) {
        const char *name = VarargStrAt(i);
        Obj *fn = MakeFunction(name, GetType("int"));
        FunctionSetBody(fn, Quote("return 42;"));
    }
}

gen_types(type_int, type_float, type_double);
```

Use this for boilerplate enum conversion helpers, serializers, or any generated
definition that source code needs to call normally. Fixed parameters still use
ordinary C parameter names:

```c
[[cccc::comptime]]
void gen_prefixed(char *prefix, ...) {
    for (int i = 0; i < VarargCount(); i++) {
        const char *name = VarargStrAt(i);
        // prefix is the fixed string argument; name is each tail argument.
    }
}

gen_prefixed("api", read, write, close);
```

Fixed parameters beyond the first 8 are stack-spilled via the VM calling
convention. A trailing `...` can receive any number of additional call-site
arguments.

### Block return from file-scope macros

A file-scope macro can return an `ND_BLOCK` node to emit a bundle of related
declarations in one call. When CCCC sees a block return from a global-scope
macro, it **unwraps** the block and splices its declarations directly into the
file-scope token stream so they are parsed as top-level definitions.

```c
[[cccc::comptime]]
Node *emit_widget_helpers(void) {
    return Quote("{ struct Widget { int x; int y; }; void widget_init(struct Widget *w) { w->x = 0; w->y = 0; } }");
}

emit_widget_helpers();

int main(void) {
    struct Widget w;
    widget_init(&w);     // widget_init is visible as a global function
    return w.x == 0 ? 42 : 1;
}
```

After expansion, `struct Widget` and `widget_init` appear directly in the
global scope as if the user had written them verbatim. Any number of
declarations can be grouped in the returned block.

This approach complements the side-effect style (calling `MakeFunction` etc.):
- **Side-effect style** — better when the generated declarations depend on
  runtime-computed names or types.
- **Block-return style** — better when the declarations can be expressed as
  literal C source and returned from one place.

### Emit directives and includes in generated output

When using `-c=generated` to serialize a file, any preprocessor directives in
the **runtime translation unit** (outside comptime blocks) are automatically
captured verbatim and appear at the top of the generated output. This means
the common case requires no special annotation:

```c
#include <string.h>   // outside comptime — auto-captured into -c=generated output

#pragma cccc comptime
// ... generated functions that use strcmp ...
```

This auto-capture applies to any directive from the primary source file that
is not inside a `#pragma cccc comptime` block: `#include`, `#define`,
`#ifdef`/`#endif`, and others.

Auto-capture is limited to the **primary** input. When several files are
passed to `-c=generated` — typically because comptime code calls into ordinary
functions defined in separate `.c` modules, whose bodies CCCC forwards into the
comptime program — those extra modules contribute no runtime code to the
serialized output, so their own `#include`/`#define` lines are not replayed
either. If one of them has a directive that genuinely belongs in the generated
C, route it with `@emit` (or `@shared` for a header the generated code needs
in scope); a routed directive is emitted from any input file.

If you need the old behavior (emit only explicitly tagged content), pass
`--emit-only` alongside `-c=generated`. In that mode you must annotate each directive
with `[[cccc::emit]]`, `@emit`, or `__attribute__((emit))` to include it:

```c
#include [[cccc::emit]] <string.h>
#include @emit <stdint.h>
#include __attribute__((emit)) <stddef.h>
#ifdef @emit _WIN32
gen_windows_helpers();
#endif @emit
```

`[[cccc::emit]]` is still useful inside comptime blocks, where auto-capture
does not apply:

```c
#pragma cccc comptime begin
#include [[cccc::emit]] <platform.h>   // escapes from comptime into output
PublishNode(fn);
#pragma cccc comptime end
```

Multiple emit includes with the same emitted `#include` line are deduplicated.
Other emitted directives keep source order and are not deduplicated, so they can
wrap file-scope macro calls and the declarations those calls generate.

A plain, unconditional captured directive (an ordinary `#include`, not one
guarded by an `#ifdef @emit`/`#if @emit` shell) is guaranteed to appear ahead
of every generated declaration in the output, even if the source line that
produced it appears later in the file — so a generated global whose type
comes from an `#include @shared`d header always has that header's `#include`
in scope. A directive still inside a conditional shell keeps its original,
interleaved position instead. A conditional shell that CCCC's own
preprocessor has already emptied — for example an include guard whose only
`#define` was pre-empted by a command-line `-D` — is dropped from the output
entirely rather than replayed as a bare `#ifndef` / `#endif` pair; nested
empty shells are dropped too. If a generated global's type comes from a
header reached only through `#include @comptime`/`@build`/`@test` — routes
that are never replayed into `-c=generated` output at all — `cccc` reports a
compile error naming the type rather than writing C nothing declares it in;
route the header with `#include @shared` instead so it's captured too.

A translation unit whose comptime code publishes nothing and which defines
no ordinary function still serializes: `-c=generated` writes a valid,
near-empty file (the banner plus any auto-captured directives). Without
`-c=generated`, the same input has nothing to compile or run, so `cccc`
reports `input defines no functions or variables` and exits non-zero.

`--emit-only`/`[[cccc::emit]]` (above) controls *which* directives are
captured into `-c=generated` output. A separate, similarly-named flag, `--emit-cccc`,
controls a different axis entirely: *whether the captured/serialized output
keeps CCCC dialect syntax* (`[[cccc::...]]` attributes, checked-pointer
qualifiers, cccc-only `#include`s) or gets stripped to portable C, across
`-E`/`-m`/`-c=generated`/`-c=native`. See
[coverage.md](coverage.md#attributes) for the full `--emit-cccc` contract.

For several raw preprocessor directives, use an emit block. Emit blocks require
an enclosing comptime context and act as a runtime escape hatch: ordinary C
declarations in the block are compiled into the runtime translation unit and
are also copied into generated output.

```c
#pragma cccc comptime begin
#pragma cccc emit begin
#ifdef _WIN32
#define CCCC_PLATFORM_WINDOWS 1
#endif
int win_init(void);            // compiled into runtime TU and emitted
#pragma cccc emit end
#pragma cccc comptime end
```

Multiple `#pragma cccc emit begin...end` sub-blocks can appear inside a single
comptime block. Same-type nesting (emit inside emit, comptime inside comptime)
is a hard error.

Functions inside emit blocks can carry `[[cccc::test]]`, `[[cccc::build]]`,
`[[cccc::build_target]]`, `[[cccc::test_setup]]`, and `[[cccc::test_teardown]]`
attributes. CCCC scans the emitted token stream for these attributes and
registers the records exactly as it would for the same attributes in normal
source, enabling comptime emit blocks to generate test and build entries
programmatically:

```c
#pragma cccc comptime begin
#pragma cccc emit begin

[[cccc::test]]
void generated_test(void) { }    // registered and run under --testing

[[cccc::build]]
int generated_build(Builder *ctx) { return 42; }  // registered under --build

#pragma cccc emit end
#pragma cccc comptime end
```

Note: C preprocessor macros (such as `AssertEq`, `Executable`, `AddSource`)
are **not** expanded inside emit blocks — tokens are appended verbatim to the
output stream, bypassing macro expansion. Use the underlying `__builtin_*`
functions directly, or move assertions outside the emit block.

When building functions via the **AST API** (`MakeFunction` + `FunctionSetBody` +
`PublishNode`) rather than emit blocks, use `AddAttribute` to apply any attribute
to the generated function — mode attributes, standard C23/GNU attributes, and
custom `@attribute` handlers all work:

```c
[[cccc::comptime]]
void gen_tests(void) {
    Obj *fn = MakeFunction("my_generated_test", GetType("void"));
    WithFn(fn) { FunctionSetBody(fn, Quote("return;")); }
    PublishNode(fn);

    // Register as [[cccc::test]] with suite, display name, and timeout inline
    AddAttribute(fn, "cccc::test(suite=\"generated\", name=\"my test name\", timeout=5000)");
}
gen_tests();
```

For build entries:

```c
// Register as [[cccc::build]]
AddAttribute(fn, "cccc::build");

// Register as [[cccc::build_target(kind=native)]]
AddAttribute(fn, "cccc::build_target(kind=native)");
```

`MarkAsTest(fn)`, `MarkAsBuild(fn)`, and `MarkAsBuildTarget(fn, kind)` are
convenience shorthands for the common case — they expand to `AddAttribute` calls
internally. Use `AddAttribute` directly when you need to express test options
(suite, display name, timeout) or apply standard attributes such as `nodiscard`
or `noreturn` to the generated function.

`AddAttribute` also accepts standard C23 and GNU attributes:

```c
AddAttribute(fn, "nodiscard");
AddAttribute(fn, "nodiscard(\"check return value\")");
AddAttribute(fn, "__attribute__((noreturn))");
AddAttribute(fn, "@myattr");   // custom @attribute handler
```

All three attribute syntaxes are supported; the string is parsed through the same
pipeline as source-level attributes.

Macros can emit source-order directives while they run:

```c
[[cccc::comptime]]
void gen(void) {
    EmitDirective("#ifdef _WIN32");
    Obj *fn = MakeFunction("win_helper", GetType("int"));
    FunctionSetBody(fn, Quote("return 42;"));
    PublishNode(fn);
    EmitDirective("#endif");
}
```

Use `@comptime`, `[[cccc::comptime]]`, or `__attribute__((comptime))` after a
preprocessor directive keyword to route that directive to the comptime
compilation stream instead of runtime source:

```c
#define @comptime CT_VALUE 42
#ifdef @comptime CT_VALUE
#define @comptime CT_SEEN 1
#endif @comptime
```

As a shorthand, writing `@directive` (the `@` sign directly before the
directive keyword) routes the directive to the **opposite** context
automatically — no need to spell out `@comptime` or `@emit`:

| Written in | `@define FOO` expands to |
|---|---|
| Runtime code | `#define @comptime FOO` |
| Comptime block | `#define @emit FOO` |
| Emit block | `#define @comptime FOO` |

```c
@define CT_VALUE 42      // equivalent to: #define @comptime CT_VALUE 42
@ifdef CT_VALUE
@define CT_SEEN 1
@endif
```

The shorthand works with all preprocessor directives: `define`, `undef`, `if`,
`ifdef`, `ifndef`, `elif`, `else`, `endif`, `include`, `pragma`, `error`,
`warning`, and `line`.
