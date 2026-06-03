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
 * - Global variable generation (_AST_GLOBAL_VAR*)
 * - Function-building context (_AST_WITH_FN)
 *
 * ## Return-value model
 *
 * A pragma macro's returned _Node* is **the node spliced at the call site**,
 * replacing the invocation.  Top-level definitions (functions, globals) are
 * **side effects** — injected into the program regardless of the return value.
 *
 *  - **Expression position** (e.g. `int x = my_macro();`): the macro must
 *    return a non-NULL node.  Returning NULL is an error.
 *
 *  - **Declaration position** (bare `my_macro();` at file scope, or an
 *    `inline` auto-run macro): there is no expression to replace.  Returning
 *    NULL — or declaring the macro `void` — is legal and means "I only emitted
 *    definitions."
 *
 * Declare a definition-only macro with a `void` return type for clarity:
 * @code
 * #pragma macro
 * void emit_helpers(void) {
 *     _Obj *fn = _AST_FUNCTION("helper", _AST_GET_TYPE("int"));
 *     _AST_WITH_FN(fn) {
 *         _AST_FUNCTION_SET_BODY(fn, _QUOTE("return 42;"));
 *     }
 *     _AST_FORWARD_DECLARE(fn);
 *     // no return needed
 * }
 * emit_helpers();
 * @endcode
 *
 * A void macro used in expression position is a compile error.
 *
 * ## Usage
 *
 * This private header is embedded into JCC and automatically injected when
 * compiling pragma macros. It is not installed as a public runtime header.
 * All functions that require VM context use the _VM macro, which
 * expands to __jcc_get_vm() - a builtin that returns the current
 * VM instance during macro execution.
 *
 * ## Example (expression macro)
 *
 * @code
 * #pragma macro
 * _Node *make_const_5(void) {
 *     return _AST_INT_LITERAL(5);
 * }
 *
 * int main(void) {
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
typedef struct JCC _VirtualMachine;
typedef struct Type _Type;
typedef struct Node _Node;
typedef struct Obj _Obj;
typedef struct Member _Member;
typedef struct EnumConstant _EnumConstant;
typedef struct Token _Token;

// Type kind enumeration (matches jcc.h)
typedef enum {
    _VOID = 0,
    _BOOL = 1,
    _CHAR = 2,
    _SHORT = 3,
    _INT = 4,
    _LONG = 5,
    _FLOAT = 6,
    _DOUBLE = 7,
    _LDOUBLE = 8,
    _ENUM = 9,
    _PTR = 10,
    _FUNC = 11,
    _ARRAY = 12,
    _VLA = 13,
    _STRUCT = 14,
    _UNION = 15,
} _TypeKind;

// Node kind enumeration (subset for pragma macro use)
typedef enum {
    _NULL_EXPR = 0,
    _ADD = 1,
    _SUB = 2,
    _MUL = 3,
    _DIV = 4,
    _NEG = 5,
    _MOD = 6,
    _BITAND = 7,
    _BITOR = 8,
    _BITXOR = 9,
    _SHL = 10,
    _SHR = 11,
    _EQ = 12,
    _NE = 13,
    _LT = 14,
    _LE = 15,
    _ASSIGN = 16,
    _COND = 17,
    _COMMA = 18,
    _MEMBER = 19,
    _ADDR = 20,
    _DEREF = 21,
    _NOT = 22,
    _BITNOT = 23,
    _LOGAND = 24,
    _LOGOR = 25,
    _RETURN = 26,
    _IF = 27,
    _FOR = 28,
    _DO = 29,
    _SWITCH = 30,
    _CASE = 31,
    _BLOCK = 32,
    _FUNCALL = 37,
    _EXPR_STMT = 38,
    _VAR = 40,
    _NUM = 42,
    _CAST = 43,
} _NodeKind;

// ============================================================================
// Magic _VM Builtin
// ============================================================================

/*!
 * @function __jcc_get_vm
 * @abstract Builtin that returns the current parent VM context.
 * @discussion Set before calling a pragma macro and cleared after.
 * @return Pointer to the current JCC VM instance.
 */
extern JCC *__jcc_get_vm(void);

/*!
 * @define _VM
 * @abstract Magic macro that references the VM instance in pragma macros.
 */
#define _VM __jcc_get_vm()

/*!
 * @function __jcc_gensym
 * @abstract Generate a unique identifier string for macro-created symbols.
 * @param vm The VM context.
 * @param prefix Prefix for the generated name.
 * @return An arena-allocated string of the form "<prefix>__<n>".
 */
const char *__jcc_gensym(JCC *vm, const char *prefix);

// ============================================================================
// Macro Diagnostics (ticket #78)
// ============================================================================

/*!
 * @function __jcc_macro_error_at
 * @abstract Emit a compiler error pointing at the source location of a node.
 * @param vm The VM context.
 * @param node A node whose tok field provides file/line/col. May be NULL
 *             (falls back to a location-less error).
 * @param fmt printf-style format string, followed by format arguments.
 * @discussion Behaves like the compiler's error_tok(): in normal mode it
 *             prints the error with file/line/col and source snippet then
 *             aborts via longjmp or exit.  When vm->collect_errors is set
 *             it records the error and compilation may continue.
 */
void __jcc_macro_error_at(JCC *vm, _Node *node, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/*!
 * @function __jcc_macro_warning_at
 * @abstract Emit a compiler warning pointing at the source location of a node.
 * @param vm The VM context.
 * @param node A node whose tok field provides file/line/col. May be NULL.
 * @param fmt printf-style format string, followed by format arguments.
 * @discussion Emitted only when -Wjcc-macro is enabled. Non-fatal unless
 *             promoted with -Werror or -Werror=jcc-macro.
 */
void __jcc_macro_warning_at(JCC *vm, _Node *node, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/*!
 * @function __jcc_quote
 * @abstract Parse a C code template string into an AST node, substituting
 *           splice points with the provided argument nodes.
 * @param vm The VM context.
 * @param tmpl A C expression or statement as a string literal.
 *
 *   **Scalar splices** — replace a single position with one node:
 *     - `$1`, `$2`, ... positional (1-indexed, reorderable, reusable).
 *     - `$$` sequential (left-to-right); cannot be mixed with `$N`.
 *
 *   **List splices** — expand a statement-position placeholder into a chain
 *   of N statements (ticket #172):
 *     - `$@1`, `$@2`, ... positional list splice.  The argument must be a
 *       `->next`-linked node chain (see `_NODE_LIST` / `__jcc_node_list`).
 *       Each `$@k;` in the template is replaced by the entire chain.
 *     - `$@` sequential list splice, parallel to `$$`.
 *     - List splice identifiers (`$@k`) may share their index with scalar
 *       splices (`$k`) in the same template, but positional and incremental
 *       styles cannot be mixed within one template.
 *
 *   List splices are valid **only in statement-list position** (inside a
 *   `{ ... }` block).  Using `$@k` as an expression operand is a
 *   compile-time error.  Call-argument and initializer splicing are not yet
 *   supported.
 *
 * @param ... _Node* arguments corresponding to the splice points.
 * @return The parsed and substituted AST node, or NULL on error.
 * @discussion Template is parsed and substituted at macro-execution (compile)
 *             time; there is no runtime overhead.  Expressions and statements
 *             are auto-detected.  Capped at ~6 splice nodes due to the 8-
 *             register FFI limit; use __jcc_quote_n for more.
 */
_Node *__jcc_quote(JCC *vm, const char *tmpl, ...);

/*!
 * @function __jcc_quote_n
 * @abstract Array-form quasi-quote; validates the splice count and supports
 *           more than 6 splice nodes.
 * @param vm The VM context.
 * @param tmpl A C expression or statement as a string literal with $N / $@N
 *             splice points.
 * @param nodes Array of _Node* splice arguments.
 * @param count Length of the nodes array.  If any $K in the template exceeds
 *              count, a compile-time error is emitted.
 * @return The parsed and substituted AST node, or NULL on error.
 */
_Node *__jcc_quote_n(JCC *vm, const char *tmpl, _Node **nodes, int count);

/*!
 * @function __jcc_node_list
 * @abstract Build a `->next`-linked node chain from an array, returning the
 *           head.  Use the result as the argument to a `$@k` list splice.
 * @param vm    The VM context.
 * @param nodes Array of _Node* to link together.  Linking stops at the first
 *              NULL element or at count, whichever comes first.
 * @param count Number of elements in the array.
 * @return Head of the chain, or NULL if count == 0 or nodes is NULL.
 * @discussion A single node is a valid chain of length 1.  An existing
 *             `->next` chain (e.g. `__jcc_ast_block(...)->body`) can also be
 *             passed directly as the splice argument without going through
 *             this helper.
 */
_Node *__jcc_node_list(JCC *vm, _Node **nodes, int count);

// ============================================================================
// Type Lookup and Introspection
// ============================================================================

/*! Find a type by name (struct/union/enum tag). Returns NULL if not found. */
_Type *__jcc_ast_find_type(JCC *vm, const char *name);

/*! Check if a type exists by name. */
bool __jcc_ast_type_exists(JCC *vm, const char *name);

/*! Lookup a type by name (includes built-in types). */
_Type *__jcc_ast_get_type(JCC *vm, const char *name);

/*! Get the _TypeKind of a type. */
_TypeKind __jcc_ast_type_kind(_Type *ty);

/*! Get sizeof() value in bytes. */
int __jcc_ast_type_size(_Type *ty);

/*! Get alignment in bytes. */
int __jcc_ast_type_align(_Type *ty);

/*! Check if type is unsigned. */
bool __jcc_ast_type_is_unsigned(_Type *ty);

/*! Check if type is const-qualified. */
bool __jcc_ast_type_is_const(_Type *ty);

/*! For pointer/array types: get the base type. */
_Type *__jcc_ast_type_base(_Type *ty);

/*! For array types: get the array length. Returns -1 if not an array. */
int __jcc_ast_type_array_len(_Type *ty);

/*! For function types: get return type. */
_Type *__jcc_ast_type_return_type(_Type *ty);

/*! For function types: get parameter count. */
int __jcc_ast_type_param_count(_Type *ty);

/*! For function types: get parameter type at index. */
_Type *__jcc_ast_type_param_at(_Type *ty, int index);

/*! For function types: check if variadic. */
bool __jcc_ast_type_is_variadic(_Type *ty);

/*! Get type name (for named types). */
const char *__jcc_ast_type_name(_Type *ty);

/*! Create a pointer type to base. */
_Type *__jcc_ast_make_pointer(JCC *vm, _Type *base);

/*! Create an array type with specified length. */
_Type *__jcc_ast_make_array(JCC *vm, _Type *base, int length);

// Ticket #171: qualified type constructors
/*! Return a const-qualified copy of ty. */
_Type *__jcc_ast_make_const(JCC *vm, _Type *ty);
/*! Return a volatile-qualified copy of ty. */
_Type *__jcc_ast_make_volatile(JCC *vm, _Type *ty);

// ============================================================================
// Enum Reflection
// ============================================================================

/*! Get the number of enum constants. Returns -1 if not an enum. */
int __jcc_ast_enum_count(JCC *vm, _Type *enum_type);

/*! Get enum constant at index (0-based). */
_EnumConstant *__jcc_ast_enum_at(JCC *vm, _Type *enum_type, int index);

/*! Find enum constant by name. */
_EnumConstant *__jcc_ast_enum_find(JCC *vm, _Type *enum_type,
                                    const char *name);

/*! Get enum constant name. */
const char *__jcc_ast_enum_constant_name(_EnumConstant *ec);

/*! Get enum constant value. */
int __jcc_ast_enum_constant_value(_EnumConstant *ec);

/*! Get the name of an enum type. */
const char *__jcc_ast_enum_name(_Type *e);

/*! Get the number of values in an enum. */
int __jcc_ast_enum_value_count(_Type *e);

/*! Get the name of an enum value at index. */
const char *__jcc_ast_enum_value_name(_Type *e, int index);

/*! Get the integer value of an enum constant at index. */
int __jcc_ast_enum_value(_Type *e, int index);

// ============================================================================
// Struct/Union Member Introspection
// ============================================================================

/*! Get the number of members. Returns -1 if not a struct/union. */
int __jcc_ast_struct_member_count(JCC *vm, _Type *struct_type);

/*! Get member at index (0-based). */
_Member *__jcc_ast_struct_member_at(JCC *vm, _Type *struct_type,
                                        int index);

/*! Find member by name. */
_Member *__jcc_ast_struct_member_find(JCC *vm, _Type *struct_type,
                                        const char *name);

/*! Get member name. */
const char *__jcc_ast_member_name(_Member *m);

/*! Get member type. */
_Type *__jcc_ast_member_type(_Member *m);

/*! Get member offset in bytes. */
int __jcc_ast_member_offset(_Member *m);

/*! Check if member is a bitfield. */
bool __jcc_ast_member_is_bitfield(_Member *m);

/*! Get bitfield width. */
int __jcc_ast_member_bitfield_width(_Member *m);

// ============================================================================
// Global Symbol Introspection
// ============================================================================

/*! Find a global symbol by name. */
_Obj *__jcc_ast_find_global(JCC *vm, const char *name);

/*! Get the total number of global symbols. */
int __jcc_ast_global_count(JCC *vm);

/*! Get global symbol at index (0-based). */
_Obj *__jcc_ast_global_at(JCC *vm, int index);

/*! Get the name of an object. */
const char *__jcc_ast_obj_name(_Obj *obj);

/*! Get the type of an object. */
_Type *__jcc_ast_obj_type(_Obj *obj);

/*! Check if object is a function. */
bool __jcc_ast_obj_is_function(_Obj *obj);

/*! Check if object is a definition. */
bool __jcc_ast_obj_is_definition(_Obj *obj);

/*! Check if object has static linkage. */
bool __jcc_ast_obj_is_static(_Obj *obj);

// ============================================================================
// AST Node Construction - Literals
// ============================================================================

/*! Create an integer literal node. */
_Node *__jcc_ast_int_literal(JCC *vm, int64_t value);

/*! Create a floating-point literal node. */
_Node *__jcc_ast_float_literal(JCC *vm, double value);

/*! Create a string literal node. */
_Node *__jcc_ast_string_literal(JCC *vm, const char *str);

/*! Create a variable reference node. */
_Node *__jcc_ast_var_ref(JCC *vm, const char *name);

/*! Create a reference to a function parameter.
 *  Use this when building function bodies to reference parameters by name.
 */
_Node *__jcc_ast_param_ref(JCC *vm, _Obj *fn, const char *name);

// ============================================================================
// AST Node Construction - Expressions
// ============================================================================

/*! Create a binary operation node. */
_Node *__jcc_ast_binary(JCC *vm, _NodeKind op, _Node *left,
                            _Node *right);

/*! Create a unary operation node. */
_Node *__jcc_ast_unary(JCC *vm, _NodeKind op, _Node *operand);

/*! Create a type cast node. */
_Node *__jcc_ast_cast(JCC *vm, _Node *expr, _Type *target_type);

// Ticket #171: new expression builders

/*! Create a ternary conditional expression node (cond ? then_expr : else_expr). */
_Node *__jcc_ast_cond(JCC *vm, _Node *cond, _Node *then_expr,
                          _Node *else_expr);

/*! Create a typed null pointer node: (void *)0. */
_Node *__jcc_ast_null(JCC *vm);

/*! Return sizeof(ty) as a compile-time integer literal. */
_Node *__jcc_ast_sizeof_type(JCC *vm, _Type *ty);

/*! Return _Alignof(ty) as a compile-time integer literal. */
_Node *__jcc_ast_alignof_type(JCC *vm, _Type *ty);

/*! Return sizeof(expr): resolve the expression's type then emit its size. */
_Node *__jcc_ast_sizeof_expr(JCC *vm, _Node *expr);

/*! Create an array subscript node: arr[idx], desugared as *(arr+idx). */
_Node *__jcc_ast_subscript(JCC *vm, _Node *arr, _Node *idx);

/*! Create a comma expression node: evaluate lhs for side effects, yield rhs. */
_Node *__jcc_ast_comma(JCC *vm, _Node *lhs, _Node *rhs);

// ============================================================================
// AST Node Construction - Statements
// ============================================================================

/*! Create a return statement node. */
_Node *__jcc_ast_return(JCC *vm, _Node *expr);

/*! Create a block (compound statement) node. */
_Node *__jcc_ast_block(JCC *vm, _Node **stmts, int count);

/*! Create an if statement node. */
_Node *__jcc_ast_if(JCC *vm, _Node *cond, _Node *then_body,
                        _Node *else_body);

/*! Create a switch statement node. */
_Node *__jcc_ast_switch(JCC *vm, _Node *cond);

/*! Add a case to a switch statement. */
void __jcc_ast_switch_add_case(JCC *vm, _Node *switch_node,
                                _Node *value, _Node *body);

/*! Set the default case for a switch statement. */
void __jcc_ast_switch_set_default(JCC *vm, _Node *switch_node,
                                    _Node *body);

/*! Create an expression statement node. */
_Node *__jcc_ast_expr_stmt(JCC *vm, _Node *expr);

// ============================================================================
// AST Node Construction - Local Variable Injection (ticket #77)
// ============================================================================

/*!
 * @function __jcc_ast_local_var
 * @abstract Declare a named local variable in the current function scope
 *           and return a variable-reference node for it.
 * @param vm The VM context.
 * @param name The variable name (user-visible).
 * @param ty The variable type.
 * @return A _VAR node referencing the new local, or NULL if called
 *         outside a function body or on invalid arguments.
 * @note  The variable is injected into the current function's locals list
 *        and will receive a stack offset when the function is compiled.
 *        For temporaries that must not capture user names, prefer
 *        __jcc_ast_local_var_unique().
 */
_Node *__jcc_ast_local_var(JCC *vm, const char *name, _Type *ty);

/*!
 * @function __jcc_ast_local_var_unique
 * @abstract Declare a hygienic (gensym'd) local variable in the current
 *           function scope and return a variable-reference node for it.
 * @param vm The VM context.
 * @param ty The variable type.
 * @return A _VAR node referencing the new local, or NULL on error.
 * @note  The generated name begins with ".L.." and is therefore not
 *        expressible as a user identifier — guaranteed no name capture.
 *        This is the safe default for macro temporaries.
 */
_Node *__jcc_ast_local_var_unique(JCC *vm, _Type *ty);

/*! Create an assignment node (target = value). */
_Node *__jcc_ast_assign(JCC *vm, _Node *target, _Node *value);

/*!
 * @function __jcc_ast_member
 * @abstract Create a struct/union member access node (obj.name).
 * @param vm The VM context.
 * @param obj An expression node whose type must be a struct or union.
 * @param name The member name as a NUL-terminated string.
 * @return A _MEMBER node, or NULL if the member is not found or
 *         obj is not a struct/union type.
 * @note The callee is responsible for dereferencing pointers first;
 *       pass the struct value directly (use __jcc_ast_unary(ND_DEREF,…)
 *       for pointer-to-struct access).
 */
_Node *__jcc_ast_member(JCC *vm, _Node *obj, const char *name);

/*!
 * @function __jcc_ast_funcall
 * @abstract Create a function call node.
 * @param vm The VM context.
 * @param callee An expression node that evaluates to a function (or function
 *               pointer). The callee's lhs field holds this expression.
 * @param args Array of argument nodes (may be NULL if n == 0).
 * @param n Number of arguments.
 * @return A _FUNCALL node, or NULL on error.
 */
_Node *__jcc_ast_funcall(JCC *vm, _Node *callee, _Node **args, int n);

/*!
 * @function __jcc_ast_while
 * @abstract Create a while loop node.
 * @param vm The VM context.
 * @param cond The loop condition expression.
 * @param body The loop body statement.
 * @return A _FOR node (JCC represents while as for with no init/inc),
 *         or NULL on error.
 */
_Node *__jcc_ast_while(JCC *vm, _Node *cond, _Node *body);

/*!
 * @function __jcc_ast_for
 * @abstract Create a for loop node.
 * @param vm The VM context.
 * @param init Initialiser expression/statement (may be NULL).
 * @param cond Loop condition (may be NULL for infinite loop).
 * @param inc Increment expression (may be NULL).
 * @param body Loop body.
 * @return A _FOR node, or NULL on error.
 */
_Node *__jcc_ast_for(JCC *vm, _Node *init, _Node *cond,
                       _Node *inc, _Node *body);

/*!
 * @function __jcc_ast_do_while
 * @abstract Create a do-while loop node.
 * @param vm The VM context.
 * @param body The loop body.
 * @param cond The loop condition (tested after each iteration).
 * @return A _DO node, or NULL on error.
 */
_Node *__jcc_ast_do_while(JCC *vm, _Node *body, _Node *cond);

// ============================================================================
// Function Generation
// ============================================================================

/*!
 * @function __jcc_ast_function
 * @abstract Create a new function object.
 * @param vm The VM context.
 * @param name The function name.
 * @param return_type The return type.
 * @return The newly created function object, or NULL on error.
 * @discussion The function is automatically added to the globals list
 *             and will be compiled when the main program is compiled.
 */
_Obj *__jcc_ast_function(JCC *vm, const char *name,
                            _Type *return_type);

/*!
 * @function __jcc_ast_forward_declare
 * @abstract Make a generated function visible at the current source position.
 * @param vm The VM context.
 * @param fn A function object created with __jcc_ast_function().
 * @return A no-op _Node on success, or NULL on invalid arguments.
 * @discussion Top-level explicit macro calls run at their source position.
 *             Call this after creating a function when later source should be
 *             able to call that generated function without a handwritten
 *             prototype.
 */
_Node *__jcc_ast_forward_declare(JCC *vm, _Obj *fn);

/*!
 * @function __jcc_ast_function_add_param
 * @abstract Add a parameter to a function.
 * @param vm The VM context.
 * @param fn The function object.
 * @param name The parameter name.
 * @param type The parameter type.
 * @discussion Parameters are added in order. Call this multiple times
 *             for multiple parameters.
 */
void __jcc_ast_function_add_param(JCC *vm, _Obj *fn, const char *name,
                                _Type *type);

/*!
 * @function __jcc_ast_function_set_body
 * @abstract Set the body of a function.
 * @param vm The VM context.
 * @param fn The function object.
 * @param body The function body (a statement or block node).
 * @discussion If body is not already a _BLOCK, it will be wrapped in one.
 */
void __jcc_ast_function_set_body(JCC *vm, _Obj *fn, _Node *body);

/*!
 * @function __jcc_ast_function_set_static
 * @abstract Set whether a function has static linkage.
 * @param fn The function object.
 * @param is_static True for static linkage, false for external.
 */
void __jcc_ast_function_set_static(_Obj *fn, bool is_static);

/*!
 * @function __jcc_ast_function_set_inline
 * @abstract Set whether a function is inline.
 * @param fn The function object.
 * @param is_inline True for inline, false otherwise.
 */
void __jcc_ast_function_set_inline(_Obj *fn, bool is_inline);

/*!
 * @function __jcc_ast_function_set_variadic
 * @abstract Set whether a function is variadic.
 * @param fn The function object.
 * @param is_variadic True for variadic, false otherwise.
 */
void __jcc_ast_function_set_variadic(_Obj *fn, bool is_variadic);

// Ticket #171: function prototype (forward declaration only, no body)
/*!
 * @function __jcc_ast_function_prototype
 * @abstract Create a function forward declaration (prototype) without a body.
 * @param vm The VM context.
 * @param name The function name.
 * @param return_type The return type.
 * @return The declaration Obj*, or NULL on error.
 * @discussion Use _AST_FUNCTION_ADD_PARAM to add parameters and
 *             _AST_FORWARD_DECLARE to expose it in scope. A subsequent
 *             _AST_FUNCTION call with the same name will reuse this Obj and
 *             fill in the body.
 */
_Obj *__jcc_ast_function_prototype(JCC *vm, const char *name,
                                    _Type *return_type);

// Ticket #171: struct/union/enum/typedef type builders

/*!
 * @function __jcc_ast_make_struct
 * @abstract Create and expose a new named struct type.
 * @param vm The VM context.
 * @param name The struct tag name.
 * @return The new struct _Type*, or NULL on error.
 * @discussion Use _AST_STRUCT_ADD_FIELD to add fields after creation.
 *             The type is immediately visible via _AST_FIND_TYPE(name).
 */
_Type *__jcc_ast_make_struct(JCC *vm, const char *name);

/*!
 * @function __jcc_ast_make_union
 * @abstract Create and expose a new named union type.
 * @param vm The VM context.
 * @param name The union tag name.
 * @return The new union _Type*, or NULL on error.
 */
_Type *__jcc_ast_make_union(JCC *vm, const char *name);

/*!
 * @function __jcc_ast_struct_add_field
 * @abstract Append a field to a struct or union and recompute its layout.
 * @param vm The VM context.
 * @param ty The struct or union type to modify.
 * @param name The field name.
 * @param field_type The field's type.
 * @return ty on success, or NULL on error.
 * @discussion For struct types, offsets are recalculated from scratch after
 *             each field addition. For union types, all fields stay at offset 0
 *             and the union size is updated to the maximum field size.
 */
_Type *__jcc_ast_struct_add_field(JCC *vm, _Type *ty, const char *name,
                                   _Type *field_type);

/*!
 * @function __jcc_ast_make_enum
 * @abstract Create and expose a new named enum type.
 * @param vm The VM context.
 * @param name The enum tag name.
 * @return The new enum _Type*, or NULL on error.
 * @discussion Use _AST_ENUM_ADD_CONSTANT to add constants after creation.
 */
_Type *__jcc_ast_make_enum(JCC *vm, const char *name);

/*!
 * @function __jcc_ast_enum_add_constant
 * @abstract Add a named constant to an enum type and expose it in scope.
 * @param vm The VM context.
 * @param ty The enum type.
 * @param name The constant name.
 * @param value The constant integer value.
 * @discussion The constant is appended to ty->enum_constants and also pushed
 *             into the current scope so it is usable as an integer constant in
 *             subsequently compiled code.
 */
void __jcc_ast_enum_add_constant(JCC *vm, _Type *ty, const char *name,
                                  int value);

/*!
 * @function __jcc_ast_make_typedef
 * @abstract Register a typedef alias for a type and expose it in scope.
 * @param vm The VM context.
 * @param name The typedef name.
 * @param underlying The aliased type.
 * @discussion After this call, _AST_FIND_TYPE(name) resolves to underlying and
 *             subsequently compiled code can use name as a type name.
 */
void __jcc_ast_make_typedef(JCC *vm, const char *name, _Type *underlying);

// ============================================================================
// Global Variable Generation (ticket #152)
// ============================================================================

/*!
 * @function __jcc_ast_global_var
 * @abstract Create a new named global variable definition.
 * @param vm   The VM context.
 * @param name The variable name (must be unique among globals).
 * @param ty   The variable type.  Use _AST_MAKE_ARRAY(char_ty, len) for byte
 *             arrays so that the size matches the init_data length.
 * @return The new Obj*, or NULL on error.
 * @discussion The variable is registered in vm->compiler.globals.  For inline
 *             macros the capture loop will stash it in macro_globals and emit
 *             an extern declaration into every input file's token stream so
 *             the parser can resolve references.
 */
_Obj *__jcc_ast_global_var(JCC *vm, const char *name, _Type *ty);

/*!
 * @function __jcc_ast_global_var_set_init_data
 * @abstract Set the initial data for a generated global variable.
 * @param vm   The VM context.
 * @param var  The global variable object.
 * @param data Pointer to the raw byte data.
 * @param len  Number of bytes to copy.  Must equal var->ty->size.
 */
void __jcc_ast_global_var_set_init_data(JCC *vm, _Obj *var,
                                        const char *data, int len);

/*!
 * @function __jcc_ast_global_var_set_static
 * @abstract Set the static (internal linkage) flag on a generated global.
 * @param var       The global variable object.
 * @param is_static True for internal linkage (file-scope static).
 */
void __jcc_ast_global_var_set_static(_Obj *var, bool is_static);

// ============================================================================
// Function-building context (ticket #148)
// ============================================================================

/*!
 * @function __jcc_ast_push_fn
 * @abstract Establish fn as the "function currently being built" so that
 *           _QUOTE("return x;") applies the correct implicit return-type cast.
 * @param vm The VM context.
 * @param fn The generated function whose return type should be used.
 * @discussion Call __jcc_ast_pop_fn (or use the _AST_WITH_FN macro) to
 *             restore the previous context.  execute_pragma_macro always
 *             restores current_fn after the macro returns, so unmatched pushes
 *             cannot leak into the main parse/codegen pass.
 */
void __jcc_ast_push_fn(JCC *vm, _Obj *fn);

/*!
 * @function __jcc_ast_pop_fn
 * @abstract Restore the function context saved by the most recent push.
 * @param vm The VM context.
 */
void __jcc_ast_pop_fn(JCC *vm);

// ============================================================================
// AST Dump Functions (ticket #58) — Nim-style dumpTree / dumpAstGen
// ============================================================================

/*!
 * @function __jcc_dump_tree
 * @abstract Print a human-readable tree representation of a node to stdout.
 * @param vm The VM context.
 * @param node The root node to print.
 * @discussion Reuses the compiler's internal cc_dump_ast text renderer.
 */
void __jcc_dump_tree(JCC *vm, _Node *node);

/*!
 * @function __jcc_dump_tree_to_string
 * @abstract Render the tree representation to a heap-allocated string.
 * @param vm The VM context.
 * @param node The root node.
 * @return An arena-allocated NUL-terminated string, or NULL on error.
 */
const char *__jcc_dump_tree_to_string(JCC *vm, _Node *node);

/*!
 * @function __jcc_dump_ast_gen
 * @abstract Print __jcc_ast_*() builder calls that would reconstruct the node.
 * @param vm The VM context.
 * @param node The root node to emit builder calls for.
 * @discussion Covers all node kinds for which reflect.c has a builder.
 *             Unsupported kinds are emitted as C comments.
 */
void __jcc_dump_ast_gen(JCC *vm, _Node *node);

/*!
 * @function __jcc_dump_ast_gen_to_string
 * @abstract Render the __jcc_ast_*() builder call sequence to a string.
 * @param vm The VM context.
 * @param node The root node.
 * @return An arena-allocated NUL-terminated string, or NULL on error.
 */
const char *__jcc_dump_ast_gen_to_string(JCC *vm, _Node *node);

// ============================================================================
// Convenience Macros (automatically pass _VM)
// ============================================================================

// Quasi-quoting helpers (ticket #1, #172)
#define _QUOTE(tmpl, ...) __jcc_quote(_VM, tmpl, ##__VA_ARGS__)
#define _QUOTE_N(tmpl, nodes, count) __jcc_quote_n(_VM, tmpl, nodes, count)
// Build a ->next-linked chain from a compound-literal array for $@k splices:
//   _NODE_LIST((_Node*[]){ a, b, c }, 3)
#define _NODE_LIST(nodes, count) __jcc_node_list(_VM, nodes, count)

// Diagnostic helpers (ticket #78) — note: variadic macros require C99+
#define _MACRO_ERROR_AT(node, ...) __jcc_macro_error_at(_VM, node, __VA_ARGS__)
#define _MACRO_WARNING_AT(node, ...) __jcc_macro_warning_at(_VM, node, __VA_ARGS__)

// AST dump helpers (ticket #58)
#define _DUMP_TREE(node) __jcc_dump_tree(_VM, node)
#define _DUMP_TREE_TO_STRING(node) __jcc_dump_tree_to_string(_VM, node)
#define _DUMP_AST_GEN(node) __jcc_dump_ast_gen(_VM, node)
#define _DUMP_AST_GEN_TO_STRING(node) __jcc_dump_ast_gen_to_string(_VM, node)
#define _GENSYM(prefix) __jcc_gensym(_VM, prefix)

#define _AST_FIND_TYPE(name) __jcc_ast_find_type(_VM, name)
#define _AST_TYPE_EXISTS(name) __jcc_ast_type_exists(_VM, name)
#define _AST_GET_TYPE(name) __jcc_ast_get_type(_VM, name)

// Type introspection — no _VM needed
#define _AST_TYPE_KIND(ty)          __jcc_ast_type_kind(ty)
#define _AST_TYPE_SIZE(ty)          __jcc_ast_type_size(ty)
#define _AST_TYPE_ALIGN(ty)         __jcc_ast_type_align(ty)
#define _AST_TYPE_IS_UNSIGNED(ty)   __jcc_ast_type_is_unsigned(ty)
#define _AST_TYPE_IS_CONST(ty)      __jcc_ast_type_is_const(ty)
#define _AST_TYPE_BASE(ty)          __jcc_ast_type_base(ty)
#define _AST_TYPE_ARRAY_LEN(ty)     __jcc_ast_type_array_len(ty)
#define _AST_TYPE_RETURN_TYPE(ty)   __jcc_ast_type_return_type(ty)
#define _AST_TYPE_PARAM_COUNT(ty)   __jcc_ast_type_param_count(ty)
#define _AST_TYPE_PARAM_AT(ty, i)   __jcc_ast_type_param_at(ty, i)
#define _AST_TYPE_IS_VARIADIC(ty)   __jcc_ast_type_is_variadic(ty)
#define _AST_TYPE_NAME(ty)          __jcc_ast_type_name(ty)

#define _AST_INT_LITERAL(val) __jcc_ast_int_literal(_VM, val)
#define _AST_FLOAT_LITERAL(val) __jcc_ast_float_literal(_VM, val)
#define _AST_STRING_LITERAL(str) __jcc_ast_string_literal(_VM, str)
#define _AST_VAR_REF(name) __jcc_ast_var_ref(_VM, name)
#define _AST_PARAM_REF(fn, name) __jcc_ast_param_ref(_VM, fn, name)

#define _AST_BINARY(op, l, r) __jcc_ast_binary(_VM, op, l, r)
#define _AST_UNARY(op, operand) __jcc_ast_unary(_VM, op, operand)
#define _AST_CAST(expr, ty) __jcc_ast_cast(_VM, expr, ty)

// Ticket #171: new expression builders
// Ternary conditional: cond ? then_expr : else_expr
#define _AST_COND(c, t, e) __jcc_ast_cond(_VM, c, t, e)
// Typed null pointer: (void *)0
#define _AST_NULL() __jcc_ast_null(_VM)
// sizeof(type) / _Alignof(type) as a compile-time integer literal
#define _AST_SIZEOF_TYPE(ty) __jcc_ast_sizeof_type(_VM, ty)
#define _AST_ALIGNOF_TYPE(ty) __jcc_ast_alignof_type(_VM, ty)
// sizeof(expr): resolves expr's type then returns its size as an integer literal
#define _AST_SIZEOF_EXPR(expr) __jcc_ast_sizeof_expr(_VM, expr)
// Array subscript: arr[idx] (desugared as *(arr+idx))
#define _AST_SUBSCRIPT(arr, idx) __jcc_ast_subscript(_VM, arr, idx)
// Comma expression: evaluate lhs (for side effects), yield rhs
#define _AST_COMMA(lhs, rhs) __jcc_ast_comma(_VM, lhs, rhs)

#define _AST_RETURN(expr) __jcc_ast_return(_VM, expr)
#define _AST_BLOCK(stmts, count) __jcc_ast_block(_VM, stmts, count)
#define _AST_IF(c, t, e) __jcc_ast_if(_VM, c, t, e)
#define _AST_SWITCH(cond) __jcc_ast_switch(_VM, cond)
#define _AST_SWITCH_ADD_CASE(sw, v, b)                                      \
    __jcc_ast_switch_add_case(_VM, sw, v, b)
#define _AST_SWITCH_SET_DEFAULT(sw, b)                                    \
    __jcc_ast_switch_set_default(_VM, sw, b)
#define _AST_EXPR_STMT(expr) __jcc_ast_expr_stmt(_VM, expr)
#define _AST_LOCAL_VAR(name, ty) __jcc_ast_local_var(_VM, name, ty)
#define _AST_LOCAL_VAR_UNIQUE(ty) __jcc_ast_local_var_unique(_VM, ty)
#define _AST_ASSIGN(target, value) __jcc_ast_assign(_VM, target, value)
#define _AST_MEMBER(obj, name) __jcc_ast_member(_VM, obj, name)
#define _AST_FUNCALL(callee, args, n) __jcc_ast_funcall(_VM, callee, args, n)
#define _AST_WHILE(cond, body) __jcc_ast_while(_VM, cond, body)
#define _AST_FOR(init, cond, inc, body) __jcc_ast_for(_VM, init, cond, inc, body)
#define _AST_DO_WHILE(body, cond) __jcc_ast_do_while(_VM, body, cond)

#define _AST_MAKE_POINTER(base) __jcc_ast_make_pointer(_VM, base)
#define _AST_MAKE_ARRAY(base, len) __jcc_ast_make_array(_VM, base, len)

// Ticket #171: qualified type constructors
#define _AST_MAKE_CONST(ty)    __jcc_ast_make_const(_VM, ty)
#define _AST_MAKE_VOLATILE(ty) __jcc_ast_make_volatile(_VM, ty)

#define _AST_ENUM_COUNT(ty) __jcc_ast_enum_count(_VM, ty)
#define _AST_ENUM_AT(ty, i) __jcc_ast_enum_at(_VM, ty, i)
#define _AST_ENUM_FIND(ty, name) __jcc_ast_enum_find(_VM, ty, name)
#define _AST_ENUM_CONSTANT_NAME(ec)   __jcc_ast_enum_constant_name(ec)
#define _AST_ENUM_CONSTANT_VALUE(ec)  __jcc_ast_enum_constant_value(ec)
#define _AST_ENUM_NAME(ty)            __jcc_ast_enum_name(ty)
#define _AST_ENUM_VALUE_COUNT(ty)     __jcc_ast_enum_value_count(ty)
#define _AST_ENUM_VALUE_NAME(ty, i)   __jcc_ast_enum_value_name(ty, i)
#define _AST_ENUM_VALUE(ty, i)        __jcc_ast_enum_value(ty, i)

#define _AST_STRUCT_MEMBER_COUNT(ty) __jcc_ast_struct_member_count(_VM, ty)
#define _AST_STRUCT_MEMBER_AT(ty, i) __jcc_ast_struct_member_at(_VM, ty, i)
#define _AST_STRUCT_MEMBER_FIND(ty, name)                                   \
    __jcc_ast_struct_member_find(_VM, ty, name)
#define _AST_MEMBER_NAME(m)             __jcc_ast_member_name(m)
#define _AST_MEMBER_TYPE(m)             __jcc_ast_member_type(m)
#define _AST_MEMBER_OFFSET(m)           __jcc_ast_member_offset(m)
#define _AST_MEMBER_IS_BITFIELD(m)      __jcc_ast_member_is_bitfield(m)
#define _AST_MEMBER_BITFIELD_WIDTH(m)   __jcc_ast_member_bitfield_width(m)

#define _AST_FIND_GLOBAL(name)        __jcc_ast_find_global(_VM, name)
#define _AST_GLOBAL_COUNT()           __jcc_ast_global_count(_VM)
#define _AST_GLOBAL_AT(i)             __jcc_ast_global_at(_VM, i)
#define _AST_OBJ_NAME(obj)            __jcc_ast_obj_name(obj)
#define _AST_OBJ_TYPE(obj)            __jcc_ast_obj_type(obj)
#define _AST_OBJ_IS_FUNCTION(obj)     __jcc_ast_obj_is_function(obj)
#define _AST_OBJ_IS_DEFINITION(obj)   __jcc_ast_obj_is_definition(obj)
#define _AST_OBJ_IS_STATIC(obj)       __jcc_ast_obj_is_static(obj)

#define _AST_FUNCTION(name, ret_type)                                       \
    __jcc_ast_function(_VM, name, ret_type)
#define _AST_FORWARD_DECLARE(fn) __jcc_ast_forward_declare(_VM, fn)
#define _AST_FUNCTION_ADD_PARAM(fn, name, type)                             \
    __jcc_ast_function_add_param(_VM, fn, name, type)
#define _AST_FUNCTION_SET_BODY(fn, body)                                    \
    __jcc_ast_function_set_body(_VM, fn, body)
#define _AST_FUNCTION_SET_STATIC(fn, is_static)                             \
    __jcc_ast_function_set_static(fn, is_static)
#define _AST_FUNCTION_SET_INLINE(fn, is_inline)                             \
    __jcc_ast_function_set_inline(fn, is_inline)
#define _AST_FUNCTION_SET_VARIADIC(fn, is_variadic)                         \
    __jcc_ast_function_set_variadic(fn, is_variadic)

// Ticket #171: function forward declaration / prototype builder
// Creates a declaration-only Obj (no body); use _AST_FUNCTION_ADD_PARAM for
// parameters and _AST_FORWARD_DECLARE to make it visible in scope.
#define _AST_FUNCTION_PROTOTYPE(name, ret)                                  \
    __jcc_ast_function_prototype(_VM, name, ret)

// Ticket #171: struct/union/enum/typedef type builders
// Build a new named aggregate and expose it so _AST_GET_TYPE(name) resolves it.
//
//   _Type *s = _AST_MAKE_STRUCT("Point");
//   _AST_STRUCT_ADD_FIELD(s, "x", _AST_GET_TYPE("int"));
//   _AST_STRUCT_ADD_FIELD(s, "y", _AST_GET_TYPE("int"));
//
// _AST_STRUCT_ADD_FIELD works for both struct and union types.
// _AST_MAKE_TYPEDEF registers name as an alias for underlying.
// _AST_ENUM_ADD_CONSTANT adds a constant to the enum AND to scope (usable as int).
#define _AST_MAKE_STRUCT(name)     __jcc_ast_make_struct(_VM, name)
#define _AST_MAKE_UNION(name)      __jcc_ast_make_union(_VM, name)
#define _AST_STRUCT_ADD_FIELD(ty, name, field_type) \
    __jcc_ast_struct_add_field(_VM, ty, name, field_type)
#define _AST_MAKE_ENUM(name)       __jcc_ast_make_enum(_VM, name)
#define _AST_ENUM_ADD_CONSTANT(ty, name, value) \
    __jcc_ast_enum_add_constant(_VM, ty, name, value)
#define _AST_MAKE_TYPEDEF(name, underlying) \
    __jcc_ast_make_typedef(_VM, name, underlying)

// Comptime variable access (ticket #188)
// Read an integer-typed #pragma comptime variable's value at compile time.
int64_t __jcc_get_comptime_int(JCC *vm, const char *name);
// Read a float/double-typed #pragma comptime variable's value.
double __jcc_get_comptime_float(JCC *vm, const char *name);
// Read a comptime scalar variable as an AST literal node (_Node*).
_Node *__jcc_get_comptime_var(JCC *vm, const char *name);
// Read a named field from a comptime struct variable as an AST literal node.
_Node *__jcc_get_comptime_member(JCC *vm, const char *var_name,
                                  const char *field);

#define _AST_GET_COMPTIME_INT(name)           __jcc_get_comptime_int(_VM, name)
#define _AST_GET_COMPTIME_FLOAT(name)         __jcc_get_comptime_float(_VM, name)
#define _AST_GET_COMPTIME_VAR(name)           __jcc_get_comptime_var(_VM, name)
#define _AST_GET_COMPTIME_MEMBER(var, field)  __jcc_get_comptime_member(_VM, var, field)

// Global variable generation (ticket #152)
#define _AST_GLOBAL_VAR(name, ty)                                           \
    __jcc_ast_global_var(_VM, name, ty)
#define _AST_GLOBAL_VAR_SET_INIT_DATA(var, data, len)                       \
    __jcc_ast_global_var_set_init_data(_VM, var, data, len)
#define _AST_GLOBAL_VAR_SET_STATIC(var, is_static)                          \
    __jcc_ast_global_var_set_static(var, is_static)

// Function-building context (ticket #148)
// Usage:
//   _AST_WITH_FN(fn) {
//       _AST_FUNCTION_SET_BODY(fn, _QUOTE("return 42;"));
//   }
// Inside the block, current_fn is set to fn so _QUOTE("return x;") casts
// to the correct return type.  The pop always runs even on early exit.
#define _AST_WITH_FN(fn)                                                    \
    for (int _jcc_fn_ctx_ = (__jcc_ast_push_fn(_VM, (fn)), 1);             \
         _jcc_fn_ctx_;                                                      \
         _jcc_fn_ctx_ = (__jcc_ast_pop_fn(_VM), 0))

#ifdef __cplusplus
}
#endif

#endif // JCC_REFLECTION_H
