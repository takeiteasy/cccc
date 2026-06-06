# JCC Compile-Time Macros

Compile-time macros are C functions that JCC compiles and runs during
compilation. They can inspect compile-time types, build AST nodes, generate
functions and global variables, and replace macro call sites with generated code.

A macro function is declared by annotating it with `[[jcc::comptime]]` (C23
attribute syntax) or the equivalent `__attribute__((comptime))` (GNU attribute
syntax). Either form is accepted everywhere.

The macro API is private to macro compilation. JCC embeds its own `reflection.h`
and injects it automatically while macro and comptime helper functions are
compiled, but that bundled header is not on the public include path. Macro code
can use the `$*`, `$quote*`, `$macro_error_at`, `$gensym`, and `$dump_*`
convenience macros directly.

## Return-Value Model

A macro's return value is **the node spliced at the call site**, replacing the
invocation. Top-level definitions — functions created with `$function()`,
globals with `$global_var()` — are **side effects** injected regardless of
what the macro returns. How generated names become visible to the parser depends
on which execution form you use.

| Call context | Return value |
|--------------|--------------|
| Expression position (`int x = mac()`) | Must return a non-NULL `$node_t *`. NULL is a compile error. |
| Declaration position (file-scope `mac();`) | Returning NULL or `void` is legal — means "I only emitted definitions." |

For definition-only macros, declare the return type `void`. This is
self-documenting and lets you omit the return statement entirely. Using a `void`
macro in expression position is a compile error.

```c
[[jcc::comptime]]
void emit_helpers(void) {
    // build functions, globals — no return needed
}
emit_helpers();
```

`$node_t *` macros may still return NULL in declaration position without error. The
old `return $int_literal(0)` idiom still works but is no longer needed.

## Execution Model

JCC supports two macro execution forms:

| Form | Source shape | When it runs | What the return value means |
|------|--------------|--------------|-----------------------------|
| Global generation | `[[jcc::comptime]] void gen(char *a1, ...)` called at file scope | Before the main parse | `NULL` / `void` return means side-effects only. An `ND_BLOCK` return splices its body declarations directly into file scope (see [Block return](#block-return-from-file-scope-macros)). Args are stringified into `char *` parameters. |
| Call-site expansion | `[[jcc::comptime(inline)]] $node_t *gen($node_t *a1, ...)` called inside code | During macro expansion after parsing | Replaces the call expression or statement |

### Pre-parse macro declaration context

Global-generation macros compile and execute **before the main parse begins**.
The macro program has JCC's private `reflection.h` API plus a conservative
snapshot of safe file-scope declarations from the preprocessed source. That
snapshot includes typedefs, struct/union/enum tag declarations, function
prototypes, `extern` declarations, and declarations without function bodies.

This makes included types and prototypes visible to `[[jcc::comptime]]` helpers
used by global-generation macros, but it does not compile arbitrary non-macro
program definitions into the macro VM. Function bodies, file-scope macro calls,
and initialized global definitions are skipped.

## Global Generation

Use a non-inline macro with a file-scope call when generated functions should be
available to the whole parsed program. The call runs before the main parse, and
JCC automatically synthesizes forward declarations for every generated function
and `extern` declarations for every generated global variable, so no manual
publication is needed.

```c
[[jcc::comptime]]
void generate_answer(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("answer", int_ty);
    $function_set_body(fn, $return($int_literal(42)));
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
is available through `_AST_VARARG_COUNT()` and
`_AST_VARARG_STR_AT(i)`.

```c
[[jcc::comptime]]
void gen_types(...) {
    for (int i = 0; i < _AST_VARARG_COUNT(); i++) {
        const char *name = _AST_VARARG_STR_AT(i);
        $obj_t *fn = $function(name, $get_type("int"));
        $function_set_body(fn, $quote("return 42;"));
    }
}

gen_types(type_int, type_float, type_double);
```

Use this for boilerplate enum conversion helpers, serializers, or any generated
definition that source code needs to call normally. Fixed parameters still use
ordinary C parameter names:

```c
[[jcc::comptime]]
void gen_prefixed(char *prefix, ...) {
    for (int i = 0; i < _AST_VARARG_COUNT(); i++) {
        const char *name = _AST_VARARG_STR_AT(i);
        // prefix is the fixed string argument; name is each tail argument.
    }
}

gen_prefixed("api", read, write, close);
```

The 8-argument limit applies only to fixed parameters. A macro declaration with
more than 8 named parameters is rejected, but a trailing `...` can receive any
number of additional call-site arguments.

### Block return from file-scope macros

A file-scope macro can return an `ND_BLOCK` node to emit a bundle of related
declarations in one call. When JCC sees a block return from a global-scope
macro, it **unwraps** the block and splices its declarations directly into the
file-scope token stream so they are parsed as top-level definitions.

```c
[[jcc::comptime]]
$node_t *emit_widget_helpers(void) {
    return $quote("{ struct Widget { int x; int y; }; void widget_init(struct Widget *w) { w->x = 0; w->y = 0; } }");
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

This approach complements the side-effect style (calling `$function` etc.):
- **Side-effect style** — better when the generated declarations depend on
  runtime-computed names or types.
- **Block-return style** — better when the declarations can be expressed as
  literal C source and returned from one place.

### Forward includes in generated output

When a macro generates code that calls a standard-library function, its
serialized C output needs a matching `#include`. Use
`__jcc_forward_include(vm, header)` (or the `$forward_include(header)`
convenience macro) to register a header; JCC prepends it to the emitted file.

```c
[[jcc::comptime]]
void gen_string_helpers(void) {
    $forward_include("<string.h>");   // emitted at top of generated output
    $obj_t *fn = $function("str_len", $get_type("int"));
    // ... build body calling strlen() ...
}

gen_string_helpers();
```

Multiple calls with the same header are deduplicated — only one `#include`
line appears in the output regardless of how many macros request it.

The `header` argument must include angle-bracket or quote delimiters:
`"<stdio.h>"` for system headers, `"\"myheader.h\""` for user headers.

`__jcc_forward_include` is the runtime-output counterpart to
`#include_comptime` (see [Comptime-only includes](#comptime-only-includes)):
`__jcc_forward_include` injects into the runtime output only;
`#include_comptime` feeds a header into the comptime unit only.

## Call-Site Expansion

Use an inline macro for call-site expansion inside expressions or statements.
The call is replaced with the returned AST during macro expansion. Inline macros
**must** return a non-NULL node and cannot be used at file scope.

Two equivalent spellings are accepted:

```c
// Attribute argument form
[[jcc::comptime(inline)]]
$node_t *double_it($node_t *value) {
    return $binary(nk_add, value, value);
}

// C keyword form (preferred)
[[jcc::comptime]]
inline $node_t *double_it($node_t *value) {
    return $binary(nk_add, value, value);
}
```

```c
[[jcc::comptime(inline)]]
$node_t *double_it($node_t *value) {
    return $binary(nk_add, value, value);
}

int main(void) {
    int x = double_it(21);
    return x;
}
```

Macro arguments are `$node_t *` pointers to the original argument ASTs. A macro
can reuse, inspect, wrap, or replace those nodes.

Inline macros also support a trailing `...` parameter. Fixed arguments are
passed as normal `$node_t *` parameters, and the variadic tail is available
through `_AST_VARARG_COUNT()`, `_AST_VARARG_AT(i)`, and
`_AST_VARARGS_AS_ARRAY()`. A macro with only `...` is valid:

```c
[[jcc::comptime(inline)]]
$node_t *sum_all(...) {
    $node_t *acc = _AST_VARARG_AT(0);
    for (int i = 1; i < _AST_VARARG_COUNT(); i++)
        acc = $binary(nk_add, acc, _AST_VARARG_AT(i));
    return acc;
}

int x = sum_all(1, 2, 3, 4);
```

Use `_AST_VARARGS_AS_ARRAY()` when forwarding the tail to an array-form builder
such as `$funcall(callee, args, n)`:

```c
[[jcc::comptime(inline)]]
$node_t *forward_call($node_t *fn_node, ...) {
    return $funcall(fn_node,
                    _AST_VARARGS_AS_ARRAY(),
                    _AST_VARARG_COUNT());
}

int x = forward_call(target_fn, 1, 2, 3);
```

`_AST_VARARGS_AS_ARRAY()` returns a borrowed read-only `$node_t **` slice that
is valid only for the current macro call. It returns `NULL` when the variadic
tail is empty. Forwarding shares AST nodes; it does not consume or clone them.
Static macro-to-macro forwarding is handled with `$quote` and splice syntax.
Dynamic macro-call construction is not part of this API.

`_AST_VARARG_AT(i)` and `_AST_VARARG_STR_AT(i)` use zero-based indexes.
Variadic helpers report a compile-time error when an index is negative, out of
range, or the helper is used with the wrong macro execution form.

Call-site expansion happens after the containing function body has already been
parsed. If an inline macro creates a separate top-level function or global, the
same parsed code can name that object only if normal C declaration rules are
satisfied. Use a global-generation macro when you want JCC to publish generated
declarations automatically.

Macro expansion is bounded by `--macro-recursion-limit=N` to catch accidental
self-recursive expansions. The default is 256. Set the limit to 0 to disable
the check.

Statement macros work the same way. Return a statement node such as
`$return(...)`, `$if(...)`, `$block(...)`, or a statement parsed with
`$quote(...)`.

```c
[[jcc::comptime]]
$node_t *return_if_zero($node_t *value) {
    return $quote("if ($1 == 0) return 0;", value);
}

int main(void) {
    int x = 0;
    return_if_zero(x);
    return 1;
}
```

## Comptime Helpers

All `[[jcc::comptime]]` functions are entry-callable from user code. A plain
helper that is only called by other comptime functions still uses the same
attribute — the distinction is just whether you call it from user code or not.

```c
[[jcc::comptime]]
int plus_one(int n) {
    return n + 1;
}

[[jcc::comptime]]
$node_t *make_value(void) {
    return $int_literal(plus_one(41));
}

int main(void) {
    return make_value();
}
```

All comptime functions are compiled together in a single pass, so they can call
each other even when the callee appears later in the translation unit.

The GNU-attribute equivalent is `__attribute__((comptime))`.

> **Deprecated:** `[[jcc::macro]]` and `__attribute__((macro))` are accepted as
> aliases for `[[jcc::comptime]]` for backwards compatibility.

### Comptime block

`#pragma jcc comptime begin` / `#pragma jcc end` opens a block where every
declaration is implicitly `[[jcc::comptime]]` — no per-declaration attribute
required.

```c
#pragma jcc comptime begin
#include <glob.h>              // treated as #include_comptime
int glob_count(const char *pat, int flags) {
    glob_t g;
    glob(pat, flags, NULL, &g);
    int n = (int)g.gl_pathc;
    globfree(&g);
    return n;
}
int helper = 7;                // comptime variable
#pragma jcc end

// Back to normal runtime code
int main(void) { ... }
```

Inside a comptime block:
- `#include` directives are queued as `#include_comptime` — invisible to the runtime translation unit.
- Function definitions are treated as `[[jcc::comptime]]`.
- Variable and struct declarations are treated as comptime variables.
- Existing `[[jcc::comptime(inline)]]` annotations are respected; explicit attributes always take precedence.

Blocks cannot be nested. A second `#pragma jcc comptime begin` while one is
already open is a hard error.

**Bare form** — omit `begin` to activate without a keyword; close with
`#pragma jcc end` as usual. Useful at the top of a dedicated comptime helper
file where the whole file is comptime:

```c
#pragma jcc comptime
int double_it(int n) { return n * 2; }
int scale = 3;
#pragma jcc end

int main(void) { return double_it(scale * 7); }
```

**Auto-close in headers** — if a header opens a comptime block but omits
`#pragma jcc end`, the block is closed automatically when the header ends
and a `[-Wcomptime-block-leak]` warning is emitted.

### Comptime-only includes

Use `#include_comptime` to include a header only during the comptime
compilation pass. The header and any macros or types it defines are invisible
to the runtime translation unit.

```c
#include_comptime <glob.h>   // only visible to comptime helpers

[[jcc::comptime]]
int glob_struct_nonempty(void) {
    return (int)sizeof(glob_t) > 0;  // glob_t is available here
}

// glob_t is NOT defined for ordinary runtime code below this point
int main(void) {
    return glob_struct_nonempty() ? 42 : 1;
}
```

This is the comptime counterpart to `__jcc_forward_include` (see
[Forward includes in generated output](#forward-includes-in-generated-output)):
`#include_comptime` feeds a header into the comptime unit only;
`__jcc_forward_include` emits an `#include` into the runtime output only.

## Comptime Variables

`[[jcc::comptime]]` can also precede a **variable or struct declaration**. The
value is evaluated during the pre-parse phase and stored so that macros can
read it at compile time.

### Scalar comptime variables

```c
[[jcc::comptime]]
int tile_size = 64;

[[jcc::comptime]]
double pi = 3.14159;

[[jcc::comptime]]
$node_t *area_of_n_tiles($node_t *n) {
    int64_t ts = $get_comptime_int("tile_size");
    return $quote("$$ * $$", n, $int_literal(ts * ts));
}
```

| API | Returns | Description |
|---|---|---|
| `$get_comptime_int(name)` | `int64_t` | Integer value of a comptime scalar |
| `$get_comptime_float(name)` | `double` | Float/double value of a comptime scalar |
| `$get_comptime_var(name)` | `$node_t *` | Comptime scalar as an AST literal node |

### Struct comptime variables

```c
[[jcc::comptime]]
struct Config { int width; int height; int channels; } cfg = { 1920, 1080, 3 };

[[jcc::comptime]]
$node_t *pixel_count(void) {
    $node_t *w = $get_comptime_member("cfg", "width");
    $node_t *h = $get_comptime_member("cfg", "height");
    return $binary(nk_mul, w, h);
}
```

Struct and union comptime variables can use tagged, anonymous, or typedef'd
aggregate types. Initializers may be constant expressions or expressions that
call comptime helper functions.

`$get_comptime_member(var_name, field)` returns the field's value as an
AST literal node. Integer and float/double members are supported. Pointer and
array members are not accessible this way.

### Scope notes

- Comptime variables support constant initializers and initializers that call
  comptime helper functions.
- Pointer and string variables produce a compile-time error at this point;
  use `$string_literal` inside the macro body instead.
- Comptime variables are **not emitted** into the output binary.

### constexpr variables

C23 `constexpr` variables can also be read by macros via a separate API.
Unlike `comptime` variables (which are JCC-specific), `constexpr` follows
standard C23 restricted constant-expression grammar — no function calls,
no variable references in the initializer.

```c
constexpr int BUF_SIZE = 256;
constexpr double SCALE  = 1.5;

[[jcc::comptime(inline)]]
$node_t *make_buf_size(void) {
    return $get_constexpr_value("BUF_SIZE");
}
```

| API | Returns | Description |
|---|---|---|
| `$get_constexpr_value(name)` | `$node_t *` | Evaluated initializer of a global `constexpr` variable as an AST literal node (integer or float) |

`$get_constexpr_value` errors at compile time if the name does not refer to
a visible global `constexpr` variable.

**`constexpr` vs `comptime`** — these are distinct qualifiers. A `constexpr`
variable is not accessible via `$get_comptime_*` and vice versa. Use
`constexpr` for standard C23 constants; use `comptime` for JCC-extension values
that can reference comptime functions.

## Quasi-Quoting

`$quote(tmpl, ...)` parses a C expression or statement template and splices
`$node_t *` values into `$1`, `$2`, and later numbered holes.

```c
[[jcc::comptime]]
$node_t *square($node_t *x) {
    return $quote("($1) * ($1)", x);
}

int main(void) {
    return square(6);
}
```

Numbered holes can be reused and reordered. Use `$$` for sequential left-to-
right holes when order is enough:

```c
[[jcc::comptime]]
$node_t *sum2($node_t *a, $node_t *b) {
    return $quote("$$ + $$", a, b);
}
```

Do not mix `$N` and `$$` in one template. Use `$quote_n(tmpl, nodes, count)`
when splice nodes are already in an array.

### List splicing with `$@N` and `$@`

`$@k;` in **statement-list position** expands an entire `->next`-linked node
chain into the block, replacing one placeholder with N statements. This is
typed unquote-splicing.

```c
[[jcc::comptime]]
$node_t *double_inc($node_t *x) {
    $node_t *chain = $node_list(($node_t*[]){
        $quote("$1 += 1;", x),
        $quote("$1 += 1;", x),
    }, 2);
    return $quote("{ $@1; }", chain);
}

int get_plus_two(int v) {
    double_inc(v);
    return v;  // v + 2
}
```

`$@` is the incremental (sequential) form, parallel to `$$`:

```c
[[jcc::comptime]]
$node_t *two_increments($node_t *a, $node_t *b) {
    return $quote("{ $@; $@; }",
                  $quote("$1 += 10;", a),
                  $quote("$1 += 20;", b));
}
```

You can mix a scalar `$N` and a list `$@N` in the same template when both are
positional. `$node_list(arr, count)` builds the `->next` chain from an array.
An existing `->next` chain (e.g. `$block(...)->body`) can also be passed
directly as the splice argument.

### Call-argument splicing

`$@k` can also appear as a **direct argument to a function call** in a `$quote`
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

[[jcc::comptime(inline)]]
$node_t *call_sum3($node_t *a, $node_t *b, $node_t *c) {
    $node_t *chain = $node_list(($node_t*[]){ a, b, c }, 3);
    return $quote("sum_ints(3, $@1)", chain); // → sum_ints(3, a, b, c)
}
```

For fixed-arity callees the spliced nodes must produce exactly the right number
of arguments after expansion. Parameter casts are applied post-expansion:

```c
int add3(int a, int b, int c) { return a + b + c; }

[[jcc::comptime(inline)]]
$node_t *call_add3($node_t *a, $node_t *b, $node_t *c) {
    $node_t *chain = $node_list(($node_t*[]){ a, b, c }, 3);
    return $quote("add3($@1)", chain); // → add3(a, b, c)
}
```

Mixing a scalar `$N` with a call-arg splice `$@M` in one template is supported:

```c
return $quote("add3($1, $@2)", first_node, pair_chain);
```

An empty chain (`$node_list` with `count == 0`, which returns NULL) is a valid
splice that inserts zero arguments. Using `$@k` as a sub-expression operand
(e.g. `"foo($@1 + 1)"`) rather than a direct argument remains a compile-time
error. Splicing the wrong number of arguments into a fixed-arity callee is also
a compile-time error.

### Compound-literal initializer-list splice

`$@k` can also appear as the **sole element** inside the braces of a compound
literal, splicing a node chain as positional initializers for a struct or
fixed-size array:

```c
[[jcc::comptime(inline)]]
$node_t *make_point($node_t *px, $node_t *py) {
    $vm_t *vm = __jcc_get_vm();
    $node_t *chain = $node_list(($node_t*[]){ px, py }, 2);
    return $quote("(struct Point){ $@1 }", chain);
    // → (struct Point){ .x = px, .y = py }  (positional, left-to-right)
}

struct Point p = make_point(10, 32); // p.x == 10, p.y == 32
```

Array compound literals are also supported:

```c
[[jcc::comptime(inline)]]
$node_t *make_arr3($node_t *a, $node_t *b, $node_t *c) {
    $vm_t *vm = __jcc_get_vm();
    $node_t *chain = $node_list(($node_t*[]){ a, b, c }, 3);
    return $quote("(int[3]){ $@1 }", chain);
}
```

**V1 restrictions:**
- `$@k` must be the only element in the braces — mixing with other initializer
  elements is a compile-time error.
- Initializers are positional (no designator syntax like `.field = val`).
- Arrays must be explicitly sized; inferred-length `arr[]` is not supported.
- A mismatch between the chain length and the number of struct fields or array
  elements is a compile-time error.

Mixing a scalar placeholder with a compound-literal splice in one template is
fine, as long as the splice occupies the sole braces element:

```c
return $quote("(struct Point){ $@2 }", unused_scalar, chain);
```

### `$quote` inside generated function bodies

`$quote("return x;")` needs to know the enclosing function's return type to
apply the correct implicit cast. When building a generated function body, wrap
the quote call in `$with_fn(fn)` to establish that context:

```c
[[jcc::comptime]]
void generate_answer(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("answer", int_ty);
    $with_fn(fn) {
        $function_set_body(fn, $quote("return 42;"));
    }
}
generate_answer();
```

Without `$with_fn`, `$quote("return x;")` at file scope (where there is no
enclosing function) will compile but the implicit return-type cast is skipped.
For macros that only use `$return($int_literal(...))` directly this does
not matter; it matters when the template produces a `return` statement.

## Type And Symbol Reflection

Macros can inspect types and global symbols that are visible at the macro
execution point.

```c
typedef enum { RED, GREEN, BLUE } Color;

[[jcc::comptime]]
$node_t *color_count(void) {
    $type_t *color = $find_type("Color");
    if (!color)
        return $int_literal(-1);
    return $int_literal($enum_count(color));
}

int main(void) {
    return color_count();
}
```

Useful reflection entry points include:

| Task | API |
|------|-----|
| Find a type by name | `$find_type(name)` |
| Get a built-in or named type | `$get_type(name)` |
| Count enum constants | `$enum_count(ty)` |
| Read enum constants | `$enum_at(ty, i)`, `$enum_constant_name(ec)`, `$enum_constant_value(ec)` |
| Count struct/union members | `$struct_member_count(ty)` |
| Read members | `$struct_member_at(ty, i)`, `$member_name(m)`, `$member_type(m)`, `$member_offset(m)` |
| Find globals | `$find_global(name)`, `$global_count()`, `$global_at(i)` |

For call-site macro expansion, `$var_ref(name)` and `$find_type(name)`
use the lexical scope where the macro call appears, including nested block
locals and typedefs. When a macro receives an expression argument and needs the
exact variable passed by the caller, prefer inspecting the argument node itself
instead of looking it up again by string name.

## Function Generation

Generated functions are `$obj_t *` values. Create the object, add parameters,
build a body, and install the body.

```c
[[jcc::comptime]]
void generate_is_even(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("is_even", int_ty);
    $function_add_param(fn, "n", int_ty);

    $node_t *n = $param_ref(fn, "n");
    $with_fn(fn) {
        $function_set_body(fn, $quote("return $1 % 2 == 0;", n));
    }
}
generate_is_even();

int main(void) {
    return is_even(42) ? 42 : 1;
}
```

For global-generation macros, JCC automatically synthesizes forward
declarations for every generated function definition, so manual publication is
not required for ordinary generated definitions. Use `$publish(obj)` when a
macro creates a declaration that later macro-generated code at the same parse
point should reference before a definition is provided, such as a prototype
generated by one macro and promoted to a definition by another.

`$function(name, ret_type)` promotes an existing forward declaration with
the same name to a generated definition. If a definition already exists, JCC
emits a compile-time error instead of silently replacing it. Use
`$gensym(prefix)` or `__jcc_gensym(_VM, prefix)` for private helper names.

```c
[[jcc::comptime]]
void make_helper(void) {
    const char *name = $gensym("helper");
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function(name, int_ty);
    $function_set_body(fn, $return($int_literal(42)));
}
make_helper();
```

`$publish_at(obj, tok)` is the same operation with an explicit diagnostic
token. Pass `$synthetic_token("label")` when the declaration belongs to
generated code rather than a source token. `$forward_declare(fn)` remains as
a compatibility alias for `$publish(fn)`.

## Global Variable Generation

Macros can emit global variables with initial data. Use `$make_array` to
size the type to match the data length; the codegen copies exactly `ty->size`
bytes from the init data.

```c
[[jcc::comptime]]
void embed_version(void) {
    $type_t *char_ty = $get_type("char");
    $type_t *arr_ty  = $make_array(char_ty, 8);
    $obj_t  *var     = $global_var("version_str", arr_ty);
    $global_var_set_init_data(var, "1.0.0\0\0", 8);
    $global_var_set_static(var, 1);  // internal linkage
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
[[jcc::comptime]]
void embed_version(void) { ... }
embed_version();
```

For explicit same-scope visibility, publish a generated global after creating
it:

```c
[[jcc::comptime]]
void embed_banner(void) {
    $type_t *char_ty = $get_type("char");
    $type_t *arr_ty = $make_array(char_ty, 4);
    $obj_t *var = $global_var("banner", arr_ty);
    $global_var_set_init_data(var, "JCC\0", 4);
    $publish_at(var, $synthetic_token("generated banner"));
}
embed_banner();
```

Struct, union, enum, and typedef builders self-publish in tag or typedef scope.
`$publish(type)` is accepted for those generated types as a no-op, which
lets macros use one publication call uniformly.

## Local Variables

Macros that expand into statements can inject locals into the current function.
Prefer `$local_var_unique(ty)` for temporary variables; it creates a name
that user source cannot capture.

```c
[[jcc::comptime]]
$node_t *save_then_return($node_t *value) {
    $type_t *int_ty = $get_type("int");
    $node_t *tmp = $local_var_unique(int_ty);
    $node_t *stmts[2] = {
        $expr_stmt($assign(tmp, value)),
        $return(tmp),
    };
    return $block(stmts, 2);
}
```

Use `$local_var(name, ty)` only when the generated local is meant to have a
specific user-visible name.

## Diagnostics And Debugging

Use source-located diagnostics when rejecting a macro argument:

```c
[[jcc::comptime]]
$node_t *require_nonzero($node_t *value) {
    if (!value)
        $macro_error_at(value, "expected an expression");
    return value;
}
```

Builder-created nodes automatically use the macro invocation as their source
location. When a diagnostic should point somewhere else, use the location
helpers explicitly:

```c
[[jcc::comptime]]
$node_t *checked_double($node_t *value) {
    $node_t *expr = $binary(nk_add, value, value);
    return $copy_location(expr, value);
}
```

Use `$synthetic_token(label)` for diagnostics that belong to deliberately
generated code rather than the call site or an input expression:

```c
[[jcc::comptime]]
$node_t *generated_error(void) {
    $node_t *expr = $int_literal(0);
    $set_token(expr, $synthetic_token("generated expression"));
    $macro_error_at(expr, "generated expression is invalid here");
    return expr;
}
```

AST dump helpers are available while developing macros:

| Helper | Use |
|--------|-----|
| `$dump_tree(node)` | Print a readable tree |
| `$dump_tree_to_string(node)` | Render a tree into a string |
| `$dump_ast_gen(node)` | Print builder calls for a node |
| `$dump_ast_gen_to_string(node)` | Render builder calls into a string |

The interactive VM debugger (`-g`) does not currently stop inside macro
execution. Macro bytecode runs during compilation, before the final program is
started under the debugger, and JCC suppresses VM debug tracing while invoking
macro functions. Use `$dump_*` helpers and source-located diagnostics for macro
debugging.

### macroexpand — macro expansion

Two functions are provided, matching Lisp's `macroexpand-1` / `macroexpand`
pair.

**`$macroexpand_1(node)`** expands a single macro call node exactly once,
without splicing the result into the AST or recursing. Useful when you need
to observe one expansion step at a time or write meta-macros that inspect
intermediate forms.

**`$macroexpand(node)`** repeatedly calls `$macroexpand_1` on the top-level
node until it is no longer a macro call (the form is *stable*). It does not
recurse into child nodes — only the outermost call is expanded. The VM's
`macro_recursion_limit` applies; exceeding it is a compile error.

```c
[[jcc::comptime(inline)]]
$node_t *make_answer(void) { return $int_literal(42); }

[[jcc::comptime(inline)]]
$node_t *wrap_answer(void) { return $quote("make_answer()"); }

[[jcc::comptime]]
void debug_macro(void) {
    $node_t *call = $quote("wrap_answer()");

    // One step: wrap_answer() -> make_answer() (still a macro call)
    $node_t *step1 = $macroexpand_1(call);
    $dump_tree(step1);

    // Full expansion: wrap_answer() -> make_answer() -> 42
    $node_t *full = $macroexpand(call);
    $dump_tree(full);
}
```

If `node` is not a macro call, both functions return `node` unchanged
(identity). If the named macro is not found or has not compiled, `node` is
returned unchanged.

Underlying functions: `__jcc_macroexpand_1(JCC *vm, $node_t *node)` and
`__jcc_macroexpand(JCC *vm, $node_t *node)`.

## API Reference

### Type APIs

| Convenience macro | Description |
|-------------------|-------------|
| `$find_type(name)` | Find a typedef, struct, union, or enum by name |
| `$type_exists(name)` | Test whether a type name exists |
| `$get_type(name)` | Get a named or built-in type such as `"int"` |
| `$type_kind(ty)` | Get `$type_kind_t` |
| `$type_size(ty)` | Get size in bytes |
| `$type_align(ty)` | Get alignment in bytes |
| `$type_is_unsigned(ty)` | Test unsigned integer type |
| `$type_is_const(ty)` | Test const-qualified type |
| `$type_base(ty)` | Get pointer or array base type |
| `$type_array_len(ty)` | Get array length, or `-1` |
| `$type_return_type(ty)` | Get function return type |
| `$type_param_count(ty)` | Count function parameters |
| `$type_param_at(ty, i)` | Get function parameter type |
| `$type_is_variadic(ty)` | Test variadic function type |
| `$type_name(ty)` | Get a type name when available |
| `$make_pointer(base)` | Create pointer type |
| `$make_array(base, len)` | Create array type |

### Enum APIs

| Convenience macro | Description |
|-------------------|-------------|
| `$enum_count(ty)` | Count enum constants |
| `$enum_at(ty, i)` | Get enum constant at index |
| `$enum_find(ty, name)` | Find enum constant by name |
| `$enum_constant_name(ec)` | Get enum constant name |
| `$enum_constant_value(ec)` | Get enum constant value |
| `$enum_name(ty)` | Get enum type name |
| `$enum_value_count(ty)` | Count enum values |
| `$enum_value_name(ty, i)` | Get enum value name |
| `$enum_value(ty, i)` | Get enum value |

### Struct And Union APIs

| Convenience macro | Description |
|-------------------|-------------|
| `$struct_member_count(ty)` | Count members |
| `$struct_member_at(ty, i)` | Get member at index |
| `$struct_member_find(ty, name)` | Find member by name |
| `$member_name(m)` | Get member name |
| `$member_type(m)` | Get member type |
| `$member_offset(m)` | Get member offset |
| `$member_is_bitfield(m)` | Test bitfield member |
| `$member_bitfield_width(m)` | Get bitfield width |

### Global Symbol APIs

| Convenience macro | Description |
|-------------------|-------------|
| `$find_global(name)` | Find global object |
| `$global_count()` | Count globals |
| `$global_at(i)` | Get global at index |
| `$obj_name(obj)` | Get object name |
| `$obj_type(obj)` | Get object type |
| `$obj_is_function(obj)` | Test function object |
| `$obj_is_definition(obj)` | Test definition |
| `$obj_is_static(obj)` | Test static linkage |

### AST Builder APIs

| Convenience macro | Description |
|-------------------|-------------|
| `$int_literal(val)` | Integer literal |
| `$float_literal(val)` | Floating-point literal |
| `$string_literal(str)` | String literal |
| `$var_ref(name)` | Variable reference |
| `$param_ref(fn, name)` | Generated function parameter reference |
| `$gensym(prefix)` | Unique arena-allocated symbol name using `__jcc_gensym` |
| `$macroexpand_1(node)` | Single-step macro expansion (identity if not a macro call) |
| `$macroexpand(node)` | Full macro expansion — repeats until the top-level form is stable |
| `$current_token()` | Opaque token for the active macro call site |
| `$synthetic_token(label)` | Opaque synthetic token for generated diagnostics |
| `$token_from_node(node)` | Opaque source token attached to a node |
| `$set_token(node, tok)` | Attach a token to a node and return the node |
| `$copy_location(dst, src)` | Copy source location from one node to another |
| `$node_list(arr, count)` | Build a `->next`-linked node chain from an array for use as a `$@k` splice argument |
| `$binary(op, l, r)` | Binary expression |
| `$unary(op, operand)` | Unary expression |
| `$cast(expr, ty)` | Cast expression |
| `$assign(target, value)` | Assignment expression |
| `$member(obj, name)` | Struct/union member expression |
| `$funcall(callee, args, n)` | Function call expression |
| `_AST_VARARGS_AS_ARRAY()` | Borrowed inline variadic argument array for forwarding |
| `$return(expr)` | Return statement |
| `$block(stmts, count)` | Compound statement |
| `$if(cond, then_body, else_body)` | If statement |
| `$switch(cond)` | Switch statement |
| `$switch_add_case(sw, value, body)` | Add switch case |
| `$switch_set_default(sw, body)` | Set switch default |
| `$expr_stmt(expr)` | Expression statement |
| `$local_var(name, ty)` | Named local variable |
| `$local_var_unique(ty)` | Hygienic temporary local |
| `$while(cond, body)` | While loop |
| `$for(init, cond, inc, body)` | For loop |
| `$do_while(body, cond)` | Do-while loop |

### Function Builder APIs

| Convenience macro | Description |
|-------------------|-------------|
| `$publish(obj_or_type)` | Publish a generated function/global in current scope; accepted as a no-op for generated types |
| `$publish_at(obj_or_type, tok)` | Publish with an explicit diagnostic token |
| `$function(name, ret_type)` | Create a function object |
| `$forward_declare(fn)` | Deprecated alias for `$publish(fn)` |
| `$function_add_param(fn, name, ty)` | Add function parameter |
| `$function_set_body(fn, body)` | Set function body |
| `$function_set_static(fn, flag)` | Set static linkage |
| `$function_set_inline(fn, flag)` | Set inline flag |
| `$function_set_variadic(fn, flag)` | Set variadic flag |
| `$with_fn(fn) { ... }` | Set `fn` as the current function context for the block so `$quote("return x;")` applies the correct return-type cast |

### Global Variable Builder APIs

| Convenience macro | Description |
|-------------------|-------------|
| `$global_var(name, ty)` | Create a global variable definition |
| `$global_var_set_init_data(var, data, len)` | Set raw initial data (`len` must equal `ty->size`) |
| `$global_var_set_static(var, flag)` | Set internal linkage (file-scope `static`) |

### Node Kinds

Use these constants with `$binary()` and `$unary()`:

```c
nk_add, nk_sub, nk_mul, nk_div, nk_mod, nk_neg
nk_bitand, nk_bitor, nk_bitxor, nk_bitnot, nk_shl, nk_shr
nk_eq, nk_ne, nk_lt, nk_le
nk_not, nk_logand, nk_logor
nk_assign, nk_addr, nk_deref, nk_comma
```

## Attribute Syntax

Both C23 attribute syntax and GNU attribute syntax are accepted everywhere:

| C23 form | GNU form |
|----------|----------|
| `[[jcc::comptime]]` | `__attribute__((comptime))` |
| `[[jcc::comptime(inline)]]` | `__attribute__((macro(inline)))` |
| `[[jcc::comptime]]` | `__attribute__((comptime))` |

The canonical form used in this document and in JCC examples is `[[jcc::comptime]]`.

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
- `$publish(obj)` publishes generated functions and globals in the current
  parser scope. Generated struct/union/enum/typedef types already self-publish,
  and `$publish(type)` is accepted as a no-op.
- `void` macros cannot be used in expression position; doing so is a compile
  error.
