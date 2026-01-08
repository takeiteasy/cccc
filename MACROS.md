# JCC Pragma Macros

JCC introduces a powerful compile-time metaprogramming feature called **Pragma Macros**. This allows you to write C functions that are executed *during compilation* to generate code, inspect types, and perform reflection.

## Overview

A pragma macro is a standard C function prefixed with `#pragma macro`. When JCC encounters a call to this function:
1. The macro function is compiled to bytecode during the parsing phase
2. When the call site is reached during AST traversal, the macro is executed
3. The returned AST `Node*` replaces the original function call in the AST
4. Compilation continues with the transformed AST

This enables Lisp-like macros in C, allowing you to write functions that generate code.

## Basic Usage

To define a pragma macro:
1. Include `<reflection.h>` which provides the reflection API
2. Add `#pragma macro` before a function definition
3. The function must return a `Node*` (an AST node)

```c
#include <reflection.h>

#pragma macro
Node *make_const_5(void) {
    JCC *vm = __jcc_get_vm();
    return ast_int_literal(vm, 5);
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
Node *my_macro(void) {
    JCC *vm = __jcc_get_vm();  // Get the VM context
    return ast_int_literal(vm, 42);
}
```

The header `<reflection.h>` also provides convenience macros that automatically pass `__jcc_get_vm()`:

```c
#pragma macro
Node *my_macro(void) {
    return AST_INT_LITERAL(42);  // Equivalent to ast_int_literal(__jcc_get_vm(), 42)
}
```

## Macro Arguments

Pragma macros can receive arguments. The arguments are passed as `Node*` pointers to the original AST nodes from the call site:

```c
#include <reflection.h>

#pragma macro
Node *double_it(Node *value) {
    JCC *vm = __jcc_get_vm();
    // Create: (value + value)
    return ast_binary(vm, ND_ADD, value, value);
}

int main(void) {
    int x = double_it(21);  // Becomes: int x = (21 + 21);
    return x - 42;          // Returns 0
}
```

## Type Reflection

One of the most powerful features is type introspection. You can inspect types defined in your program during compilation.

### Enum Reflection Example

```c
#include <reflection.h>
#include <string.h>

typedef enum { COLOR_RED, COLOR_GREEN, COLOR_BLUE } Color;

#pragma macro
Node *enum_count_check(void) {
    JCC *vm = __jcc_get_vm();
    
    // Find the Color type by name
    Type *color_type = ast_find_type(vm, "Color");
    if (!color_type) {
        return ast_string_literal(vm, "type_not_found");
    }
    
    // Get the number of enum constants
    int count = ast_enum_count(vm, color_type);
    
    if (count == 3) {
        return ast_string_literal(vm, "has_3_colors");
    }
    return ast_string_literal(vm, "unexpected_count");
}

int main(void) {
    const char *result = enum_count_check();
    return strcmp(result, "has_3_colors");  // Returns 0 if equal
}
```

### Iterating Enum Constants

```c
#pragma macro
Node *get_first_enum_name(void) {
    JCC *vm = __jcc_get_vm();
    
    Type *color_type = ast_find_type(vm, "Color");
    if (!color_type) {
        return ast_string_literal(vm, "unknown");
    }
    
    // Get the first enum constant
    EnumConstant *ec = ast_enum_at(vm, color_type, 0);
    if (ec) {
        const char *name = ast_enum_constant_name(ec);
        return ast_string_literal(vm, name);  // Returns "COLOR_RED"
    }
    return ast_string_literal(vm, "no_constants");
}
```

## API Reference

### VM Context

| Function | Description |
|----------|-------------|
| `__jcc_get_vm()` | Get the current VM context (required for all API calls) |

### Type Lookup

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `ast_find_type(vm, name)` | `AST_FIND_TYPE(name)` | Find a type by name (typedef, struct, enum) |
| `ast_type_exists(vm, name)` | `AST_TYPE_EXISTS(name)` | Check if a type exists |
| `ast_get_type(vm, name)` | `AST_GET_TYPE(name)` | Get type (includes built-in types like "int") |

### Type Introspection

| Function | Description |
|----------|-------------|
| `ast_type_kind(ty)` | Get TypeKind (TY_INT, TY_ENUM, TY_STRUCT, etc.) |
| `ast_type_size(ty)` | Get sizeof() in bytes |
| `ast_type_align(ty)` | Get alignment in bytes |
| `ast_type_is_unsigned(ty)` | Check if unsigned |
| `ast_type_is_const(ty)` | Check if const-qualified |
| `ast_type_base(ty)` | For pointer/array: get base type |
| `ast_type_array_len(ty)` | For arrays: get length (-1 if not array) |

### Type Construction

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `ast_make_pointer(vm, base)` | `AST_MAKE_POINTER(base)` | Create pointer type |
| `ast_make_array(vm, base, len)` | `AST_MAKE_ARRAY(base, len)` | Create array type |

### Enum Reflection

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `ast_enum_count(vm, ty)` | `AST_ENUM_COUNT(ty)` | Number of constants (-1 if not enum) |
| `ast_enum_at(vm, ty, i)` | `AST_ENUM_AT(ty, i)` | Get constant at index |
| `ast_enum_find(vm, ty, name)` | `AST_ENUM_FIND(ty, name)` | Find constant by name |
| `ast_enum_constant_name(ec)` | `AST_ENUM_CONSTANT_NAME(ec)` | Get constant name |
| `ast_enum_constant_value(ec)` | `AST_ENUM_CONSTANT_VALUE(ec)` | Get constant value |

### Struct/Union Reflection

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `ast_struct_member_count(vm, ty)` | `AST_STRUCT_MEMBER_COUNT(ty)` | Number of members |
| `ast_struct_member_at(vm, ty, i)` | `AST_STRUCT_MEMBER_AT(ty, i)` | Get member at index |
| `ast_struct_member_find(vm, ty, name)` | `AST_STRUCT_MEMBER_FIND(ty, name)` | Find member by name |
| `ast_member_name(m)` | `AST_MEMBER_NAME(m)` | Get member name |
| `ast_member_type(m)` | `AST_MEMBER_TYPE(m)` | Get member type |
| `ast_member_offset(m)` | `AST_MEMBER_OFFSET(m)` | Get member offset in bytes |

### AST Construction - Literals

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `ast_int_literal(vm, val)` | `AST_INT_LITERAL(val)` | Create integer literal |
| `ast_float_literal(vm, val)` | `AST_FLOAT_LITERAL(val)` | Create float literal |
| `ast_string_literal(vm, str)` | `AST_STRING_LITERAL(str)` | Create string literal |
| `ast_var_ref(vm, name)` | `AST_VAR_REF(name)` | Reference a variable by name |

### AST Construction - Expressions

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `ast_binary(vm, op, l, r)` | `AST_BINARY(op, l, r)` | Binary operation (ND_ADD, ND_MUL, etc.) |
| `ast_unary(vm, op, expr)` | `AST_UNARY(op, expr)` | Unary operation (ND_NEG, ND_NOT, etc.) |
| `ast_cast(vm, expr, ty)` | `AST_CAST(expr, ty)` | Type cast |

### AST Construction - Statements

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `ast_return(vm, expr)` | `AST_RETURN(expr)` | Return statement |
| `ast_block(vm, stmts, n)` | `AST_BLOCK(stmts, n)` | Block of statements |
| `ast_if(vm, cond, then, else)` | `AST_IF(c, t, e)` | If statement |
| `ast_switch(vm, cond)` | `AST_SWITCH(cond)` | Switch statement |
| `ast_switch_add_case(vm, sw, val, body)` | `AST_SWITCH_ADD_CASE(sw, v, b)` | Add case to switch |
| `ast_switch_set_default(vm, sw, body)` | `AST_SWITCH_SET_DEFAULT(sw, b)` | Set default case |
| `ast_expr_stmt(vm, expr)` | `AST_EXPR_STMT(expr)` | Expression statement |

### AST Construction - Function Generation

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `ast_function(vm, name, ret_type)` | `AST_FUNCTION(name, ret_type)` | Create a new function |
| `ast_function_add_param(vm, fn, name, ty)` | `AST_FUNCTION_ADD_PARAM(fn, name, ty)` | Add parameter to function |
| `ast_function_set_body(vm, fn, body)` | `AST_FUNCTION_SET_BODY(fn, body)` | Set function body |
| `ast_function_set_static(fn, is_static)` | - | Set static linkage |
| `ast_function_set_inline(fn, is_inline)` | - | Set inline attribute |
| `ast_function_set_variadic(fn, is_variadic)` | - | Set variadic attribute |

### Node Kinds (for ast_binary/ast_unary)

```c
// Arithmetic
ND_ADD, ND_SUB, ND_MUL, ND_DIV, ND_MOD, ND_NEG

// Bitwise
ND_BITAND, ND_BITOR, ND_BITXOR, ND_BITNOT, ND_SHL, ND_SHR

// Comparison
ND_EQ, ND_NE, ND_LT, ND_LE

// Logical
ND_NOT, ND_LOGAND, ND_LOGOR

// Other
ND_ASSIGN, ND_ADDR, ND_DEREF, ND_COMMA
```

## Function Generation

Pragma macros can generate entire functions at compile-time. This is useful for generating boilerplate code, serializers, enum-to-string converters, and more.

### Basic Function Generation

```c
#include <reflection.h>

// Forward declare the function we'll generate
int generated_func(void);

#pragma macro
Node *generate_func(void) {
    JCC *vm = __jcc_get_vm();
    
    // Create a function that returns 42
    Obj *fn = ast_function(vm, "generated_func", ast_get_type(vm, "int"));
    
    // Set the function body: return 42;
    Node *body = ast_return(vm, ast_int_literal(vm, 42));
    ast_function_set_body(vm, fn, body);
    
    // Return NULL - the function is already added to globals
    return AST_INT_LITERAL(0);  // Placeholder, value ignored
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
| `ast_function(vm, name, ret_type)` | Create a new function object |
| `ast_function_add_param(vm, fn, name, type)` | Add a parameter to the function |
| `ast_function_set_body(vm, fn, body)` | Set the function body (statement node) |
| `ast_function_set_static(fn, is_static)` | Set static linkage |
| `ast_function_set_inline(fn, is_inline)` | Set inline attribute |
| `ast_function_set_variadic(fn, is_variadic)` | Set variadic attribute |

### Important Notes

1. **Forward declarations required**: You must forward-declare the generated function before the macro call so the compiler knows its signature when it's called later.

2. **Macro return value**: The macro still needs to return a `Node*`, but for function generation this is typically just a placeholder value (like `AST_INT_LITERAL(0)`).

3. **Function lookup**: `ast_function()` checks for existing forward declarations and updates them rather than creating duplicates.

### Generating Functions with Parameters

```c
#include <reflection.h>

// Forward declare
int add_numbers(int a, int b);

#pragma macro
Node *gen_add_func(void) {
    JCC *vm = __jcc_get_vm();
    
    Type *int_type = ast_get_type(vm, "int");
    Obj *fn = ast_function(vm, "add_numbers", int_type);
    
    // Add parameters
    ast_function_add_param(vm, fn, "a", int_type);
    ast_function_add_param(vm, fn, "b", int_type);
    
    // Body: return a + b;
    // Use ast_param_ref to reference parameters, and assign to intermediate variables
    Node *a_ref = ast_param_ref(vm, fn, "a");
    Node *b_ref = ast_param_ref(vm, fn, "b");
    Node *sum = ast_binary(vm, ND_ADD, a_ref, b_ref);
    Node *body = ast_return(vm, sum);
    ast_function_set_body(vm, fn, body);
    
    return ast_int_literal(vm, 0);
}

int main(void) {
    gen_add_func();
    return add_numbers(20, 22) - 42;  // Returns 0
}
```

## Limitations

Current limitations of pragma macros:

1. **Single expression context**: Macros are designed to return a single expression node that replaces the call. Complex multi-statement generation requires using `ast_block()`.

2. **Compile-time only**: Macro code runs during compilation. Runtime values cannot be inspected.

3. **No recursive macros**: A macro cannot call another pragma macro (yet).

4. **Forward declarations for generated functions**: Functions generated by macros must be forward-declared before the point where they are called.

## Implementation Notes

Pragma macros work by:
1. Capturing the function body tokens during preprocessing
2. Compiling the macro function to bytecode when first encountered
3. Executing the bytecode in the same VM when the macro is called
4. Replacing the call AST node with the returned node
5. Running `add_type()` on the result to ensure proper type information

String literals created by `ast_string_literal()` are immediately allocated in the data segment to ensure they're available at runtime.

See `include/reflection.h` for the complete API documentation.
