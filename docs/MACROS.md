# JCC Compile-Time Macros

Compile-time macros are C functions that JCC compiles and runs during
compilation. They can inspect compile-time types, build AST nodes, generate
functions and global variables, and replace macro call sites with generated code.

A macro function is declared by annotating it with `[[jcc::macro]]` (C23
attribute syntax) or the equivalent `__attribute__((macro))` (GNU attribute
syntax). Either form is accepted everywhere.

The macro API is private to macro compilation. JCC embeds its own `reflection.h`
and injects it automatically while macro and comptime helper functions are
compiled, but that bundled header is not on the public include path. Macro code
can use the `_AST_*`, `_QUOTE*`, `_MACRO_ERROR_AT`, `_GENSYM`, and `_DUMP_*`
convenience macros directly.

## Return-Value Model

A macro's return value is **the node spliced at the call site**, replacing the
invocation. Top-level definitions — functions created with `_AST_FUNCTION()`,
globals with `_AST_GLOBAL_VAR()` — are **side effects** injected regardless of
what the macro returns. How generated names become visible to the parser depends
on which execution form you use.

| Call context | Return value |
|--------------|--------------|
| Expression position (`int x = mac()`) | Must return a non-NULL `_Node *`. NULL is a compile error. |
| Declaration position (file-scope `mac();`) | Returning NULL or `void` is legal — means "I only emitted definitions." |

For definition-only macros, declare the return type `void`. This is
self-documenting and lets you omit the return statement entirely. Using a `void`
macro in expression position is a compile error.

```c
[[jcc::macro]]
void emit_helpers(void) {
    // build functions, globals — no return needed
}
emit_helpers();
```

`_Node *` macros may still return NULL in declaration position without error. The
old `return _AST_INT_LITERAL(0)` idiom still works but is no longer needed.

## Execution Model

JCC supports two macro execution forms:

| Form | Source shape | When it runs | What the return value means |
|------|--------------|--------------|-----------------------------|
| Global generation | `[[jcc::macro]] void gen(void)` called at file scope | Before the main parse | Ignored; side effects generate declarations/functions/globals visible everywhere |
| Call-site expansion | `[[jcc::macro(inline)]] _Node *gen(args...)` called inside code | During macro expansion after parsing | Replaces the call expression or statement |

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
[[jcc::macro]]
void generate_answer(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("answer", int_ty);
    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(42)));
}

generate_answer();

int main(void) {
    return answer();
}
```

Global-generation macros take no call-site arguments. Use them for boilerplate
functions, enum conversion helpers, serializers, or any generated definition
that source code needs to call normally.

### Forward includes in generated output

When a macro generates code that calls a standard-library function, its
serialized C output needs a matching `#include`. Use
`__jcc_forward_include(vm, header)` (or the `_FORWARD_INCLUDE(header)`
convenience macro) to register a header; JCC prepends it to the emitted file.

```c
[[jcc::macro]]
void gen_string_helpers(void) {
    _FORWARD_INCLUDE("<string.h>");   // emitted at top of generated output
    _Obj *fn = _AST_FUNCTION("str_len", _AST_GET_TYPE("int"));
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

Use an inline macro (`[[jcc::macro(inline)]]`) for call-site expansion inside
expressions or statements. The call is replaced with the returned AST during
macro expansion. Inline macros **must** return a non-NULL node and cannot be
used at file scope.

```c
[[jcc::macro(inline)]]
_Node *double_it(_Node *value) {
    return _AST_BINARY(_ADD, value, value);
}

int main(void) {
    int x = double_it(21);
    return x;
}
```

Macro arguments are `_Node *` pointers to the original argument ASTs. A macro
can reuse, inspect, wrap, or replace those nodes.

Call-site expansion happens after the containing function body has already been
parsed. If an inline macro creates a separate top-level function or global, the
same parsed code can name that object only if normal C declaration rules are
satisfied. Use a global-generation macro when you want JCC to publish generated
declarations automatically.

Macro expansion is bounded by `--macro-recursion-limit=N` to catch accidental
self-recursive expansions. The default is 256. Set the limit to 0 to disable
the check.

Statement macros work the same way. Return a statement node such as
`_AST_RETURN(...)`, `_AST_IF(...)`, `_AST_BLOCK(...)`, or a statement parsed with
`_QUOTE(...)`.

```c
[[jcc::macro]]
_Node *return_if_zero(_Node *value) {
    return _QUOTE("if ($1 == 0) return 0;", value);
}

int main(void) {
    int x = 0;
    return_if_zero(x);
    return 1;
}
```

## Comptime Helpers

Use `[[jcc::comptime]]` for helper functions that should be callable by macros
but should not expand from ordinary program call sites.

```c
[[jcc::comptime]]
int plus_one(int n) {
    return n + 1;
}

[[jcc::macro]]
_Node *make_value(void) {
    return _AST_INT_LITERAL(plus_one(41));
}

int main(void) {
    return make_value();
}
```

Macros and comptime helpers are compiled together, so they can call each other
even when the callee appears later in the translation unit.

The GNU-attribute equivalent is `__attribute__((comptime))`.

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

`[[jcc::comptime]]` can also precede a **variable or struct declaration** with a
constant initializer. The value is evaluated during the pre-parse phase and
stored so that macros can read it at compile time.

### Scalar comptime variables

```c
[[jcc::comptime]]
int tile_size = 64;

[[jcc::comptime]]
double pi = 3.14159;

[[jcc::macro]]
_Node *area_of_n_tiles(_Node *n) {
    int64_t ts = _AST_GET_COMPTIME_INT("tile_size");
    return _QUOTE("$$ * $$", n, _AST_INT_LITERAL(ts * ts));
}
```

| API | Returns | Description |
|---|---|---|
| `_AST_GET_COMPTIME_INT(name)` | `int64_t` | Integer value of a comptime scalar |
| `_AST_GET_COMPTIME_FLOAT(name)` | `double` | Float/double value of a comptime scalar |
| `_AST_GET_COMPTIME_VAR(name)` | `_Node *` | Comptime scalar as an AST literal node |

### Struct comptime variables

```c
[[jcc::comptime]]
struct Config { int width; int height; int channels; } cfg = { 1920, 1080, 3 };

[[jcc::macro]]
_Node *pixel_count(void) {
    _Node *w = _AST_GET_COMPTIME_MEMBER("cfg", "width");
    _Node *h = _AST_GET_COMPTIME_MEMBER("cfg", "height");
    return _AST_BINARY(_MUL, w, h);
}
```

`_AST_GET_COMPTIME_MEMBER(var_name, field)` returns the field's value as an
AST literal node. Integer and float/double members are supported. Pointer and
array members are not accessible this way.

### Scope notes

- Comptime variables must have **constant initializers** (literals and
  arithmetic on literals). Calls to comptime functions in the initializer
  are not yet supported.
- Pointer and string variables produce a compile-time error at this point;
  use `_AST_STRING_LITERAL` inside the macro body instead.
- Comptime variables are **not emitted** into the output binary.

## Quasi-Quoting

`_QUOTE(tmpl, ...)` parses a C expression or statement template and splices
`_Node *` values into `$1`, `$2`, and later numbered holes.

```c
[[jcc::macro]]
_Node *square(_Node *x) {
    return _QUOTE("($1) * ($1)", x);
}

int main(void) {
    return square(6);
}
```

Numbered holes can be reused and reordered. Use `$$` for sequential left-to-
right holes when order is enough:

```c
[[jcc::macro]]
_Node *sum2(_Node *a, _Node *b) {
    return _QUOTE("$$ + $$", a, b);
}
```

Do not mix `$N` and `$$` in one template. Use `_QUOTE_N(tmpl, nodes, count)`
when splice nodes are already in an array.

### List splicing with `$@N` and `$@`

`$@k;` in **statement-list position** expands an entire `->next`-linked node
chain into the block, replacing one placeholder with N statements. This is
typed unquote-splicing.

```c
[[jcc::macro]]
_Node *double_inc(_Node *x) {
    _Node *chain = _NODE_LIST((_Node*[]){
        _QUOTE("$1 += 1;", x),
        _QUOTE("$1 += 1;", x),
    }, 2);
    return _QUOTE("{ $@1; }", chain);
}

int get_plus_two(int v) {
    double_inc(v);
    return v;  // v + 2
}
```

`$@` is the incremental (sequential) form, parallel to `$$`:

```c
[[jcc::macro]]
_Node *two_increments(_Node *a, _Node *b) {
    return _QUOTE("{ $@; $@; }",
                  _QUOTE("$1 += 10;", a),
                  _QUOTE("$1 += 20;", b));
}
```

You can mix a scalar `$N` and a list `$@N` in the same template when both are
positional. `_NODE_LIST(arr, count)` builds the `->next` chain from an array.
An existing `->next` chain (e.g. `_AST_BLOCK(...)->body`) can also be passed
directly as the splice argument.

List splices are valid **only in statement-list position** (inside a `{ ... }`
block). Using `$@k` as an expression operand is a compile-time error. Call-arg
splicing and initializer splicing are not yet supported.

### `_QUOTE` inside generated function bodies

`_QUOTE("return x;")` needs to know the enclosing function's return type to
apply the correct implicit cast. When building a generated function body, wrap
the quote call in `_AST_WITH_FN(fn)` to establish that context:

```c
[[jcc::macro]]
void generate_answer(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("answer", int_ty);
    _AST_WITH_FN(fn) {
        _AST_FUNCTION_SET_BODY(fn, _QUOTE("return 42;"));
    }
}
generate_answer();
```

Without `_AST_WITH_FN`, `_QUOTE("return x;")` at file scope (where there is no
enclosing function) will compile but the implicit return-type cast is skipped.
For macros that only use `_AST_RETURN(_AST_INT_LITERAL(...))` directly this does
not matter; it matters when the template produces a `return` statement.

## Type And Symbol Reflection

Macros can inspect types and global symbols that are visible at the macro
execution point.

```c
typedef enum { RED, GREEN, BLUE } Color;

[[jcc::macro]]
_Node *color_count(void) {
    _Type *color = _AST_FIND_TYPE("Color");
    if (!color)
        return _AST_INT_LITERAL(-1);
    return _AST_INT_LITERAL(_AST_ENUM_COUNT(color));
}

int main(void) {
    return color_count();
}
```

Useful reflection entry points include:

| Task | API |
|------|-----|
| Find a type by name | `_AST_FIND_TYPE(name)` |
| Get a built-in or named type | `_AST_GET_TYPE(name)` |
| Count enum constants | `_AST_ENUM_COUNT(ty)` |
| Read enum constants | `_AST_ENUM_AT(ty, i)`, `_AST_ENUM_CONSTANT_NAME(ec)`, `_AST_ENUM_CONSTANT_VALUE(ec)` |
| Count struct/union members | `_AST_STRUCT_MEMBER_COUNT(ty)` |
| Read members | `_AST_STRUCT_MEMBER_AT(ty, i)`, `_AST_MEMBER_NAME(m)`, `_AST_MEMBER_TYPE(m)`, `_AST_MEMBER_OFFSET(m)` |
| Find globals | `_AST_FIND_GLOBAL(name)`, `_AST_GLOBAL_COUNT()`, `_AST_GLOBAL_AT(i)` |

For call-site macro expansion, `_AST_VAR_REF(name)` and `_AST_FIND_TYPE(name)`
use the lexical scope where the macro call appears, including nested block
locals and typedefs. When a macro receives an expression argument and needs the
exact variable passed by the caller, prefer inspecting the argument node itself
instead of looking it up again by string name.

## Function Generation

Generated functions are `_Obj *` values. Create the object, add parameters,
build a body, and install the body.

```c
[[jcc::macro]]
void generate_is_even(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("is_even", int_ty);
    _AST_FUNCTION_ADD_PARAM(fn, "n", int_ty);

    _Node *n = _AST_PARAM_REF(fn, "n");
    _AST_WITH_FN(fn) {
        _AST_FUNCTION_SET_BODY(fn, _QUOTE("return $1 % 2 == 0;", n));
    }
}
generate_is_even();

int main(void) {
    return is_even(42) ? 42 : 1;
}
```

For global-generation macros, JCC automatically synthesizes forward
declarations for every generated function definition, so `_AST_FORWARD_DECLARE`
is no longer required. You only need it when a macro creates a prototype that
later source should reference before the definition is provided (e.g. a
prototype generated by one macro and promoted to a definition by another).

`_AST_FUNCTION(name, ret_type)` promotes an existing forward declaration with
the same name to a generated definition. If a definition already exists, JCC
emits a compile-time error instead of silently replacing it. Use
`_GENSYM(prefix)` or `__jcc_gensym(_VM, prefix)` for private helper names.

```c
[[jcc::macro]]
void make_helper(void) {
    const char *name = _GENSYM("helper");
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION(name, int_ty);
    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(42)));
}
make_helper();
```

## Global Variable Generation

Macros can emit global variables with initial data. Use `_AST_MAKE_ARRAY` to
size the type to match the data length; the codegen copies exactly `ty->size`
bytes from the init data.

```c
[[jcc::macro]]
void embed_version(void) {
    _Type *char_ty = _AST_GET_TYPE("char");
    _Type *arr_ty  = _AST_MAKE_ARRAY(char_ty, 8);
    _Obj  *var     = _AST_GLOBAL_VAR("version_str", arr_ty);
    _AST_GLOBAL_VAR_SET_INIT_DATA(var, "1.0.0\0\0", 8);
    _AST_GLOBAL_VAR_SET_STATIC(var, 1);  // internal linkage
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
[[jcc::macro]]
void embed_version(void) { ... }
embed_version();
```

## Local Variables

Macros that expand into statements can inject locals into the current function.
Prefer `_AST_LOCAL_VAR_UNIQUE(ty)` for temporary variables; it creates a name
that user source cannot capture.

```c
[[jcc::macro]]
_Node *save_then_return(_Node *value) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Node *tmp = _AST_LOCAL_VAR_UNIQUE(int_ty);
    _Node *stmts[2] = {
        _AST_EXPR_STMT(_AST_ASSIGN(tmp, value)),
        _AST_RETURN(tmp),
    };
    return _AST_BLOCK(stmts, 2);
}
```

Use `_AST_LOCAL_VAR(name, ty)` only when the generated local is meant to have a
specific user-visible name.

## Diagnostics And Debugging

Use source-located diagnostics when rejecting a macro argument:

```c
[[jcc::macro]]
_Node *require_nonzero(_Node *value) {
    if (!value)
        _MACRO_ERROR_AT(value, "expected an expression");
    return value;
}
```

Builder-created nodes automatically use the macro invocation as their source
location. When a diagnostic should point somewhere else, use the location
helpers explicitly:

```c
[[jcc::macro]]
_Node *checked_double(_Node *value) {
    _Node *expr = _AST_BINARY(_ADD, value, value);
    return _AST_COPY_LOCATION(expr, value);
}
```

Use `_AST_SYNTHETIC_TOKEN(label)` for diagnostics that belong to deliberately
generated code rather than the call site or an input expression:

```c
[[jcc::macro]]
_Node *generated_error(void) {
    _Node *expr = _AST_INT_LITERAL(0);
    _AST_SET_TOKEN(expr, _AST_SYNTHETIC_TOKEN("generated expression"));
    _MACRO_ERROR_AT(expr, "generated expression is invalid here");
    return expr;
}
```

AST dump helpers are available while developing macros:

| Helper | Use |
|--------|-----|
| `_DUMP_TREE(node)` | Print a readable tree |
| `_DUMP_TREE_TO_STRING(node)` | Render a tree into a string |
| `_DUMP_AST_GEN(node)` | Print builder calls for a node |
| `_DUMP_AST_GEN_TO_STRING(node)` | Render builder calls into a string |

The interactive VM debugger (`-g`) does not currently stop inside macro
execution. Macro bytecode runs during compilation, before the final program is
started under the debugger, and JCC suppresses VM debug tracing while invoking
macro functions. Use `_DUMP_*` helpers and source-located diagnostics for macro
debugging.

### macroexpand — macro expansion

Two functions are provided, matching Lisp's `macroexpand-1` / `macroexpand`
pair.

**`_MACROEXPAND_1(node)`** expands a single macro call node exactly once,
without splicing the result into the AST or recursing. Useful when you need
to observe one expansion step at a time or write meta-macros that inspect
intermediate forms.

**`_MACROEXPAND(node)`** repeatedly calls `_MACROEXPAND_1` on the top-level
node until it is no longer a macro call (the form is *stable*). It does not
recurse into child nodes — only the outermost call is expanded. The VM's
`macro_recursion_limit` applies; exceeding it is a compile error.

```c
[[jcc::macro(inline)]]
_Node *make_answer(void) { return _AST_INT_LITERAL(42); }

[[jcc::macro(inline)]]
_Node *wrap_answer(void) { return _QUOTE("make_answer()"); }

[[jcc::comptime]]
void debug_macro(void) {
    _Node *call = _QUOTE("wrap_answer()");

    // One step: wrap_answer() -> make_answer() (still a macro call)
    _Node *step1 = _MACROEXPAND_1(call);
    _DUMP_TREE(step1);

    // Full expansion: wrap_answer() -> make_answer() -> 42
    _Node *full = _MACROEXPAND(call);
    _DUMP_TREE(full);
}
```

If `node` is not a macro call, both functions return `node` unchanged
(identity). If the named macro is not found or has not compiled, `node` is
returned unchanged.

Underlying functions: `__jcc_macroexpand_1(JCC *vm, _Node *node)` and
`__jcc_macroexpand(JCC *vm, _Node *node)`.

## API Reference

### Type APIs

| Convenience macro | Description |
|-------------------|-------------|
| `_AST_FIND_TYPE(name)` | Find a typedef, struct, union, or enum by name |
| `_AST_TYPE_EXISTS(name)` | Test whether a type name exists |
| `_AST_GET_TYPE(name)` | Get a named or built-in type such as `"int"` |
| `_AST_TYPE_KIND(ty)` | Get `_TypeKind` |
| `_AST_TYPE_SIZE(ty)` | Get size in bytes |
| `_AST_TYPE_ALIGN(ty)` | Get alignment in bytes |
| `_AST_TYPE_IS_UNSIGNED(ty)` | Test unsigned integer type |
| `_AST_TYPE_IS_CONST(ty)` | Test const-qualified type |
| `_AST_TYPE_BASE(ty)` | Get pointer or array base type |
| `_AST_TYPE_ARRAY_LEN(ty)` | Get array length, or `-1` |
| `_AST_TYPE_RETURN_TYPE(ty)` | Get function return type |
| `_AST_TYPE_PARAM_COUNT(ty)` | Count function parameters |
| `_AST_TYPE_PARAM_AT(ty, i)` | Get function parameter type |
| `_AST_TYPE_IS_VARIADIC(ty)` | Test variadic function type |
| `_AST_TYPE_NAME(ty)` | Get a type name when available |
| `_AST_MAKE_POINTER(base)` | Create pointer type |
| `_AST_MAKE_ARRAY(base, len)` | Create array type |

### Enum APIs

| Convenience macro | Description |
|-------------------|-------------|
| `_AST_ENUM_COUNT(ty)` | Count enum constants |
| `_AST_ENUM_AT(ty, i)` | Get enum constant at index |
| `_AST_ENUM_FIND(ty, name)` | Find enum constant by name |
| `_AST_ENUM_CONSTANT_NAME(ec)` | Get enum constant name |
| `_AST_ENUM_CONSTANT_VALUE(ec)` | Get enum constant value |
| `_AST_ENUM_NAME(ty)` | Get enum type name |
| `_AST_ENUM_VALUE_COUNT(ty)` | Count enum values |
| `_AST_ENUM_VALUE_NAME(ty, i)` | Get enum value name |
| `_AST_ENUM_VALUE(ty, i)` | Get enum value |

### Struct And Union APIs

| Convenience macro | Description |
|-------------------|-------------|
| `_AST_STRUCT_MEMBER_COUNT(ty)` | Count members |
| `_AST_STRUCT_MEMBER_AT(ty, i)` | Get member at index |
| `_AST_STRUCT_MEMBER_FIND(ty, name)` | Find member by name |
| `_AST_MEMBER_NAME(m)` | Get member name |
| `_AST_MEMBER_TYPE(m)` | Get member type |
| `_AST_MEMBER_OFFSET(m)` | Get member offset |
| `_AST_MEMBER_IS_BITFIELD(m)` | Test bitfield member |
| `_AST_MEMBER_BITFIELD_WIDTH(m)` | Get bitfield width |

### Global Symbol APIs

| Convenience macro | Description |
|-------------------|-------------|
| `_AST_FIND_GLOBAL(name)` | Find global object |
| `_AST_GLOBAL_COUNT()` | Count globals |
| `_AST_GLOBAL_AT(i)` | Get global at index |
| `_AST_OBJ_NAME(obj)` | Get object name |
| `_AST_OBJ_TYPE(obj)` | Get object type |
| `_AST_OBJ_IS_FUNCTION(obj)` | Test function object |
| `_AST_OBJ_IS_DEFINITION(obj)` | Test definition |
| `_AST_OBJ_IS_STATIC(obj)` | Test static linkage |

### AST Builder APIs

| Convenience macro | Description |
|-------------------|-------------|
| `_AST_INT_LITERAL(val)` | Integer literal |
| `_AST_FLOAT_LITERAL(val)` | Floating-point literal |
| `_AST_STRING_LITERAL(str)` | String literal |
| `_AST_VAR_REF(name)` | Variable reference |
| `_AST_PARAM_REF(fn, name)` | Generated function parameter reference |
| `_GENSYM(prefix)` | Unique arena-allocated symbol name using `__jcc_gensym` |
| `_MACROEXPAND_1(node)` | Single-step macro expansion (identity if not a macro call) |
| `_MACROEXPAND(node)` | Full macro expansion — repeats until the top-level form is stable |
| `_AST_CURRENT_TOKEN()` | Opaque token for the active macro call site |
| `_AST_SYNTHETIC_TOKEN(label)` | Opaque synthetic token for generated diagnostics |
| `_AST_TOKEN_FROM_NODE(node)` | Opaque source token attached to a node |
| `_AST_SET_TOKEN(node, tok)` | Attach a token to a node and return the node |
| `_AST_COPY_LOCATION(dst, src)` | Copy source location from one node to another |
| `_NODE_LIST(arr, count)` | Build a `->next`-linked node chain from an array for use as a `$@k` splice argument |
| `_AST_BINARY(op, l, r)` | Binary expression |
| `_AST_UNARY(op, operand)` | Unary expression |
| `_AST_CAST(expr, ty)` | Cast expression |
| `_AST_ASSIGN(target, value)` | Assignment expression |
| `_AST_MEMBER(obj, name)` | Struct/union member expression |
| `_AST_FUNCALL(callee, args, n)` | Function call expression |
| `_AST_RETURN(expr)` | Return statement |
| `_AST_BLOCK(stmts, count)` | Compound statement |
| `_AST_IF(cond, then_body, else_body)` | If statement |
| `_AST_SWITCH(cond)` | Switch statement |
| `_AST_SWITCH_ADD_CASE(sw, value, body)` | Add switch case |
| `_AST_SWITCH_SET_DEFAULT(sw, body)` | Set switch default |
| `_AST_EXPR_STMT(expr)` | Expression statement |
| `_AST_LOCAL_VAR(name, ty)` | Named local variable |
| `_AST_LOCAL_VAR_UNIQUE(ty)` | Hygienic temporary local |
| `_AST_WHILE(cond, body)` | While loop |
| `_AST_FOR(init, cond, inc, body)` | For loop |
| `_AST_DO_WHILE(body, cond)` | Do-while loop |

### Function Builder APIs

| Convenience macro | Description |
|-------------------|-------------|
| `_AST_FUNCTION(name, ret_type)` | Create a function object |
| `_AST_FORWARD_DECLARE(fn)` | Publish a generated function declaration at the current source position |
| `_AST_FUNCTION_ADD_PARAM(fn, name, ty)` | Add function parameter |
| `_AST_FUNCTION_SET_BODY(fn, body)` | Set function body |
| `_AST_FUNCTION_SET_STATIC(fn, flag)` | Set static linkage |
| `_AST_FUNCTION_SET_INLINE(fn, flag)` | Set inline flag |
| `_AST_FUNCTION_SET_VARIADIC(fn, flag)` | Set variadic flag |
| `_AST_WITH_FN(fn) { ... }` | Set `fn` as the current function context for the block so `_QUOTE("return x;")` applies the correct return-type cast |

### Global Variable Builder APIs

| Convenience macro | Description |
|-------------------|-------------|
| `_AST_GLOBAL_VAR(name, ty)` | Create a global variable definition |
| `_AST_GLOBAL_VAR_SET_INIT_DATA(var, data, len)` | Set raw initial data (`len` must equal `ty->size`) |
| `_AST_GLOBAL_VAR_SET_STATIC(var, flag)` | Set internal linkage (file-scope `static`) |

### Node Kinds

Use these constants with `_AST_BINARY()` and `_AST_UNARY()`:

```c
_ADD, _SUB, _MUL, _DIV, _MOD, _NEG
_BITAND, _BITOR, _BITXOR, _BITNOT, _SHL, _SHR
_EQ, _NE, _LT, _LE
_NOT, _LOGAND, _LOGOR
_ASSIGN, _ADDR, _DEREF, _COMMA
```

## Attribute Syntax

Both C23 attribute syntax and GNU attribute syntax are accepted everywhere:

| C23 form | GNU form |
|----------|----------|
| `[[jcc::macro]]` | `__attribute__((macro))` |
| `[[jcc::macro(inline)]]` | `__attribute__((macro(inline)))` |
| `[[jcc::comptime]]` | `__attribute__((comptime))` |

The canonical form used in this document and in JCC examples is `[[jcc::macro]]`.

## Constraints

- Macro calls accept at most 8 arguments.
- Macro code runs at compile time and cannot inspect runtime values.
- Global-generation macros compile before the main parse. They can see safe
  file-scope declarations from preprocessed includes and source, but arbitrary
  non-macro function bodies and initialized globals are not compiled into the
  macro VM.
- Global-generation macros run before the main parse, so generated definitions
  are visible everywhere. Inline macros expand at the call site and follow
  normal C declaration rules for any side-effect definitions they emit.
- `_AST_FORWARD_DECLARE(fn)` publishes function names only. Types, globals, and
  other declarations follow the normal parser state visible at the macro
  execution point.
- `void` macros cannot be used in expression position; doing so is a compile
  error.
