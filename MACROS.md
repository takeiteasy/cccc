# JCC Pragma Macros

JCC introduces a powerful compile-time metaprogramming feature called **Pragma Macros**. This allows you to write C functions that are executed *during compilation* to generate code, inspect types, and perform reflection.

## Overview

A pragma macro is a standard C function prefixed with `#pragma macro`. When JCC encounters a call to this function:
1. The macro function is compiled to bytecode during the parsing phase
2. When the call site is reached during AST traversal, the macro is executed
3. The returned AST `_Node*` replaces the original function call in the AST
4. Compilation continues with the transformed AST

This enables Lisp-like macros in C, allowing you to write functions that generate code.

## Basic Usage

To define a pragma macro:
1. Add `#pragma macro` before a function definition
2. The function must return a `_Node*` (an AST node)
3. Use the reflection API; `<reflection.h>` is included implicitly while pragma
   macro functions are compiled

```c
#pragma macro
_Node *make_const_5(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    return __jcc_ast_int_literal(vm, 5);
}

int main(void) {
    int x = make_const_5(); // Replaced at compile-time with: int x = 5;
    return x - 5;           // Returns 0
}
```

## The VM Context

All AST construction and reflection functions require access to the compiler state (the VM). Use `__jcc_get_vm()` to obtain this context:

```c
#pragma macro
_Node *my_macro(void) {
    _VirtualMachine *vm = __jcc_get_vm();  // Get the VM context
    return __jcc_ast_int_literal(vm, 42);
}
```

The implicitly included `<reflection.h>` also provides convenience macros that automatically pass `__jcc_get_vm()`:

```c
#pragma macro
_Node *my_macro(void) {
    return _AST_INT_LITERAL(42);  // Equivalent to __jcc_ast_int_literal(__jcc_get_vm(), 42)
}
```

## Inline Pragma Macros

An `inline` pragma macro runs automatically at its declaration point — no explicit call site is required. This is useful for macros that generate functions or definitions that need to be visible to the rest of the translation unit.

Add the `inline` keyword between `#pragma macro` and the return type:

```c
#pragma macro
inline _Node *generate_add(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    _Type *int_type = __jcc_ast_get_type(vm, "int");

    _Obj *fn = __jcc_ast_function(vm, "add", int_type);
    __jcc_ast_function_add_param(vm, fn, "a", int_type);
    __jcc_ast_function_add_param(vm, fn, "b", int_type);

    _Node *body = __jcc_ast_return(vm,
        __jcc_ast_binary(vm, _ADD,
            __jcc_ast_param_ref(vm, fn, "a"),
            __jcc_ast_param_ref(vm, fn, "b")));
    __jcc_ast_function_set_body(vm, fn, body);

    return __jcc_ast_int_literal(vm, 0); // return value is ignored
}

int main(void) {
    // No macro call and no forward declaration needed.
    // generate_add() ran at declaration; add() is now defined.
    return add(20, 22) - 42;  // Returns 0
}
```

Key differences from regular pragma macros:

- **Auto-execution**: the macro runs when the compiler reaches its declaration, not when it is called.
- **No call site**: the function never appears in the program's source as a call.
- **Automatic forward declarations**: any functions created with `__jcc_ast_function()` inside an inline macro get synthetic forward declarations prepended to the token stream, so later code can call them without manual `extern` declarations.
- **No arguments**: inline macros always take `void`; they cannot receive `_Node*` arguments from a call site.

## Macro Arguments

Pragma macros can receive arguments. The arguments are passed as `_Node*` pointers to the original AST nodes from the call site:

```c
#pragma macro
_Node *double_it(_Node *value) {
    _VirtualMachine *vm = __jcc_get_vm();
    // Create: (value + value)
    return __jcc_ast_binary(vm, _ADD, value, value);
}

int main(void) {
    int x = double_it(21);  // Becomes: int x = (21 + 21);
    return x - 42;          // Returns 0
}
```

## Quasi-Quoting

For many macros, writing a C template is clearer than manually composing
builder calls. `__jcc_quote()` parses an expression or statement template during
macro execution and returns the generated AST node. Use `$1`, `$2`, ... to
splice macro argument nodes into the template.

```c
#pragma macro
_Node *square(_Node *x) {
    return _QUOTE("($1) * ($1)", x);
}

int main(void) {
    return square(6) - 36;
}
```

Numbered splice points can be reused or reordered. `$$` is also supported for
sequential left-to-right splices, but do not mix `$$` and `$N` in the same
template.

Use `__jcc_quote_n()` when the number of splice nodes is dynamic or too large for
the variadic `__jcc_quote()` call:

```c
#pragma macro
_Node *add_three(_Node *a, _Node *b, _Node *c) {
    _Node *nodes[3] = {a, b, c};
    return __jcc_quote_n(__jcc_get_vm(), "$1 + $2 + $3", nodes, 3);
}
```

Templates may be expressions or statements. Statement templates are useful when
combined with block or control-flow builders.

## Type Reflection

One of the most powerful features is type introspection. You can inspect types defined in your program during compilation.

### Enum Reflection Example

```c
#include <string.h>

typedef enum { COLOR_RED, COLOR_GREEN, COLOR_BLUE } Color;

#pragma macro
_Node *enum_count_check(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    
    // Find the Color type by name
    _Type *color_type = __jcc_ast_find_type(vm, "Color");
    if (!color_type) {
        return __jcc_ast_string_literal(vm, "type_not_found");
    }
    
    // Get the number of enum constants
    int count = __jcc_ast_enum_count(vm, color_type);
    
    if (count == 3) {
        return __jcc_ast_string_literal(vm, "has_3_colors");
    }
    return __jcc_ast_string_literal(vm, "unexpected_count");
}

int main(void) {
    const char *result = enum_count_check();
    return strcmp(result, "has_3_colors");  // Returns 0 if equal
}
```

### Iterating Enum Constants

```c
#pragma macro
_Node *get_first_enum_name(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    
    _Type *color_type = __jcc_ast_find_type(vm, "Color");
    if (!color_type) {
        return __jcc_ast_string_literal(vm, "unknown");
    }
    
    // Get the first enum constant
    _EnumConstant *ec = __jcc_ast_enum_at(vm, color_type, 0);
    if (ec) {
        const char *name = __jcc_ast_enum_constant_name(ec);
        return __jcc_ast_string_literal(vm, name);  // Returns "COLOR_RED"
    }
    return __jcc_ast_string_literal(vm, "no_constants");
}
```

## API Reference

### VM Context

| Function | Description |
|----------|-------------|
| `__jcc_get_vm()` | Get the current VM context (required for all API calls) |

### Diagnostics

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_error_at(vm, node, fmt, ...)` | `_ERROR_AT(node, ...)` | Emit a source-located compiler error at `node` |
| `__jcc_warning_at(vm, node, fmt, ...)` | `_WARNING_AT(node, ...)` | Emit a source-located compiler warning at `node` |

### Quoting

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_quote(vm, tmpl, ...)` | `_QUOTE(tmpl, ...)` | Parse a C template and splice `_Node*` arguments |
| `__jcc_quote_n(vm, tmpl, nodes, count)` | `_QUOTE_N(tmpl, nodes, count)` | Array-form quote for dynamic or larger splice lists |

### Type Lookup

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_ast_find_type(vm, name)` | `_AST_FIND_TYPE(name)` | Find a type by name (typedef, struct, enum) |
| `__jcc_ast_type_exists(vm, name)` | `_AST_TYPE_EXISTS(name)` | Check if a type exists |
| `__jcc_ast_get_type(vm, name)` | `_AST_GET_TYPE(name)` | Get type (includes built-in types like "int") |

### Type Introspection

These functions take no `vm` argument, so their macros are plain aliases.

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_ast_type_kind(ty)` | `_AST_TYPE_KIND(ty)` | Get `_TypeKind` (`_INT`, `_ENUM`, `_STRUCT`, etc.) |
| `__jcc_ast_type_size(ty)` | `_AST_TYPE_SIZE(ty)` | Get `sizeof()` in bytes |
| `__jcc_ast_type_align(ty)` | `_AST_TYPE_ALIGN(ty)` | Get alignment in bytes |
| `__jcc_ast_type_is_unsigned(ty)` | `_AST_TYPE_IS_UNSIGNED(ty)` | Check if unsigned |
| `__jcc_ast_type_is_const(ty)` | `_AST_TYPE_IS_CONST(ty)` | Check if const-qualified |
| `__jcc_ast_type_base(ty)` | `_AST_TYPE_BASE(ty)` | For pointer/array: get base type |
| `__jcc_ast_type_array_len(ty)` | `_AST_TYPE_ARRAY_LEN(ty)` | For arrays: get length (-1 if not array) |
| `__jcc_ast_type_return_type(ty)` | `_AST_TYPE_RETURN_TYPE(ty)` | For functions: get return type |
| `__jcc_ast_type_param_count(ty)` | `_AST_TYPE_PARAM_COUNT(ty)` | For functions: get parameter count |
| `__jcc_ast_type_param_at(ty, index)` | `_AST_TYPE_PARAM_AT(ty, i)` | For functions: get parameter type at index |
| `__jcc_ast_type_is_variadic(ty)` | `_AST_TYPE_IS_VARIADIC(ty)` | For functions: check variadic flag |
| `__jcc_ast_type_name(ty)` | `_AST_TYPE_NAME(ty)` | Get the type name when available |

### Type Construction

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_ast_make_pointer(vm, base)` | `_AST_MAKE_POINTER(base)` | Create pointer type |
| `__jcc_ast_make_array(vm, base, len)` | `_AST_MAKE_ARRAY(base, len)` | Create array type |

### Enum Reflection

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_ast_enum_count(vm, ty)` | `_AST_ENUM_COUNT(ty)` | Number of constants (-1 if not enum) |
| `__jcc_ast_enum_at(vm, ty, i)` | `_AST_ENUM_AT(ty, i)` | Get constant at index |
| `__jcc_ast_enum_find(vm, ty, name)` | `_AST_ENUM_FIND(ty, name)` | Find constant by name |
| `__jcc_ast_enum_constant_name(ec)` | `_AST_ENUM_CONSTANT_NAME(ec)` | Get constant name |
| `__jcc_ast_enum_constant_value(ec)` | `_AST_ENUM_CONSTANT_VALUE(ec)` | Get constant integer value |
| `__jcc_ast_enum_name(ty)` | `_AST_ENUM_NAME(ty)` | Get enum type name |
| `__jcc_ast_enum_value_count(ty)` | `_AST_ENUM_VALUE_COUNT(ty)` | Get enum value count |
| `__jcc_ast_enum_value_name(ty, i)` | `_AST_ENUM_VALUE_NAME(ty, i)` | Get enum value name at index |
| `__jcc_ast_enum_value(ty, i)` | `_AST_ENUM_VALUE(ty, i)` | Get enum integer value at index |

### Struct/Union Reflection

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_ast_struct_member_count(vm, ty)` | `_AST_STRUCT_MEMBER_COUNT(ty)` | Number of members |
| `__jcc_ast_struct_member_at(vm, ty, i)` | `_AST_STRUCT_MEMBER_AT(ty, i)` | Get member at index |
| `__jcc_ast_struct_member_find(vm, ty, name)` | `_AST_STRUCT_MEMBER_FIND(ty, name)` | Find member by name |
| `__jcc_ast_member_name(m)` | `_AST_MEMBER_NAME(m)` | Get member name |
| `__jcc_ast_member_type(m)` | `_AST_MEMBER_TYPE(m)` | Get member type |
| `__jcc_ast_member_offset(m)` | `_AST_MEMBER_OFFSET(m)` | Get member offset in bytes |
| `__jcc_ast_member_is_bitfield(m)` | `_AST_MEMBER_IS_BITFIELD(m)` | Check if member is a bitfield |
| `__jcc_ast_member_bitfield_width(m)` | `_AST_MEMBER_BITFIELD_WIDTH(m)` | Get bitfield width in bits |

### Global Symbol Reflection

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_ast_find_global(vm, name)` | `_AST_FIND_GLOBAL(name)` | Find global symbol by name |
| `__jcc_ast_global_count(vm)` | `_AST_GLOBAL_COUNT()` | Count global symbols |
| `__jcc_ast_global_at(vm, i)` | `_AST_GLOBAL_AT(i)` | Get global symbol at index |
| `__jcc_ast_obj_name(obj)` | `_AST_OBJ_NAME(obj)` | Get object name |
| `__jcc_ast_obj_type(obj)` | `_AST_OBJ_TYPE(obj)` | Get object type |
| `__jcc_ast_obj_is_function(obj)` | `_AST_OBJ_IS_FUNCTION(obj)` | Check if object is a function |
| `__jcc_ast_obj_is_definition(obj)` | `_AST_OBJ_IS_DEFINITION(obj)` | Check if object has a definition |
| `__jcc_ast_obj_is_static(obj)` | `_AST_OBJ_IS_STATIC(obj)` | Check if object has static linkage |

### AST Construction - Literals

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_ast_int_literal(vm, val)` | `_AST_INT_LITERAL(val)` | Create integer literal |
| `__jcc_ast_float_literal(vm, val)` | `_AST_FLOAT_LITERAL(val)` | Create float literal |
| `__jcc_ast_string_literal(vm, str)` | `_AST_STRING_LITERAL(str)` | Create string literal |
| `__jcc_ast_var_ref(vm, name)` | `_AST_VAR_REF(name)` | Reference a variable by name |
| `__jcc_ast_param_ref(vm, fn, name)` | `_AST_PARAM_REF(fn, name)` | Reference a generated function parameter |

### AST Construction - Expressions

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_ast_binary(vm, op, l, r)` | `_AST_BINARY(op, l, r)` | Binary operation (_ADD, _MUL, etc.) |
| `__jcc_ast_unary(vm, op, expr)` | `_AST_UNARY(op, expr)` | Unary operation (_NEG, _NOT, etc.) |
| `__jcc_ast_cast(vm, expr, ty)` | `_AST_CAST(expr, ty)` | Type cast |
| `__jcc_ast_assign(vm, target, value)` | `_AST_ASSIGN(target, value)` | Assignment expression |
| `__jcc_ast_member(vm, obj, name)` | `_AST_MEMBER(obj, name)` | Struct/union member access |
| `__jcc_ast_funcall(vm, callee, args, n)` | `_AST_FUNCALL(callee, args, n)` | Function call expression |

### AST Construction - Statements

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_ast_return(vm, expr)` | `_AST_RETURN(expr)` | Return statement |
| `__jcc_ast_block(vm, stmts, n)` | `_AST_BLOCK(stmts, n)` | Block of statements |
| `__jcc_ast_if(vm, cond, then, else)` | `_AST_IF(c, t, e)` | If statement |
| `__jcc_ast_switch(vm, cond)` | `_AST_SWITCH(cond)` | Switch statement |
| `__jcc_ast_switch_add_case(vm, sw, val, body)` | `_AST_SWITCH_ADD_CASE(sw, v, b)` | Add case to switch |
| `__jcc_ast_switch_set_default(vm, sw, body)` | `_AST_SWITCH_SET_DEFAULT(sw, b)` | Set default case |
| `__jcc_ast_expr_stmt(vm, expr)` | `_AST_EXPR_STMT(expr)` | Expression statement |
| `__jcc_ast_local_var(vm, name, ty)` | `_AST_LOCAL_VAR(name, ty)` | Inject a named local variable |
| `__jcc_ast_local_var_unique(vm, ty)` | `_AST_LOCAL_VAR_UNIQUE(ty)` | Inject a hygienic temporary local |
| `__jcc_ast_while(vm, cond, body)` | `_AST_WHILE(cond, body)` | While loop |
| `__jcc_ast_for(vm, init, cond, inc, body)` | `_AST_FOR(init, cond, inc, body)` | For loop |
| `__jcc_ast_do_while(vm, body, cond)` | `_AST_DO_WHILE(body, cond)` | Do-while loop |

### AST Construction - Function Generation

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_ast_function(vm, name, ret_type)` | `_AST_FUNCTION(name, ret_type)` | Create a new function |
| `__jcc_ast_function_add_param(vm, fn, name, ty)` | `_AST_FUNCTION_ADD_PARAM(fn, name, ty)` | Add parameter to function |
| `__jcc_ast_function_set_body(vm, fn, body)` | `_AST_FUNCTION_SET_BODY(fn, body)` | Set function body |
| `__jcc_ast_function_set_static(fn, is_static)` | - | Set static linkage |
| `__jcc_ast_function_set_inline(fn, is_inline)` | - | Set inline attribute |
| `__jcc_ast_function_set_variadic(fn, is_variadic)` | - | Set variadic attribute |

### AST Dumps

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `__jcc_dump_tree(vm, node)` | `_DUMP_TREE(node)` | Print a human-readable AST tree |
| `__jcc_dump_tree_to_string(vm, node)` | `_DUMP_TREE_TO_STRING(node)` | Render a human-readable AST tree to a string |
| `__jcc_dump_ast_gen(vm, node)` | `_DUMP_AST_GEN(node)` | Print builder calls that reconstruct a node |
| `__jcc_dump_ast_gen_to_string(vm, node)` | `_DUMP_AST_GEN_TO_STRING(node)` | Render builder calls to a string |

### Node Kinds (for __jcc_ast_binary/__jcc_ast_unary)

```c
// Arithmetic
_ADD, _SUB, _MUL, _DIV, _MOD, _NEG

// Bitwise
_BITAND, _BITOR, _BITXOR, _BITNOT, _SHL, _SHR

// Comparison
_EQ, _NE, _LT, _LE

// Logical
_NOT, _LOGAND, _LOGOR

// Other
_ASSIGN, _ADDR, _DEREF, _COMMA
```

## Function Generation

Pragma macros can generate entire functions at compile-time. This is useful for generating boilerplate code, serializers, enum-to-string converters, and more.

### Basic Function Generation

```c
// Forward declare the function we'll generate
int generated_func(void);

#pragma macro
_Node *generate_func(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    
    // Create a function that returns 42
    _Obj *fn = __jcc_ast_function(vm, "generated_func", __jcc_ast_get_type(vm, "int"));
    
    // Set the function body: return 42;
    _Node *body = __jcc_ast_return(vm, __jcc_ast_int_literal(vm, 42));
    __jcc_ast_function_set_body(vm, fn, body);
    
    // Return NULL - the function is already added to globals
    return _AST_INT_LITERAL(0);  // Placeholder, value ignored
}

int main(void) {
    generate_func();  // Macro runs at compile-time, generates the function
    
    int result = generated_func();  // Call the generated function
    return result - 42;  // Returns 0
}
```

### Function Generation API

| Function | Description |
|----------|-------------|
| `__jcc_ast_function(vm, name, ret_type)` | Create a new function object |
| `__jcc_ast_function_add_param(vm, fn, name, type)` | Add a parameter to the function |
| `__jcc_ast_function_set_body(vm, fn, body)` | Set the function body (statement node) |
| `__jcc_ast_function_set_static(fn, is_static)` | Set static linkage |
| `__jcc_ast_function_set_inline(fn, is_inline)` | Set inline attribute |
| `__jcc_ast_function_set_variadic(fn, is_variadic)` | Set variadic attribute |

### Important Notes

1. **Forward declarations required**: You must forward-declare the generated function before the macro call so the compiler knows its signature when it's called later.

2. **Macro return value**: The macro still needs to return a `_Node*`, but for function generation this is typically just a placeholder value (like `_AST_INT_LITERAL(0)`).

3. **Function lookup**: `__jcc_ast_function()` checks for existing forward declarations and updates them rather than creating duplicates.

### Generating Functions with Parameters

```c
// Forward declare
int add_numbers(int a, int b);

#pragma macro
_Node *gen_add_func(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    
    _Type *int_type = __jcc_ast_get_type(vm, "int");
    _Obj *fn = __jcc_ast_function(vm, "add_numbers", int_type);
    
    // Add parameters
    __jcc_ast_function_add_param(vm, fn, "a", int_type);
    __jcc_ast_function_add_param(vm, fn, "b", int_type);
    
    // Body: return a + b;
    // Use __jcc_ast_param_ref to reference parameters, and assign to intermediate variables
    _Node *a_ref = __jcc_ast_param_ref(vm, fn, "a");
    _Node *b_ref = __jcc_ast_param_ref(vm, fn, "b");
    _Node *sum = __jcc_ast_binary(vm, _ADD, a_ref, b_ref);
    _Node *body = __jcc_ast_return(vm, sum);
    __jcc_ast_function_set_body(vm, fn, body);
    
    return __jcc_ast_int_literal(vm, 0);
}

int main(void) {
    gen_add_func();
    return add_numbers(20, 22) - 42;  // Returns 0
}
```

## Hygienic Local Variables

Macros that expand into statements often need temporary locals. Prefer
`__jcc_ast_local_var_unique()` for these temporaries so generated names cannot
collide with user variables:

```c
#pragma macro
_Node *with_temp(_Node *value) {
    _VirtualMachine *vm = __jcc_get_vm();
    _Type *int_ty = _AST_GET_TYPE("int");
    _Node *tmp = _AST_LOCAL_VAR_UNIQUE(int_ty);
    _Node *set_tmp = _AST_EXPR_STMT(_AST_ASSIGN(tmp, value));
    _Node *ret_tmp = _AST_RETURN(tmp);
    _Node *stmts[2] = {set_tmp, ret_tmp};
    return _AST_BLOCK(stmts, 2);
}
```

Use `__jcc_ast_local_var()` only when the generated local must have a deliberate,
user-visible name.

## Limitations

Current limitations of pragma macros:

1. **Single expression context**: Macros are designed to return a single expression node that replaces the call. Complex multi-statement generation requires using `__jcc_ast_block()`.

2. **Compile-time only**: Macro code runs during compilation. Runtime values cannot be inspected.

3. **Forward declarations for generated functions**: Functions generated by macros must be forward-declared before the point where they are called.

4. **Argument count**: Pragma macro calls currently support at most 8
   arguments. Calls with more arguments are rejected during compilation.

## Implementation Notes

Pragma macros work by:
1. Capturing the function body tokens during preprocessing
2. Compiling captured macro functions to bytecode before macro expansion
3. Executing the bytecode in the same VM when the macro is called
4. Replacing the call AST node with the returned node
5. Running `add_type()` on the result to ensure proper type information

Pragma macro functions can call other pragma macro functions directly from
their compile-time bytecode. This includes calls to macros defined later in the
same translation unit, because all captured macro signatures are made available
before their bodies are compiled.

Pragma macros can also call explicit compile-time helper functions marked with
`#pragma comptime`. These helpers are compiled into the same compile-time
bytecode unit as pragma macros, but they are not expanded from normal program
call sites:

```c
#pragma comptime
int helper(int n) {
    return n + 1;
}

#pragma macro
_Node *make_value(void) {
    return _AST_INT_LITERAL(helper(41));
}

int main(void) {
    return make_value(); // Replaced at compile-time with 42
}
```

`#pragma comptime` helpers may call other pragma macros or comptime helpers,
including helpers defined later in the translation unit. Ordinary runtime
functions are not automatically compiled for macro use; mark shared
compile-time helpers explicitly with `#pragma comptime`.

String literals created by `__jcc_ast_string_literal()` are immediately allocated in the data segment to ensure they're available at runtime.

See `include/reflection.h` for the complete API documentation.
