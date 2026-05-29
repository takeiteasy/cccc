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
 * All functions that require VM context use the JCC_VM macro, which
 * expands to jcc_get_vm() - a builtin that returns the current
 * VM instance during macro execution.
 *
 * ## Example
 *
 * @code
 * #pragma macro
 * JCC_Node *make_const_5() {
 *     return JCC_AST_INT_LITERAL(5);
 * }
 *
 * int main() {
 *     int x = make_const_5();  // x == 5
 *     return 0;
 * }
 * @endcode
 */

#ifndef JCC_REFLECTION_H
#define JCC_REFLECTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations (opaque types for pragma macros)
typedef struct JCC JCC;
typedef struct Type JCC_Type;
typedef struct Node JCC_Node;
typedef struct Obj JCC_Obj;
typedef struct Member JCC_Member;
typedef struct EnumConstant JCC_EnumConstant;
typedef struct Token JCC_Token;

// Type kind enumeration (matches jcc.h)
typedef enum {
    JCC_TY_VOID = 0,
    JCC_TY_BOOL = 1,
    JCC_TY_CHAR = 2,
    JCC_TY_SHORT = 3,
    JCC_TY_INT = 4,
    JCC_TY_LONG = 5,
    JCC_TY_FLOAT = 6,
    JCC_TY_DOUBLE = 7,
    JCC_TY_LDOUBLE = 8,
    JCC_TY_ENUM = 9,
    JCC_TY_PTR = 10,
    JCC_TY_FUNC = 11,
    JCC_TY_ARRAY = 12,
    JCC_TY_VLA = 13,
    JCC_TY_STRUCT = 14,
    JCC_TY_UNION = 15,
} JCC_TypeKind;

// Node kind enumeration (subset for pragma macro use)
typedef enum {
    JCC_ND_NULL_EXPR = 0,
    JCC_ND_ADD = 1,
    JCC_ND_SUB = 2,
    JCC_ND_MUL = 3,
    JCC_ND_DIV = 4,
    JCC_ND_NEG = 5,
    JCC_ND_MOD = 6,
    JCC_ND_BITAND = 7,
    JCC_ND_BITOR = 8,
    JCC_ND_BITXOR = 9,
    JCC_ND_SHL = 10,
    JCC_ND_SHR = 11,
    JCC_ND_EQ = 12,
    JCC_ND_NE = 13,
    JCC_ND_LT = 14,
    JCC_ND_LE = 15,
    JCC_ND_ASSIGN = 16,
    JCC_ND_COND = 17,
    JCC_ND_COMMA = 18,
    JCC_ND_MEMBER = 19,
    JCC_ND_ADDR = 20,
    JCC_ND_DEREF = 21,
    JCC_ND_NOT = 22,
    JCC_ND_BITNOT = 23,
    JCC_ND_LOGAND = 24,
    JCC_ND_LOGOR = 25,
    JCC_ND_RETURN = 26,
    JCC_ND_IF = 27,
    JCC_ND_FOR = 28,
    JCC_ND_DO = 29,
    JCC_ND_SWITCH = 30,
    JCC_ND_CASE = 31,
    JCC_ND_BLOCK = 32,
    JCC_ND_FUNCALL = 37,
    JCC_ND_EXPR_STMT = 38,
    JCC_ND_VAR = 40,
    JCC_ND_NUM = 42,
    JCC_ND_CAST = 43,
} JCC_NodeKind;

// ============================================================================
// Magic JCC_VM Builtin
// ============================================================================

/*!
 * @function jcc_get_vm
 * @abstract Builtin that returns the current parent VM context.
 * @discussion Set before calling a pragma macro and cleared after.
 * @return Pointer to the current JCC VM instance.
 */
extern JCC *jcc_get_vm(void);

/*!
 * @define JCC_VM
 * @abstract Magic macro that references the VM instance in pragma macros.
 */
#define JCC_VM jcc_get_vm()

// ============================================================================
// Type Lookup and Introspection
// ============================================================================

/*! Find a type by name (struct/union/enum tag). Returns NULL if not found. */
JCC_Type *jcc_ast_find_type(JCC *vm, const char *name);

/*! Check if a type exists by name. */
bool jcc_ast_type_exists(JCC *vm, const char *name);

/*! Lookup a type by name (includes built-in types). */
JCC_Type *jcc_ast_get_type(JCC *vm, const char *name);

/*! Get the JCC_TypeKind of a type. */
JCC_TypeKind jcc_ast_type_kind(JCC_Type *ty);

/*! Get sizeof() value in bytes. */
int jcc_ast_type_size(JCC_Type *ty);

/*! Get alignment in bytes. */
int jcc_ast_type_align(JCC_Type *ty);

/*! Check if type is unsigned. */
bool jcc_ast_type_is_unsigned(JCC_Type *ty);

/*! Check if type is const-qualified. */
bool jcc_ast_type_is_const(JCC_Type *ty);

/*! For pointer/array types: get the base type. */
JCC_Type *jcc_ast_type_base(JCC_Type *ty);

/*! For array types: get the array length. Returns -1 if not an array. */
int jcc_ast_type_array_len(JCC_Type *ty);

/*! For function types: get return type. */
JCC_Type *jcc_ast_type_return_type(JCC_Type *ty);

/*! For function types: get parameter count. */
int jcc_ast_type_param_count(JCC_Type *ty);

/*! For function types: get parameter type at index. */
JCC_Type *jcc_ast_type_param_at(JCC_Type *ty, int index);

/*! For function types: check if variadic. */
bool jcc_ast_type_is_variadic(JCC_Type *ty);

/*! Get type name (for named types). */
const char *jcc_ast_type_name(JCC_Type *ty);

/*! Create a pointer type to base. */
JCC_Type *jcc_ast_make_pointer(JCC *vm, JCC_Type *base);

/*! Create an array type with specified length. */
JCC_Type *jcc_ast_make_array(JCC *vm, JCC_Type *base, int length);

// ============================================================================
// Enum Reflection
// ============================================================================

/*! Get the number of enum constants. Returns -1 if not an enum. */
int jcc_ast_enum_count(JCC *vm, JCC_Type *enum_type);

/*! Get enum constant at index (0-based). */
JCC_EnumConstant *jcc_ast_enum_at(JCC *vm, JCC_Type *enum_type, int index);

/*! Find enum constant by name. */
JCC_EnumConstant *jcc_ast_enum_find(JCC *vm, JCC_Type *enum_type,
                                    const char *name);

/*! Get enum constant name. */
const char *jcc_ast_enum_constant_name(JCC_EnumConstant *ec);

/*! Get enum constant value. */
int jcc_ast_enum_constant_value(JCC_EnumConstant *ec);

/*! Get the name of an enum type. */
const char *jcc_ast_enum_name(JCC_Type *e);

/*! Get the number of values in an enum. */
int jcc_ast_enum_value_count(JCC_Type *e);

/*! Get the name of an enum value at index. */
const char *jcc_ast_enum_value_name(JCC_Type *e, int index);

/*! Get the integer value of an enum constant at index. */
int jcc_ast_enum_value(JCC_Type *e, int index);

// ============================================================================
// Struct/Union Member Introspection
// ============================================================================

/*! Get the number of members. Returns -1 if not a struct/union. */
int jcc_ast_struct_member_count(JCC *vm, JCC_Type *struct_type);

/*! Get member at index (0-based). */
JCC_Member *jcc_ast_struct_member_at(JCC *vm, JCC_Type *struct_type,
                                        int index);

/*! Find member by name. */
JCC_Member *jcc_ast_struct_member_find(JCC *vm, JCC_Type *struct_type,
                                        const char *name);

/*! Get member name. */
const char *jcc_ast_member_name(JCC_Member *m);

/*! Get member type. */
JCC_Type *jcc_ast_member_type(JCC_Member *m);

/*! Get member offset in bytes. */
int jcc_ast_member_offset(JCC_Member *m);

/*! Check if member is a bitfield. */
bool jcc_ast_member_is_bitfield(JCC_Member *m);

/*! Get bitfield width. */
int jcc_ast_member_bitfield_width(JCC_Member *m);

// ============================================================================
// Global Symbol Introspection
// ============================================================================

/*! Find a global symbol by name. */
JCC_Obj *jcc_ast_find_global(JCC *vm, const char *name);

/*! Get the total number of global symbols. */
int jcc_ast_global_count(JCC *vm);

/*! Get global symbol at index (0-based). */
JCC_Obj *jcc_ast_global_at(JCC *vm, int index);

/*! Get the name of an object. */
const char *jcc_ast_obj_name(JCC_Obj *obj);

/*! Get the type of an object. */
JCC_Type *jcc_ast_obj_type(JCC_Obj *obj);

/*! Check if object is a function. */
bool jcc_ast_obj_is_function(JCC_Obj *obj);

/*! Check if object is a definition. */
bool jcc_ast_obj_is_definition(JCC_Obj *obj);

/*! Check if object has static linkage. */
bool jcc_ast_obj_is_static(JCC_Obj *obj);

// ============================================================================
// AST Node Construction - Literals
// ============================================================================

/*! Create an integer literal node. */
JCC_Node *jcc_ast_int_literal(JCC *vm, int64_t value);

/*! Create a floating-point literal node. */
JCC_Node *jcc_ast_float_literal(JCC *vm, double value);

/*! Create a string literal node. */
JCC_Node *jcc_ast_string_literal(JCC *vm, const char *str);

/*! Create a variable reference node. */
JCC_Node *jcc_ast_var_ref(JCC *vm, const char *name);

/*! Create a reference to a function parameter.
 *  Use this when building function bodies to reference parameters by name.
 */
JCC_Node *jcc_ast_param_ref(JCC *vm, JCC_Obj *fn, const char *name);

// ============================================================================
// AST Node Construction - Expressions
// ============================================================================

/*! Create a binary operation node. */
JCC_Node *jcc_ast_binary(JCC *vm, JCC_NodeKind op, JCC_Node *left,
                            JCC_Node *right);

/*! Create a unary operation node. */
JCC_Node *jcc_ast_unary(JCC *vm, JCC_NodeKind op, JCC_Node *operand);

/*! Create a type cast node. */
JCC_Node *jcc_ast_cast(JCC *vm, JCC_Node *expr, JCC_Type *target_type);

// ============================================================================
// AST Node Construction - Statements
// ============================================================================

/*! Create a return statement node. */
JCC_Node *jcc_ast_return(JCC *vm, JCC_Node *expr);

/*! Create a block (compound statement) node. */
JCC_Node *jcc_ast_block(JCC *vm, JCC_Node **stmts, int count);

/*! Create an if statement node. */
JCC_Node *jcc_ast_if(JCC *vm, JCC_Node *cond, JCC_Node *then_body,
                        JCC_Node *else_body);

/*! Create a switch statement node. */
JCC_Node *jcc_ast_switch(JCC *vm, JCC_Node *cond);

/*! Add a case to a switch statement. */
void jcc_ast_switch_add_case(JCC *vm, JCC_Node *switch_node,
                                JCC_Node *value, JCC_Node *body);

/*! Set the default case for a switch statement. */
void jcc_ast_switch_set_default(JCC *vm, JCC_Node *switch_node,
                                    JCC_Node *body);

/*! Create an expression statement node. */
JCC_Node *jcc_ast_expr_stmt(JCC *vm, JCC_Node *expr);

// ============================================================================
// Function Generation
// ============================================================================

/*!
 * @function jcc_ast_function
 * @abstract Create a new function object.
 * @param vm The VM context.
 * @param name The function name.
 * @param return_type The return type.
 * @return The newly created function object, or NULL on error.
 * @discussion The function is automatically added to the globals list
 *             and will be compiled when the main program is compiled.
 */
JCC_Obj *jcc_ast_function(JCC *vm, const char *name,
                            JCC_Type *return_type);

/*!
 * @function jcc_ast_function_add_param
 * @abstract Add a parameter to a function.
 * @param vm The VM context.
 * @param fn The function object.
 * @param name The parameter name.
 * @param type The parameter type.
 * @discussion Parameters are added in order. Call this multiple times
 *             for multiple parameters.
 */
void jcc_ast_function_add_param(JCC *vm, JCC_Obj *fn, const char *name,
                                JCC_Type *type);

/*!
 * @function jcc_ast_function_set_body
 * @abstract Set the body of a function.
 * @param vm The VM context.
 * @param fn The function object.
 * @param body The function body (a statement or block node).
 * @discussion If body is not already a JCC_ND_BLOCK, it will be wrapped in one.
 */
void jcc_ast_function_set_body(JCC *vm, JCC_Obj *fn, JCC_Node *body);

/*!
 * @function jcc_ast_function_set_static
 * @abstract Set whether a function has static linkage.
 * @param fn The function object.
 * @param is_static True for static linkage, false for external.
 */
void jcc_ast_function_set_static(JCC_Obj *fn, bool is_static);

/*!
 * @function jcc_ast_function_set_inline
 * @abstract Set whether a function is inline.
 * @param fn The function object.
 * @param is_inline True for inline, false otherwise.
 */
void jcc_ast_function_set_inline(JCC_Obj *fn, bool is_inline);

/*!
 * @function jcc_ast_function_set_variadic
 * @abstract Set whether a function is variadic.
 * @param fn The function object.
 * @param is_variadic True for variadic, false otherwise.
 */
void jcc_ast_function_set_variadic(JCC_Obj *fn, bool is_variadic);

// ============================================================================
// Convenience Macros (automatically pass JCC_VM)
// ============================================================================

#define JCC_AST_FIND_TYPE(name) jcc_ast_find_type(JCC_VM, name)
#define JCC_AST_TYPE_EXISTS(name) jcc_ast_type_exists(JCC_VM, name)
#define JCC_AST_GET_TYPE(name) jcc_ast_get_type(JCC_VM, name)

#define JCC_AST_INT_LITERAL(val) jcc_ast_int_literal(JCC_VM, val)
#define JCC_AST_FLOAT_LITERAL(val) jcc_ast_float_literal(JCC_VM, val)
#define JCC_AST_STRING_LITERAL(str) jcc_ast_string_literal(JCC_VM, str)
#define JCC_AST_VAR_REF(name) jcc_ast_var_ref(JCC_VM, name)
#define JCC_AST_PARAM_REF(fn, name) jcc_ast_param_ref(JCC_VM, fn, name)

#define JCC_AST_BINARY(op, l, r) jcc_ast_binary(JCC_VM, op, l, r)
#define JCC_AST_UNARY(op, operand) jcc_ast_unary(JCC_VM, op, operand)
#define JCC_AST_CAST(expr, ty) jcc_ast_cast(JCC_VM, expr, ty)

#define JCC_AST_RETURN(expr) jcc_ast_return(JCC_VM, expr)
#define JCC_AST_BLOCK(stmts, count) jcc_ast_block(JCC_VM, stmts, count)
#define JCC_AST_IF(c, t, e) jcc_ast_if(JCC_VM, c, t, e)
#define JCC_AST_SWITCH(cond) jcc_ast_switch(JCC_VM, cond)
#define JCC_AST_SWITCH_ADD_CASE(sw, v, b)                                      \
    jcc_ast_switch_add_case(JCC_VM, sw, v, b)
#define JCC_AST_SWITCH_SET_DEFAULT(sw, b)                                    \
    jcc_ast_switch_set_default(JCC_VM, sw, b)
#define JCC_AST_EXPR_STMT(expr) jcc_ast_expr_stmt(JCC_VM, expr)

#define JCC_AST_MAKE_POINTER(base) jcc_ast_make_pointer(JCC_VM, base)
#define JCC_AST_MAKE_ARRAY(base, len) jcc_ast_make_array(JCC_VM, base, len)

#define JCC_AST_ENUM_COUNT(ty) jcc_ast_enum_count(JCC_VM, ty)
#define JCC_AST_ENUM_AT(ty, i) jcc_ast_enum_at(JCC_VM, ty, i)
#define JCC_AST_ENUM_FIND(ty, name) jcc_ast_enum_find(JCC_VM, ty, name)
#define JCC_AST_ENUM_CONSTANT_NAME(ec) jcc_ast_enum_constant_name(ec)
#define JCC_AST_ENUM_CONSTANT_VALUE(ec) jcc_ast_enum_constant_value(ec)

#define JCC_AST_STRUCT_MEMBER_COUNT(ty) jcc_ast_struct_member_count(JCC_VM, ty)
#define JCC_AST_STRUCT_MEMBER_AT(ty, i) jcc_ast_struct_member_at(JCC_VM, ty, i)
#define JCC_AST_STRUCT_MEMBER_FIND(ty, name)                                   \
    jcc_ast_struct_member_find(JCC_VM, ty, name)
#define JCC_AST_MEMBER_NAME(m) jcc_ast_member_name(m)
#define JCC_AST_MEMBER_TYPE(m) jcc_ast_member_type(m)
#define JCC_AST_MEMBER_OFFSET(m) jcc_ast_member_offset(m)

#define JCC_AST_FIND_GLOBAL(name) jcc_ast_find_global(JCC_VM, name)
#define JCC_AST_GLOBAL_COUNT() jcc_ast_global_count(JCC_VM)
#define JCC_AST_GLOBAL_AT(i) jcc_ast_global_at(JCC_VM, i)

#define JCC_AST_FUNCTION(name, ret_type)                                       \
    jcc_ast_function(JCC_VM, name, ret_type)
#define JCC_AST_FUNCTION_ADD_PARAM(fn, name, type)                             \
    jcc_ast_function_add_param(JCC_VM, fn, name, type)
#define JCC_AST_FUNCTION_SET_BODY(fn, body)                                    \
    jcc_ast_function_set_body(JCC_VM, fn, body)
#define JCC_AST_FUNCTION_SET_STATIC(fn, is_static)                             \
    jcc_ast_function_set_static(fn, is_static)
#define JCC_AST_FUNCTION_SET_INLINE(fn, is_inline)                             \
    jcc_ast_function_set_inline(fn, is_inline)
#define JCC_AST_FUNCTION_SET_VARIADIC(fn, is_variadic)                         \
    jcc_ast_function_set_variadic(fn, is_variadic)

#ifdef __cplusplus
}
#endif

#endif // JCC_REFLECTION_H
