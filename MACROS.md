# JCC Pragma Macros

JCC introduces a powerful compile-time metaprogramming feature called **Pragma Macros**. This allows you to write C functions that are executed *during compilation* to generate code, inspect types, and perform reflection.

## Overview

A pragma macro is a standard C function prefixed with `#pragma macro`. When JCC encounters a call to this function:
1. The macro function is compiled to bytecode during the parsing phase
2. When the call site is reached during AST traversal, the macro is executed
3. The returned AST `JCC_Node*` replaces the original function call in the AST
4. Compilation continues with the transformed AST

This enables Lisp-like macros in C, allowing you to write functions that generate code.

## Basic Usage

To define a pragma macro:
1. Add `#pragma macro` before a function definition
2. The function must return a `JCC_Node*` (an AST node)
3. Use the reflection API; `<reflection.h>` is included implicitly while pragma
   macro functions are compiled

```c
#pragma macro
JCC_Node *make_const_5(void) {
    JCC *vm = jcc_get_vm();
    return jcc_ast_int_literal(vm, 5);
}

int main(void) {
    int x = make_const_5(); // Replaced at compile-time with: int x = 5;
    return x - 5;           // Returns 0
}
```

## The VM Context

All AST construction and reflection functions require access to the compiler state (the VM). Use `jcc_get_vm()` to obtain this context:

```c
#pragma macro
JCC_Node *my_macro(void) {
    JCC *vm = jcc_get_vm();  // Get the VM context
    return jcc_ast_int_literal(vm, 42);
}
```

The implicitly included `<reflection.h>` also provides convenience macros that automatically pass `jcc_get_vm()`:

```c
#pragma macro
JCC_Node *my_macro(void) {
    return JCC_AST_INT_LITERAL(42);  // Equivalent to jcc_ast_int_literal(jcc_get_vm(), 42)
}
```

## Macro Arguments

Pragma macros can receive arguments. The arguments are passed as `JCC_Node*` pointers to the original AST nodes from the call site:

```c
#pragma macro
JCC_Node *double_it(JCC_Node *value) {
    JCC *vm = jcc_get_vm();
    // Create: (value + value)
    return jcc_ast_binary(vm, JCC_ND_ADD, value, value);
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
#include <string.h>

typedef enum { COLOR_RED, COLOR_GREEN, COLOR_BLUE } Color;

#pragma macro
JCC_Node *enum_count_check(void) {
    JCC *vm = jcc_get_vm();
    
    // Find the Color type by name
    JCC_Type *color_type = jcc_ast_find_type(vm, "Color");
    if (!color_type) {
        return jcc_ast_string_literal(vm, "type_not_found");
    }
    
    // Get the number of enum constants
    int count = jcc_ast_enum_count(vm, color_type);
    
    if (count == 3) {
        return jcc_ast_string_literal(vm, "has_3_colors");
    }
    return jcc_ast_string_literal(vm, "unexpected_count");
}

int main(void) {
    const char *result = enum_count_check();
    return strcmp(result, "has_3_colors");  // Returns 0 if equal
}
```

### Iterating Enum Constants

```c
#pragma macro
JCC_Node *get_first_enum_name(void) {
    JCC *vm = jcc_get_vm();
    
    JCC_Type *color_type = jcc_ast_find_type(vm, "Color");
    if (!color_type) {
        return jcc_ast_string_literal(vm, "unknown");
    }
    
    // Get the first enum constant
    JCC_EnumConstant *ec = jcc_ast_enum_at(vm, color_type, 0);
    if (ec) {
        const char *name = jcc_ast_enum_constant_name(ec);
        return jcc_ast_string_literal(vm, name);  // Returns "COLOR_RED"
    }
    return jcc_ast_string_literal(vm, "no_constants");
}
```

## API Reference

### VM Context

| Function | Description |
|----------|-------------|
| `jcc_get_vm()` | Get the current VM context (required for all API calls) |

### Type Lookup

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `jcc_ast_find_type(vm, name)` | `JCC_AST_FIND_TYPE(name)` | Find a type by name (typedef, struct, enum) |
| `jcc_ast_type_exists(vm, name)` | `JCC_AST_TYPE_EXISTS(name)` | Check if a type exists |
| `jcc_ast_get_type(vm, name)` | `JCC_AST_GET_TYPE(name)` | Get type (includes built-in types like "int") |

### Type Introspection

| Function | Description |
|----------|-------------|
| `jcc_ast_type_kind(ty)` | Get JCC_TypeKind (JCC_TY_INT, JCC_TY_ENUM, JCC_TY_STRUCT, etc.) |
| `jcc_ast_type_size(ty)` | Get sizeof() in bytes |
| `jcc_ast_type_align(ty)` | Get alignment in bytes |
| `jcc_ast_type_is_unsigned(ty)` | Check if unsigned |
| `jcc_ast_type_is_const(ty)` | Check if const-qualified |
| `jcc_ast_type_base(ty)` | For pointer/array: get base type |
| `jcc_ast_type_array_len(ty)` | For arrays: get length (-1 if not array) |

### Type Construction

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `jcc_ast_make_pointer(vm, base)` | `JCC_AST_MAKE_POINTER(base)` | Create pointer type |
| `jcc_ast_make_array(vm, base, len)` | `JCC_AST_MAKE_ARRAY(base, len)` | Create array type |

### Enum Reflection

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `jcc_ast_enum_count(vm, ty)` | `JCC_AST_ENUM_COUNT(ty)` | Number of constants (-1 if not enum) |
| `jcc_ast_enum_at(vm, ty, i)` | `JCC_AST_ENUM_AT(ty, i)` | Get constant at index |
| `jcc_ast_enum_find(vm, ty, name)` | `JCC_AST_ENUM_FIND(ty, name)` | Find constant by name |
| `jcc_ast_enum_constant_name(ec)` | `JCC_AST_ENUM_CONSTANT_NAME(ec)` | Get constant name |
| `jcc_ast_enum_constant_value(ec)` | `JCC_AST_ENUM_CONSTANT_VALUE(ec)` | Get constant value |

### Struct/Union Reflection

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `jcc_ast_struct_member_count(vm, ty)` | `JCC_AST_STRUCT_MEMBER_COUNT(ty)` | Number of members |
| `jcc_ast_struct_member_at(vm, ty, i)` | `JCC_AST_STRUCT_MEMBER_AT(ty, i)` | Get member at index |
| `jcc_ast_struct_member_find(vm, ty, name)` | `JCC_AST_STRUCT_MEMBER_FIND(ty, name)` | Find member by name |
| `jcc_ast_member_name(m)` | `JCC_AST_MEMBER_NAME(m)` | Get member name |
| `jcc_ast_member_type(m)` | `JCC_AST_MEMBER_TYPE(m)` | Get member type |
| `jcc_ast_member_offset(m)` | `JCC_AST_MEMBER_OFFSET(m)` | Get member offset in bytes |

### AST Construction - Literals

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `jcc_ast_int_literal(vm, val)` | `JCC_AST_INT_LITERAL(val)` | Create integer literal |
| `jcc_ast_float_literal(vm, val)` | `JCC_AST_FLOAT_LITERAL(val)` | Create float literal |
| `jcc_ast_string_literal(vm, str)` | `JCC_AST_STRING_LITERAL(str)` | Create string literal |
| `jcc_ast_var_ref(vm, name)` | `JCC_AST_VAR_REF(name)` | Reference a variable by name |

### AST Construction - Expressions

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `jcc_ast_binary(vm, op, l, r)` | `JCC_AST_BINARY(op, l, r)` | Binary operation (JCC_ND_ADD, JCC_ND_MUL, etc.) |
| `jcc_ast_unary(vm, op, expr)` | `JCC_AST_UNARY(op, expr)` | Unary operation (JCC_ND_NEG, JCC_ND_NOT, etc.) |
| `jcc_ast_cast(vm, expr, ty)` | `JCC_AST_CAST(expr, ty)` | Type cast |

### AST Construction - Statements

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `jcc_ast_return(vm, expr)` | `JCC_AST_RETURN(expr)` | Return statement |
| `jcc_ast_block(vm, stmts, n)` | `JCC_AST_BLOCK(stmts, n)` | Block of statements |
| `jcc_ast_if(vm, cond, then, else)` | `JCC_AST_IF(c, t, e)` | If statement |
| `jcc_ast_switch(vm, cond)` | `JCC_AST_SWITCH(cond)` | Switch statement |
| `jcc_ast_switch_add_case(vm, sw, val, body)` | `JCC_AST_SWITCH_ADD_CASE(sw, v, b)` | Add case to switch |
| `jcc_ast_switch_set_default(vm, sw, body)` | `JCC_AST_SWITCH_SET_DEFAULT(sw, b)` | Set default case |
| `jcc_ast_expr_stmt(vm, expr)` | `JCC_AST_EXPR_STMT(expr)` | Expression statement |

### AST Construction - Function Generation

| Function | Convenience Macro | Description |
|----------|-------------------|-------------|
| `jcc_ast_function(vm, name, ret_type)` | `JCC_AST_FUNCTION(name, ret_type)` | Create a new function |
| `jcc_ast_function_add_param(vm, fn, name, ty)` | `JCC_AST_FUNCTION_ADD_PARAM(fn, name, ty)` | Add parameter to function |
| `jcc_ast_function_set_body(vm, fn, body)` | `JCC_AST_FUNCTION_SET_BODY(fn, body)` | Set function body |
| `jcc_ast_function_set_static(fn, is_static)` | - | Set static linkage |
| `jcc_ast_function_set_inline(fn, is_inline)` | - | Set inline attribute |
| `jcc_ast_function_set_variadic(fn, is_variadic)` | - | Set variadic attribute |

### Node Kinds (for jcc_ast_binary/jcc_ast_unary)

```c
// Arithmetic
JCC_ND_ADD, JCC_ND_SUB, JCC_ND_MUL, JCC_ND_DIV, JCC_ND_MOD, JCC_ND_NEG

// Bitwise
JCC_ND_BITAND, JCC_ND_BITOR, JCC_ND_BITXOR, JCC_ND_BITNOT, JCC_ND_SHL, JCC_ND_SHR

// Comparison
JCC_ND_EQ, JCC_ND_NE, JCC_ND_LT, JCC_ND_LE

// Logical
JCC_ND_NOT, JCC_ND_LOGAND, JCC_ND_LOGOR

// Other
JCC_ND_ASSIGN, JCC_ND_ADDR, JCC_ND_DEREF, JCC_ND_COMMA
```

## Function Generation

Pragma macros can generate entire functions at compile-time. This is useful for generating boilerplate code, serializers, enum-to-string converters, and more.

### Basic Function Generation

```c
// Forward declare the function we'll generate
int generated_func(void);

#pragma macro
JCC_Node *generate_func(void) {
    JCC *vm = jcc_get_vm();
    
    // Create a function that returns 42
    JCC_Obj *fn = jcc_ast_function(vm, "generated_func", jcc_ast_get_type(vm, "int"));
    
    // Set the function body: return 42;
    JCC_Node *body = jcc_ast_return(vm, jcc_ast_int_literal(vm, 42));
    jcc_ast_function_set_body(vm, fn, body);
    
    // Return NULL - the function is already added to globals
    return JCC_AST_INT_LITERAL(0);  // Placeholder, value ignored
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
| `jcc_ast_function(vm, name, ret_type)` | Create a new function object |
| `jcc_ast_function_add_param(vm, fn, name, type)` | Add a parameter to the function |
| `jcc_ast_function_set_body(vm, fn, body)` | Set the function body (statement node) |
| `jcc_ast_function_set_static(fn, is_static)` | Set static linkage |
| `jcc_ast_function_set_inline(fn, is_inline)` | Set inline attribute |
| `jcc_ast_function_set_variadic(fn, is_variadic)` | Set variadic attribute |

### Important Notes

1. **Forward declarations required**: You must forward-declare the generated function before the macro call so the compiler knows its signature when it's called later.

2. **Macro return value**: The macro still needs to return a `JCC_Node*`, but for function generation this is typically just a placeholder value (like `JCC_AST_INT_LITERAL(0)`).

3. **Function lookup**: `jcc_ast_function()` checks for existing forward declarations and updates them rather than creating duplicates.

### Generating Functions with Parameters

```c
// Forward declare
int add_numbers(int a, int b);

#pragma macro
JCC_Node *gen_add_func(void) {
    JCC *vm = jcc_get_vm();
    
    JCC_Type *int_type = jcc_ast_get_type(vm, "int");
    JCC_Obj *fn = jcc_ast_function(vm, "add_numbers", int_type);
    
    // Add parameters
    jcc_ast_function_add_param(vm, fn, "a", int_type);
    jcc_ast_function_add_param(vm, fn, "b", int_type);
    
    // Body: return a + b;
    // Use jcc_ast_param_ref to reference parameters, and assign to intermediate variables
    JCC_Node *a_ref = jcc_ast_param_ref(vm, fn, "a");
    JCC_Node *b_ref = jcc_ast_param_ref(vm, fn, "b");
    JCC_Node *sum = jcc_ast_binary(vm, JCC_ND_ADD, a_ref, b_ref);
    JCC_Node *body = jcc_ast_return(vm, sum);
    jcc_ast_function_set_body(vm, fn, body);
    
    return jcc_ast_int_literal(vm, 0);
}

int main(void) {
    gen_add_func();
    return add_numbers(20, 22) - 42;  // Returns 0
}
```

## Limitations

Current limitations of pragma macros:

1. **Single expression context**: Macros are designed to return a single expression node that replaces the call. Complex multi-statement generation requires using `jcc_ast_block()`.

2. **Compile-time only**: Macro code runs during compilation. Runtime values cannot be inspected.

3. **Forward declarations for generated functions**: Functions generated by macros must be forward-declared before the point where they are called.

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

String literals created by `jcc_ast_string_literal()` are immediately allocated in the data segment to ensure they're available at runtime.

See `include/reflection.h` for the complete API documentation.
