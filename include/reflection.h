/*
 JCC: JIT C Compiler - Pragma Macro Reflection API

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/*!
 * @file reflection.h
 * @brief Reflection and AST Construction API for JCC Pragma Macros
 *
 * This header provides APIs for:
 * - Type introspection and lookup
 * - Enum reflection
 * - Struct/union member introspection
 * - AST node construction (literals, expressions, statements)
 *
 * ## Usage
 *
 * This header is automatically included when compiling pragma macros.
 * All functions that require VM context use the __VM macro, which
 * expands to __jcc_get_vm() - a builtin that returns the current
 * VM instance during macro execution.
 *
 * ## Example
 *
 * @code
 * #pragma macro
 * Node *make_const_5() {
 *     return AST_INT_LITERAL(5);
 * }
 *
 * int main() {
 *     int x = make_const_5();  // x == 5
 *     return 0;
 * }
 * @endcode
 */

#ifndef REFLECTION_H
#define REFLECTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations (opaque types for pragma macros)
typedef struct JCC JCC;
typedef struct Type Type;
typedef struct Node Node;
typedef struct Obj Obj;
typedef struct Member Member;
typedef struct EnumConstant EnumConstant;
typedef struct Token Token;

// Type kind enumeration (matches jcc.h)
typedef enum {
    TY_VOID = 0,
    TY_BOOL = 1,
    TY_CHAR = 2,
    TY_SHORT = 3,
    TY_INT = 4,
    TY_LONG = 5,
    TY_FLOAT = 6,
    TY_DOUBLE = 7,
    TY_LDOUBLE = 8,
    TY_ENUM = 9,
    TY_PTR = 10,
    TY_FUNC = 11,
    TY_ARRAY = 12,
    TY_VLA = 13,
    TY_STRUCT = 14,
    TY_UNION = 15,
} TypeKind;

// Node kind enumeration (subset for pragma macro use)
typedef enum {
    ND_NULL_EXPR = 0,
    ND_ADD = 1,
    ND_SUB = 2,
    ND_MUL = 3,
    ND_DIV = 4,
    ND_NEG = 5,
    ND_MOD = 6,
    ND_BITAND = 7,
    ND_BITOR = 8,
    ND_BITXOR = 9,
    ND_SHL = 10,
    ND_SHR = 11,
    ND_EQ = 12,
    ND_NE = 13,
    ND_LT = 14,
    ND_LE = 15,
    ND_ASSIGN = 16,
    ND_COND = 17,
    ND_COMMA = 18,
    ND_MEMBER = 19,
    ND_ADDR = 20,
    ND_DEREF = 21,
    ND_NOT = 22,
    ND_BITNOT = 23,
    ND_LOGAND = 24,
    ND_LOGOR = 25,
    ND_RETURN = 26,
    ND_IF = 27,
    ND_FOR = 28,
    ND_DO = 29,
    ND_SWITCH = 30,
    ND_CASE = 31,
    ND_BLOCK = 32,
    ND_FUNCALL = 37,
    ND_EXPR_STMT = 38,
    ND_VAR = 40,
    ND_NUM = 42,
    ND_CAST = 43,
} NodeKind;

// ============================================================================
// Magic __VM Builtin
// ============================================================================

/*!
 * @function __jcc_get_vm
 * @abstract Builtin that returns the current parent VM context.
 * @discussion Set before calling a pragma macro and cleared after.
 * @return Pointer to the current JCC VM instance.
 */
extern JCC *__jcc_get_vm(void);

/*!
 * @define __VM
 * @abstract Magic macro that references the VM instance in pragma macros.
 */
#define __VM __jcc_get_vm()

// ============================================================================
// Type Lookup and Introspection
// ============================================================================

/*! Find a type by name (struct/union/enum tag). Returns NULL if not found. */
Type *ast_find_type(JCC *vm, const char *name);

/*! Check if a type exists by name. */
bool ast_type_exists(JCC *vm, const char *name);

/*! Lookup a type by name (includes built-in types). */
Type *ast_get_type(JCC *vm, const char *name);

/*! Get the TypeKind of a type. */
TypeKind ast_type_kind(Type *ty);

/*! Get sizeof() value in bytes. */
int ast_type_size(Type *ty);

/*! Get alignment in bytes. */
int ast_type_align(Type *ty);

/*! Check if type is unsigned. */
bool ast_type_is_unsigned(Type *ty);

/*! Check if type is const-qualified. */
bool ast_type_is_const(Type *ty);

/*! For pointer/array types: get the base type. */
Type *ast_type_base(Type *ty);

/*! For array types: get the array length. Returns -1 if not an array. */
int ast_type_array_len(Type *ty);

/*! For function types: get return type. */
Type *ast_type_return_type(Type *ty);

/*! For function types: get parameter count. */
int ast_type_param_count(Type *ty);

/*! For function types: get parameter type at index. */
Type *ast_type_param_at(Type *ty, int index);

/*! For function types: check if variadic. */
bool ast_type_is_variadic(Type *ty);

/*! Get type name (for named types). */
const char *ast_type_name(Type *ty);

/*! Create a pointer type to base. */
Type *ast_make_pointer(JCC *vm, Type *base);

/*! Create an array type with specified length. */
Type *ast_make_array(JCC *vm, Type *base, int length);

// ============================================================================
// Enum Reflection
// ============================================================================

/*! Get the number of enum constants. Returns -1 if not an enum. */
int ast_enum_count(JCC *vm, Type *enum_type);

/*! Get enum constant at index (0-based). */
EnumConstant *ast_enum_at(JCC *vm, Type *enum_type, int index);

/*! Find enum constant by name. */
EnumConstant *ast_enum_find(JCC *vm, Type *enum_type, const char *name);

/*! Get enum constant name. */
const char *ast_enum_constant_name(EnumConstant *ec);

/*! Get enum constant value. */
int ast_enum_constant_value(EnumConstant *ec);

/*! Get the name of an enum type. */
const char *ast_enum_name(Type *e);

/*! Get the number of values in an enum. */
int ast_enum_value_count(Type *e);

/*! Get the name of an enum value at index. */
const char *ast_enum_value_name(Type *e, int index);

/*! Get the integer value of an enum constant at index. */
int ast_enum_value(Type *e, int index);

// ============================================================================
// Struct/Union Member Introspection
// ============================================================================

/*! Get the number of members. Returns -1 if not a struct/union. */
int ast_struct_member_count(JCC *vm, Type *struct_type);

/*! Get member at index (0-based). */
Member *ast_struct_member_at(JCC *vm, Type *struct_type, int index);

/*! Find member by name. */
Member *ast_struct_member_find(JCC *vm, Type *struct_type, const char *name);

/*! Get member name. */
const char *ast_member_name(Member *m);

/*! Get member type. */
Type *ast_member_type(Member *m);

/*! Get member offset in bytes. */
int ast_member_offset(Member *m);

/*! Check if member is a bitfield. */
bool ast_member_is_bitfield(Member *m);

/*! Get bitfield width. */
int ast_member_bitfield_width(Member *m);

// ============================================================================
// Global Symbol Introspection
// ============================================================================

/*! Find a global symbol by name. */
Obj *ast_find_global(JCC *vm, const char *name);

/*! Get the total number of global symbols. */
int ast_global_count(JCC *vm);

/*! Get global symbol at index (0-based). */
Obj *ast_global_at(JCC *vm, int index);

/*! Get the name of an object. */
const char *ast_obj_name(Obj *obj);

/*! Get the type of an object. */
Type *ast_obj_type(Obj *obj);

/*! Check if object is a function. */
bool ast_obj_is_function(Obj *obj);

/*! Check if object is a definition. */
bool ast_obj_is_definition(Obj *obj);

/*! Check if object has static linkage. */
bool ast_obj_is_static(Obj *obj);

// ============================================================================
// AST Node Construction - Literals
// ============================================================================

/*! Create an integer literal node. */
Node *ast_int_literal(JCC *vm, int64_t value);

/*! Create a floating-point literal node. */
Node *ast_float_literal(JCC *vm, double value);

/*! Create a string literal node. */
Node *ast_string_literal(JCC *vm, const char *str);

/*! Create a variable reference node. */
Node *ast_var_ref(JCC *vm, const char *name);

/*! Create a reference to a function parameter.
 *  Use this when building function bodies to reference parameters by name.
 */
Node *ast_param_ref(JCC *vm, Obj *fn, const char *name);

// ============================================================================
// AST Node Construction - Expressions
// ============================================================================

/*! Create a binary operation node. */
Node *ast_binary(JCC *vm, NodeKind op, Node *left, Node *right);

/*! Create a unary operation node. */
Node *ast_unary(JCC *vm, NodeKind op, Node *operand);

/*! Create a type cast node. */
Node *ast_cast(JCC *vm, Node *expr, Type *target_type);

// ============================================================================
// AST Node Construction - Statements
// ============================================================================

/*! Create a return statement node. */
Node *ast_return(JCC *vm, Node *expr);

/*! Create a block (compound statement) node. */
Node *ast_block(JCC *vm, Node **stmts, int count);

/*! Create an if statement node. */
Node *ast_if(JCC *vm, Node *cond, Node *then_body, Node *else_body);

/*! Create a switch statement node. */
Node *ast_switch(JCC *vm, Node *cond);

/*! Add a case to a switch statement. */
void ast_switch_add_case(JCC *vm, Node *switch_node, Node *value, Node *body);

/*! Set the default case for a switch statement. */
void ast_switch_set_default(JCC *vm, Node *switch_node, Node *body);

/*! Create an expression statement node. */
Node *ast_expr_stmt(JCC *vm, Node *expr);

// ============================================================================
// Function Generation
// ============================================================================

/*!
 * @function ast_function
 * @abstract Create a new function object.
 * @param vm The VM context.
 * @param name The function name.
 * @param return_type The return type.
 * @return The newly created function object, or NULL on error.
 * @discussion The function is automatically added to the globals list
 *             and will be compiled when the main program is compiled.
 */
Obj *ast_function(JCC *vm, const char *name, Type *return_type);

/*!
 * @function ast_function_add_param
 * @abstract Add a parameter to a function.
 * @param vm The VM context.
 * @param fn The function object.
 * @param name The parameter name.
 * @param type The parameter type.
 * @discussion Parameters are added in order. Call this multiple times
 *             for multiple parameters.
 */
void ast_function_add_param(JCC *vm, Obj *fn, const char *name, Type *type);

/*!
 * @function ast_function_set_body
 * @abstract Set the body of a function.
 * @param vm The VM context.
 * @param fn The function object.
 * @param body The function body (a statement or block node).
 * @discussion If body is not already a ND_BLOCK, it will be wrapped in one.
 */
void ast_function_set_body(JCC *vm, Obj *fn, Node *body);

/*!
 * @function ast_function_set_static
 * @abstract Set whether a function has static linkage.
 * @param fn The function object.
 * @param is_static True for static linkage, false for external.
 */
void ast_function_set_static(Obj *fn, bool is_static);

/*!
 * @function ast_function_set_inline
 * @abstract Set whether a function is inline.
 * @param fn The function object.
 * @param is_inline True for inline, false otherwise.
 */
void ast_function_set_inline(Obj *fn, bool is_inline);

/*!
 * @function ast_function_set_variadic
 * @abstract Set whether a function is variadic.
 * @param fn The function object.
 * @param is_variadic True for variadic, false otherwise.
 */
void ast_function_set_variadic(Obj *fn, bool is_variadic);

// ============================================================================
// Convenience Macros (automatically pass __VM)
// ============================================================================

#define AST_FIND_TYPE(name) ast_find_type(__VM, name)
#define AST_TYPE_EXISTS(name) ast_type_exists(__VM, name)
#define AST_GET_TYPE(name) ast_get_type(__VM, name)

#define AST_INT_LITERAL(val) ast_int_literal(__VM, val)
#define AST_FLOAT_LITERAL(val) ast_float_literal(__VM, val)
#define AST_STRING_LITERAL(str) ast_string_literal(__VM, str)
#define AST_VAR_REF(name) ast_var_ref(__VM, name)
#define AST_PARAM_REF(fn, name) ast_param_ref(__VM, fn, name)

#define AST_BINARY(op, l, r) ast_binary(__VM, op, l, r)
#define AST_UNARY(op, operand) ast_unary(__VM, op, operand)
#define AST_CAST(expr, ty) ast_cast(__VM, expr, ty)

#define AST_RETURN(expr) ast_return(__VM, expr)
#define AST_BLOCK(stmts, count) ast_block(__VM, stmts, count)
#define AST_IF(c, t, e) ast_if(__VM, c, t, e)
#define AST_SWITCH(cond) ast_switch(__VM, cond)
#define AST_SWITCH_ADD_CASE(sw, v, b) ast_switch_add_case(__VM, sw, v, b)
#define AST_SWITCH_SET_DEFAULT(sw, b) ast_switch_set_default(__VM, sw, b)
#define AST_EXPR_STMT(expr) ast_expr_stmt(__VM, expr)

#define AST_MAKE_POINTER(base) ast_make_pointer(__VM, base)
#define AST_MAKE_ARRAY(base, len) ast_make_array(__VM, base, len)

#define AST_ENUM_COUNT(ty) ast_enum_count(__VM, ty)
#define AST_ENUM_AT(ty, i) ast_enum_at(__VM, ty, i)
#define AST_ENUM_FIND(ty, name) ast_enum_find(__VM, ty, name)
#define AST_ENUM_CONSTANT_NAME(ec) ast_enum_constant_name(ec)
#define AST_ENUM_CONSTANT_VALUE(ec) ast_enum_constant_value(ec)

#define AST_STRUCT_MEMBER_COUNT(ty) ast_struct_member_count(__VM, ty)
#define AST_STRUCT_MEMBER_AT(ty, i) ast_struct_member_at(__VM, ty, i)
#define AST_STRUCT_MEMBER_FIND(ty, name) ast_struct_member_find(__VM, ty, name)
#define AST_MEMBER_NAME(m) ast_member_name(m)
#define AST_MEMBER_TYPE(m) ast_member_type(m)
#define AST_MEMBER_OFFSET(m) ast_member_offset(m)

#define AST_FIND_GLOBAL(name) ast_find_global(__VM, name)
#define AST_GLOBAL_COUNT() ast_global_count(__VM)
#define AST_GLOBAL_AT(i) ast_global_at(__VM, i)

#define AST_FUNCTION(name, ret_type) ast_function(__VM, name, ret_type)
#define AST_FUNCTION_ADD_PARAM(fn, name, type)                                 \
    ast_function_add_param(__VM, fn, name, type)
#define AST_FUNCTION_SET_BODY(fn, body) ast_function_set_body(__VM, fn, body)
#define AST_FUNCTION_SET_STATIC(fn, is_static)                                 \
    ast_function_set_static(fn, is_static)
#define AST_FUNCTION_SET_INLINE(fn, is_inline)                                 \
    ast_function_set_inline(fn, is_inline)
#define AST_FUNCTION_SET_VARIADIC(fn, is_variadic)                             \
    ast_function_set_variadic(fn, is_variadic)

#ifdef __cplusplus
}
#endif

#endif // REFLECTION_H
