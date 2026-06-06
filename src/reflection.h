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
 * - Global variable generation ($global_var*)
 * - Function-building context ($with_fn)
 *
 * ## Return-value model
 *
 * A pragma macro's returned $node_t* is **the node spliced at the call site**,
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
 *     $obj_t *fn = $function("helper", $get_type("int"));
 *     $with_fn(fn) {
 *         $function_set_body(fn, $quote("return 42;"));
 *     }
 *     $publish(fn);
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
 * $node_t *make_const_5(void) {
 *     return $int_literal(5);
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
typedef struct JCC $vm_t;
typedef struct Type $type_t;
typedef struct Node $node_t;
typedef struct Obj $obj_t;
typedef struct Member $member_t;
typedef struct EnumConstant $enum_constant_t;
typedef struct Token $token_t;

// Type kind enumeration (matches jcc.h)
typedef enum {
    tk_void = 0,
    tk_bool = 1,
    tk_char = 2,
    tk_short = 3,
    tk_int = 4,
    tk_long = 5,
    tk_float = 6,
    tk_double = 7,
    tk_ldouble = 8,
    tk_enum = 9,
    tk_ptr = 10,
    tk_func = 11,
    tk_array = 12,
    tk_vla = 13,
    tk_struct = 14,
    tk_union = 15,
} $type_kind_t;

// Node kind enumeration (subset for pragma macro use)
typedef enum {
    nk_null_expr = 0,
    nk_add = 1,
    nk_sub = 2,
    nk_mul = 3,
    nk_div = 4,
    nk_neg = 5,
    nk_mod = 6,
    nk_bitand = 7,
    nk_bitor = 8,
    nk_bitxor = 9,
    nk_shl = 10,
    nk_shr = 11,
    nk_eq = 12,
    nk_ne = 13,
    nk_lt = 14,
    nk_le = 15,
    nk_assign = 16,
    nk_cond = 17,
    nk_comma = 18,
    nk_member = 19,
    nk_addr = 20,
    nk_deref = 21,
    nk_not = 22,
    nk_bitnot = 23,
    nk_logand = 24,
    nk_logor = 25,
    nk_return = 26,
    nk_if = 27,
    nk_for = 28,
    nk_do = 29,
    nk_switch = 30,
    nk_case = 31,
    nk_block = 32,
    nk_funcall = 37,
    nk_expr_stmt = 38,
    nk_var = 40,
    nk_num = 42,
    nk_cast = 43,
    nk_macro_call = 51,
} $node_kind_t;

// ============================================================================
// Magic _VM Builtin
// ============================================================================

/*!
 * @function __jcc_get_vm
 * @abstract Builtin that returns the current parent VM context.
 * @discussion Set before calling a pragma macro and cleared after.
 *             Use the @c _VM convenience macro inside a macro body to refer
 *             to the active VM instance.
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
 * @discussion Convenience wrapper: $gensym(prefix).
 */
const char *__jcc_gensym(JCC *vm, const char *prefix);

/*!
 * @function __jcc_forward_include
 * @abstract Register a header to be prepended as an #include in serialized output.
 * @discussion Called from a macro body to declare that the generated runtime code
 *             depends on @a header. The header string must include delimiters,
 *             e.g. @c "<string.h>" or @c "\"myheader.h\"". Duplicate registrations
 *             for the same header are silently ignored.
 *             Convenience wrapper: $forward_include(header).
 * @param vm     The VM context.
 * @param header The header string including angle-brackets or quotes.
 */
void __jcc_forward_include(JCC *vm, const char *header);

/*!
 * @function __jcc_macroexpand_1
 * @abstract Lisp-style single-step macro expansion (macroexpand-1 semantics).
 * @discussion If @a node is an @c ND_MACRO_CALL node, execute the macro once
 *             and return the resulting node without splicing it into the AST
 *             or recursing into nested macro calls. If @a node is not a macro
 *             call, it is returned unchanged (identity).
 *             Convenience wrapper: $macroexpand_1(node).
 * @param vm The VM context.
 * @param node The node to (possibly) expand.
 * @return The expanded node, or @a node itself if it is not a macro call.
 */
$node_t *__jcc_macroexpand_1(JCC *vm, $node_t *node);

/*!
 * @function __jcc_macroexpand
 * @abstract Lisp-style full macro expansion.
 * @discussion Repeatedly calls @c __jcc_macroexpand_1 on the top-level node
 *             until it is no longer an @c ND_MACRO_CALL (i.e. the form is
 *             stable). Does not recurse into child nodes. Respects the VM's
 *             @c macro_recursion_limit.  Convenience wrapper: $macroexpand(node).
 * @param vm The VM context.
 * @param node The node to fully expand.
 * @return The fully expanded node, or @a node itself if it is not a macro call.
 */
$node_t *__jcc_macroexpand(JCC *vm, $node_t *node);

/*!
 * @function __jcc_ast_vararg_count
 * @abstract Return the number of variadic arguments for the active macro call.
 * @param vm The VM context.
 * @return Number of arguments after the fixed parameters.
 */
int __jcc_ast_vararg_count(JCC *vm);

/*!
 * @function __jcc_ast_vararg_at
 * @abstract Return an inline macro's variadic AST argument by zero-based index.
 * @param vm The VM context.
 * @param index Zero-based variadic argument index.
 * @return The argument node.
 * @discussion Emits a compile-time error if @a index is out of range or the
 *             active macro call is a global-generation string macro.
 */
$node_t *__jcc_ast_vararg_at(JCC *vm, int index);

/*!
 * @function __jcc_ast_varargs_as_array
 * @abstract Return an inline macro's variadic AST arguments as an array.
 * @param vm The VM context.
 * @return Borrowed array of variadic argument nodes, or NULL when the active
 *         inline macro call has no variadic arguments.
 * @discussion The returned array is read-only and valid only for the current
 *             macro call's lifetime. It shares nodes with the original macro
 *             arguments and does not consume or clone them. Emits a
 *             compile-time error if the active macro call is a
 *             global-generation string macro.
 */
$node_t **__jcc_ast_varargs_as_array(JCC *vm);

/*!
 * @function __jcc_ast_vararg_str_at
 * @abstract Return a global-generation macro's stringified variadic argument.
 * @param vm The VM context.
 * @param index Zero-based variadic argument index.
 * @return The stringified token argument.
 * @discussion Emits a compile-time error if @a index is out of range or the
 *             active macro call is an inline AST macro.
 */
const char *__jcc_ast_vararg_str_at(JCC *vm, int index);

int _AST_VARARG_COUNT();
$node_t *_AST_VARARG_AT(int index);
$node_t **_AST_VARARGS_AS_ARRAY();
const char *_AST_VARARG_STR_AT(int index);

// ============================================================================
// Generated Node Source Locations (ticket #173)
// ============================================================================

/*!
 * @function __jcc_ast_current_token
 * @abstract Return the token for the macro invocation currently being executed.
 * @param vm The VM context.
 * @return Opaque token for the active macro call site, or NULL outside macro
 *         execution.
 * @discussion Convenience wrapper: $current_token().
 */
$token_t *__jcc_ast_current_token(JCC *vm);

/*!
 * @function __jcc_ast_synthetic_token
 * @abstract Create an opaque synthetic source token for generated AST nodes.
 * @param vm The VM context.
 * @param label Short diagnostic label for the synthetic location.
 * @return Arena-allocated synthetic token, or NULL on error.
 * @discussion Use this when a generated node should diagnose against a stable
 *             generated location instead of the macro call or an input node.
 *             Convenience wrapper: $synthetic_token(label).
 */
$token_t *__jcc_ast_synthetic_token(JCC *vm, const char *label);

/*!
 * @function __jcc_ast_token_from_node
 * @abstract Return the opaque source token attached to a node.
 * @param node Node to inspect.
 * @return The node token, or NULL.
 * @discussion Convenience wrapper: $token_from_node(node).
 */
$token_t *__jcc_ast_token_from_node($node_t *node);

/*!
 * @function __jcc_ast_set_token
 * @abstract Attach an opaque source token to a node.
 * @param node Node to update.
 * @param tok Token from __jcc_ast_current_token(),
 *            __jcc_ast_synthetic_token(), or __jcc_ast_token_from_node().
 * @return node, for chaining.
 * @discussion Convenience wrapper: $set_token(node, tok).
 */
$node_t *__jcc_ast_set_token($node_t *node, $token_t *tok);

/*!
 * @function __jcc_ast_copy_location
 * @abstract Copy the source token from one node to another.
 * @param dst Generated node to update.
 * @param src Source node whose location should be reused.
 * @return dst, for chaining.
 * @discussion Convenience wrapper: $copy_location(dst, src).
 */
$node_t *__jcc_ast_copy_location($node_t *dst, $node_t *src);

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
 *             Convenience wrapper: $macro_error_at(node, ...).
 */
void __jcc_macro_error_at(JCC *vm, $node_t *node, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/*!
 * @function __jcc_macro_warning_at
 * @abstract Emit a compiler warning pointing at the source location of a node.
 * @param vm The VM context.
 * @param node A node whose tok field provides file/line/col. May be NULL.
 * @param fmt printf-style format string, followed by format arguments.
 * @discussion Emitted only when -Wjcc-macro is enabled. Non-fatal unless
 *             promoted with -Werror or -Werror=jcc-macro.
 *             Convenience wrapper: $macro_warning_at(node, ...).
 */
void __jcc_macro_warning_at(JCC *vm, $node_t *node, const char *fmt, ...)
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
 *       `->next`-linked node chain (see `$node_list` / `__jcc_node_list`).
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
 * @param ... $node_t* arguments corresponding to the splice points.
 * @return The parsed and substituted AST node, or NULL on error.
 * @discussion Template is parsed and substituted at macro-execution (compile)
 *             time; there is no runtime overhead.  Expressions and statements
 *             are auto-detected.  Capped at ~6 splice nodes due to the 8-
 *             register FFI limit; use __jcc_quote_n for more.
 *             Convenience wrapper: $quote(tmpl, ...).
 */
$node_t *__jcc_quote(JCC *vm, const char *tmpl, ...);

/*!
 * @function __jcc_quote_n
 * @abstract Array-form quasi-quote; validates the splice count and supports
 *           more than 6 splice nodes.
 * @param vm The VM context.
 * @param tmpl A C expression or statement as a string literal with $N / $@N
 *             splice points.
 * @param nodes Array of $node_t* splice arguments.
 * @param count Length of the nodes array.  If any $K in the template exceeds
 *              count, a compile-time error is emitted.
 * @return The parsed and substituted AST node, or NULL on error.
 * @discussion Convenience wrapper: $quote_n(tmpl, nodes, count).
 */
$node_t *__jcc_quote_n(JCC *vm, const char *tmpl, $node_t **nodes, int count);

/*!
 * @function __jcc_node_list
 * @abstract Build a `->next`-linked node chain from an array, returning the
 *           head.  Use the result as the argument to a `$@k` list splice.
 * @param vm    The VM context.
 * @param nodes Array of $node_t* to link together.  Linking stops at the first
 *              NULL element or at count, whichever comes first.
 * @param count Number of elements in the array.
 * @return Head of the chain, or NULL if count == 0 or nodes is NULL.
 * @discussion A single node is a valid chain of length 1.  An existing
 *             `->next` chain (e.g. `__jcc_ast_block(...)->body`) can also be
 *             passed directly as the splice argument without going through
 *             this helper.  Convenience wrapper: $node_list(nodes, count).
 */
$node_t *__jcc_node_list(JCC *vm, $node_t **nodes, int count);

// ============================================================================
// Type Lookup and Introspection
// ============================================================================

/*!
 * @function __jcc_ast_find_type
 * @abstract Look up a type by tag name (struct/union/enum).
 * @param vm The VM context.
 * @param name The tag name to look up.
 * @return The matching $type_t*, or NULL if not found.
 * @discussion Convenience wrapper: $find_type(name).
 */
$type_t *__jcc_ast_find_type(JCC *vm, const char *name);

/*!
 * @function __jcc_ast_type_exists
 * @abstract Check whether a type is currently in scope by name.
 * @param vm The VM context.
 * @param name The type name to look up.
 * @return True if the name resolves to a type, false otherwise.
 * @discussion Convenience wrapper: $type_exists(name).
 */
bool __jcc_ast_type_exists(JCC *vm, const char *name);

/*!
 * @function __jcc_ast_get_type
 * @abstract Look up a type by name, falling back to the built-in primitives.
 * @param vm The VM context.
 * @param name The type name to look up.
 * @return The matching $type_t*, or NULL if not found.
 * @discussion Convenience wrapper: $get_type(name).
 */
$type_t *__jcc_ast_get_type(JCC *vm, const char *name);

/*!
 * @function __jcc_ast_type_kind
 * @abstract Return the $type_kind_t tag of a type.
 * @param ty The type to inspect.
 * @return The type kind (tk_int, tk_struct, tk_ptr, ...).
 * @discussion Convenience wrapper: $type_kind(ty).
 */
$type_kind_t __jcc_ast_type_kind($type_t *ty);

/*!
 * @function __jcc_ast_type_size
 * @abstract Return sizeof(ty) in bytes.
 * @param ty The type to inspect.
 * @return The size in bytes.
 * @discussion Convenience wrapper: $type_size(ty).
 */
int __jcc_ast_type_size($type_t *ty);

/*!
 * @function __jcc_ast_type_align
 * @abstract Return _Alignof(ty) in bytes.
 * @param ty The type to inspect.
 * @return The alignment in bytes.
 * @discussion Convenience wrapper: $type_align(ty).
 */
int __jcc_ast_type_align($type_t *ty);

/*!
 * @function __jcc_ast_type_is_unsigned
 * @abstract Test whether an integer type is unsigned.
 * @param ty The type to inspect.
 * @return True for unsigned integer types, false otherwise.
 * @discussion Convenience wrapper: $type_is_unsigned(ty).
 */
bool __jcc_ast_type_is_unsigned($type_t *ty);

/*!
 * @function __jcc_ast_type_is_const
 * @abstract Test whether a type is const-qualified.
 * @param ty The type to inspect.
 * @return True if ty has a const qualifier, false otherwise.
 * @discussion Convenience wrapper: $type_is_const(ty).
 */
bool __jcc_ast_type_is_const($type_t *ty);

/*!
 * @function __jcc_ast_type_base
 * @abstract Return the element type of a pointer or array.
 * @param ty The pointer/array type to inspect.
 * @return The base $type_t*, or NULL if ty is not a pointer or array.
 * @discussion Convenience wrapper: $type_base(ty).
 */
$type_t *__jcc_ast_type_base($type_t *ty);

/*!
 * @function __jcc_ast_type_array_len
 * @abstract Return the fixed length of an array type.
 * @param ty The array type to inspect.
 * @return The element count for tk_array types, -1 otherwise.
 * @discussion Convenience wrapper: $type_array_len(ty).
 */
int __jcc_ast_type_array_len($type_t *ty);

/*!
 * @function __jcc_ast_type_return_type
 * @abstract Return the return type of a function type.
 * @param ty The function type to inspect.
 * @return The return $type_t*, or NULL if ty is not a function type.
 * @discussion Convenience wrapper: $type_return_type(ty).
 */
$type_t *__jcc_ast_type_return_type($type_t *ty);

/*!
 * @function __jcc_ast_type_param_count
 * @abstract Return the number of declared parameters of a function type.
 * @param ty The function type to inspect.
 * @return The parameter count for tk_func types, -1 otherwise.
 * @discussion Convenience wrapper: $type_param_count(ty).
 */
int __jcc_ast_type_param_count($type_t *ty);

/*!
 * @function __jcc_ast_type_param_at
 * @abstract Return the type of the parameter at the given index.
 * @param ty The function type to inspect.
 * @param index Zero-based parameter index.
 * @return The parameter's $type_t*, or NULL on out-of-range or non-function ty.
 * @discussion Convenience wrapper: $type_param_at(ty, index).
 */
$type_t *__jcc_ast_type_param_at($type_t *ty, int index);

/*!
 * @function __jcc_ast_type_is_variadic
 * @abstract Test whether a function type is variadic.
 * @param ty The type to inspect.
 * @return True for variadic function types, false otherwise.
 * @discussion Convenience wrapper: $type_is_variadic(ty).
 */
bool __jcc_ast_type_is_variadic($type_t *ty);

/*!
 * @function __jcc_ast_type_name
 * @abstract Return the user-visible name of a type, if any.
 * @param ty The type to inspect.
 * @return A NUL-terminated string owned by ty, or NULL for anonymous types.
 * @discussion Convenience wrapper: $type_name(ty).
 */
const char *__jcc_ast_type_name($type_t *ty);

/*!
 * @function __jcc_ast_make_pointer
 * @abstract Build a pointer-to-base type.
 * @param vm The VM context.
 * @param base The pointed-to type.
 * @return A $type_t* representing "base *", or NULL on error.
 * @discussion Convenience wrapper: $make_pointer(base).
 */
$type_t *__jcc_ast_make_pointer(JCC *vm, $type_t *base);

/*!
 * @function __jcc_ast_make_array
 * @abstract Build a fixed-length array type.
 * @param vm The VM context.
 * @param base The element type.
 * @param length The element count.
 * @return A $type_t* representing "base[length]", or NULL on error.
 * @discussion Convenience wrapper: $make_array(base, length).
 */
$type_t *__jcc_ast_make_array(JCC *vm, $type_t *base, int length);

// Ticket #171: qualified type constructors
/*!
 * @function __jcc_ast_make_const
 * @abstract Return a const-qualified copy of ty.
 * @param vm The VM context.
 * @param ty The type to qualify.
 * @return A const-qualified $type_t*, or NULL on error.
 * @discussion Convenience wrapper: $make_const(ty).
 */
$type_t *__jcc_ast_make_const(JCC *vm, $type_t *ty);
/*!
 * @function __jcc_ast_make_volatile
 * @abstract Return a volatile-qualified copy of ty.
 * @param vm The VM context.
 * @param ty The type to qualify.
 * @return A volatile-qualified $type_t*, or NULL on error.
 * @discussion Convenience wrapper: $make_volatile(ty).
 */
$type_t *__jcc_ast_make_volatile(JCC *vm, $type_t *ty);

// ============================================================================
// Enum Reflection
// ============================================================================

/*!
 * @function __jcc_ast_enum_count
 * @abstract Return the number of constants in an enum type.
 * @param vm The VM context.
 * @param enum_type The enum type to inspect.
 * @return The constant count, or -1 if enum_type is not an enum.
 * @discussion Convenience wrapper: $enum_count(ty).
 */
int __jcc_ast_enum_count(JCC *vm, $type_t *enum_type);

/*!
 * @function __jcc_ast_enum_at
 * @abstract Return the enum constant at a given index.
 * @param vm The VM context.
 * @param enum_type The enum type to inspect.
 * @param index Zero-based index.
 * @return The $enum_constant_t*, or NULL on out-of-range or non-enum.
 * @discussion Convenience wrapper: $enum_at(ty, index).
 */
$enum_constant_t *__jcc_ast_enum_at(JCC *vm, $type_t *enum_type, int index);

/*!
 * @function __jcc_ast_enum_find
 * @abstract Look up an enum constant by name.
 * @param vm The VM context.
 * @param enum_type The enum type to search.
 * @param name The constant name to look up.
 * @return The matching $enum_constant_t*, or NULL if not found.
 * @discussion Convenience wrapper: $enum_find(ty, name).
 */
$enum_constant_t *__jcc_ast_enum_find(JCC *vm, $type_t *enum_type,
                                    const char *name);

/*!
 * @function __jcc_ast_enum_constant_name
 * @abstract Return the name of an enum constant.
 * @param ec The enum constant.
 * @return A NUL-terminated string owned by ec.
 * @discussion Convenience wrapper: $enum_constant_name(ec).
 */
const char *__jcc_ast_enum_constant_name($enum_constant_t *ec);

/*!
 * @function __jcc_ast_enum_constant_value
 * @abstract Return the integer value of an enum constant.
 * @param ec The enum constant.
 * @return The constant's integer value.
 * @discussion Convenience wrapper: $enum_constant_value(ec).
 */
int __jcc_ast_enum_constant_value($enum_constant_t *ec);

/*!
 * @function __jcc_ast_enum_name
 * @abstract Return the tag name of an enum type.
 * @param e The enum type to inspect.
 * @return A NUL-terminated string owned by e.
 * @discussion Convenience wrapper: $enum_name(ty).
 */
const char *__jcc_ast_enum_name($type_t *e);

/*!
 * @function __jcc_ast_enum_value_count
 * @abstract Return the number of values in an enum type.
 * @param e The enum type to inspect.
 * @return The constant count, or -1 if e is not an enum.
 * @discussion Convenience wrapper: $enum_value_count(ty).
 */
int __jcc_ast_enum_value_count($type_t *e);

/*!
 * @function __jcc_ast_enum_value_name
 * @abstract Return the name of the enum constant at the given index.
 * @param e The enum type to inspect.
 * @param index Zero-based index.
 * @return A NUL-terminated string, or NULL on out-of-range.
 * @discussion Convenience wrapper: $enum_value_name(ty, index).
 */
const char *__jcc_ast_enum_value_name($type_t *e, int index);

/*!
 * @function __jcc_ast_enum_value
 * @abstract Return the integer value of the enum constant at the given index.
 * @param e The enum type to inspect.
 * @param index Zero-based index.
 * @return The constant's integer value, or -1 on out-of-range.
 * @discussion Convenience wrapper: $enum_value(ty, index).
 */
int __jcc_ast_enum_value($type_t *e, int index);

// ============================================================================
// Struct/Union Member Introspection
// ============================================================================

/*!
 * @function __jcc_ast_struct_member_count
 * @abstract Return the number of members of a struct or union type.
 * @param vm The VM context.
 * @param struct_type The struct or union type to inspect.
 * @return The member count, or -1 if struct_type is not a struct/union.
 * @discussion Convenience wrapper: $struct_member_count(ty).
 */
int __jcc_ast_struct_member_count(JCC *vm, $type_t *struct_type);

/*!
 * @function __jcc_ast_struct_member_at
 * @abstract Return the member at the given index.
 * @param vm The VM context.
 * @param struct_type The struct or union type to inspect.
 * @param index Zero-based member index.
 * @return The $member_t*, or NULL on out-of-range or non-aggregate.
 * @discussion Convenience wrapper: $struct_member_at(ty, index).
 */
$member_t *__jcc_ast_struct_member_at(JCC *vm, $type_t *struct_type,
                                        int index);

/*!
 * @function __jcc_ast_struct_member_find
 * @abstract Look up a struct or union member by name.
 * @param vm The VM context.
 * @param struct_type The struct or union type to search.
 * @param name The member name to look up.
 * @return The matching $member_t*, or NULL if not found.
 * @discussion Convenience wrapper: $struct_member_find(ty, name).
 */
$member_t *__jcc_ast_struct_member_find(JCC *vm, $type_t *struct_type,
                                        const char *name);

/*!
 * @function __jcc_ast_member_name
 * @abstract Return the name of a struct/union member.
 * @param m The member to inspect.
 * @return A NUL-terminated string owned by m.
 * @discussion Convenience wrapper: $member_name(m).
 */
const char *__jcc_ast_member_name($member_t *m);

/*!
 * @function __jcc_ast_member_type
 * @abstract Return the type of a struct/union member.
 * @param m The member to inspect.
 * @return The member's $type_t*.
 * @discussion Convenience wrapper: $member_type(m).
 */
$type_t *__jcc_ast_member_type($member_t *m);

/*!
 * @function __jcc_ast_member_offset
 * @abstract Return the byte offset of a struct/union member.
 * @param m The member to inspect.
 * @return The offset in bytes.
 * @discussion Convenience wrapper: $member_offset(m).
 */
int __jcc_ast_member_offset($member_t *m);

/*!
 * @function __jcc_ast_member_is_bitfield
 * @abstract Test whether a member is a bitfield.
 * @param m The member to inspect.
 * @return True if the member is a bitfield, false otherwise.
 * @discussion Convenience wrapper: $member_is_bitfield(m).
 */
bool __jcc_ast_member_is_bitfield($member_t *m);

/*!
 * @function __jcc_ast_member_bitfield_width
 * @abstract Return the bit width of a bitfield member.
 * @param m The member to inspect.
 * @return The bit width for bitfield members, 0 otherwise.
 * @discussion Convenience wrapper: $member_bitfield_width(m).
 */
int __jcc_ast_member_bitfield_width($member_t *m);

// ============================================================================
// Global Symbol Introspection
// ============================================================================

/*!
 * @function __jcc_ast_find_global
 * @abstract Look up a global symbol by name.
 * @param vm The VM context.
 * @param name The global name to look up.
 * @return The matching $obj_t*, or NULL if not found.
 * @discussion Convenience wrapper: $find_global(name).
 */
$obj_t *__jcc_ast_find_global(JCC *vm, const char *name);

/*!
 * @function __jcc_ast_global_count
 * @abstract Return the total number of global symbols.
 * @param vm The VM context.
 * @return The count of globals.
 * @discussion Convenience wrapper: $global_count().
 */
int __jcc_ast_global_count(JCC *vm);

/*!
 * @function __jcc_ast_global_at
 * @abstract Return the global symbol at the given index.
 * @param vm The VM context.
 * @param index Zero-based global index.
 * @return The $obj_t* at the given slot, or NULL on out-of-range.
 * @discussion Convenience wrapper: $global_at(index).
 */
$obj_t *__jcc_ast_global_at(JCC *vm, int index);

/*!
 * @function __jcc_ast_obj_name
 * @abstract Return the name of a global object.
 * @param obj The object to inspect.
 * @return A NUL-terminated string owned by obj.
 * @discussion Convenience wrapper: $obj_name(obj).
 */
const char *__jcc_ast_obj_name($obj_t *obj);

/*!
 * @function __jcc_ast_obj_type
 * @abstract Return the type of a global object.
 * @param obj The object to inspect.
 * @return The object's $type_t*.
 * @discussion Convenience wrapper: $obj_type(obj).
 */
$type_t *__jcc_ast_obj_type($obj_t *obj);

/*!
 * @function __jcc_ast_obj_is_function
 * @abstract Test whether a global object is a function.
 * @param obj The object to inspect.
 * @return True for functions, false for variables.
 * @discussion Convenience wrapper: $obj_is_function(obj).
 */
bool __jcc_ast_obj_is_function($obj_t *obj);

/*!
 * @function __jcc_ast_obj_is_definition
 * @abstract Test whether a global object has a definition.
 * @param obj The object to inspect.
 * @return True for defined objects, false for declarations only.
 * @discussion Convenience wrapper: $obj_is_definition(obj).
 */
bool __jcc_ast_obj_is_definition($obj_t *obj);

/*!
 * @function __jcc_ast_obj_is_static
 * @abstract Test whether a global object has internal (static) linkage.
 * @param obj The object to inspect.
 * @return True for static linkage, false for external.
 * @discussion Convenience wrapper: $obj_is_static(obj).
 */
bool __jcc_ast_obj_is_static($obj_t *obj);

// ============================================================================
// AST Node Construction - Literals
// ============================================================================

/*!
 * @function __jcc_ast_int_literal
 * @abstract Build an integer literal AST node.
 * @param vm The VM context.
 * @param value The integer value.
 * @return An nk_num node for value.
 * @discussion Convenience wrapper: $int_literal(value).
 */
$node_t *__jcc_ast_int_literal(JCC *vm, int64_t value);

/*!
 * @function __jcc_ast_float_literal
 * @abstract Build a floating-point literal AST node.
 * @param vm The VM context.
 * @param value The floating-point value.
 * @return An nk_num node for value.
 * @discussion Convenience wrapper: $float_literal(value).
 */
$node_t *__jcc_ast_float_literal(JCC *vm, double value);

/*!
 * @function __jcc_ast_string_literal
 * @abstract Build a string literal AST node.
 * @param vm The VM context.
 * @param str A NUL-terminated string.
 * @return An nk_num string-literal node.
 * @discussion Convenience wrapper: $string_literal(str).
 */
$node_t *__jcc_ast_string_literal(JCC *vm, const char *str);

/*!
 * @function __jcc_ast_var_ref
 * @abstract Build a variable reference AST node.
 * @param vm The VM context.
 * @param name The variable name.
 * @return An nk_var node referencing name.
 * @discussion Convenience wrapper: $var_ref(name).
 */
$node_t *__jcc_ast_var_ref(JCC *vm, const char *name);

/*!
 * @function __jcc_ast_param_ref
 * @abstract Build a reference to a function parameter by name.
 * @param vm The VM context.
 * @param fn The function object whose parameter is being referenced.
 * @param name The parameter name.
 * @return An nk_var node for the named parameter.
 * @discussion Use this when building function bodies to reference parameters
 *             by name.  Convenience wrapper: $param_ref(fn, name).
 */
$node_t *__jcc_ast_param_ref(JCC *vm, $obj_t *fn, const char *name);

// ============================================================================
// AST Node Construction - Expressions
// ============================================================================

/*!
 * @function __jcc_ast_binary
 * @abstract Build a binary operation AST node.
 * @param vm The VM context.
 * @param op The operator kind (nk_add, nk_sub, ...).
 * @param left The left-hand operand.
 * @param right The right-hand operand.
 * @return The binary expression node.
 * @discussion Convenience wrapper: $binary(op, left, right).
 */
$node_t *__jcc_ast_binary(JCC *vm, $node_kind_t op, $node_t *left,
                            $node_t *right);

/*!
 * @function __jcc_ast_unary
 * @abstract Build a unary operation AST node.
 * @param vm The VM context.
 * @param op The operator kind (nk_neg, nk_deref, ...).
 * @param operand The operand expression.
 * @return The unary expression node.
 * @discussion Convenience wrapper: $unary(op, operand).
 */
$node_t *__jcc_ast_unary(JCC *vm, $node_kind_t op, $node_t *operand);

/*!
 * @function __jcc_ast_cast
 * @abstract Build a type cast AST node.
 * @param vm The VM context.
 * @param expr The expression to cast.
 * @param target_type The type to cast to.
 * @return An nk_cast node.
 * @discussion Convenience wrapper: $cast(expr, target_type).
 */
$node_t *__jcc_ast_cast(JCC *vm, $node_t *expr, $type_t *target_type);

// Ticket #171: new expression builders

/*!
 * @function __jcc_ast_cond
 * @abstract Build a ternary conditional expression node (cond ? then : else).
 * @param vm The VM context.
 * @param cond The condition expression.
 * @param then_expr The expression evaluated when cond is non-zero.
 * @param else_expr The expression evaluated when cond is zero.
 * @return An nk_cond node.
 * @discussion Convenience wrapper: $cond(cond, then_expr, else_expr).
 */
$node_t *__jcc_ast_cond(JCC *vm, $node_t *cond, $node_t *then_expr,
                          $node_t *else_expr);

/*!
 * @function __jcc_ast_null
 * @abstract Build a typed null pointer node: (void *)0.
 * @param vm The VM context.
 * @return An nk_num node representing a typed NULL.
 * @discussion Convenience wrapper: $null().
 */
$node_t *__jcc_ast_null(JCC *vm);

/*!
 * @function __jcc_ast_sizeof_type
 * @abstract Emit sizeof(ty) as a compile-time integer literal.
 * @param vm The VM context.
 * @param ty The type to measure.
 * @return An nk_num node holding sizeof(ty).
 * @discussion Convenience wrapper: $sizeof_type(ty).
 */
$node_t *__jcc_ast_sizeof_type(JCC *vm, $type_t *ty);

/*!
 * @function __jcc_ast_alignof_type
 * @abstract Emit _Alignof(ty) as a compile-time integer literal.
 * @param vm The VM context.
 * @param ty The type to measure.
 * @return An nk_num node holding _Alignof(ty).
 * @discussion Convenience wrapper: $alignof_type(ty).
 */
$node_t *__jcc_ast_alignof_type(JCC *vm, $type_t *ty);

/*!
 * @function __jcc_ast_sizeof_expr
 * @abstract Emit sizeof(expr): resolve the expression's type then its size.
 * @param vm The VM context.
 * @param expr The expression whose type to measure.
 * @return An nk_num node holding sizeof(expr).
 * @discussion Convenience wrapper: $sizeof_expr(expr).
 */
$node_t *__jcc_ast_sizeof_expr(JCC *vm, $node_t *expr);

/*!
 * @function __jcc_ast_subscript
 * @abstract Build an array subscript node: arr[idx], desugared as *(arr+idx).
 * @param vm The VM context.
 * @param arr The array (or pointer) expression.
 * @param idx The index expression.
 * @return An nk_add / nk_deref node pair representing the subscript.
 * @discussion Convenience wrapper: $subscript(arr, idx).
 */
$node_t *__jcc_ast_subscript(JCC *vm, $node_t *arr, $node_t *idx);

/*!
 * @function __jcc_ast_comma
 * @abstract Build a comma expression: evaluate lhs, yield rhs.
 * @param vm The VM context.
 * @param lhs The expression evaluated for side effects.
 * @param rhs The expression whose value is the result.
 * @return An nk_comma node.
 * @discussion Convenience wrapper: $comma(lhs, rhs).
 */
$node_t *__jcc_ast_comma(JCC *vm, $node_t *lhs, $node_t *rhs);

// ============================================================================
// AST Node Construction - Statements
// ============================================================================

/*!
 * @function __jcc_ast_return
 * @abstract Build a return statement node.
 * @param vm The VM context.
 * @param expr The value to return (may be NULL for `return;` in void functions).
 * @return An nk_return node.
 * @discussion Convenience wrapper: $return(expr).
 */
$node_t *__jcc_ast_return(JCC *vm, $node_t *expr);

/*!
 * @function __jcc_ast_block
 * @abstract Build a block (compound statement) node.
 * @param vm The VM context.
 * @param stmts Array of statement nodes, or NULL if count is 0.
 * @param count Number of statements in the array.
 * @return An nk_block node.
 * @discussion Convenience wrapper: $block(stmts, count).
 */
$node_t *__jcc_ast_block(JCC *vm, $node_t **stmts, int count);

/*!
 * @function __jcc_ast_if
 * @abstract Build an if statement node.
 * @param vm The VM context.
 * @param cond The condition expression.
 * @param then_body The body executed when cond is non-zero.
 * @param else_body The body executed when cond is zero, or NULL.
 * @return An nk_if node.
 * @discussion Convenience wrapper: $if(cond, then_body, else_body).
 */
$node_t *__jcc_ast_if(JCC *vm, $node_t *cond, $node_t *then_body,
                        $node_t *else_body);

/*!
 * @function __jcc_ast_switch
 * @abstract Build a switch statement node.
 * @param vm The VM context.
 * @param cond The expression to switch on.
 * @return An nk_switch node.  Use __jcc_ast_switch_add_case and
 *         __jcc_ast_switch_set_default to populate it.
 * @discussion Convenience wrapper: $switch(cond).
 */
$node_t *__jcc_ast_switch(JCC *vm, $node_t *cond);

/*!
 * @function __jcc_ast_switch_add_case
 * @abstract Append a case to a switch statement.
 * @param vm The VM context.
 * @param switch_node The switch node returned by __jcc_ast_switch.
 * @param value The case value expression.
 * @param body The body statement for this case.
 * @discussion Convenience wrapper: $switch_add_case(sw, value, body).
 */
void __jcc_ast_switch_add_case(JCC *vm, $node_t *switch_node,
                                $node_t *value, $node_t *body);

/*!
 * @function __jcc_ast_switch_set_default
 * @abstract Set the default case for a switch statement.
 * @param vm The VM context.
 * @param switch_node The switch node returned by __jcc_ast_switch.
 * @param body The default-case body statement.
 * @discussion Convenience wrapper: $switch_set_default(sw, body).
 */
void __jcc_ast_switch_set_default(JCC *vm, $node_t *switch_node,
                                    $node_t *body);

/*!
 * @function __jcc_ast_expr_stmt
 * @abstract Build an expression statement node.
 * @param vm The VM context.
 * @param expr The expression to evaluate for side effects.
 * @return An nk_expr_stmt node.
 * @discussion Convenience wrapper: $expr_stmt(expr).
 */
$node_t *__jcc_ast_expr_stmt(JCC *vm, $node_t *expr);

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
 * @return A nk_var node referencing the new local, or NULL if called
 *         outside a function body or on invalid arguments.
 * @note  The variable is injected into the current function's locals list
 *        and will receive a stack offset when the function is compiled.
 *        For temporaries that must not capture user names, prefer
 *        __jcc_ast_local_var_unique().
 * @discussion Convenience wrapper: $local_var(name, ty).
 */
$node_t *__jcc_ast_local_var(JCC *vm, const char *name, $type_t *ty);

/*!
 * @function __jcc_ast_local_var_unique
 * @abstract Declare a hygienic (gensym'd) local variable in the current
 *           function scope and return a variable-reference node for it.
 * @param vm The VM context.
 * @param ty The variable type.
 * @return A nk_var node referencing the new local, or NULL on error.
 * @note  The generated name begins with ".L.." and is therefore not
 *        expressible as a user identifier — guaranteed no name capture.
 *        This is the safe default for macro temporaries.
 * @discussion Convenience wrapper: $local_var_unique(ty).
 */
$node_t *__jcc_ast_local_var_unique(JCC *vm, $type_t *ty);

/*!
 * @function __jcc_ast_assign
 * @abstract Build an assignment node (target = value).
 * @param vm The VM context.
 * @param target The lvalue expression being assigned to.
 * @param value The rvalue expression to assign.
 * @return An nk_assign node, or NULL on error.
 * @discussion Convenience wrapper: $assign(target, value).
 */
$node_t *__jcc_ast_assign(JCC *vm, $node_t *target, $node_t *value);

/*!
 * @function __jcc_ast_member
 * @abstract Create a struct/union member access node (obj.name).
 * @param vm The VM context.
 * @param obj An expression node whose type must be a struct or union.
 * @param name The member name as a NUL-terminated string.
 * @return A nk_member node, or NULL if the member is not found or
 *         obj is not a struct/union type.
 * @note The callee is responsible for dereferencing pointers first;
 *       pass the struct value directly (use __jcc_ast_unary(ND_DEREF,…)
 *       for pointer-to-struct access).
 * @discussion Convenience wrapper: $member(obj, name).
 */
$node_t *__jcc_ast_member(JCC *vm, $node_t *obj, const char *name);

/*!
 * @function __jcc_ast_funcall
 * @abstract Create a function call node.
 * @param vm The VM context.
 * @param callee An expression node that evaluates to a function (or function
 *               pointer). The callee's lhs field holds this expression.
 * @param args Array of argument nodes (may be NULL if n == 0).
 * @param n Number of arguments.
 * @return A nk_funcall node, or NULL on error.
 * @discussion Convenience wrapper: $funcall(callee, args, n).
 */
$node_t *__jcc_ast_funcall(JCC *vm, $node_t *callee, $node_t **args, int n);

/*!
 * @function __jcc_ast_while
 * @abstract Create a while loop node.
 * @param vm The VM context.
 * @param cond The loop condition expression.
 * @param body The loop body statement.
 * @return A nk_for node (JCC represents while as for with no init/inc),
 *         or NULL on error.
 * @discussion Convenience wrapper: $while(cond, body).
 */
$node_t *__jcc_ast_while(JCC *vm, $node_t *cond, $node_t *body);

/*!
 * @function __jcc_ast_for
 * @abstract Create a for loop node.
 * @param vm The VM context.
 * @param init Initialiser expression/statement (may be NULL).
 * @param cond Loop condition (may be NULL for infinite loop).
 * @param inc Increment expression (may be NULL).
 * @param body Loop body.
 * @return A nk_for node, or NULL on error.
 * @discussion Convenience wrapper: $for(init, cond, inc, body).
 */
$node_t *__jcc_ast_for(JCC *vm, $node_t *init, $node_t *cond,
                       $node_t *inc, $node_t *body);

/*!
 * @function __jcc_ast_do_while
 * @abstract Create a do-while loop node.
 * @param vm The VM context.
 * @param body The loop body.
 * @param cond The loop condition (tested after each iteration).
 * @return A nk_do node, or NULL on error.
 * @discussion Convenience wrapper: $do_while(body, cond).
 */
$node_t *__jcc_ast_do_while(JCC *vm, $node_t *body, $node_t *cond);

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
 *             Convenience wrapper: $function(name, return_type).
 */
$obj_t *__jcc_ast_function(JCC *vm, const char *name,
                            $type_t *return_type);

/*!
 * @function __jcc_ast_publish
 * @abstract Make a generated object visible at the current source position.
 * @param vm The VM context.
 * @param obj A function or global variable object created by the AST builders.
 * @param tok Optional representative token for diagnostics, or NULL.
 * @return A no-op $node_t on success, or NULL on invalid arguments.
 * @discussion Top-level explicit macro calls run at their source position.
 *             Call this after creating a function or global variable when
 *             later macro-generated code at the same parse point should be
 *             able to reference it without a handwritten declaration.
 *             Convenience wrapper: $publish(obj) / $publish_at(obj, tok).
 */
$node_t *__jcc_ast_publish(JCC *vm, $obj_t *obj, $token_t *tok);

/*!
 * @function __jcc_ast_publish_type
 * @abstract Accept a generated type declaration as already published.
 * @param vm The VM context.
 * @param ty A type created by $make_struct, $make_union,
 *           $make_enum, or $make_typedef.
 * @param tok Optional representative token for diagnostics, or NULL.
 * @return A no-op $node_t on success, or NULL on invalid arguments.
 * @discussion Generated type builders self-register in tag or typedef scope.
 *             This function lets $publish(type) be used uniformly; there
 *             is no separate convenience macro for this entry point.
 */
$node_t *__jcc_ast_publish_type(JCC *vm, $type_t *ty, $token_t *tok);

/*!
 * @function __jcc_ast_forward_declare
 * @abstract Deprecated alias for publishing a generated function.
 * @param vm The VM context.
 * @param fn A function object created with __jcc_ast_function().
 * @return A no-op $node_t on success, or NULL on invalid arguments.
 * @discussion Use __jcc_ast_publish(vm, fn, NULL) or $publish(fn).
 *             Convenience wrapper: $forward_declare(fn).
 */
$node_t *__jcc_ast_forward_declare(JCC *vm, $obj_t *fn);

/*!
 * @function __jcc_ast_function_add_param
 * @abstract Add a parameter to a function.
 * @param vm The VM context.
 * @param fn The function object.
 * @param name The parameter name.
 * @param type The parameter type.
 * @discussion Parameters are added in order. Call this multiple times
 *             for multiple parameters.  Convenience wrapper:
 *             $function_add_param(fn, name, type).
 */
void __jcc_ast_function_add_param(JCC *vm, $obj_t *fn, const char *name,
                                $type_t *type);

/*!
 * @function __jcc_ast_function_set_body
 * @abstract Set the body of a function.
 * @param vm The VM context.
 * @param fn The function object.
 * @param body The function body (a statement or block node).
 * @discussion If body is not already a nk_block, it will be wrapped in one.
 *             Convenience wrapper: $function_set_body(fn, body).
 */
void __jcc_ast_function_set_body(JCC *vm, $obj_t *fn, $node_t *body);

/*!
 * @function __jcc_ast_function_set_static
 * @abstract Set whether a function has static linkage.
 * @param fn The function object.
 * @param is_static True for static linkage, false for external.
 * @discussion Convenience wrapper: $function_set_static(fn, is_static).
 */
void __jcc_ast_function_set_static($obj_t *fn, bool is_static);

/*!
 * @function __jcc_ast_function_set_inline
 * @abstract Set whether a function is inline.
 * @param fn The function object.
 * @param is_inline True for inline, false otherwise.
 * @discussion Convenience wrapper: $function_set_inline(fn, is_inline).
 */
void __jcc_ast_function_set_inline($obj_t *fn, bool is_inline);

/*!
 * @function __jcc_ast_function_set_variadic
 * @abstract Set whether a function is variadic.
 * @param fn The function object.
 * @param is_variadic True for variadic, false otherwise.
 * @discussion Convenience wrapper: $function_set_variadic(fn, is_variadic).
 */
void __jcc_ast_function_set_variadic($obj_t *fn, bool is_variadic);

// Ticket #171: function prototype (forward declaration only, no body)
/*!
 * @function __jcc_ast_function_prototype
 * @abstract Create a function forward declaration (prototype) without a body.
 * @param vm The VM context.
 * @param name The function name.
 * @param return_type The return type.
 * @return The declaration Obj*, or NULL on error.
 * @discussion Use $function_add_param to add parameters and
 *             $publish to expose it in scope. A subsequent
 *             $function call with the same name will reuse this Obj and
 *             fill in the body.  Convenience wrapper: $function_prototype(name, return_type).
 */
$obj_t *__jcc_ast_function_prototype(JCC *vm, const char *name,
                                    $type_t *return_type);

// Ticket #171: struct/union/enum/typedef type builders

/*!
 * @function __jcc_ast_make_struct
 * @abstract Create and expose a new named struct type.
 * @param vm The VM context.
 * @param name The struct tag name.
 * @return The new struct $type_t*, or NULL on error.
 * @discussion Use $struct_add_field to add fields after creation.
 *             The type is immediately visible via $find_type(name).
 *             Convenience wrapper: $make_struct(name).
 */
$type_t *__jcc_ast_make_struct(JCC *vm, const char *name);

/*!
 * @function __jcc_ast_make_union
 * @abstract Create and expose a new named union type.
 * @param vm The VM context.
 * @param name The union tag name.
 * @return The new union $type_t*, or NULL on error.
 * @discussion Convenience wrapper: $make_union(name).
 */
$type_t *__jcc_ast_make_union(JCC *vm, const char *name);

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
 *             Convenience wrapper: $struct_add_field(ty, name, field_type).
 */
$type_t *__jcc_ast_struct_add_field(JCC *vm, $type_t *ty, const char *name,
                                   $type_t *field_type);

/*!
 * @function __jcc_ast_make_enum
 * @abstract Create and expose a new named enum type.
 * @param vm The VM context.
 * @param name The enum tag name.
 * @return The new enum $type_t*, or NULL on error.
 * @discussion Use $enum_add_constant to add constants after creation.
 *             Convenience wrapper: $make_enum(name).
 */
$type_t *__jcc_ast_make_enum(JCC *vm, const char *name);

/*!
 * @function __jcc_ast_enum_add_constant
 * @abstract Add a named constant to an enum type and expose it in scope.
 * @param vm The VM context.
 * @param ty The enum type.
 * @param name The constant name.
 * @param value The constant integer value.
 * @discussion The constant is appended to ty->enum_constants and also pushed
 *             into the current scope so it is usable as an integer constant in
 *             subsequently compiled code.  Convenience wrapper:
 *             $enum_add_constant(ty, name, value).
 */
void __jcc_ast_enum_add_constant(JCC *vm, $type_t *ty, const char *name,
                                  int value);

/*!
 * @function __jcc_ast_make_typedef
 * @abstract Register a typedef alias for a type and expose it in scope.
 * @param vm The VM context.
 * @param name The typedef name.
 * @param underlying The aliased type.
 * @return The aliased type after registration, or NULL on invalid arguments.
 * @discussion After this call, $find_type(name) resolves to underlying and
 *             subsequently compiled code can use name as a type name.
 *             Convenience wrapper: $make_typedef(name, underlying).
 */
$type_t *__jcc_ast_make_typedef(JCC *vm, const char *name, $type_t *underlying);

// ============================================================================
// Global Variable Generation (ticket #152)
// ============================================================================

/*!
 * @function __jcc_ast_global_var
 * @abstract Create a new named global variable definition.
 * @param vm   The VM context.
 * @param name The variable name (must be unique among globals).
 * @param ty   The variable type.  Use $make_array(char_ty, len) for byte
 *             arrays so that the size matches the init_data length.
 * @return The new Obj*, or NULL on error.
 * @discussion The variable is registered in vm->compiler.globals.  For inline
 *             macros the capture loop will stash it in macro_globals and emit
 *             an extern declaration into every input file's token stream so
 *             the parser can resolve references.
 *             Convenience wrapper: $global_var(name, ty).
 */
$obj_t *__jcc_ast_global_var(JCC *vm, const char *name, $type_t *ty);

/*!
 * @function __jcc_ast_global_var_set_init_data
 * @abstract Set the initial data for a generated global variable.
 * @param vm   The VM context.
 * @param var  The global variable object.
 * @param data Pointer to the raw byte data.
 * @param len  Number of bytes to copy.  Must equal var->ty->size.
 * @discussion Convenience wrapper: $global_var_set_init_data(var, data, len).
 */
void __jcc_ast_global_var_set_init_data(JCC *vm, $obj_t *var,
                                        const char *data, int len);

/*!
 * @function __jcc_ast_global_var_set_static
 * @abstract Set the static (internal linkage) flag on a generated global.
 * @param var       The global variable object.
 * @param is_static True for internal linkage (file-scope static).
 * @discussion Convenience wrapper: $global_var_set_static(var, is_static).
 */
void __jcc_ast_global_var_set_static($obj_t *var, bool is_static);

// ============================================================================
// Function-building context (ticket #148)
// ============================================================================

/*!
 * @function __jcc_ast_push_fn
 * @abstract Establish fn as the "function currently being built" so that
 *           $quote("return x;") applies the correct implicit return-type cast.
 * @param vm The VM context.
 * @param fn The generated function whose return type should be used.
 * @discussion Call __jcc_ast_pop_fn (or use the $with_fn macro) to
 *             restore the previous context.  execute_pragma_macro always
 *             restores current_fn after the macro returns, so unmatched pushes
 *             cannot leak into the main parse/codegen pass.  There is no
 *             1:1 convenience macro; use the @c $with_fn(fn) { ... } block
 *             helper to bracket push/pop pairs in a macro body.
 */
void __jcc_ast_push_fn(JCC *vm, $obj_t *fn);

/*!
 * @function __jcc_ast_pop_fn
 * @abstract Restore the function context saved by the most recent push.
 * @param vm The VM context.
 * @discussion Inverse of __jcc_ast_push_fn; typically used through the
 *             @c $with_fn(fn) { ... } block helper, which performs the
 *             matching pop even on early exit.
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
 *             Convenience wrapper: $dump_tree(node).
 */
void __jcc_dump_tree(JCC *vm, $node_t *node);

/*!
 * @function __jcc_dump_tree_to_string
 * @abstract Render the tree representation to a heap-allocated string.
 * @param vm The VM context.
 * @param node The root node.
 * @return An arena-allocated NUL-terminated string, or NULL on error.
 * @discussion Convenience wrapper: $dump_tree_to_string(node).
 */
const char *__jcc_dump_tree_to_string(JCC *vm, $node_t *node);

/*!
 * @function __jcc_dump_ast_gen
 * @abstract Print __jcc_ast_*() builder calls that would reconstruct the node.
 * @param vm The VM context.
 * @param node The root node to emit builder calls for.
 * @discussion Covers all node kinds for which reflect.c has a builder.
 *             Unsupported kinds are emitted as C comments.
 *             Convenience wrapper: $dump_ast_gen(node).
 */
void __jcc_dump_ast_gen(JCC *vm, $node_t *node);

/*!
 * @function __jcc_dump_ast_gen_to_string
 * @abstract Render the __jcc_ast_*() builder call sequence to a string.
 * @param vm The VM context.
 * @param node The root node.
 * @return An arena-allocated NUL-terminated string, or NULL on error.
 * @discussion Convenience wrapper: $dump_ast_gen_to_string(node).
 */
const char *__jcc_dump_ast_gen_to_string(JCC *vm, $node_t *node);

// ============================================================================
// Convenience Macros (automatically pass _VM)
// ============================================================================

// Quasi-quoting helpers (ticket #1, #172)
#define $quote(tmpl, ...) __jcc_quote(_VM, tmpl, ##__VA_ARGS__)
#define $quote_n(tmpl, nodes, count) __jcc_quote_n(_VM, tmpl, nodes, count)
// Build a ->next-linked chain from a compound-literal array for $@k splices:
//   $node_list(($node_t*[]){ a, b, c }, 3)
#define $node_list(nodes, count) __jcc_node_list(_VM, nodes, count)

// Diagnostic helpers (ticket #78) — note: variadic macros require C99+
#define $macro_error_at(node, ...) __jcc_macro_error_at(_VM, node, __VA_ARGS__)
#define $macro_warning_at(node, ...) __jcc_macro_warning_at(_VM, node, __VA_ARGS__)

// AST dump helpers (ticket #58)
#define $dump_tree(node) __jcc_dump_tree(_VM, node)
#define $dump_tree_to_string(node) __jcc_dump_tree_to_string(_VM, node)
#define $dump_ast_gen(node) __jcc_dump_ast_gen(_VM, node)
#define $dump_ast_gen_to_string(node) __jcc_dump_ast_gen_to_string(_VM, node)
#define $gensym(prefix) __jcc_gensym(_VM, prefix)
#define $forward_include(header) __jcc_forward_include(_VM, header)
#define $macroexpand_1(node) __jcc_macroexpand_1(_VM, node)
#define $macroexpand(node) __jcc_macroexpand(_VM, node)
#define _AST_VARARG_COUNT() __jcc_ast_vararg_count(_VM)
#define _AST_VARARG_AT(i) __jcc_ast_vararg_at(_VM, i)
#define _AST_VARARGS_AS_ARRAY() __jcc_ast_varargs_as_array(_VM)
#define _AST_VARARG_STR_AT(i) __jcc_ast_vararg_str_at(_VM, i)

#define $current_token() __jcc_ast_current_token(_VM)
#define $synthetic_token(label) __jcc_ast_synthetic_token(_VM, label)
#define $token_from_node(node) __jcc_ast_token_from_node(node)
#define $set_token(node, tok) __jcc_ast_set_token(node, tok)
#define $copy_location(dst, src) __jcc_ast_copy_location(dst, src)

#define $find_type(name) __jcc_ast_find_type(_VM, name)
#define $type_exists(name) __jcc_ast_type_exists(_VM, name)
#define $get_type(name) __jcc_ast_get_type(_VM, name)

// Type introspection — no _VM needed
#define $type_kind(ty)          __jcc_ast_type_kind(ty)
#define $type_size(ty)          __jcc_ast_type_size(ty)
#define $type_align(ty)         __jcc_ast_type_align(ty)
#define $type_is_unsigned(ty)   __jcc_ast_type_is_unsigned(ty)
#define $type_is_const(ty)      __jcc_ast_type_is_const(ty)
#define $type_base(ty)          __jcc_ast_type_base(ty)
#define $type_array_len(ty)     __jcc_ast_type_array_len(ty)
#define $type_return_type(ty)   __jcc_ast_type_return_type(ty)
#define $type_param_count(ty)   __jcc_ast_type_param_count(ty)
#define $type_param_at(ty, i)   __jcc_ast_type_param_at(ty, i)
#define $type_is_variadic(ty)   __jcc_ast_type_is_variadic(ty)
#define $type_name(ty)          __jcc_ast_type_name(ty)

#define $int_literal(val) __jcc_ast_int_literal(_VM, val)
#define $float_literal(val) __jcc_ast_float_literal(_VM, val)
#define $string_literal(str) __jcc_ast_string_literal(_VM, str)
#define $var_ref(name) __jcc_ast_var_ref(_VM, name)
#define $param_ref(fn, name) __jcc_ast_param_ref(_VM, fn, name)

#define $binary(op, l, r) __jcc_ast_binary(_VM, op, l, r)
#define $unary(op, operand) __jcc_ast_unary(_VM, op, operand)
#define $cast(expr, ty) __jcc_ast_cast(_VM, expr, ty)

// Ticket #171: new expression builders
// Ternary conditional: cond ? then_expr : else_expr
#define $cond(c, t, e) __jcc_ast_cond(_VM, c, t, e)
// Typed null pointer: (void *)0
#define $null() __jcc_ast_null(_VM)
// sizeof(type) / _Alignof(type) as a compile-time integer literal
#define $sizeof_type(ty) __jcc_ast_sizeof_type(_VM, ty)
#define $alignof_type(ty) __jcc_ast_alignof_type(_VM, ty)
// sizeof(expr): resolves expr's type then returns its size as an integer literal
#define $sizeof_expr(expr) __jcc_ast_sizeof_expr(_VM, expr)
// Array subscript: arr[idx] (desugared as *(arr+idx))
#define $subscript(arr, idx) __jcc_ast_subscript(_VM, arr, idx)
// Comma expression: evaluate lhs (for side effects), yield rhs
#define $comma(lhs, rhs) __jcc_ast_comma(_VM, lhs, rhs)

#define $return(expr) __jcc_ast_return(_VM, expr)
#define $block(stmts, count) __jcc_ast_block(_VM, stmts, count)
#define $if(c, t, e) __jcc_ast_if(_VM, c, t, e)
#define $switch(cond) __jcc_ast_switch(_VM, cond)
#define $switch_add_case(sw, v, b)                                      \
    __jcc_ast_switch_add_case(_VM, sw, v, b)
#define $switch_set_default(sw, b)                                    \
    __jcc_ast_switch_set_default(_VM, sw, b)
#define $expr_stmt(expr) __jcc_ast_expr_stmt(_VM, expr)
#define $local_var(name, ty) __jcc_ast_local_var(_VM, name, ty)
#define $local_var_unique(ty) __jcc_ast_local_var_unique(_VM, ty)
#define $assign(target, value) __jcc_ast_assign(_VM, target, value)
#define $member(obj, name) __jcc_ast_member(_VM, obj, name)
#define $funcall(callee, args, n) __jcc_ast_funcall(_VM, callee, args, n)
#define $while(cond, body) __jcc_ast_while(_VM, cond, body)
#define $for(init, cond, inc, body) __jcc_ast_for(_VM, init, cond, inc, body)
#define $do_while(body, cond) __jcc_ast_do_while(_VM, body, cond)

#define $make_pointer(base) __jcc_ast_make_pointer(_VM, base)
#define $make_array(base, len) __jcc_ast_make_array(_VM, base, len)

// Ticket #171: qualified type constructors
#define $make_const(ty)    __jcc_ast_make_const(_VM, ty)
#define $make_volatile(ty) __jcc_ast_make_volatile(_VM, ty)

#define $enum_count(ty) __jcc_ast_enum_count(_VM, ty)
#define $enum_at(ty, i) __jcc_ast_enum_at(_VM, ty, i)
#define $enum_find(ty, name) __jcc_ast_enum_find(_VM, ty, name)
#define $enum_constant_name(ec)   __jcc_ast_enum_constant_name(ec)
#define $enum_constant_value(ec)  __jcc_ast_enum_constant_value(ec)
#define $enum_name(ty)            __jcc_ast_enum_name(ty)
#define $enum_value_count(ty)     __jcc_ast_enum_value_count(ty)
#define $enum_value_name(ty, i)   __jcc_ast_enum_value_name(ty, i)
#define $enum_value(ty, i)        __jcc_ast_enum_value(ty, i)

#define $struct_member_count(ty) __jcc_ast_struct_member_count(_VM, ty)
#define $struct_member_at(ty, i) __jcc_ast_struct_member_at(_VM, ty, i)
#define $struct_member_find(ty, name)                                   \
    __jcc_ast_struct_member_find(_VM, ty, name)
#define $member_name(m)             __jcc_ast_member_name(m)
#define $member_type(m)             __jcc_ast_member_type(m)
#define $member_offset(m)           __jcc_ast_member_offset(m)
#define $member_is_bitfield(m)      __jcc_ast_member_is_bitfield(m)
#define $member_bitfield_width(m)   __jcc_ast_member_bitfield_width(m)

#define $find_global(name)        __jcc_ast_find_global(_VM, name)
#define $global_count()           __jcc_ast_global_count(_VM)
#define $global_at(i)             __jcc_ast_global_at(_VM, i)
#define $obj_name(obj)            __jcc_ast_obj_name(obj)
#define $obj_type(obj)            __jcc_ast_obj_type(obj)
#define $obj_is_function(obj)     __jcc_ast_obj_is_function(obj)
#define $obj_is_definition(obj)   __jcc_ast_obj_is_definition(obj)
#define $obj_is_static(obj)       __jcc_ast_obj_is_static(obj)

#define $function(name, ret_type)                                       \
    __jcc_ast_function(_VM, name, ret_type)
#define $publish(decl)                                                  \
    _Generic((decl),                                                        \
        $obj_t *: __jcc_ast_publish,                                          \
        $type_t *: __jcc_ast_publish_type                                     \
    )(_VM, (decl), 0)
#define $publish_at(decl, tok)                                          \
    _Generic((decl),                                                        \
        $obj_t *: __jcc_ast_publish,                                          \
        $type_t *: __jcc_ast_publish_type                                     \
    )(_VM, (decl), (tok))
#define $forward_declare(fn) __jcc_ast_publish(_VM, fn, 0)
#define $function_add_param(fn, name, type)                             \
    __jcc_ast_function_add_param(_VM, fn, name, type)
#define $function_set_body(fn, body)                                    \
    __jcc_ast_function_set_body(_VM, fn, body)
#define $function_set_static(fn, is_static)                             \
    __jcc_ast_function_set_static(fn, is_static)
#define $function_set_inline(fn, is_inline)                             \
    __jcc_ast_function_set_inline(fn, is_inline)
#define $function_set_variadic(fn, is_variadic)                         \
    __jcc_ast_function_set_variadic(fn, is_variadic)

// Ticket #171: function forward declaration / prototype builder
// Creates a declaration-only Obj (no body); use $function_add_param for
// parameters and $publish to make it visible in scope.
#define $function_prototype(name, ret)                                  \
    __jcc_ast_function_prototype(_VM, name, ret)

// Ticket #171: struct/union/enum/typedef type builders
// Build a new named aggregate and expose it so $get_type(name) resolves it.
//
//   $type_t *s = $make_struct("Point");
//   $struct_add_field(s, "x", $get_type("int"));
//   $struct_add_field(s, "y", $get_type("int"));
//
// $struct_add_field works for both struct and union types.
// $make_typedef registers name as an alias for underlying and returns it.
// $enum_add_constant adds a constant to the enum AND to scope (usable as int).
#define $make_struct(name)     __jcc_ast_make_struct(_VM, name)
#define $make_union(name)      __jcc_ast_make_union(_VM, name)
#define $struct_add_field(ty, name, field_type) \
    __jcc_ast_struct_add_field(_VM, ty, name, field_type)
#define $make_enum(name)       __jcc_ast_make_enum(_VM, name)
#define $enum_add_constant(ty, name, value) \
    __jcc_ast_enum_add_constant(_VM, ty, name, value)
#define $make_typedef(name, underlying) \
    __jcc_ast_make_typedef(_VM, name, underlying)

// Comptime variable access (ticket #188)
/*!
 * @function __jcc_get_comptime_int
 * @abstract Read an integer-typed @c #pragma comptime variable's value at
 *           compile time.
 * @param vm The VM context.
 * @param name The comptime variable's name.
 * @return The 64-bit integer value, or 0 if the variable is not defined.
 * @discussion Convenience wrapper: $get_comptime_int(name).
 */
int64_t __jcc_get_comptime_int(JCC *vm, const char *name);
/*!
 * @function __jcc_get_comptime_float
 * @abstract Read a float/double-typed @c #pragma comptime variable's value
 *           at compile time.
 * @param vm The VM context.
 * @param name The comptime variable's name.
 * @return The double value, or 0.0 if the variable is not defined.
 * @discussion Convenience wrapper: $get_comptime_float(name).
 */
double __jcc_get_comptime_float(JCC *vm, const char *name);
/*!
 * @function __jcc_get_comptime_var
 * @abstract Read a comptime scalar variable as an AST literal node.
 * @param vm The VM context.
 * @param name The comptime variable's name.
 * @return A nk_num node representing the variable's value, or NULL on error.
 * @discussion Convenience wrapper: $get_comptime_var(name).
 */
$node_t *__jcc_get_comptime_var(JCC *vm, const char *name);
/*!
 * @function __jcc_get_comptime_member
 * @abstract Read a named field from a comptime struct variable as an AST
 *           literal node.
 * @param vm The VM context.
 * @param var_name The comptime struct variable's name.
 * @param field The field name to look up.
 * @return A nk_num node for the field's value, or NULL on error.
 * @discussion Convenience wrapper: $get_comptime_member(var_name, field).
 */
$node_t *__jcc_get_comptime_member(JCC *vm, const char *var_name,
                                  const char *field);

#define $get_comptime_int(name)           __jcc_get_comptime_int(_VM, name)
#define $get_comptime_float(name)         __jcc_get_comptime_float(_VM, name)
#define $get_comptime_var(name)           __jcc_get_comptime_var(_VM, name)
#define $get_comptime_member(var, field)  __jcc_get_comptime_member(_VM, var, field)

// Constexpr variable access (ticket #189)
/*!
 * @function __jcc_get_constexpr_value
 * @abstract Read the evaluated initializer of a global @c constexpr variable
 *           as an AST literal node.
 * @param vm The VM context.
 * @param name The constexpr variable's name.
 * @return A nk_num node (integer or float, depending on the variable's type),
 *         or NULL on error.
 * @discussion Errors at compile time if @a name does not refer to a visible
 *             @c constexpr variable.  Convenience wrapper:
 *             $get_constexpr_value(name).
 */
$node_t *__jcc_get_constexpr_value(JCC *vm, const char *name);

#define $get_constexpr_value(name)  __jcc_get_constexpr_value(_VM, name)

// Global variable generation (ticket #152)
#define $global_var(name, ty)                                           \
    __jcc_ast_global_var(_VM, name, ty)
#define $global_var_set_init_data(var, data, len)                       \
    __jcc_ast_global_var_set_init_data(_VM, var, data, len)
#define $global_var_set_static(var, is_static)                          \
    __jcc_ast_global_var_set_static(var, is_static)

// Function-building context (ticket #148)
// Usage:
//   $with_fn(fn) {
//       $function_set_body(fn, $quote("return 42;"));
//   }
// Inside the block, current_fn is set to fn so $quote("return x;") casts
// to the correct return type.  The pop always runs even on early exit.
#define $with_fn(fn)                                                    \
    for (int _jcc_fn_ctx_ = (__jcc_ast_push_fn(_VM, (fn)), 1);             \
         _jcc_fn_ctx_;                                                      \
         _jcc_fn_ctx_ = (__jcc_ast_pop_fn(_VM), 0))

#ifdef __cplusplus
}
#endif

#endif // JCC_REFLECTION_H
