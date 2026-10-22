# Macro Execution Model

How and when comptime functions run, and what they can see.

## Execution Model

CCCC supports two macro execution forms:

| Form | Source shape | When it runs | What the return value means |
|------|--------------|--------------|-----------------------------|
| Global generation | `[[cccc::comptime]] void gen(char *a1, ...)` called at file scope | Before the main parse | `NULL` / `void` return means side-effects only. An `ND_BLOCK` return splices its body declarations directly into file scope (see [Block return](macros-global.md#block-return-from-file-scope-macros)). Args are stringified into `char *` parameters. |
| Call-site expansion | `[[cccc::comptime]] Node *gen(Node *a1, ...)` called inside code | During macro expansion after parsing | Replaces the call expression or statement |
| Custom attribute | `@comptime(attribute("name")) void gen(AttrTarget *target, ...)` used by `@name` on a file-scope declaration or a struct/union member | During the main parse, after the target declaration is built | Return value is ignored; generated declarations are side effects. Attribute arguments are passed as `Node *` expression nodes. |

### Pre-parse macro declaration context

Global-generation macros compile and execute **before the main parse begins**.
The macro program has CCCC's private `reflection.h` API plus **demand-driven
access** to file-scope declarations from the preprocessed source: typedefs,
struct/union/enum tags (and their enumerators), function prototypes, `extern`
declarations, and declarations without function bodies. A function
**definition** resolves by its signature first, exactly as a plain prototype
would — but if comptime code goes on to actually *call* that function (or
take its address), the definition's whole body is pulled into the comptime
program too, so the call resolves. This works wherever the definition lives:
the primary file, another `.c` on the command line, or a file reached only
through a declaration-only header. A conventional `.h` (declarations) + `.c`
(definitions, compiled and linked separately) split is therefore usable for
comptime-callable code — see
[Comptime-callable code across files](#comptime-callable-code-across-files).
Nothing is forwarded up front — a declaration resolves into the comptime
program only when comptime code actually references its name (directly, via a
`Quote()`/`QuoteN()` template, or via a `reflection.h` lookup like
`GetType("Name")`/`VarRef("name")`/`FindGlobal("name")`), once, regardless of
how many places reference it. A function whose signature has resolved and
whose definition is among the input files then has its body forwarded too; if
that body cannot compile in the isolated comptime context it is dropped
silently unless comptime code actually calls the function, in which case the
call is a compile error.

This applies to declarations from **any non-system file** in the
translation unit — the primary source file, a header that defines
`[[cccc::comptime]]` code, or a plain declaration-only header with no
comptime code of its own. `#include` boundaries are not semantically
meaningful for declaration visibility: a comptime function can name a type
or prototype from any regular `#include`d header, the same as if it were
written in its own file. **System headers** (resolved via `<...>`, e.g.
`<glob.h>`) are the one exception — see below.

Referencing a name a comptime body never actually needs costs nothing: an
unused declaration, however large, is never parsed into the comptime
program. A struct that transitively names other types (a member of another
struct, a typedef chain) pulls in exactly its dependencies, resolved
recursively as each is needed.

The tag a declaration registers under (for `struct`/`union`/`enum` lookups)
is only the `struct|union|enum IDENT` in that declaration's own
specifiers, at the statement's top level — never one that merely appears
nested inside a function declarator's parameter list. `typedef int
(*fp)(struct S *);` and `int f(struct S *);` both register `fp`/`f` as the
name a comptime body can look up; `struct S` there is a reference, not this
statement's own tag (fixed in cccc #907).

The scoping above applies to **declarations and types** but **not
preprocessor `#define` macros**. The comptime pass starts with an isolated
macro state containing only CCCC builtins, command-line `-D` defines, and
any `#define @shared` opt-ins (see below). `#define`s from the main source
file and any included headers are **not** forwarded to comptime function
bodies otherwise. Use `@shared` routing on the `#include` to make a whole
header's `#define`s visible in both the runtime and comptime contexts, or
`@shared` on the `#define` itself to opt in a single macro in place:

```c
#define @shared AOT_MAX_SCOPE 64   // visible to both the runtime TU and comptime bodies
#define AOT_OTHER 1                // visible only to the runtime TU

[[cccc::comptime]]
Node *gen(void) {
    int floats[AOT_MAX_SCOPE];     // fine
    return MakeIntLiteral(0);
}
```

`[[cccc::shared]]` and `__attribute__((shared))` work identically to `@shared`
on `#define`, same as they do on `#include`. A `-D` command-line define
survives even if the source file later `#define`s the same name — the CLI
value is what the comptime pass sees.

This makes included types and prototypes visible to `[[cccc::comptime]]` helpers
used by global-generation macros. A function definition's signature is indexed
the same as a prototype; its body is parsed into the comptime program only if
comptime code actually calls it (see
[Comptime-callable code across files](#comptime-callable-code-across-files)).
File-scope macro calls are always skipped.

Names inside a `Quote()`/`QuoteN()` template resolve through this same
demand-driven index, at whatever point the template is parsed — which for a
template built by a **file-scope-called** comptime function (`gen();` at file
scope, as opposed to one only ever called from inside another function's
body) is *after* the comptime program's own parse has already finished. A
template built that way can still name a type, tag, enum constant, or
function the same as comptime source can — but not a plain file-scope
*variable* (see the initialized-global rule just below): by the time such a
template's tokens are parsed, the comptime program's data segment has
already been allocated, so there is no slot left to splice a variable's
storage into. That reference produces the usual `undefined variable`, not a
silent no-op.

An initialized global (an object, not a type or prototype) is narrower: it is
only resolved when declared directly in the **main source file**, and only
when its initializer is a self-contained constant — literals, punctuators,
`sizeof`/casts, no identifiers (so no function calls, no references to other
globals, no enum constants, no typedef-name casts):

```c
static const char *g_name = "widget"; // resolves -- literal initializer
static const int g_max = 64;          // resolves

[[cccc::comptime]]
Node *gen(void) {
    int n = (int)strlen(g_name) + g_max; // g_name, g_max both visible
    return MakeIntLiteral(n);
}
```

The comptime pass sees a *snapshot* of the initial value — writes to the
global inside a comptime body do not reach the runtime translation unit. An
uninitialized object (`static int counter;`) follows the same rule as types:
it resolves from the main source file or from a file that itself defines
`[[cccc::comptime]]` code, on demand. This asymmetry — types/prototypes
resolve from any non-system file, but *objects* only from the main file (or,
uninitialized, a comptime-defining file) — exists because an object is real
storage: the comptime program's data segment is allocated once, right after
its own declarations are parsed, so an object resolved any later (say, from
inside a `GetType`/`VarRef` call at comptime *execution* time) would have no
slot to read or write through. Referencing an initialized global that fails
this rule from a comptime body produces `undefined variable` with a hint
naming `[[cccc::comptime]]` variables (see
[Comptime Variables](macros-variables.md#comptime-variables)) or `#define @shared` as the
supported ways to make the value visible instead.

Reading a global that *does* satisfy the rule above from the comptime
body's own source does not create a lasting alias to it, either. Doing so
splices a copy of the object into the comptime program itself, with its own
storage in the comptime program's data segment — it is a snapshot, not a
window onto the runtime translation unit's copy. A `Quote()` template built
later by the same comptime function still cannot name that global: it now
resolves the comptime program's copy (the wrong one) rather than the
runtime one, and is rejected the same way as any other out-of-reach
variable reference, naming the comptime-copy relationship explicitly in the
error. There is no supported way to write into a runtime *defined* global
from a `Quote()` template by name; use the function's return value, an out
parameter, or `[[cccc::comptime]]` variables instead.

A declaration-only `extern` brought into scope with `#include @shared` is
the exception: it has no storage of its own in either program, so there is
no comptime-side copy to shadow. A `Quote()` template may name it freely —
codegen emits a plain symbol reference that the system linker resolves
against the definition in whichever translation unit or object provides it.
This is how a generated program refers to shared runtime constants (an
interned sentinel value, a lookup table) by name.

A `typedef` (or a struct/union/enum type definition) written directly inside a
`#pragma cccc comptime begin/end` region, or marked with `[[cccc::comptime]]`
itself, is passed through into the main source file's token stream unchanged —
it declares a type, not a comptime function or variable, so it needs no
comptime execution at all. It reaches the macro program the same way any other
typedef does: on demand, from any non-system file.

Note: global variable definitions whose scalar initializer expression contains a
`Node *`-returning comptime call (see
[Comptime functions in global initializers](macros-global.md#comptime-functions-in-global-initializers))
are the exception — those initializers are evaluated after the main parse completes,
once all symbols are in scope.

Pass `--comptime-include-all` to widen declaration resolution to **system
headers** too (e.g. `<glob.h>`'s `glob_t`, otherwise still only reachable via
`@shared`/`@comptime` routing) and to forward all `#define` macros to the
comptime pass. `#include [[cccc::comptime]]` and `#pragma cccc comptime
begin...end` blocks are always available regardless of this flag.

Referencing an identifier that's only defined via a runtime-TU `#define`
inside a comptime function body produces `undefined variable`, with a note
pointing at `#define @shared`, `@shared` include routing, `-D`, or
`--comptime-include-all` — the identifier is genuinely invisible at that
point, not a bug in ordinary preprocessing. A comptime compile that collects
any such error aborts cleanly (with diagnostics) rather than compiling and
executing the partial/invalid program.

### Comptime-callable code across files

A helper an ordinary program splits into a header of declarations and a `.c`
of definitions is callable from `[[cccc::comptime]]` code, provided the `.c`
is one of the source files handed to cccc in the same invocation. The
comptime pass indexes file-scope declarations across every input file, so a
`[[cccc::comptime]]` body can name `helper_double` from a plain header; when
the body generates a call to it, the definition's body is pulled in from
whichever input file defines it, and any functions *that* body calls are
pulled in the same way, transitively.

```c
/* helper.h — declaration only */
int helper_double(int n);
```

```c
/* helper.c — definition, a separate command-line input */
int helper_double(int n) { return n * 2; }
```

```c
/* main.c */
#include @comptime "helper.h"

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(helper_double(21))));
}
gen();

int main(void) { return f(); }   /* f() == 42, evaluated at compile time */
```

```
cccc main.c helper.c
```

The `helper.h` include can be `@comptime`, `@shared`, or a plain `#include`
whose declarations resolve on demand — routing only affects whether the
header's own `#define` macros and types also reach runtime code. What matters
is that `helper.c` is passed to cccc: a definition compiled to an object file
and linked separately is invisible to the comptime pass, and calling it
produces `undefined function in macro bytecode`. The same error is reported if
a forwarded body cannot be compiled in the isolated comptime context — for
instance it calls a third function the comptime pass has no body for, or uses
a `#define` macro that never reached comptime (see
[Include scoping](macros-helpers.md#include-scoping)) — and comptime code calls it anyway.

Forwarding is strictly demand-driven. A prototype that reaches the comptime
program through a `@shared` (or plain) header but is never named by any
comptime code — the usual case for a `@shared` header that also declares a
module's runtime-only functions — costs nothing: its definition is not
pulled in, so a body that could not compile in the isolated comptime context
is never even attempted. Only a function a comptime body actually calls (or
takes the address of), directly or transitively through another forwarded
body, has its definition forwarded.

A host libc or otherwise FFI-registered function is the exception to the
"definition must be an input" rule. It has no C body to forward, but the
comptime VM can dispatch it through the same FFI mechanism runtime code uses,
so comptime code may call it, take its address, and store it in a
function-pointer table — provided its *declaration* reaches the comptime pass
(an explicit prototype in the primary file, or `@comptime`/`@shared` routing
of the header that declares it; a plain `#include` does not route
declarations to comptime). An ordinary C function still needs its definition
among cccc's inputs.

### Macro isolation between comptime functions

All `[[cccc::comptime]]` function bodies are assembled into a single
compilation unit and preprocessed together. By default, each function body is
preprocessed in its own macro-table scope: a `#define`/`#undef` inside one
comptime function body is not visible to any other comptime function body.

```c
[[cccc::comptime]] int helper(void) {
#define LOCAL 1
    return LOCAL;
}

[[cccc::comptime]]
void generate(void) {
    // LOCAL is not visible here — isolated to helper()'s body.
}
```

`#define`s from `reflection.h`, `#include [[cccc::comptime]]` /
`#pragma cccc comptime begin...end` blocks, `@shared`-included headers, and
individual `#define @shared` macros are part of the macro table *before* any
comptime function body begins and remain visible to every comptime function
body. Primary-file `#define`s are **not** forwarded otherwise; place them in
an `@shared` header, or mark the individual `#define` itself `@shared`, to
opt them into the comptime context (see [Include scoping](macros-helpers.md#include-scoping)).

Pass `--allow-comptime-pp-bleed` to restore the pre-isolation behavior, where
all comptime function bodies share a single macro table and `#define`/`#undef`
in one body affects the others.

### Calling one comptime function from another

Because all `[[cccc::comptime]]` function bodies are assembled into a single
compilation unit (see above), a prototype for every comptime function is
emitted before any definition. This means mutual recursion between comptime
helper functions works without a forward declaration:

```c
[[cccc::comptime]]
int is_even(int n) {
    if (n == 0) return 1;
    return is_odd(n - 1);   // is_odd's prototype is already in scope
}

[[cccc::comptime]]
int is_odd(int n) {
    if (n == 0) return 0;
    return is_even(n - 1);
}
```

A bodyless `[[cccc::comptime]] ret name(params);` declaration is therefore
unnecessary and is treated as a no-op. If the name is never captured with an
attributed definition elsewhere in the file, that's flagged as a compile
error (`comptime function 'name' declared but never defined`) rather than
silently compiling nothing.

This is unrelated to forward-declaring *generated runtime* functions the
macro program emits into the output program (`PublishNode` / `MakeFunction`
promoting an existing forward declaration) — see
[Function Generation](macros-builders.md#function-generation).
