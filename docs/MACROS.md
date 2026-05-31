# JCC Pragma Macros

Pragma macros are C functions that JCC compiles and runs during compilation.
They can inspect compile-time types, build AST nodes, generate functions, and
replace macro call sites with generated code.

The macro API is automatically available while pragma macro and comptime helper
functions are compiled. Macro code can use the `_AST_*`, `_QUOTE*`, `_MACRO_ERROR_AT`,
`_GENSYM`, and `_DUMP_*` convenience macros directly.

## Execution Model

JCC supports three pragma macro execution forms:

| Form | Source shape | When it runs | What the return value means |
|------|--------------|--------------|-----------------------------|
| Inline generation | `#pragma macro inline _Node *gen(void)` | Before the main parse | Ignored; side effects generate declarations/functions |
| File-scope call | `gen();` at file scope | While parsing that file, at that source position | Ignored; side effects generate declarations/functions |
| Call-site expansion | `gen(args...)` inside code | During macro expansion after parsing | Replaces the call expression or statement |

The macro function itself must return `_Node *`. For generation-style macros,
return `_AST_INT_LITERAL(0)` or another harmless node; the generated functions
and declarations come from API calls made by the macro.

## Inline Generation

Use an `inline` pragma macro when generated functions should be available to the
whole parsed program without an explicit call site. Inline macros run before the
main parse, and functions created with `_AST_FUNCTION()` receive parser-visible
synthetic declarations automatically.

```c
#pragma macro
inline _Node *generate_answer(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("answer", int_ty);

    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(42)));
    return _AST_INT_LITERAL(0);
}

int main(void) {
    return answer();
}
```

Inline macros take no call-site arguments. Use them for global code generation:
boilerplate functions, enum conversion helpers, serializers, or any generated
definition that source code needs to call normally.

## File-Scope Macro Calls

Use a file-scope macro call when generation should happen at a specific source
position. The macro runs when the parser reaches the call. If the macro creates
a function that later source should call, publish it with
`_AST_FORWARD_DECLARE(fn)`.

```c
#pragma macro
_Node *generate_add(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("add", int_ty);

    _AST_FUNCTION_ADD_PARAM(fn, "a", int_ty);
    _AST_FUNCTION_ADD_PARAM(fn, "b", int_ty);

    _Node *sum = _AST_BINARY(_ADD, _AST_PARAM_REF(fn, "a"),
                             _AST_PARAM_REF(fn, "b"));
    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(sum));
    _AST_FORWARD_DECLARE(fn);

    return _AST_INT_LITERAL(0);
}

generate_add();

int main(void) {
    return add(20, 22);
}
```

The declaration created by `_AST_FORWARD_DECLARE(fn)` is visible from the macro
call position onward. Code before the file-scope call follows normal C
declaration rules and cannot call the generated function by name.

## Call-Site Expansion

A normal pragma macro call inside an expression or statement is parsed as a
macro call node. During macro expansion, JCC executes the macro and replaces the
call with the returned AST.

```c
#pragma macro
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

Macro expansion is bounded by `--macro-recursion-limit=N` to catch accidental
self-recursive expansions. The default is 256. Set the limit to 0 to disable
the check.

Statement macros work the same way. Return a statement node such as
`_AST_RETURN(...)`, `_AST_IF(...)`, `_AST_BLOCK(...)`, or a statement parsed with
`_QUOTE(...)`.

```c
#pragma macro
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

Use `#pragma comptime` for helper functions that should be callable by pragma
macros but should not expand from ordinary program call sites.

```c
#pragma comptime
int plus_one(int n) {
    return n + 1;
}

#pragma macro
_Node *make_value(void) {
    return _AST_INT_LITERAL(plus_one(41));
}

int main(void) {
    return make_value();
}
```

Pragma macros and comptime helpers are compiled together, so they can call each
other even when the callee appears later in the translation unit.

## Quasi-Quoting

`_QUOTE(tmpl, ...)` parses a C expression or statement template and splices
`_Node *` values into `$1`, `$2`, and later numbered holes.

```c
#pragma macro
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
#pragma macro
_Node *sum2(_Node *a, _Node *b) {
    return _QUOTE("$$ + $$", a, b);
}
```

Do not mix `$N` and `$$` in one template. Use `_QUOTE_N(tmpl, nodes, count)`
when splice nodes are already in an array.

## Type And Symbol Reflection

Macros can inspect types and global symbols that are visible at the macro
execution point.

```c
typedef enum { RED, GREEN, BLUE } Color;

#pragma macro
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
#pragma macro
inline _Node *generate_is_even(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("is_even", int_ty);
    _AST_FUNCTION_ADD_PARAM(fn, "n", int_ty);

    _Node *n = _AST_PARAM_REF(fn, "n");
    _Node *body = _AST_RETURN(_QUOTE("$1 % 2 == 0", n));
    _AST_FUNCTION_SET_BODY(fn, body);

    return _AST_INT_LITERAL(0);
}

int main(void) {
    return is_even(42) ? 42 : 1;
}
```

For file-scope explicit calls, call `_AST_FORWARD_DECLARE(fn)` after the
signature is complete and before source code needs the generated function name.
Inline macros handle parser-visible declarations automatically.

`_AST_FUNCTION(name, ret_type)` promotes an existing forward declaration with
the same name to a generated definition. If a definition already exists, JCC
emits a compile-time error instead of silently replacing it. Use
`_GENSYM(prefix)` or `__jcc_gensym(_VM, prefix)` for private helper names.

```c
#pragma macro
_Node *make_helper(void) {
    const char *name = _GENSYM("helper");
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION(name, int_ty);

    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(42)));
    return _AST_INT_LITERAL(0);
}
```

## Local Variables

Macros that expand into statements can inject locals into the current function.
Prefer `_AST_LOCAL_VAR_UNIQUE(ty)` for temporary variables; it creates a name
that user source cannot capture.

```c
#pragma macro
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
#pragma macro
_Node *require_nonzero(_Node *value) {
    if (!value)
        _MACRO_ERROR_AT(value, "expected an expression");
    return value;
}
```

AST dump helpers are available while developing macros:

| Helper | Use |
|--------|-----|
| `_DUMP_TREE(node)` | Print a readable tree |
| `_DUMP_TREE_TO_STRING(node)` | Render a tree into a string |
| `_DUMP_AST_GEN(node)` | Print builder calls for a node |
| `_DUMP_AST_GEN_TO_STRING(node)` | Render builder calls into a string |

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

### Node Kinds

Use these constants with `_AST_BINARY()` and `_AST_UNARY()`:

```c
_ADD, _SUB, _MUL, _DIV, _MOD, _NEG
_BITAND, _BITOR, _BITXOR, _BITNOT, _SHL, _SHR
_EQ, _NE, _LT, _LE
_NOT, _LOGAND, _LOGOR
_ASSIGN, _ADDR, _DEREF, _COMMA
```

## Constraints

- Pragma macro calls accept at most 8 arguments.
- Macro code runs at compile time and cannot inspect runtime values.
- File-scope explicit generation follows source order. Use inline macros for
  whole-program pre-parse generation, or `_AST_FORWARD_DECLARE(fn)` for
  explicit file-scope publication.
- `_AST_FORWARD_DECLARE(fn)` publishes function names only. Types, globals, and
  other declarations follow the normal parser state visible at the macro
  execution point.
