# CCCC Compile-Time Macros

Compile-time macros are C functions that CCCC compiles and runs during
compilation. They can inspect compile-time types, build AST nodes, generate
functions and global variables, and replace macro call sites with generated code.

A macro function is declared by annotating it with `[[cccc::comptime]]` (C23
attribute syntax), `__attribute__((comptime))` (GNU attribute syntax), or the
concise `@comptime` shorthand. The `__comptime` and `__comptime__` keyword
aliases are accepted as the same CCCC-specific marker. All forms are equivalent
and accepted
everywhere:

```c
// All forms are identical:
[[cccc::comptime]]       int helper(int n) { return n * 2; }
__attribute__((comptime)) int helper(int n) { return n * 2; }
@comptime                int helper(int n) { return n * 2; }
__comptime               int helper(int n) { return n * 2; }

// Inline variant (usable in expression position):
[[cccc::comptime(inline)]]  Node *make_val(void) { return MakeIntLiteral(42); }
@comptime(inline)           Node *make_val(void) { return MakeIntLiteral(42); }
__comptime__(inline)        Node *make_val(void) { return MakeIntLiteral(42); }
```

The `@macro(inline)`, `__macro`, and `__macro__` spellings also work for the
deprecated `[[cccc::macro]]` alias.

The macro API is private to macro compilation. CCCC embeds its own `cccc/reflection.h`
and injects it automatically while macro and comptime helper functions are
compiled, but that bundled header is not on the public include path. Macro code
can use the `$*`, `Quote*`, `MacroErrorAt`, `Gensym`, and `$dump_*`
convenience macros directly.

## Return-Value Model

A macro's return value is **the node spliced at the call site**, replacing the
invocation. Top-level definitions — functions created with `MakeFunction()`,
globals with `GlobalVar()` — are **side effects** injected regardless of
what the macro returns. How generated names become visible to the parser depends
on which execution form you use.

| Call context | Return value |
|--------------|--------------|
| Expression position (`int x = mac()`) | Must return a non-NULL `Node *`. NULL is a compile error. |
| Declaration position (file-scope `mac();`) | Returning NULL or `void` is legal — means "I only emitted definitions." |

For definition-only macros, declare the return type `void`. This is
self-documenting and lets you omit the return statement entirely. Using a `void`
macro in expression position is a compile error.

```c
[[cccc::comptime]]
void emit_helpers(void) {
    // build functions, globals — no return needed
}
emit_helpers();
```

`Node *` macros may still return NULL in declaration position without error. The
old `return MakeIntLiteral(0)` idiom still works but is no longer needed.

## Execution Model

CCCC supports two macro execution forms:

| Form | Source shape | When it runs | What the return value means |
|------|--------------|--------------|-----------------------------|
| Global generation | `[[cccc::comptime]] void gen(char *a1, ...)` called at file scope | Before the main parse | `NULL` / `void` return means side-effects only. An `ND_BLOCK` return splices its body declarations directly into file scope (see [Block return](#block-return-from-file-scope-macros)). Args are stringified into `char *` parameters. |
| Call-site expansion | `[[cccc::comptime(inline)]] Node *gen(Node *a1, ...)` called inside code | During macro expansion after parsing | Replaces the call expression or statement |
| Custom attribute | `@macro(attribute("name")) void gen(AttrTarget *target, ...)` used by `@name` on a file-scope declaration | During the main parse, after the target declaration is built | Return value is ignored; generated declarations are side effects. Attribute arguments are passed as `Node *` expression nodes. |

### Pre-parse macro declaration context

Global-generation macros compile and execute **before the main parse begins**.
The macro program has CCCC's private `reflection.h` API plus a conservative
snapshot of safe file-scope declarations from the preprocessed source. That
snapshot includes typedefs, struct/union/enum tag declarations, function
prototypes, `extern` declarations, and declarations without function bodies.

This makes included types and prototypes visible to `[[cccc::comptime]]` helpers
used by global-generation macros, but it does not compile arbitrary non-macro
program definitions into the macro VM. Function bodies, file-scope macro calls,
and initialized global definitions are skipped.

To prevent declarations from regular `#include`d headers from leaking into the
comptime pass, pass `--strict-comptime-includes`. Only the main source file's own
file-scope declarations are forwarded; `#include [[cccc::comptime]]` and
`#pragma cccc comptime begin...end` blocks are unaffected.

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

`#define`s from the main source file, `reflection.h`, `#include [[cccc::comptime]]`
/ `#pragma cccc comptime begin...end` blocks, and comptime variable declarations
are all part of the macro table *before* any comptime function body begins, so
they remain visible to every comptime function body.

Pass `--allow-comptime-pp-bleed` to restore the pre-isolation behavior, where
all comptime function bodies share a single macro table and `#define`/`#undef`
in one body affects the others.

### Custom Attributes

Comptime macros can register declaration attributes for file-scope declarations:

```c
@macro(attribute("serialize"))
void define_serializer(AttrTarget *target) {
    Type *ty = $ATTR_TARGET_TYPE(target);
    const char *name = AttrTargetName(target);

    Obj *fn = MakeFunction("serialize_Point", GetType("int"));
    FunctionAddParam(fn, "p", MakePointer(ty));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("return sizeof(struct Point);"));
    }
    PublishNode(fn);
}

@serialize
struct Point { int x; int y; };
```

The handler's first parameter is always `AttrTarget *`. Fixed parameters
after that receive attribute arguments as `Node *` expression nodes, so
`@answer(123)` calls a handler shaped like:

```c
@macro(attribute("answer"))
void answer_attr(AttrTarget *target, Node *value) { ... }
```

Variadic attribute handlers use the same `$ast_vararg_*` helpers as inline
macros. The target helpers are:

| Helper | Description |
|---|---|
| `GetAttrTargetKind(target)` | `ATTR_TARGET_TYPE`, `ATTR_TARGET_FUNCTION`, `ATTR_TARGET_GLOBAL`, or `ATTR_TARGET_TYPEDEF` |
| `AttrTargetName(target)` | Source name when available |
| `$ATTR_TARGET_TYPE(target)` | Target type |
| `AttrTargetObj(target)` | Function/global object, or `NULL` for type/typedef targets |
| `AttrTargetToken(target)` | Source token for diagnostics |

Custom attributes are v1 file-scope features. Applying one to a local variable
or struct member is a compile error.

## Global Generation

Use a non-inline macro with a file-scope call when generated functions should be
available to the whole parsed program. The call runs before the main parse, and
CCCC automatically synthesizes forward declarations for every generated function
and `extern` declarations for every generated global variable, so no manual
publication is needed.

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

When using `-G` to serialize a file, any preprocessor directives in the
**runtime translation unit** (outside comptime blocks) are automatically
captured verbatim and appear at the top of the generated output. This means
the common case requires no special annotation:

```c
#include <string.h>   // outside comptime — auto-captured into -G output

#pragma cccc comptime
// ... generated functions that use strcmp ...
```

This auto-capture applies to any directive from the primary source file that
is not inside a `#pragma cccc comptime` block: `#include`, `#define`,
`#ifdef`/`#endif`, and others.

If you need the old behavior (emit only explicitly tagged content), pass
`--emit-only` alongside `-G`. In that mode you must annotate each directive
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

## Call-Site Expansion

Use an inline macro for call-site expansion inside expressions or statements.
The call is replaced with the returned AST during macro expansion. Inline macros
**must** return a non-NULL node and cannot be used at file scope.

Two equivalent spellings are accepted:

```c
// Attribute argument form
[[cccc::comptime(inline)]]
Node *double_it(Node *value) {
    return MakeBinary(NK_ADD, value, value);
}

// C keyword form (preferred)
[[cccc::comptime]]
inline Node *double_it(Node *value) {
    return MakeBinary(NK_ADD, value, value);
}
```

```c
[[cccc::comptime(inline)]]
Node *double_it(Node *value) {
    return MakeBinary(NK_ADD, value, value);
}

int main(void) {
    int x = double_it(21);
    return x;
}
```

Macro arguments are `Node *` pointers to the original argument ASTs. A macro
can reuse, inspect, wrap, or replace those nodes.

Inline macros also support a trailing `...` parameter. Fixed arguments are
passed as normal `Node *` parameters, and the variadic tail is available
through `VarargCount()`, `VarargAt(i)`, and
`VarargAsArray()`. A macro with only `...` is valid:

```c
[[cccc::comptime(inline)]]
Node *sum_all(...) {
    Node *acc = VarargAt(0);
    for (int i = 1; i < VarargCount(); i++)
        acc = MakeBinary(NK_ADD, acc, VarargAt(i));
    return acc;
}

int x = sum_all(1, 2, 3, 4);
```

Use `VarargAsArray()` when forwarding the tail to an array-form builder
such as `MakeFuncCall(callee, args, n)`:

```c
[[cccc::comptime(inline)]]
Node *forward_call(Node *fn_node, ...) {
    return MakeFuncCall(fn_node,
                    VarargAsArray(),
                    VarargCount());
}

int x = forward_call(target_fn, 1, 2, 3);
```

`VarargAsArray()` returns a borrowed read-only `Node **` slice that
is valid only for the current macro call. It returns `NULL` when the variadic
tail is empty. Forwarding shares AST nodes; it does not consume or clone them.
Static macro-to-macro forwarding is handled with `Quote` and splice syntax.
Dynamic macro-call construction is not part of this API.

`VarargAt(i)` and `VarargStrAt(i)` use zero-based indexes.
Variadic helpers report a compile-time error when an index is negative, out of
range, or the helper is used with the wrong macro execution form.

Call-site expansion happens after the containing function body has already been
parsed. If an inline macro creates a separate top-level function or global, the
same parsed code can name that object only if normal C declaration rules are
satisfied. Use a global-generation macro when you want CCCC to publish generated
declarations automatically.

Comptime functions can use ordinary static data tables for lookup work,
including pointer arrays and struct arrays that point at string literals or
other static data:

```c
[[cccc::comptime]]
const char *lookup(const char *name) {
    static const char *headers[] = {"stdio.h", "string.h"};
    return strcmp(name, headers[0]) == 0 ? headers[0] : 0;
}
```

Static initializer tables can also store pointers to other comptime functions.
Entries resolve to callable macro VM function addresses:

```c
[[cccc::comptime]]
int handler_a(void) { return 10; }

[[cccc::comptime]]
int handler_b(void) { return 20; }

[[cccc::comptime]]
int dispatch(int i) {
    static int (*const table[])(void) = {handler_a, handler_b};
    return table[i]();
}
```

Macro expansion is bounded by `--macro-recursion-limit=N` to catch accidental
self-recursive expansions. The default is 256. Set the limit to 0 to disable
the check.

Statement macros work the same way. Return a statement node such as
`MakeReturn(...)`, `MakeIf(...)`, `MakeBlock(...)`, or a statement parsed with
`Quote(...)`.

```c
[[cccc::comptime]]
Node *return_if_zero(Node *value) {
    return Quote("if ($1 == 0) return 0;", value);
}

int main(void) {
    int x = 0;
    return_if_zero(x);
    return 1;
}
```

## Comptime Helpers

All `[[cccc::comptime]]` functions are entry-callable from user code. A plain
helper that is only called by other comptime functions still uses the same
attribute — the distinction is just whether you call it from user code or not.

```c
[[cccc::comptime]]
int plus_one(int n) {
    return n + 1;
}

[[cccc::comptime]]
Node *make_value(void) {
    return MakeIntLiteral(plus_one(41));
}

int main(void) {
    return make_value();
}
```

All comptime functions are compiled together in a single pass, so they can call
each other even when the callee appears later in the translation unit.

The GNU-attribute equivalent is `__attribute__((comptime))`.

> **Deprecated:** `[[cccc::macro]]` and `__attribute__((macro))` are accepted as
> aliases for `[[cccc::comptime]]` for backwards compatibility.

### Comptime block

`#pragma cccc comptime begin` / `#pragma cccc comptime end` opens a block where every
declaration is implicitly `[[cccc::comptime]]` — no per-declaration attribute
required.

```c
#pragma cccc comptime begin
#include <glob.h>              // treated as comptime-only inside the block
int glob_count(const char *pat, int flags) {
    glob_t g;
    glob(pat, flags, NULL, &g);
    int n = (int)g.gl_pathc;
    globfree(&g);
    return n;
}
int helper = 7;                // comptime variable
#pragma cccc comptime end

// Back to normal runtime code
int main(void) { ... }
```

Inside a comptime block:
- `#include` directives are queued as comptime-only includes — invisible to the runtime translation unit.
- preprocessor directives marked `@emit` are copied to generated output.
- `#pragma cccc emit begin...end` opens a runtime escape sub-block; multiple sub-blocks are allowed.
- Function definitions are treated as `[[cccc::comptime]]`.
- Variable and struct declarations are treated as comptime variables.
- Existing `[[cccc::comptime(inline)]]` annotations are respected; explicit attributes always take precedence.

Nesting rules: alternation between comptime and emit contexts is allowed. A
second `#pragma cccc comptime begin` while a comptime context is active, or a
second `#pragma cccc emit begin` while an emit context is active, is a hard
error. Emit contexts always require an enclosing comptime context.

**Bare form** — omit `begin` to mark everything from the pragma to EOF as
comptime. No closing `end` is needed or accepted; the bare form always runs to
the end of the file:

```c
#include <stdio.h>
#include <string.h>
#include [[cccc::comptime]] <glob.h>

#pragma cccc comptime       // everything from here to EOF is comptime

int glob_count(const char *pat, int flags) {
    glob_t g;
    glob(pat, flags, NULL, &g);
    int n = (int)g.gl_pathc;
    globfree(&g);
    return n;
}
int scale = 3;

glob_count("include/*.h", 0);  // file-scope macro call
```

**Auto-close in headers** — if a `#pragma cccc comptime begin` block in a header
omits `#pragma cccc comptime end`, the block is closed automatically when the
header ends and a `[-Wcomptime-block-leak]` warning is emitted. The bare form
never triggers this warning.

### Comptime-only includes

Use `#include [[cccc::comptime]]` to include a header only during the comptime
compilation pass. The `@comptime` and `__attribute__((comptime))` spellings are
also accepted after `#include`. The header and any macros or types it defines
are invisible to the runtime translation unit.

```c
#include [[cccc::comptime]] <glob.h>
// Equivalent: #include @comptime <glob.h>
// Equivalent: #include __attribute__((comptime)) <glob.h>

[[cccc::comptime]]
int glob_struct_nonempty(void) {
    return (int)sizeof(glob_t) > 0;  // glob_t is available here
}

// glob_t is NOT defined for ordinary runtime code below this point
int main(void) {
    return glob_struct_nonempty() ? 42 : 1;
}
```

This is the comptime counterpart to `#include [[cccc::emit]]` (see
[Emit directives and includes in generated output](#emit-directives-and-includes-in-generated-output)):
`[[cccc::comptime]]` feeds a header into the comptime unit only;
`[[cccc::emit]]` emits an `#include` into the generated output only.

## Comptime Variables

`[[cccc::comptime]]` can also precede a **variable or struct declaration**. The
value is evaluated during the pre-parse phase and stored so that macros can
read it at compile time.

### Scalar comptime variables

```c
[[cccc::comptime]]
int tile_size = 64;

[[cccc::comptime]]
double pi = 3.14159;

[[cccc::comptime]]
Node *area_of_n_tiles(Node *n) {
    int64_t ts = GetComptimeInt("tile_size");
    return Quote("$$ * $$", n, MakeIntLiteral(ts * ts));
}
```

| API | Returns | Description |
|---|---|---|
| `GetComptimeInt(name)` | `int64_t` | Integer value of a comptime scalar |
| `GetComptimeFloat(name)` | `double` | Float/double value of a comptime scalar |
| `GetComptimeVar(name)` | `Node *` | Comptime scalar as an AST literal node |
| `GetComptimePtr(name)` | `Node *` | Address of a static shadow copy of the evaluated comptime variable |

Use `GetComptimePtr(name)` when generated code needs an addressable value
rather than a literal copy:

```c
[[cccc::comptime]]
int threshold = 42;

[[cccc::comptime(inline)]]
Node *threshold_ptr(void) {
    return GetComptimePtr("threshold");
}

int *p = threshold_ptr(); // *p == 42
```

### Struct comptime variables

```c
[[cccc::comptime]]
struct Config { int width; int height; int channels; } cfg = { 1920, 1080, 3 };

[[cccc::comptime]]
Node *pixel_count(void) {
    Node *w = GetComptimeMember("cfg", "width");
    Node *h = GetComptimeMember("cfg", "height");
    return MakeBinary(NK_MUL, w, h);
}
```

Struct and union comptime variables can use tagged, anonymous, or typedef'd
aggregate types. Initializers may be constant expressions or expressions that
call comptime helper functions.

`GetComptimeMember(var_name, field)` returns the field's value as an
AST literal node. Integer and float/double members are supported. Pointer and
array members are not accessible this way.

### Scope notes

- Comptime variables support constant initializers and initializers that call
  comptime helper functions.
- Pointer and string variables produce a compile-time error at this point;
  use `MakeStringLiteral` inside the macro body instead.
- Comptime variables are **not emitted** into the output binary.

### constexpr variables

C23 `constexpr` variables can also be read by macros via a separate API.
Unlike `comptime` variables (which are CCCC-specific), `constexpr` follows
standard C23 restricted constant-expression grammar — no function calls,
no variable references in the initializer.

```c
constexpr int BUF_SIZE = 256;
constexpr double SCALE  = 1.5;

[[cccc::comptime(inline)]]
Node *make_buf_size(void) {
    return GetConstexprValue("BUF_SIZE");
}
```

| API | Returns | Description |
|---|---|---|
| `GetConstexprValue(name)` | `Node *` | Evaluated initializer of a global `constexpr` variable as an AST literal node (integer or float) |

`GetConstexprValue` errors at compile time if the name does not refer to
a visible global `constexpr` variable.

**`constexpr` vs `comptime`** — these are distinct qualifiers. A `constexpr`
variable is not accessible via `$get_comptime_*` and vice versa. Use
`constexpr` for standard C23 constants; use `comptime` for CCCC-extension values
that can reference comptime functions.

## Quasi-Quoting

Backtick quasi-quotes embed a C expression or statement directly in a
comptime function. `${...}` evaluates a comptime expression that returns a
`Node *` and splices that node into the template:

```c
[[cccc::comptime(inline)]]
Node *add_one(Node *x) {
    return `return ${x} + 1;`;
}

int answer(void) {
    add_one(41);
}
```

Backticks are raw and may span lines, so C string quotes and ordinary
backslashes do not need another layer of escaping. `\`` inserts a literal
backtick; all other backslashes are preserved. Interpolation expressions use
normal C parsing and preprocessing, including commas and nested braces:

```c
[[cccc::comptime(inline)]]
Node *choose_second(Node *a, Node *b) {
    return `return ${ ((Node *[]){ a, b })[1] };`;
}
```

`$identifier` remains the reflect operator inside the generated template, so
for example `` `return sizeof($Point);` `` reflects `Point` when the template
is parsed. Backtick interpolation is scalar and uses the same variadic capacity
as `Quote(...)` (up to 64 splice nodes). Use `Quote(...)` directly for `$N`,
`$$`, or `$@` list splicing, and use `QuoteN(...)` when more splice nodes are
required. Legacy `Quote` placeholders are rejected as backtick template tokens
(text such as `"$1"` inside a C string literal remains ordinary text). Nested
backtick quasi-quotes are not supported.

The two forms use the same quote engine:

| Backtick form | Equivalent `Quote` form |
|---|---|
| `` `return 42;` `` | `Quote("return 42;")` |
| `` `return ${x} + 1;` `` | `Quote("return $1 + 1;", x)` |
| Multiline raw template | Escaped C string template |

`Quote(tmpl, ...)` parses a C expression or statement template and splices
`Node *` values into `$1`, `$2`, and later numbered holes.

```c
[[cccc::comptime]]
Node *square(Node *x) {
    return Quote("($1) * ($1)", x);
}

int main(void) {
    return square(6);
}
```

Numbered holes can be reused and reordered. Use `$$` for sequential left-to-
right holes when order is enough:

```c
[[cccc::comptime]]
Node *sum2(Node *a, Node *b) {
    return Quote("$$ + $$", a, b);
}
```

Do not mix `$N` and `$$` in one template. Use `QuoteN(tmpl, nodes, count)`
when splice nodes are already in an array.

### List splicing with `$@N` and `$@`

`$@k;` in **statement-list position** expands an entire `->next`-linked node
chain into the block, replacing one placeholder with N statements. This is
typed unquote-splicing.

```c
[[cccc::comptime]]
Node *double_inc(Node *x) {
    Node *chain = NodeList((Node*[]){
        Quote("$1 += 1;", x),
        Quote("$1 += 1;", x),
    }, 2);
    return Quote("{ $@1; }", chain);
}

int get_plus_two(int v) {
    double_inc(v);
    return v;  // v + 2
}
```

`$@` is the incremental (sequential) form, parallel to `$$`:

```c
[[cccc::comptime]]
Node *two_increments(Node *a, Node *b) {
    return Quote("{ $@; $@; }",
                  Quote("$1 += 10;", a),
                  Quote("$1 += 20;", b));
}
```

You can mix a scalar `$N` and a list `$@N` in the same template when both are
positional. `NodeList(arr, count)` builds the `->next` chain from an array.
An existing `->next` chain (e.g. `MakeBlock(...)->body`) can also be passed
directly as the splice argument.

### Call-argument splicing

`$@k` can also appear as a **direct argument to a function call** in a `Quote`
template, expanding the chain into the callee's argument list. This works for
both **variadic callees** (functions accepting `...`) and **fixed-arity
callees** (functions with an exact parameter count).

For variadic callees the splice occupies zero or more slots in the variadic
portion:

```c
#include <stdarg.h>

int sum_ints(int count, ...) {
    va_list args; va_start(args, count);
    int s = 0;
    for (int i = 0; i < count; i++) s += va_arg(args, int);
    va_end(args);
    return s;
}

[[cccc::comptime(inline)]]
Node *call_sum3(Node *a, Node *b, Node *c) {
    Node *chain = NodeList((Node*[]){ a, b, c }, 3);
    return Quote("sum_ints(3, $@1)", chain); // → sum_ints(3, a, b, c)
}
```

For fixed-arity callees the spliced nodes must produce exactly the right number
of arguments after expansion. Parameter casts are applied post-expansion:

```c
int add3(int a, int b, int c) { return a + b + c; }

[[cccc::comptime(inline)]]
Node *call_add3(Node *a, Node *b, Node *c) {
    Node *chain = NodeList((Node*[]){ a, b, c }, 3);
    return Quote("add3($@1)", chain); // → add3(a, b, c)
}
```

Mixing a scalar `$N` with a call-arg splice `$@M` in one template is supported:

```c
return Quote("add3($1, $@2)", first_node, pair_chain);
```

An empty chain (`NodeList` with `count == 0`, which returns NULL) is a valid
splice that inserts zero arguments. Using `$@k` as a sub-expression operand
(e.g. `"foo($@1 + 1)"`) rather than a direct argument remains a compile-time
error. Splicing the wrong number of arguments into a fixed-arity callee is also
a compile-time error.

### Compound-literal initializer-list splice

`$@k` can also appear inside the braces of a compound literal, splicing a node
chain as positional initializers for a struct or array:

```c
[[cccc::comptime(inline)]]
Node *make_point(Node *px, Node *py) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *chain = NodeList((Node*[]){ px, py }, 2);
    return Quote("(struct Point){ $@1 }", chain);
    // → (struct Point){ .x = px, .y = py }  (positional, left-to-right)
}

struct Point p = make_point(10, 32); // p.x == 10, p.y == 32
```

Array compound literals are also supported:

```c
[[cccc::comptime(inline)]]
Node *make_arr3(Node *a, Node *b, Node *c) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *chain = NodeList((Node*[]){ a, b, c }, 3);
    return Quote("(int[3]){ $@1 }", chain);
}
```

The splice can mix with ordinary positional initializer elements. Elements
after the splice are placed after the expanded chain:

```c
return Quote("(struct Triple){ 1, $@1 }", pair_chain);
return Quote("(int[]){ 1, $@1, 4 }", pair_chain);
```

Inferred array length is supported for compound literals such as
`(int[]){ $@1 }` and `(int[]){ 1, $@1, 4 }`.

Initializer splices remain positional; use `InitStruct` for designated or
partial struct/union initialization. A mismatch between the expanded element
count and the number of struct fields or explicit array elements is a
compile-time error.

Mixing a scalar placeholder with a compound-literal splice in one template is
fine:

```c
return Quote("(struct Point){ $@2 }", unused_scalar, chain);
```

### `Quote` inside generated function bodies

`Quote("return x;")` needs to know the enclosing function's return type to
apply the correct implicit cast. When building a generated function body, wrap
the quote call in `WithFn(fn)` to establish that context:

```c
[[cccc::comptime]]
void generate_answer(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("answer", int_ty);
    WithFn(fn) {
        FunctionSetBody(fn, Quote("return 42;"));
    }
}
generate_answer();
```

Without `WithFn`, `Quote("return x;")` at file scope (where there is no
enclosing function) will compile but the implicit return-type cast is skipped.
For macros that only use `MakeReturn(MakeIntLiteral(...))` directly this does
not matter; it matters when the template produces a `return` statement.

## Type And Symbol Reflection

Macros can inspect types and global symbols that are visible at the macro
execution point.

### `$identifier` — Compile-Time Reflect Operator

The `$identifier` expression resolves a name to a compile-time pointer at
parse time:

| Expression | Result type | Resolves to |
|------------|-------------|-------------|
| `$TypeName` | `Type *` | The `Type *` for a typedef alias or struct/union/enum tag |
| `$varname` | `Obj *` | The `Obj *` for a global variable |
| `$fnname` | `Obj *` | The `Obj *` for a function |

The name must be visible in the current scope; an unknown name is a
compile-time error.

`$` followed by `{` is the backtick-splice operator (not reflect). Splice
placeholders inside `Quote` templates (`$1`, `$$`, `$@`) are also distinct.

```c
typedef struct { int x; int y; } Point;
int counter = 0;

[[cccc::comptime(inline)]]
Node *reflect_type_kind(void) {
    Type *ty = $Point;                       // Type* for Point
    return MakeIntLiteral(GetTypeKind(ty));  // TK_STRUCT
}

[[cccc::comptime(inline)]]
Node *reflect_var(void) {
    Obj *obj = $counter;                     // Obj* for counter
    return MakeIntLiteral(obj ? 1 : 0);
}
```

`$SomeType` can also be used directly as a call argument to a comptime inline
macro, avoiding an intermediate variable:

```c
[[cccc::comptime(inline)]]
Node *describe(Type *ty) {
    return MakeIntLiteral(GetTypeKind(ty));
}

// At the call site:
int kind = describe($Point);
```

```c
typedef enum { RED, GREEN, BLUE } Color;

[[cccc::comptime]]
Node *color_count(void) {
    Type *color = FindType("Color");
    if (!color)
        return MakeIntLiteral(-1);
    return MakeIntLiteral(EnumCount(color));
}

int main(void) {
    return color_count();
}
```

Useful reflection entry points include:

| Task | API |
|------|-----|
| Find a type by name | `FindType(name)` |
| Get a built-in or named type | `GetType(name)` |
| Count enum constants | `EnumCount(ty)` |
| Read enum constants | `EnumAt(ty, i)`, `EnumConstantName(ec)`, `EnumConstantValue(ec)` |
| Count struct/union members | `StructMemberCount(ty)` |
| Read members | `StructMemberAt(ty, i)`, `MemberName(m)`, `MemberType(m)`, `MemberOffset(m)` |
| Find globals | `FindGlobal(name)`, `GlobalCount()`, `GlobalAt(i)` |

For call-site macro expansion, `MakeVarRef(name)` and `FindType(name)`
use the lexical scope where the macro call appears, including nested block
locals and typedefs. When a macro receives an expression argument and needs the
exact variable passed by the caller, prefer inspecting the argument node itself
instead of looking it up again by string name.

## Function Generation

Generated functions are `Obj *` values. Create the object, add parameters,
build a body, and install the body.

```c
[[cccc::comptime]]
void generate_is_even(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("is_even", int_ty);
    FunctionAddParam(fn, "n", int_ty);

    Node *n = MakeParamRef(fn, "n");
    WithFn(fn) {
        FunctionSetBody(fn, Quote("return $1 % 2 == 0;", n));
    }
}
generate_is_even();

int main(void) {
    return is_even(42) ? 42 : 1;
}
```

For global-generation macros, CCCC automatically synthesizes forward
declarations for every generated function definition, so manual publication is
not required for ordinary generated definitions. Use `PublishNode(obj)` when a
macro creates a declaration that later macro-generated code at the same parse
point should reference before a definition is provided, such as a prototype
generated by one macro and promoted to a definition by another.

`MakeFunction(name, ret_type)` promotes an existing forward declaration with
the same name to a generated definition. If a definition already exists, CCCC
emits a compile-time error instead of silently replacing it. Use
`Gensym(prefix)` or `__builtin_gensym(VM, prefix)` for private helper names.

```c
[[cccc::comptime]]
void make_helper(void) {
    const char *name = Gensym("helper");
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction(name, int_ty);
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
}
make_helper();
```

`PublishNodeAt(obj, tok)` is the same operation with an explicit diagnostic
token. Pass `SyntheticToken("label")` when the declaration belongs to
generated code rather than a source token. `$forward_declare(fn)` remains as
a compatibility alias for `PublishNode(fn)`.

## Global Variable Generation

Macros can emit global variables with initial data. Use `MakeArray` to
size the type to match the data length; the codegen copies exactly `ty->size`
bytes from the init data.

```c
[[cccc::comptime]]
void embed_version(void) {
    Type *char_ty = GetType("char");
    Type *arr_ty  = MakeArray(char_ty, 8);
    Obj  *var     = GlobalVar("version_str", arr_ty);
    GlobalVarSetInitData(var, "1.0.0\0\0", 8);
    GlobalVarSetStatic(var, 1);  // internal linkage
}
embed_version();

int main(void) {
    return version_str[0] != '1';
}
```

Global-generation macros automatically synthesize an `extern` declaration for
every generated global variable, so the parser can resolve references without a
manual declaration.

```c
[[cccc::comptime]]
void embed_version(void) { ... }
embed_version();
```

For explicit same-scope visibility, publish a generated global after creating
it:

```c
[[cccc::comptime]]
void embed_banner(void) {
    Type *char_ty = GetType("char");
    Type *arr_ty = MakeArray(char_ty, 4);
    Obj *var = GlobalVar("banner", arr_ty);
    GlobalVarSetInitData(var, "CCCC\0", 4);
    PublishNodeAt(var, SyntheticToken("generated banner"));
}
embed_banner();
```

Struct, union, enum, and typedef builders self-publish in tag or typedef scope.
`PublishNode(type)` is accepted for those generated types as a no-op, which
lets macros use one publication call uniformly.

## Local Variables

Macros that expand into statements can inject locals into the current function.
Prefer `MakeLocalVarUnique(ty)` for temporary variables; it creates a name
that user source cannot capture.

```c
[[cccc::comptime]]
Node *save_then_return(Node *value) {
    Type *int_ty = GetType("int");
    Node *tmp = MakeLocalVarUnique(int_ty);
    Node *stmts[2] = {
        MakeExprStmt(MakeAssign(tmp, value)),
        MakeReturn(tmp),
    };
    return MakeBlock(stmts, 2);
}
```

Use `MakeLocalVar(name, ty)` only when the generated local is meant to have a
specific user-visible name.

`MakeLocalVarUnique` and `MakeLocalVar` work correctly inside `WithFn` blocks:
the new local is allocated to the function established by `WithFn`, not to
any outer function context.

## Initializer Builders

Three builders create initialized aggregate values without requiring `Quote`
template syntax.

### `CompoundLiteral(ty, ...)`

Positional compound literal. Elements are assigned left-to-right to struct
members or array elements. All fields/elements must be provided.

```c
[[cccc::comptime(inline)]]
Node *make_point(Node *px, Node *py) {
    Type *pt = GetType("Point");
    Node *lit = CompoundLiteral(pt, px, py);
    return MakeMember(lit, "x");
}
```

### `InitArray(elem_ty, ...)`

Array compound literal with explicit element type. The array length is inferred
from the argument count.

```c
[[cccc::comptime(inline)]]
Node *second_of_three(Node *a, Node *b, Node *c) {
    Type *int_ty = GetType("int");
    Node *arr = InitArray(int_ty, a, b, c);
    return MakeSubscript(arr, MakeIntLiteral(1));
}
```

### `InitStruct(ty, fields, values, n)`

Designated struct or union init. `fields` is a `const char **` naming the
members; `values` is the corresponding `Node **` array; `n` is the count.
Unspecified members are zero-initialized.

```c
[[cccc::comptime(inline)]]
Node *partial_point(void) {
    Type *pt = GetType("Point");
    const char *flds[] = {"x"};
    Node *vals[] = {MakeIntLiteral(5)};
    Node *s = InitStruct(pt, flds, vals, 1);
    return MakeMember(s, "y");   // y == 0
}
```

### Storage model

| Call context | Storage |
|---|---|
| Inside a function (inline macro or `WithFn` block) | Stack-allocated local; zeroed and assigned at run time |
| Outside any `WithFn` in a non-inline macro | Static anonymous global variable; init data is evaluated at compile time and stored in the data segment |

The file-scope (static) case requires that initializer values are compile-time
constants that can be folded by `cc_eval`. Passing a non-constant expression
(e.g. a function call) in that context is a compile-time error.

```c
[[cccc::comptime]]
void gen_lookup(void) {
    Type *int_ty = GetType("int");

    // Outside WithFn: static global int[4] = {0, 1, 4, 9}
    Node *table = InitArray(int_ty,
        MakeIntLiteral(0), MakeIntLiteral(1),
        MakeIntLiteral(4), MakeIntLiteral(9));

    Obj *fn = MakeFunction("lookup", int_ty);
    WithFn(fn) {
        FunctionAddParam(fn, "i", int_ty);
        FunctionSetBody(fn,
            MakeReturn(MakeSubscript(table, MakeParamRef(fn, "i"))));
    }
}
gen_lookup();
```

## Macro Standard Library (ticket #235)

`include/cccc/reflection.h` ships a bundled set of `$`-prefixed helpers for
common macro-author tasks. Because reflection.h is implicitly included in every
macro/comptime TU, these are available without any extra `#include`.

### String and Memory Wrappers

Thin AST wrappers over `memcpy`, `strlen`, and `strcmp`:

```c
Node *dst, *src, *n;
Memcpy(dst, src, n);      // memcpy(dst, src, n)
Node *str;
Strlen(str);              // strlen(str)
Node *a, *b;
Strcmp(a, b);             // strcmp(a, b)
```

`<string.h>` is always declared in the comptime TU so these work without
additional includes.

### `ForeachMember(type, varname, body)`

Host-side loop over every struct/union member:

```c
[[cccc::comptime]]
void print_offsets(void) {
    Type *ty = GetType("MyStruct");
    ForeachMember(ty, m, {
        // m is a Member* — use MemberName(m), MemberOffset(m), etc.
        int off = MemberOffset(m);
    });
}
```

Each iteration executes `body` at compile time with `varname` bound to the
current `Member *`.

### `OffsetofChain(type, ...)`

Sum offsets through a chain of nested struct fields:

```c
Type *ty = GetType("Outer");
int off = OffsetofChain(ty, "inner", "x");
// equivalent to offsetof(Outer, inner) + offsetof(Inner, x)
```

Returns `-1` if any field name in the chain is not found.

### Serialization Helpers

#### `Serialize(type, expr, buf)` / `Deserialize(type, buf)`

Build a block that flat-copies a struct into/out of a raw byte buffer:

```c
[[cccc::comptime(inline)]]
Node *ser(Node *val, Node *buf) {
    return Serialize(GetType("Point"), val, buf);
}
[[cccc::comptime(inline)]]
Node *deser(Node *buf) {
    return Deserialize(GetType("Point"), buf);
}
```

V1 scope: scalars and flat structs (nested flat structs are recursed;
pointer-typed members are copied as raw pointer bytes).

#### `@serialize` / `@deserialize` attributes

Attach directly to a struct to auto-generate `<Type>_serialize` and
`<Type>_deserialize` functions:

```c
@serialize
@deserialize
struct Point { int x; int y; };

// Generates:
//   int Point_serialize(Point *self, void *buf);   // returns sizeof(Point)
//   Point Point_deserialize(void *buf);
```

### Enum String Conversion

#### `EnumToString(type, expr)` / `EnumFromString(type, expr)`

Build a switch or if-chain that converts between enum values and their name
strings:

```c
[[cccc::comptime(inline)]]
Node *color_name(Node *v) {
    return EnumToString(GetType("Color"), v);
}
```

#### `@enum_to_string` / `@enum_from_string` attributes

```c
@enum_to_string
@enum_from_string
typedef enum { RED, GREEN, BLUE } Color;

// Generates:
//   const char *Color_to_string(Color v);
//   Color Color_from_string(const char *s);  // returns -1 on no match
```

### Code Generators

#### `@generate_getters` / `@generate_setters`

Publish one `get_<field>` / `set_<field>` function per struct member:

```c
@generate_getters
@generate_setters
struct Point { int x; int y; };

// Generates:
//   int get_x(struct Point *self);
//   int get_y(struct Point *self);
//   void set_x(struct Point *self, int value);
//   void set_y(struct Point *self, int value);
```

#### `@generate_constructor`

Publish a `<Type>_create(field1, field2, ...)` constructor:

```c
@generate_constructor
struct Point { int x; int y; };

// Generates:
//   struct Point Point_create(int x, int y);
```

V1 cap: up to 64 members per struct.

#### FP-Style Array Generators

`GenerateSum`, `GenerateMap`, `GenerateReduce`, and `GenerateFilter`
publish typed array helpers for a given element type. Call from a comptime
function invoked at file scope:

```c
[[cccc::comptime]]
void setup_int_helpers(void) {
    Type *int_ty = GetType("int");
    GenerateSum(int_ty);    // int sum_int(int *arr, size_t n)
    GenerateMap(int_ty);    // void map_int(int *arr, size_t n, int *out, int (*f)(int))
    GenerateReduce(int_ty); // int reduce_int(int *arr, size_t n, int init, int (*f)(int, int))
    GenerateFilter(int_ty); // void filter_int(int *arr, size_t n, int *out, size_t *out_n, bool (*pred)(int))
}
setup_int_helpers();
```

`GenerateMap` and `GenerateFilter` use caller-provided output buffers
(no `malloc`). `GenerateFilter` sets `*out_n` to the number of elements
written.

| Generator | Signature |
|---|---|
| `GenerateSum(T)` | `T sum_T(T *arr, size_t n)` |
| `GenerateMap(T)` | `void map_T(T *arr, size_t n, T *out, T (*f)(T))` |
| `GenerateReduce(T)` | `T reduce_T(T *arr, size_t n, T init, T (*f)(T, T))` |
| `GenerateFilter(T)` | `void filter_T(T *arr, size_t n, T *out, size_t *out_n, bool (*pred)(T))` |

The function name suffix is derived from the element type's canonical C name
(`int`, `ulong`, `double`, etc.) or its user-defined type name for struct/enum
types.

### Reflection API additions (ticket #235)

New entries in the reflection table:

| Task | API |
|------|-----|
| Iterate all struct/union members | `ForeachMember(ty, var, body)` |
| Nested-field byte offset | `OffsetofChain(ty, "field", ...)` |
| Flat struct serialization | `Serialize(ty, expr, buf)`, `Deserialize(ty, buf)` |
| Enum → string switch | `EnumToString(ty, expr)` |
| String → enum if-chain | `EnumFromString(ty, expr)` |
| Generate typed sum/map/reduce/filter | `GenerateSum(T)`, `GenerateMap(T)`, `GenerateReduce(T)`, `GenerateFilter(T)` |

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
[[cccc::comptime(inline)]]
Node *make_answer(void) { return MakeIntLiteral(42); }

[[cccc::comptime(inline)]]
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

Underlying functions: `__builtin_macroexpand_1(VirtualMachine *vm, Node *node)` and
`__builtin_macroexpand(VirtualMachine *vm, Node *node)`.

## API Reference

### Type APIs

| Convenience macro | Description |
|-------------------|-------------|
| `FindType(name)` | Find a typedef, struct, union, or enum by name |
| `TypeExists(name)` | Test whether a type name exists |
| `GetType(name)` | Get a named or built-in type such as `"int"` |
| `GetTypeKind(ty)` | Get `TypeKind` |
| `TypeSize(ty)` | Get size in bytes |
| `TypeAlign(ty)` | Get alignment in bytes |
| `TypeIsUnsigned(ty)` | Test unsigned integer type |
| `TypeIsConst(ty)` | Test const-qualified type |
| `TypeBase(ty)` | Get pointer or array base type |
| `TypeArrayLen(ty)` | Get array length, or `-1` |
| `TypeReturnType(ty)` | Get function return type |
| `TypeParamCount(ty)` | Count function parameters |
| `TypeParamAt(ty, i)` | Get function parameter type |
| `TypeIsVariadic(ty)` | Test variadic function type |
| `TypeName(ty)` | Get a type name when available |
| `MakePointer(base)` | Create pointer type |
| `MakeArray(base, len)` | Create array type |

### Enum APIs

| Convenience macro | Description |
|-------------------|-------------|
| `EnumCount(ty)` | Count enum constants |
| `EnumAt(ty, i)` | Get enum constant at index |
| `EnumFind(ty, name)` | Find enum constant by name |
| `EnumConstantName(ec)` | Get enum constant name |
| `EnumConstantValue(ec)` | Get enum constant value |
| `EnumName(ty)` | Get enum type name |
| `EnumValueCount(ty)` | Count enum values |
| `EnumValueName(ty, i)` | Get enum value name |
| `EnumValue(ty, i)` | Get enum value |

### Struct And Union APIs

| Convenience macro | Description |
|-------------------|-------------|
| `StructMemberCount(ty)` | Count members |
| `StructMemberAt(ty, i)` | Get member at index |
| `StructMemberFind(ty, name)` | Find member by name |
| `MemberName(m)` | Get member name |
| `MemberType(m)` | Get member type |
| `MemberOffset(m)` | Get member offset |
| `MemberIsBitfield(m)` | Test bitfield member |
| `MemberBitfieldWidth(m)` | Get bitfield width |

### Global Symbol APIs

| Convenience macro | Description |
|-------------------|-------------|
| `FindGlobal(name)` | Find global object |
| `GlobalCount()` | Count globals |
| `GlobalAt(i)` | Get global at index |
| `ObjName(obj)` | Get object name |
| `ObjType(obj)` | Get object type |
| `ObjIsFunction(obj)` | Test function object |
| `ObjIsDefinition(obj)` | Test definition |
| `ObjIsStatic(obj)` | Test static linkage |

### AST Builder APIs

| Convenience macro | Description |
|-------------------|-------------|
| `MakeIntLiteral(val)` | Integer literal |
| `MakeFloatLiteral(val)` | Floating-point literal |
| `MakeStringLiteral(str)` | String literal |
| `MakeVarRef(name)` | Variable reference |
| `MakeParamRef(fn, name)` | Generated function parameter reference |
| `Gensym(prefix)` | Unique arena-allocated symbol name using `__builtin_gensym` |
| `MacroExpand1(node)` | Single-step macro expansion (identity if not a macro call) |
| `MacroExpand(node)` | Full macro expansion — repeats until the top-level form is stable |
| `CurrentToken()` | Opaque token for the active macro call site |
| `SyntheticToken(label)` | Opaque synthetic token for generated diagnostics |
| `TokenFromNode(node)` | Opaque source token attached to a node |
| `SetToken(node, tok)` | Attach a token to a node and return the node |
| `CopyLocation(dst, src)` | Copy source location from one node to another |
| `NodeList(arr, count)` | Build a `->next`-linked node chain from an array for use as a `$@k` splice argument |
| `MakeBinary(op, l, r)` | Binary expression |
| `MakeUnary(op, operand)` | Unary expression |
| `MakeCast(expr, ty)` | Cast expression |
| `MakeAssign(target, value)` | Assignment expression |
| `MakeMember(obj, name)` | Struct/union member expression |
| `MakeSubscript(arr, idx)` | Array subscript expression (`arr[idx]`) |
| `CompoundLiteral(ty, ...)` | Positional compound literal; stack-local in function scope, static global outside `WithFn` |
| `InitArray(elem_ty, ...)` | Array compound literal with explicit element type; length inferred from argument count |
| `InitStruct(ty, fields, values, n)` | Designated struct/union init; unspecified members are zero |
| `MakeFuncCall(callee, args, n)` | Function call expression |
| `VarargAsArray()` | Borrowed inline variadic argument array for forwarding |
| `MakeReturn(expr)` | Return statement |
| `MakeBlock(stmts, count)` | Compound statement |
| `BlockAddStmt(block, stmt)` / `BlockAddStmt(stmt)` | Append a statement to a block; shorthand form uses `WithBlock` |
| `MakeIf(cond, then_body, else_body)` | If statement |
| `MakeSwitch(cond)` | Switch statement |
| `SwitchAddCase(sw, value, body)` / `SwitchAddCase(value, body)` | Add switch case; shorthand form uses `WithSwitch` |
| `SwitchSetDefault(sw, body)` / `SwitchSetDefault(body)` | Set switch default; shorthand form uses `WithSwitch` |
| `MakeExprStmt(expr)` | Expression statement |
| `MakeLocalVar(name, ty)` | Named local variable |
| `MakeLocalVarUnique(ty)` | Hygienic temporary local |
| `MakeWhile(cond, body)` | While loop |
| `MakeFor(init, cond, inc, body)` | For loop |
| `MakeDoWhile(body, cond)` | Do-while loop |

### Function Builder APIs

| Convenience macro | Description |
|-------------------|-------------|
| `PublishNode(obj_or_type)` | Publish a generated function/global in current scope; accepted as a no-op for generated types |
| `PublishNodeAt(obj_or_type, tok)` | Publish with an explicit diagnostic token |
| `MakeFunction(name, ret_type)` | Create a function object |
| `$forward_declare(fn)` | Deprecated alias for `PublishNode(fn)` |
| `FunctionAddParam(fn, name, ty)` | Add function parameter |
| `FunctionSetBody(fn, body)` | Set function body |
| `FunctionSetStatic(fn, flag)` | Set static linkage |
| `FunctionSetInline(fn, flag)` | Set inline flag |
| `FunctionSetVariadic(fn, flag)` | Set variadic flag |
| `WithFn(fn) { ... }` | Set `fn` as the current function context for the block so `Quote("return x;")` applies the correct return-type cast |
| `WithBlock(block) { ... }` | Set `block` as the current block context for `BlockAddStmt(stmt)` |

### Aggregate And Switch Builder Contexts

Scoped builder helpers let macros group child additions without repeating the
parent pointer at every call site. The explicit forms remain available.

```c
[[cccc::comptime]]
void generate_point(void) {
    Type *int_ty = GetType("int");
    Type *point = MakeStruct("Point");
    WithStruct(point) {
        StructAddField("x", int_ty);
        StructAddField("y", int_ty);
    }
}
generate_point();
```

```c
[[cccc::comptime]]
void generate_switch(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("classify", int_ty);
    FunctionAddParam(fn, "x", int_ty);

    WithFn(fn) {
        Node *sw = MakeSwitch(MakeParamRef(fn, "x"));
        WithSwitch(sw) {
            SwitchAddCase(MakeIntLiteral(0), MakeReturn(MakeIntLiteral(1)));
            SwitchSetDefault(MakeReturn(MakeIntLiteral(-1)));
        }
        FunctionSetBody(fn, sw);
    }
}
generate_switch();
```

`WithEnum(e)` similarly enables `EnumAddConstant(name, value)`, and
`WithBlock(block)` enables `BlockAddStmt(stmt)`.

### Global Variable Builder APIs

| Convenience macro | Description |
|-------------------|-------------|
| `GlobalVar(name, ty)` | Create a global variable definition |
| `GlobalVarSetInitData(var, data, len)` | Set raw initial data (`len` must equal `ty->size`) |
| `GlobalVarSetStatic(var, flag)` | Set internal linkage (file-scope `static`) |

### Node Kinds

Use these constants with `MakeBinary()` and `MakeUnary()`:

```c
NK_ADD, NK_SUB, NK_MUL, NK_DIV, NK_MOD, NK_NEG
NK_BITAND, NK_BITOR, NK_BITXOR, NK_BITNOT, NK_SHL, NK_SHR
NK_EQ, NK_NE, NK_LT, NK_LE
NK_NOT, NK_LOGAND, NK_LOGOR
NK_ASSIGN, NK_ADDR, NK_DEREF, NK_COMMA
```

## Attribute Syntax

Both C23 attribute syntax and GNU attribute syntax are accepted everywhere:

| C23 form | GNU form |
|----------|----------|
| `[[cccc::comptime]]` | `__attribute__((comptime))` |
| `[[cccc::comptime(inline)]]` | `__attribute__((macro(inline)))` |
| `[[cccc::comptime]]` | `__attribute__((comptime))` |

The canonical form used in this document and in CCCC examples is `[[cccc::comptime]]`.

## Constraints

- Macro declarations accept at most 8 fixed parameters. A trailing `...`
  variadic tail can receive additional call-site arguments.
- Macro code runs at compile time and cannot inspect runtime values.
- Global-generation macros compile before the main parse. They can see safe
  file-scope declarations from preprocessed includes and source, but arbitrary
  non-macro function bodies and initialized globals are not compiled into the
  macro VM.
- Global-generation macros run before the main parse, so generated definitions
  are visible everywhere. Inline macros expand at the call site and follow
  normal C declaration rules for any side-effect definitions they emit.
- `PublishNode(obj)` publishes generated functions and globals in the current
  parser scope. Generated struct/union/enum/typedef types already self-publish,
  and `PublishNode(type)` is accepted as a no-op.
- `void` macros cannot be used in expression position; doing so is a compile
  error.
