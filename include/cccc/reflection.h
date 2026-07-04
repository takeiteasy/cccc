/*
 CCCC: Comprehensiev C Compensation Compiler - Pragma Macro Reflection API

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
 * @brief Reflection and AST Construction API for CCCC Pragma Macros
 *
 * This header provides APIs for:
 * - Type introspection and lookup
 * - Enum reflection
 * - Struct/union member introspection
 * - AST node construction (literals, expressions, statements)
 * - Global variable generation (GlobalVar*)
 * - Function-building context (WithFn)
 *
 * ## Return-value model
 *
 * A pragma macro's returned Node* is **the node spliced at the call site**,
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
 *     Obj *fn = MakeFunction("helper", GetType("int"));
 *     WithFn(fn) {
 *         FunctionSetBody(fn, Quote("return 42;"));
 *     }
 *     PublishNode(fn);
 *     // no return needed
 * }
 * emit_helpers();
 * @endcode
 *
 * A void macro used in expression position is a compile error.
 *
 * ## Usage
 *
 * This private header is embedded into CCCC and automatically injected when
 * compiling pragma macros. It is not installed as a public runtime header.
 * All functions that require VM context use the VM macro, which
 * expands to __builtin_get_vm() - a builtin that returns the current
 * VM instance during macro execution.
 *
 * ## Example (expression macro)
 *
 * @code
 * #pragma macro
 * Node *make_const_5(void) {
 *     return MakeIntLiteral(5);
 * }
 *
 * int main(void) {
 *     int x = make_const_5();  // x == 5
 *     return 0;
 * }
 * @endcode
 */

#ifndef CCCC_REFLECTION_H
#define CCCC_REFLECTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations (opaque types for pragma macros)
typedef struct VirtualMachine VirtualMachine;
typedef struct VirtualMachine VirtualMachine;
typedef struct Type Type;
typedef struct Node Node;
typedef struct Obj Obj;
typedef struct Member Member;
typedef struct EnumConstant EnumConstant;
typedef struct Token Token;
typedef struct AttrTarget AttrTarget;

// Type kind enumeration (matches cccc.h TypeKind)
typedef enum {
    TK_VOID = 0,
    TK_BOOL = 1,
    TK_CHAR = 2,
    TK_SHORT = 3,
    TK_INT = 4,
    TK_LONG = 5,
    TK_FLOAT = 6,
    TK_DOUBLE = 7,
    TK_LDOUBLE = 8,
    TK_ENUM = 9,
    TK_PTR = 10,
    TK_FUNC = 11,
    TK_ARRAY = 12,
    TK_VLA = 13,
    TK_STRUCT = 14,
    TK_UNION = 15,
} TypeKind;

// Node kind enumeration (subset for pragma macro use; ND_ prefix matches cccc.h)
typedef enum {
    NK_NULL_EXPR = 0,
    NK_ADD = 1,
    NK_SUB = 2,
    NK_MUL = 3,
    NK_DIV = 4,
    NK_NEG = 5,
    NK_MOD = 6,
    NK_BITAND = 7,
    NK_BITOR = 8,
    NK_BITXOR = 9,
    NK_SHL = 10,
    NK_SHR = 11,
    NK_EQ = 12,
    NK_NE = 13,
    NK_LT = 14,
    NK_LE = 15,
    NK_ASSIGN = 16,
    NK_COND = 17,
    NK_COMMA = 18,
    NK_MEMBER = 19,
    NK_ADDR = 20,
    NK_DEREF = 21,
    NK_NOT = 22,
    NK_BITNOT = 23,
    NK_LOGAND = 24,
    NK_LOGOR = 25,
    NK_RETURN = 26,
    NK_IF = 27,
    NK_FOR = 28,
    NK_DO = 29,
    NK_SWITCH = 30,
    NK_CASE = 31,
    NK_BLOCK = 32,
    NK_FUNCALL = 37,
    NK_EXPR_STMT = 38,
    NK_VAR = 40,
    NK_NUM = 42,
    NK_CAST = 43,
    NK_MACRO_CALL = 51,
} NodeKind;

typedef enum {
    ATTR_TARGET_TYPEDEF = 1,
    ATTR_TARGET_TYPE = 2,
    ATTR_TARGET_FUNCTION = 3,
    ATTR_TARGET_GLOBAL = 4,
} AttrTargetKind;

// ============================================================================
// Magic VM Builtin
// ============================================================================

/*!
 * @function __builtin_get_vm
 * @abstract Builtin that returns the current parent VM context.
 * @discussion Set before calling a pragma macro and cleared after.
 *             Use the @c VM convenience macro inside a macro body to refer
 *             to the active VM instance.
 * @return Pointer to the current CCCC VM instance.
 */
extern VirtualMachine *__builtin_get_vm(void);

/*!
 * @define VM
 * @abstract Magic macro that references the VM instance in pragma macros.
 */
#define VM __builtin_get_vm()

/*!
 * @function __builtin_gensym
 * @abstract Generate a unique identifier string for macro-created symbols.
 * @param vm The VM context.
 * @param prefix Prefix for the generated name.
 * @return An arena-allocated string of the form "<prefix>__<n>".
 * @discussion Convenience wrapper: Gensym(prefix).
 */
const char *__builtin_gensym(VirtualMachine *vm, const char *prefix);

/*!
 * @function __builtin_macroexpand_1
 * @abstract Lisp-style single-step macro expansion (macroexpand-1 semantics).
 * @discussion If @a node is an @c NK_MACRO_CALL node, execute the macro once
 *             and return the resulting node without splicing it into the AST
 *             or recursing into nested macro calls. If @a node is not a macro
 *             call, it is returned unchanged (identity).
 *             Convenience wrapper: MacroExpand1(node).
 * @param vm The VM context.
 * @param node The node to (possibly) expand.
 * @return The expanded node, or @a node itself if it is not a macro call.
 */
Node *__builtin_macroexpand_1(VirtualMachine *vm, Node *node);

/*!
 * @function __builtin_macroexpand
 * @abstract Lisp-style full macro expansion.
 * @discussion Repeatedly calls @c __builtin_macroexpand_1 on the top-level node
 *             until it is no longer an @c NK_MACRO_CALL (i.e. the form is
 *             stable). Does not recurse into child nodes. Respects the VM's
 *             @c macro_recursion_limit.  Convenience wrapper: MacroExpand(node).
 * @param vm The VM context.
 * @param node The node to fully expand.
 * @return The fully expanded node, or @a node itself if it is not a macro call.
 */
Node *__builtin_macroexpand(VirtualMachine *vm, Node *node);

/*!
 * @function __builtin_ast_vararg_count
 * @abstract Return the number of variadic arguments for the active macro call.
 * @param vm The VM context.
 * @return Number of arguments after the fixed parameters.
 */
int __builtin_ast_vararg_count(VirtualMachine *vm);

/*!
 * @function __builtin_ast_vararg_at
 * @abstract Return an inline macro's variadic AST argument by zero-based index.
 * @param vm The VM context.
 * @param index Zero-based variadic argument index.
 * @return The argument node.
 * @discussion Emits a compile-time error if @a index is out of range or the
 *             active macro call is a global-generation string macro.
 */
Node *__builtin_ast_vararg_at(VirtualMachine *vm, int index);

/*!
 * @function __builtin_ast_varargs_as_array
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
Node **__builtin_ast_varargs_as_array(VirtualMachine *vm);

/*!
 * @function __builtin_ast_vararg_str_at
 * @abstract Return a global-generation macro's stringified variadic argument.
 * @param vm The VM context.
 * @param index Zero-based variadic argument index.
 * @return The stringified token argument.
 * @discussion Emits a compile-time error if @a index is out of range or the
 *             active macro call is an inline AST macro.
 */
const char *__builtin_ast_vararg_str_at(VirtualMachine *vm, int index);

// ============================================================================
// Generated Node Source Locations (ticket #173)
// ============================================================================

/*!
 * @function __builtin_ast_current_token
 * @abstract Return the token for the macro invocation currently being executed.
 * @param vm The VM context.
 * @return Opaque token for the active macro call site, or NULL outside macro
 *         execution.
 * @discussion Convenience wrapper: CurrentToken().
 */
Token *__builtin_ast_current_token(VirtualMachine *vm);

/*!
 * @function __builtin_ast_synthetic_token
 * @abstract Create an opaque synthetic source token for generated AST nodes.
 * @param vm The VM context.
 * @param label Short diagnostic label for the synthetic location.
 * @return Arena-allocated synthetic token, or NULL on error.
 * @discussion Use this when a generated node should diagnose against a stable
 *             generated location instead of the macro call or an input node.
 *             Convenience wrapper: SyntheticToken(label).
 */
Token *__builtin_ast_synthetic_token(VirtualMachine *vm, const char *label);

/*!
 * @function __builtin_ast_token_from_node
 * @abstract Return the opaque source token attached to a node.
 * @param node Node to inspect.
 * @return The node token, or NULL.
 * @discussion Convenience wrapper: TokenFromNode(node).
 */
Token *__builtin_ast_token_from_node(Node *node);

/*!
 * @function __builtin_ast_set_token
 * @abstract Attach an opaque source token to a node.
 * @param node Node to update.
 * @param tok Token from __builtin_ast_current_token(),
 *            __builtin_ast_synthetic_token(), or __builtin_ast_token_from_node().
 * @return node, for chaining.
 * @discussion Convenience wrapper: SetToken(node, tok).
 */
Node *__builtin_ast_set_token(Node *node, Token *tok);

/*!
 * @function __builtin_ast_copy_location
 * @abstract Copy the source token from one node to another.
 * @param dst Generated node to update.
 * @param src Source node whose location should be reused.
 * @return dst, for chaining.
 * @discussion Convenience wrapper: CopyLocation(dst, src).
 */
Node *__builtin_ast_copy_location(Node *dst, Node *src);

// ============================================================================
// Macro Diagnostics (ticket #78)
// ============================================================================

/*!
 * @function __builtin_macro_error_at
 * @abstract Emit a compiler error pointing at the source location of a node.
 * @param vm The VM context.
 * @param node A node whose tok field provides file/line/col. May be NULL
 *             (falls back to a location-less error).
 * @param fmt printf-style format string, followed by format arguments.
 * @discussion Behaves like the compiler's error_tok(): in normal mode it
 *             prints the error with file/line/col and source snippet then
 *             aborts via longjmp or exit.  When vm->collect_errors is set
 *             it records the error and compilation may continue.
 *             Convenience wrapper: MacroErrorAt(node, ...).
 */
void __builtin_macro_error_at(VirtualMachine *vm, Node *node, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/*!
 * @function __builtin_macro_warning_at
 * @abstract Emit a compiler warning pointing at the source location of a node.
 * @param vm The VM context.
 * @param node A node whose tok field provides file/line/col. May be NULL.
 * @param fmt printf-style format string, followed by format arguments.
 * @discussion Emitted only when -Wcccc-macro is enabled. Non-fatal unless
 *             promoted with -Werror or -Werror=cccc-macro.
 *             Convenience wrapper: MacroWarningAt(node, ...).
 */
void __builtin_macro_warning_at(VirtualMachine *vm, Node *node, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/*!
 * @function __builtin_quote
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
 *       `->next`-linked node chain (see `NodeList` / `__builtin_node_list`).
 *       Each `$@k;` in the template is replaced by the entire chain.
 *     - `$@` sequential list splice, parallel to `$$`.
 *     - List splice identifiers (`$@k`) may share their index with scalar
 *       splices (`$k`) in the same template, but positional and incremental
 *       styles cannot be mixed within one template.
 *
 *   List splices are valid in statement-list position (inside a `{ ... }`
 *   block), direct call-argument position, or compound-literal initializer
 *   lists. Using `$@k` as an expression operand is a compile-time error.
 *
 * @param ... Node* arguments corresponding to the splice points.
 * @return The parsed and substituted AST node, or NULL on error.
 * @discussion Template is parsed and substituted at macro-execution (compile)
 *             time; there is no runtime overhead.  Expressions and statements
 *             are auto-detected. The variadic form supports up to 64 splice
 *             nodes; use __builtin_quote_n for larger node arrays.
 *             Convenience wrapper: Quote(tmpl, ...).
 */
Node *__builtin_quote(VirtualMachine *vm, const char *tmpl, ...);

/*!
 * @function __builtin_quote_n
 * @abstract Array-form quasi-quote that validates the caller-provided splice
 *           count and supports larger node arrays than the variadic form.
 * @param vm The VM context.
 * @param tmpl A C expression or statement as a string literal with $N / $@N
 *             splice points.
 * @param nodes Array of Node* splice arguments.
 * @param count Length of the nodes array.  If any $K in the template exceeds
 *              count, a compile-time error is emitted.
 * @return The parsed and substituted AST node, or NULL on error.
 * @discussion Convenience wrapper: QuoteN(tmpl, nodes, count).
 */
Node *__builtin_quote_n(VirtualMachine *vm, const char *tmpl, Node **nodes, int count);

/*!
 * @function __builtin_node_list
 * @abstract Build a `->next`-linked node chain from an array, returning the
 *           head.  Use the result as the argument to a `$@k` list splice.
 * @param vm    The VM context.
 * @param nodes Array of Node* to link together.  Linking stops at the first
 *              NULL element or at count, whichever comes first.
 * @param count Number of elements in the array.
 * @return Head of the chain, or NULL if count == 0 or nodes is NULL.
 * @discussion A single node is a valid chain of length 1.  An existing
 *             `->next` chain (e.g. `__builtin_ast_block(...)->body`) can also be
 *             passed directly as the splice argument without going through
 *             this helper.  Convenience wrapper: NodeList(nodes, count).
 */
Node *__builtin_node_list(VirtualMachine *vm, Node **nodes, int count);

// ============================================================================
// Type Lookup and Introspection
// ============================================================================

/*!
 * @function __builtin_ast_find_type
 * @abstract Look up a type by tag name (struct/union/enum).
 * @param vm The VM context.
 * @param name The tag name to look up.
 * @return The matching Type*, or NULL if not found.
 * @discussion Convenience wrapper: FindType(name).
 */
Type *__builtin_ast_find_type(VirtualMachine *vm, const char *name);

/*!
 * @function __builtin_ast_type_exists
 * @abstract Check whether a type is currently in scope by name.
 * @param vm The VM context.
 * @param name The type name to look up.
 * @return True if the name resolves to a type, false otherwise.
 * @discussion Convenience wrapper: TypeExists(name).
 */
bool __builtin_ast_type_exists(VirtualMachine *vm, const char *name);

/*!
 * @function __builtin_ast_get_type
 * @abstract Look up a type by name, falling back to the built-in primitives.
 * @param vm The VM context.
 * @param name The type name to look up.
 * @return The matching Type*, or NULL if not found.
 * @discussion Convenience wrapper: GetType(name).
 */
Type *__builtin_ast_get_type(VirtualMachine *vm, const char *name);

/*!
 * @function __builtin_ast_type_kind
 * @abstract Return the TypeKind tag of a type.
 * @param ty The type to inspect.
 * @return The type kind (TK_INT, TK_STRUCT, TK_PTR, ...).
 * @discussion Convenience wrapper: GetTypeKind(ty).
 */
TypeKind __builtin_ast_type_kind(Type *ty);

/*!
 * @function __builtin_ast_type_size
 * @abstract Return sizeof(ty) in bytes.
 * @param ty The type to inspect.
 * @return The size in bytes.
 * @discussion Convenience wrapper: TypeSize(ty).
 */
int __builtin_ast_type_size(Type *ty);

/*!
 * @function __builtin_ast_type_align
 * @abstract Return _Alignof(ty) in bytes.
 * @param ty The type to inspect.
 * @return The alignment in bytes.
 * @discussion Convenience wrapper: TypeAlign(ty).
 */
int __builtin_ast_type_align(Type *ty);

/*!
 * @function __builtin_ast_type_is_unsigned
 * @abstract Test whether an integer type is unsigned.
 * @param ty The type to inspect.
 * @return True for unsigned integer types, false otherwise.
 * @discussion Convenience wrapper: TypeIsUnsigned(ty).
 */
bool __builtin_ast_type_is_unsigned(Type *ty);

/*!
 * @function __builtin_ast_type_is_const
 * @abstract Test whether a type is const-qualified.
 * @param ty The type to inspect.
 * @return True if ty has a const qualifier, false otherwise.
 * @discussion Convenience wrapper: TypeIsConst(ty).
 */
bool __builtin_ast_type_is_const(Type *ty);

/*!
 * @function __builtin_ast_type_base
 * @abstract Return the element type of a pointer or array.
 * @param ty The pointer/array type to inspect.
 * @return The base Type*, or NULL if ty is not a pointer or array.
 * @discussion Convenience wrapper: TypeBase(ty).
 */
Type *__builtin_ast_type_base(Type *ty);

/*!
 * @function __builtin_ast_type_array_len
 * @abstract Return the fixed length of an array type.
 * @param ty The array type to inspect.
 * @return The element count for TY_ARRAY types, -1 otherwise.
 * @discussion Convenience wrapper: TypeArrayLen(ty).
 */
int __builtin_ast_type_array_len(Type *ty);

/*!
 * @function __builtin_ast_type_return_type
 * @abstract Return the return type of a function type.
 * @param ty The function type to inspect.
 * @return The return Type*, or NULL if ty is not a function type.
 * @discussion Convenience wrapper: TypeReturnType(ty).
 */
Type *__builtin_ast_type_return_type(Type *ty);

/*!
 * @function __builtin_ast_type_param_count
 * @abstract Return the number of declared parameters of a function type.
 * @param ty The function type to inspect.
 * @return The parameter count for TY_FUNC types, -1 otherwise.
 * @discussion Convenience wrapper: TypeParamCount(ty).
 */
int __builtin_ast_type_param_count(Type *ty);

/*!
 * @function __builtin_ast_type_param_at
 * @abstract Return the type of the parameter at the given index.
 * @param ty The function type to inspect.
 * @param index Zero-based parameter index.
 * @return The parameter's Type*, or NULL on out-of-range or non-function ty.
 * @discussion Convenience wrapper: TypeParamAt(ty, index).
 */
Type *__builtin_ast_type_param_at(Type *ty, int index);

/*!
 * @function __builtin_ast_type_is_variadic
 * @abstract Test whether a function type is variadic.
 * @param ty The type to inspect.
 * @return True for variadic function types, false otherwise.
 * @discussion Convenience wrapper: TypeIsVariadic(ty).
 */
bool __builtin_ast_type_is_variadic(Type *ty);

/*!
 * @function __builtin_ast_type_name
 * @abstract Return the user-visible name of a type, if any.
 * @param ty The type to inspect.
 * @return A freshly-allocated NUL-terminated string, or NULL for anonymous types.
 * @discussion Convenience wrapper: TypeName(ty).
 */
const char *__builtin_ast_type_name(Type *ty);

/*!
 * @function __builtin_ast_type_c_name
 * @abstract Return a valid C identifier fragment naming `ty`.
 * @param vm The VM context.
 * @param ty The type to inspect.
 * @return TypeName(ty) for named types, or a builtin spelling ("int",
 *   "double", "ulong", ...) for builtin scalar types, or NULL.
 * @discussion Convenience wrapper: TypeCName(ty). Intended for naming
 *   generated functions, e.g. sum_<T> from GenerateSum(elem_type).
 */
const char *__builtin_ast_type_c_name(VirtualMachine *vm, Type *ty);

/*!
 * @function __builtin_ast_make_pointer
 * @abstract Build a pointer-to-base type.
 * @param vm The VM context.
 * @param base The pointed-to type.
 * @return A Type* representing "base *", or NULL on error.
 * @discussion Convenience wrapper: MakePointer(base).
 */
Type *__builtin_ast_make_pointer(VirtualMachine *vm, Type *base);

/*!
 * @function __builtin_ast_make_array
 * @abstract Build a fixed-length array type.
 * @param vm The VM context.
 * @param base The element type.
 * @param length The element count.
 * @return A Type* representing "base[length]", or NULL on error.
 * @discussion Convenience wrapper: MakeArray(base, length).
 */
Type *__builtin_ast_make_array(VirtualMachine *vm, Type *base, int length);

/*!
 * @function __builtin_ast_make_func_ptr_type
 * @abstract Build a pointer-to-function type, e.g. "T (*)(T)".
 * @param vm The VM context.
 * @param return_ty The function's return type.
 * @param param_types Array of parameter types (each copy_type()'d internally).
 * @param nparams Number of entries in param_types (max 16).
 * @return A Type* representing "return_ty (*)(param_types...)", or NULL on error.
 * @discussion Convenience wrapper: MakeFuncPtrType(return_ty, param_types, nparams).
 */
Type *__builtin_ast_make_func_ptr_type(VirtualMachine *vm, Type *return_ty,
                                        Type **param_types, int nparams);

// Ticket #171: qualified type constructors
/*!
 * @function __builtin_ast_make_const
 * @abstract Return a const-qualified copy of ty.
 * @param vm The VM context.
 * @param ty The type to qualify.
 * @return A const-qualified Type*, or NULL on error.
 * @discussion Convenience wrapper: MakeConst(ty).
 */
Type *__builtin_ast_make_const(VirtualMachine *vm, Type *ty);
/*!
 * @function __builtin_ast_make_volatile
 * @abstract Return a volatile-qualified copy of ty.
 * @param vm The VM context.
 * @param ty The type to qualify.
 * @return A volatile-qualified Type*, or NULL on error.
 * @discussion Convenience wrapper: MakeVolatile(ty).
 */
Type *__builtin_ast_make_volatile(VirtualMachine *vm, Type *ty);

// ============================================================================
// Enum Reflection
// ============================================================================

/*!
 * @function __builtin_ast_enum_count
 * @abstract Return the number of constants in an enum type.
 * @param vm The VM context.
 * @param enum_type The enum type to inspect.
 * @return The constant count, or -1 if enum_type is not an enum.
 * @discussion Convenience wrapper: EnumCount(ty).
 */
int __builtin_ast_enum_count(VirtualMachine *vm, Type *enum_type);

/*!
 * @function __builtin_ast_enum_at
 * @abstract Return the enum constant at a given index.
 * @param vm The VM context.
 * @param enum_type The enum type to inspect.
 * @param index Zero-based index.
 * @return The EnumConstant*, or NULL on out-of-range or non-enum.
 * @discussion Convenience wrapper: EnumAt(ty, index).
 */
EnumConstant *__builtin_ast_enum_at(VirtualMachine *vm, Type *enum_type, int index);

/*!
 * @function __builtin_ast_enum_find
 * @abstract Look up an enum constant by name.
 * @param vm The VM context.
 * @param enum_type The enum type to search.
 * @param name The constant name to look up.
 * @return The matching EnumConstant*, or NULL if not found.
 * @discussion Convenience wrapper: EnumFind(ty, name).
 */
EnumConstant *__builtin_ast_enum_find(VirtualMachine *vm, Type *enum_type,
                                    const char *name);

/*!
 * @function __builtin_ast_enum_constant_name
 * @abstract Return the name of an enum constant.
 * @param ec The enum constant.
 * @return A NUL-terminated string owned by ec.
 * @discussion Convenience wrapper: EnumConstantName(ec).
 */
const char *__builtin_ast_enum_constant_name(EnumConstant *ec);

/*!
 * @function __builtin_ast_enum_constant_value
 * @abstract Return the integer value of an enum constant.
 * @param ec The enum constant.
 * @return The constant's integer value (int64_t to support C23 wide underlying types).
 * @discussion Convenience wrapper: EnumConstantValue(ec).
 */
int64_t __builtin_ast_enum_constant_value(EnumConstant *ec);

/*!
 * @function __builtin_ast_enum_name
 * @abstract Return the tag name of an enum type.
 * @param e The enum type to inspect.
 * @return A NUL-terminated string owned by e.
 * @discussion Convenience wrapper: EnumName(ty).
 */
const char *__builtin_ast_enum_name(Type *e);

/*!
 * @function __builtin_ast_enum_value_count
 * @abstract Return the number of values in an enum type.
 * @param e The enum type to inspect.
 * @return The constant count, or -1 if e is not an enum.
 * @discussion Convenience wrapper: EnumValueCount(ty).
 */
int __builtin_ast_enum_value_count(Type *e);

/*!
 * @function __builtin_ast_enum_value_name
 * @abstract Return the name of the enum constant at the given index.
 * @param e The enum type to inspect.
 * @param index Zero-based index.
 * @return A NUL-terminated string, or NULL on out-of-range.
 * @discussion Convenience wrapper: EnumValueName(ty, index).
 */
const char *__builtin_ast_enum_value_name(Type *e, int index);

/*!
 * @function __builtin_ast_enum_value
 * @abstract Return the integer value of the enum constant at the given index.
 * @param e The enum type to inspect.
 * @param index Zero-based index.
 * @return The constant's integer value (int64_t), or -1 on out-of-range.
 * @discussion Convenience wrapper: EnumValue(ty, index).
 */
int64_t __builtin_ast_enum_value(Type *e, int index);

/*!
 * @function __builtin_ast_enum_to_string_switch
 * @abstract Build a `switch (expr) { case V0: return "Name0"; ...
 *           default: return ""; }` over the constants of an enum type.
 * @param vm The virtual machine instance.
 * @param ty The enum type.
 * @param expr The expression to switch on (the enum value).
 * @return An NK_SWITCH node. Caller wraps it in a function returning
 *         `const char *`.
 * @discussion Convenience wrapper: EnumToString(ty, expr).
 */
Node *__builtin_ast_enum_to_string_switch(VirtualMachine *vm, Type *ty, Node *expr);

/*!
 * @function __builtin_ast_enum_from_string_chain
 * @abstract Build a block of `if (strcmp(expr, "Name0") == 0) return V0; ...
 *           return -1;` over the constants of an enum type.
 * @param vm The virtual machine instance.
 * @param ty The enum type.
 * @param expr The expression to compare (a `const char *`).
 * @return An NK_BLOCK node. Caller wraps it in a function returning the
 *         enum type (or an int).
 * @discussion Convenience wrapper: EnumFromString(ty, expr).
 */
Node *__builtin_ast_enum_from_string_chain(VirtualMachine *vm, Type *ty, Node *expr);

// ============================================================================
// Struct/Union Member Introspection
// ============================================================================

/*!
 * @function __builtin_ast_struct_member_count
 * @abstract Return the number of members of a struct or union type.
 * @param vm The VM context.
 * @param struct_type The struct or union type to inspect.
 * @return The member count, or -1 if struct_type is not a struct/union.
 * @discussion Convenience wrapper: StructMemberCount(ty).
 */
int __builtin_ast_struct_member_count(VirtualMachine *vm, Type *struct_type);

/*!
 * @function __builtin_ast_struct_member_at
 * @abstract Return the member at the given index.
 * @param vm The VM context.
 * @param struct_type The struct or union type to inspect.
 * @param index Zero-based member index.
 * @return The Member*, or NULL on out-of-range or non-aggregate.
 * @discussion Convenience wrapper: StructMemberAt(ty, index).
 */
Member *__builtin_ast_struct_member_at(VirtualMachine *vm, Type *struct_type,
                                        int index);

/*!
 * @function __builtin_ast_struct_member_find
 * @abstract Look up a struct or union member by name.
 * @param vm The VM context.
 * @param struct_type The struct or union type to search.
 * @param name The member name to look up.
 * @return The matching Member*, or NULL if not found.
 * @discussion Convenience wrapper: StructMemberFind(ty, name).
 */
Member *__builtin_ast_struct_member_find(VirtualMachine *vm, Type *struct_type,
                                        const char *name);

/*!
 * @function __builtin_ast_member_name
 * @abstract Return the name of a struct/union member.
 * @param m The member to inspect.
 * @return A NUL-terminated string owned by m.
 * @discussion Convenience wrapper: MemberName(m).
 */
const char *__builtin_ast_member_name(Member *m);

/*!
 * @function __builtin_ast_member_type
 * @abstract Return the type of a struct/union member.
 * @param m The member to inspect.
 * @return The member's Type*.
 * @discussion Convenience wrapper: MemberType(m).
 */
Type *__builtin_ast_member_type(Member *m);

/*!
 * @function __builtin_ast_member_offset
 * @abstract Return the byte offset of a struct/union member.
 * @param m The member to inspect.
 * @return The offset in bytes.
 * @discussion Convenience wrapper: MemberOffset(m).
 */
int __builtin_ast_member_offset(Member *m);

/*!
 * @function __builtin_ast_member_is_bitfield
 * @abstract Test whether a member is a bitfield.
 * @param m The member to inspect.
 * @return True if the member is a bitfield, false otherwise.
 * @discussion Convenience wrapper: MemberIsBitfield(m).
 */
bool __builtin_ast_member_is_bitfield(Member *m);

/*!
 * @function __builtin_ast_member_bitfield_width
 * @abstract Return the bit width of a bitfield member.
 * @param m The member to inspect.
 * @return The bit width for bitfield members, 0 otherwise.
 * @discussion Convenience wrapper: MemberBitfieldWidth(m).
 */
int __builtin_ast_member_bitfield_width(Member *m);

/*!
 * @function __builtin_ast_offsetof_chain
 * @abstract Compute the byte offset of a (possibly nested) member chain.
 * @param vm The virtual machine instance.
 * @param ty The starting struct/union type.
 * @param names An array of member names to walk, innermost last.
 * @param n The number of names in the chain.
 * @return The summed byte offset, or -1 if any name cannot be resolved.
 * @discussion Convenience wrapper: OffsetofChain(ty, "a", "b", ...).
 */
int64_t __builtin_ast_offsetof_chain(VirtualMachine *vm, Type *ty,
                                   const char **names, int n);

// ============================================================================
// Global Symbol Introspection
// ============================================================================

/*!
 * @function __builtin_ast_find_global
 * @abstract Look up a global symbol by name.
 * @param vm The VM context.
 * @param name The global name to look up.
 * @return The matching Obj*, or NULL if not found.
 * @discussion Convenience wrapper: FindGlobal(name).
 */
Obj *__builtin_ast_find_global(VirtualMachine *vm, const char *name);

/*!
 * @function __builtin_ast_global_count
 * @abstract Return the total number of global symbols.
 * @param vm The VM context.
 * @return The count of globals.
 * @discussion Convenience wrapper: GlobalCount().
 */
int __builtin_ast_global_count(VirtualMachine *vm);

/*!
 * @function __builtin_ast_global_at
 * @abstract Return the global symbol at the given index.
 * @param vm The VM context.
 * @param index Zero-based global index.
 * @return The Obj* at the given slot, or NULL on out-of-range.
 * @discussion Convenience wrapper: GlobalAt(index).
 */
Obj *__builtin_ast_global_at(VirtualMachine *vm, int index);

/*!
 * @function __builtin_ast_obj_name
 * @abstract Return the name of a global object.
 * @param obj The object to inspect.
 * @return A NUL-terminated string owned by obj.
 * @discussion Convenience wrapper: ObjName(obj).
 */
const char *__builtin_ast_obj_name(Obj *obj);

/*!
 * @function __builtin_ast_obj_type
 * @abstract Return the type of a global object.
 * @param obj The object to inspect.
 * @return The object's Type*.
 * @discussion Convenience wrapper: ObjType(obj).
 */
Type *__builtin_ast_obj_type(Obj *obj);

/*!
 * @function __builtin_ast_obj_is_function
 * @abstract Test whether a global object is a function.
 * @param obj The object to inspect.
 * @return True for functions, false for variables.
 * @discussion Convenience wrapper: ObjIsFunction(obj).
 */
bool __builtin_ast_obj_is_function(Obj *obj);

/*!
 * @function __builtin_ast_obj_is_definition
 * @abstract Test whether a global object has a definition.
 * @param obj The object to inspect.
 * @return True for defined objects, false for declarations only.
 * @discussion Convenience wrapper: ObjIsDefinition(obj).
 */
bool __builtin_ast_obj_is_definition(Obj *obj);

/*!
 * @function __builtin_ast_obj_is_static
 * @abstract Test whether a global object has internal (static) linkage.
 * @param obj The object to inspect.
 * @return True for static linkage, false for external.
 * @discussion Convenience wrapper: ObjIsStatic(obj).
 */
bool __builtin_ast_obj_is_static(Obj *obj);

/*!
 * @function __builtin_attr_target_kind
 * @abstract Return the kind of declaration decorated by a custom attribute.
 */
int __builtin_attr_target_kind(AttrTarget *target);

/*!
 * @function __builtin_attr_target_name
 * @abstract Return the decorated declaration's source name, when available.
 */
const char *__builtin_attr_target_name(AttrTarget *target);

/*!
 * @function __builtin_attr_target_type
 * @abstract Return the decorated declaration's type.
 */
Type *__builtin_attr_target_type(AttrTarget *target);

/*!
 * @function __builtin_attr_target_obj
 * @abstract Return the decorated function or global object, or NULL for type targets.
 */
Obj *__builtin_attr_target_obj(AttrTarget *target);

/*!
 * @function __builtin_attr_target_token
 * @abstract Return a source token for the decorated declaration.
 */
Token *__builtin_attr_target_token(AttrTarget *target);

// ============================================================================
// AST Node Construction - Literals
// ============================================================================

/*!
 * @function __builtin_ast_int_literal
 * @abstract Build an integer literal AST node.
 * @param vm The VM context.
 * @param value The integer value.
 * @return An NK_NUM node for value.
 * @discussion Convenience wrapper: MakeIntLiteral(value).
 */
Node *__builtin_ast_int_literal(VirtualMachine *vm, int64_t value);

/*!
 * @function __builtin_ast_float_literal
 * @abstract Build a floating-point literal AST node.
 * @param vm The VM context.
 * @param value The floating-point value.
 * @return An NK_NUM node for value.
 * @discussion Convenience wrapper: MakeFloatLiteral(value).
 */
Node *__builtin_ast_float_literal(VirtualMachine *vm, double value);

/*!
 * @function __builtin_ast_string_literal
 * @abstract Build a string literal AST node.
 * @param vm The VM context.
 * @param str A NUL-terminated string.
 * @return An NK_NUM string-literal node.
 * @discussion Convenience wrapper: MakeStringLiteral(str).
 */
Node *__builtin_ast_string_literal(VirtualMachine *vm, const char *str);

/*!
 * @function __builtin_ast_var_ref
 * @abstract Build a variable reference AST node.
 * @param vm The VM context.
 * @param name The variable name.
 * @return An NK_VAR node referencing name.
 * @discussion Convenience wrapper: MakeVarRef(name).
 */
Node *__builtin_ast_var_ref(VirtualMachine *vm, const char *name);

/*!
 * @function __builtin_ast_param_ref
 * @abstract Build a reference to a function parameter by name.
 * @param vm The VM context.
 * @param fn The function object whose parameter is being referenced.
 * @param name The parameter name.
 * @return An NK_VAR node for the named parameter.
 * @discussion Use this when building function bodies to reference parameters
 *             by name.  Convenience wrapper: MakeParamRef(fn, name).
 */
Node *__builtin_ast_param_ref(VirtualMachine *vm, Obj *fn, const char *name);

// ============================================================================
// AST Node Construction - Expressions
// ============================================================================

/*!
 * @function __builtin_ast_binary
 * @abstract Build a binary operation AST node.
 * @param vm The VM context.
 * @param op The operator kind (NK_ADD, NK_SUB, ...).
 * @param left The left-hand operand.
 * @param right The right-hand operand.
 * @return The binary expression node.
 * @discussion Convenience wrapper: MakeBinary(op, left, right).
 */
Node *__builtin_ast_binary(VirtualMachine *vm, NodeKind op, Node *left,
                            Node *right);

/*!
 * @function __builtin_ast_unary
 * @abstract Build a unary operation AST node.
 * @param vm The VM context.
 * @param op The operator kind (NK_NEG, NK_DEREF, ...).
 * @param operand The operand expression.
 * @return The unary expression node.
 * @discussion Convenience wrapper: MakeUnary(op, operand).
 */
Node *__builtin_ast_unary(VirtualMachine *vm, NodeKind op, Node *operand);

/*!
 * @function __builtin_ast_cast
 * @abstract Build a type cast AST node.
 * @param vm The VM context.
 * @param expr The expression to cast.
 * @param target_type The type to cast to.
 * @return An NK_CAST node.
 * @discussion Convenience wrapper: MakeCast(expr, target_type).
 */
Node *__builtin_ast_cast(VirtualMachine *vm, Node *expr, Type *target_type);

// Ticket #171: new expression builders

/*!
 * @function __builtin_ast_cond
 * @abstract Build a ternary conditional expression node (cond ? then : else).
 * @param vm The VM context.
 * @param cond The condition expression.
 * @param then_expr The expression evaluated when cond is non-zero.
 * @param else_expr The expression evaluated when cond is zero.
 * @return An NK_COND node.
 * @discussion Convenience wrapper: MakeCond(cond, then_expr, else_expr).
 */
Node *__builtin_ast_cond(VirtualMachine *vm, Node *cond, Node *then_expr,
                          Node *else_expr);

/*!
 * @function __builtin_ast_null
 * @abstract Build a typed null pointer node: (void *)0.
 * @param vm The VM context.
 * @return An NK_NUM node representing a typed NULL.
 * @discussion Convenience wrapper: MakeNull().
 */
Node *__builtin_ast_null(VirtualMachine *vm);

/*!
 * @function __builtin_ast_sizeof_type
 * @abstract Emit sizeof(ty) as a compile-time integer literal.
 * @param vm The VM context.
 * @param ty The type to measure.
 * @return An NK_NUM node holding sizeof(ty).
 * @discussion Convenience wrapper: MakeSizeofType(ty).
 */
Node *__builtin_ast_sizeof_type(VirtualMachine *vm, Type *ty);

/*!
 * @function __builtin_ast_alignof_type
 * @abstract Emit _Alignof(ty) as a compile-time integer literal.
 * @param vm The VM context.
 * @param ty The type to measure.
 * @return An NK_NUM node holding _Alignof(ty).
 * @discussion Convenience wrapper: MakeAlignofType(ty).
 */
Node *__builtin_ast_alignof_type(VirtualMachine *vm, Type *ty);

/*!
 * @function __builtin_ast_sizeof_expr
 * @abstract Emit sizeof(expr): resolve the expression's type then its size.
 * @param vm The VM context.
 * @param expr The expression whose type to measure.
 * @return An NK_NUM node holding sizeof(expr).
 * @discussion Convenience wrapper: MakeSizeofExpr(expr).
 */
Node *__builtin_ast_sizeof_expr(VirtualMachine *vm, Node *expr);

/*!
 * @function __builtin_ast_subscript
 * @abstract Build an array subscript node: arr[idx], desugared as *(arr+idx).
 * @param vm The VM context.
 * @param arr The array (or pointer) expression.
 * @param idx The index expression.
 * @return An NK_ADD / NK_DEREF node pair representing the subscript.
 * @discussion Convenience wrapper: MakeSubscript(arr, idx).
 */
Node *__builtin_ast_subscript(VirtualMachine *vm, Node *arr, Node *idx);

/*!
 * @function __builtin_ast_comma
 * @abstract Build a comma expression: evaluate lhs, yield rhs.
 * @param vm The VM context.
 * @param lhs The expression evaluated for side effects.
 * @param rhs The expression whose value is the result.
 * @return An NK_COMMA node.
 * @discussion Convenience wrapper: MakeComma(lhs, rhs).
 */
Node *__builtin_ast_comma(VirtualMachine *vm, Node *lhs, Node *rhs);

// ============================================================================
// AST Node Construction - Statements
// ============================================================================

/*!
 * @function __builtin_ast_return
 * @abstract Build a return statement node.
 * @param vm The VM context.
 * @param expr The value to return (may be NULL for `return;` in void functions).
 * @return An NK_RETURN node.
 * @discussion Convenience wrapper: MakeReturn(expr).
 */
Node *__builtin_ast_return(VirtualMachine *vm, Node *expr);

/*!
 * @function __builtin_ast_block
 * @abstract Build a block (compound statement) node.
 * @param vm The VM context.
 * @param stmts Array of statement nodes, or NULL if count is 0.
 * @param count Number of statements in the array.
 * @return An NK_BLOCK node.
 * @discussion Convenience wrapper: MakeBlock(stmts, count).
 */
Node *__builtin_ast_block(VirtualMachine *vm, Node **stmts, int count);

/*!
 * @function __builtin_ast_block_add_stmt
 * @abstract Append a statement to a block node.
 * @param vm The VM context.
 * @param block The NK_BLOCK node to modify.
 * @param stmt The statement to append.
 * @return The block on success, or NULL on invalid arguments.
 * @discussion Convenience wrapper: BlockAddStmt(block, stmt), or
 *             BlockAddStmt(stmt) inside WithBlock(block).
 */
Node *__builtin_ast_block_add_stmt(VirtualMachine *vm, Node *block, Node *stmt);

/*!
 * @function __builtin_ast_if
 * @abstract Build an if statement node.
 * @param vm The VM context.
 * @param cond The condition expression.
 * @param then_body The body executed when cond is non-zero.
 * @param else_body The body executed when cond is zero, or NULL.
 * @return An NK_IF node.
 * @discussion Convenience wrapper: MakeIf(cond, then_body, else_body).
 */
Node *__builtin_ast_if(VirtualMachine *vm, Node *cond, Node *then_body,
                        Node *else_body);

/*!
 * @function __builtin_ast_switch
 * @abstract Build a switch statement node.
 * @param vm The VM context.
 * @param cond The expression to switch on.
 * @return An NK_SWITCH node.  Use __builtin_ast_switch_add_case and
 *         __builtin_ast_switch_set_default to populate it.
 * @discussion Convenience wrapper: MakeSwitch(cond).
 */
Node *__builtin_ast_switch(VirtualMachine *vm, Node *cond);

/*!
 * @function __builtin_ast_switch_add_case
 * @abstract Append a case to a switch statement.
 * @param vm The VM context.
 * @param switch_node The switch node returned by __builtin_ast_switch.
 * @param value The case value expression.
 * @param body The body statement for this case.
 * @discussion Convenience wrapper: SwitchAddCase(sw, value, body), or
 *             SwitchAddCase(value, body) inside WithSwitch(sw).
 */
void __builtin_ast_switch_add_case(VirtualMachine *vm, Node *switch_node,
                                Node *value, Node *body);

/*!
 * @function __builtin_ast_switch_set_default
 * @abstract Set the default case for a switch statement.
 * @param vm The VM context.
 * @param switch_node The switch node returned by __builtin_ast_switch.
 * @param body The default-case body statement.
 * @discussion Convenience wrapper: SwitchSetDefault(sw, body), or
 *             SwitchSetDefault(body) inside WithSwitch(sw).
 */
void __builtin_ast_switch_set_default(VirtualMachine *vm, Node *switch_node,
                                    Node *body);

/*!
 * @function __builtin_ast_expr_stmt
 * @abstract Build an expression statement node.
 * @param vm The VM context.
 * @param expr The expression to evaluate for side effects.
 * @return An NK_EXPR_STMT node.
 * @discussion Convenience wrapper: MakeExprStmt(expr).
 */
Node *__builtin_ast_expr_stmt(VirtualMachine *vm, Node *expr);

// ============================================================================
// AST Node Construction - Local Variable Injection (ticket #77)
// ============================================================================

/*!
 * @function __builtin_ast_local_var
 * @abstract Declare a named local variable in the current function scope
 *           and return a variable-reference node for it.
 * @param vm The VM context.
 * @param name The variable name (user-visible).
 * @param ty The variable type.
 * @return A NK_VAR node referencing the new local, or NULL if called
 *         outside a function body or on invalid arguments.
 * @note  The variable is injected into the current function's locals list
 *        and will receive a stack offset when the function is compiled.
 *        For temporaries that must not capture user names, prefer
 *        __builtin_ast_local_var_unique().
 * @discussion Convenience wrapper: MakeLocalVar(name, ty).
 */
Node *__builtin_ast_local_var(VirtualMachine *vm, const char *name, Type *ty);

/*!
 * @function __builtin_ast_local_var_unique
 * @abstract Declare a hygienic (gensym'd) local variable in the current
 *           function scope and return a variable-reference node for it.
 * @param vm The VM context.
 * @param ty The variable type.
 * @return A NK_VAR node referencing the new local, or NULL on error.
 * @note  The generated name begins with ".L.." and is therefore not
 *        expressible as a user identifier — guaranteed no name capture.
 *        This is the safe default for macro temporaries.
 * @discussion Convenience wrapper: MakeLocalVarUnique(ty).
 */
Node *__builtin_ast_local_var_unique(VirtualMachine *vm, Type *ty);

/*!
 * @function __builtin_ast_assign
 * @abstract Build an assignment node (target = value).
 * @param vm The VM context.
 * @param target The lvalue expression being assigned to.
 * @param value The rvalue expression to assign.
 * @return An NK_ASSIGN node, or NULL on error.
 * @discussion Convenience wrapper: MakeAssign(target, value).
 */
Node *__builtin_ast_assign(VirtualMachine *vm, Node *target, Node *value);

/*!
 * @function __builtin_ast_member
 * @abstract Create a struct/union member access node (obj.name).
 * @param vm The VM context.
 * @param obj An expression node whose type must be a struct or union.
 * @param name The member name as a NUL-terminated string.
 * @return A NK_MEMBER node, or NULL if the member is not found or
 *         obj is not a struct/union type.
 * @note The callee is responsible for dereferencing pointers first;
 *       pass the struct value directly (use __builtin_ast_unary(NK_DEREF,…)
 *       for pointer-to-struct access).
 * @discussion Convenience wrapper: MakeMember(obj, name).
 */
Node *__builtin_ast_member(VirtualMachine *vm, Node *obj, const char *name);

/*!
 * @function __builtin_ast_funcall
 * @abstract Create a function call node.
 * @param vm The VM context.
 * @param callee An expression node that evaluates to a function (or function
 *               pointer). The callee's lhs field holds this expression.
 * @param args Array of argument nodes (may be NULL if n == 0).
 * @param n Number of arguments.
 * @return A NK_FUNCALL node, or NULL on error.
 * @discussion Convenience wrapper: MakeFuncCall(callee, args, n).
 */
Node *__builtin_ast_funcall(VirtualMachine *vm, Node *callee, Node **args, int n);

/*!
 * @function __builtin_ast_while
 * @abstract Create a while loop node.
 * @param vm The VM context.
 * @param cond The loop condition expression.
 * @param body The loop body statement.
 * @return A NK_FOR node (CCCC represents while as for with no init/inc),
 *         or NULL on error.
 * @discussion Convenience wrapper: MakeWhile(cond, body).
 */
Node *__builtin_ast_while(VirtualMachine *vm, Node *cond, Node *body);

/*!
 * @function __builtin_ast_for
 * @abstract Create a for loop node.
 * @param vm The VM context.
 * @param init Initialiser expression/statement (may be NULL).
 * @param cond Loop condition (may be NULL for infinite loop).
 * @param inc Increment expression (may be NULL).
 * @param body Loop body.
 * @return A NK_FOR node, or NULL on error.
 * @discussion Convenience wrapper: MakeFor(init, cond, inc, body).
 */
Node *__builtin_ast_for(VirtualMachine *vm, Node *init, Node *cond,
                       Node *inc, Node *body);

/*!
 * @function __builtin_ast_do_while
 * @abstract Create a do-while loop node.
 * @param vm The VM context.
 * @param body The loop body.
 * @param cond The loop condition (tested after each iteration).
 * @return A NK_DO node, or NULL on error.
 * @discussion Convenience wrapper: MakeDoWhile(body, cond).
 */
Node *__builtin_ast_do_while(VirtualMachine *vm, Node *body, Node *cond);

// ============================================================================
// Function Generation
// ============================================================================

/*!
 * @function __builtin_ast_function
 * @abstract Create a new function object.
 * @param vm The VM context.
 * @param name The function name.
 * @param return_type The return type.
 * @return The newly created function object, or NULL on error.
 * @discussion The function is automatically added to the globals list
 *             and will be compiled when the main program is compiled.
 *             Convenience wrapper: MakeFunction(name, return_type).
 */
Obj *__builtin_ast_function(VirtualMachine *vm, const char *name,
                            Type *return_type);

/*!
 * @function __builtin_ast_publish
 * @abstract Make a generated object visible at the current source position.
 * @param vm The VM context.
 * @param obj A function or global variable object created by the AST builders.
 * @param tok Optional representative token for diagnostics, or NULL.
 * @return A no-op Node on success, or NULL on invalid arguments.
 * @discussion Top-level explicit macro calls run at their source position.
 *             Call this after creating a function or global variable when
 *             later macro-generated code at the same parse point should be
 *             able to reference it without a handwritten declaration.
 *             Convenience wrapper: PublishNode(obj) / PublishNodeAt(obj, tok).
 */
Node *__builtin_ast_publish(VirtualMachine *vm, Obj *obj, Token *tok);

/*!
 * @function __builtin_ast_publish_type
 * @abstract Accept a generated type declaration as already published.
 * @param vm The VM context.
 * @param ty A type created by MakeStruct, MakeUnion,
 *           MakeEnum, or MakeTypedef.
 * @param tok Optional representative token for diagnostics, or NULL.
 * @return A no-op Node on success, or NULL on invalid arguments.
 * @discussion Generated type builders self-register in tag or typedef scope.
 *             This function lets PublishNode(type) be used uniformly; there
 *             is no separate convenience macro for this entry point.
 */
Node *__builtin_ast_publish_type(VirtualMachine *vm, Type *ty, Token *tok);

/*!
 * @function __builtin_emit_directive
 * @abstract Emit one raw preprocessor directive line into generated output.
 * @param vm The VM context.
 * @param line Complete directive text, for example "#ifdef _WIN32".
 * @discussion Convenience wrapper: EmitDirective(line).
 */
void __builtin_emit_directive(VirtualMachine *vm, const char *line);


/*!
 * @function __builtin_ast_function_add_param
 * @abstract Add a parameter to a function.
 * @param vm The VM context.
 * @param fn The function object.
 * @param name The parameter name.
 * @param type The parameter type.
 * @discussion Parameters are added in order. Call this multiple times
 *             for multiple parameters.  Convenience wrapper:
 *             FunctionAddParam(fn, name, type).
 */
void __builtin_ast_function_add_param(VirtualMachine *vm, Obj *fn, const char *name,
                                Type *type);

/*!
 * @function __builtin_ast_function_set_body
 * @abstract Set the body of a function.
 * @param vm The VM context.
 * @param fn The function object.
 * @param body The function body (a statement or block node).
 * @discussion If body is not already a NK_BLOCK, it will be wrapped in one.
 *             Convenience wrapper: FunctionSetBody(fn, body).
 */
void __builtin_ast_function_set_body(VirtualMachine *vm, Obj *fn, Node *body);

/*!
 * @function __builtin_ast_function_set_static
 * @abstract Set whether a function has static linkage.
 * @param fn The function object.
 * @param is_static True for static linkage, false for external.
 * @discussion Convenience wrapper: FunctionSetStatic(fn, is_static).
 */
void __builtin_ast_function_set_static(Obj *fn, bool is_static);

/*!
 * @function __builtin_ast_function_set_inline
 * @abstract Set whether a function is inline.
 * @param fn The function object.
 * @param is_inline True for inline, false otherwise.
 * @discussion Convenience wrapper: FunctionSetInline(fn, is_inline).
 */
void __builtin_ast_function_set_inline(Obj *fn, bool is_inline);

/*!
 * @function __builtin_ast_function_set_variadic
 * @abstract Set whether a function is variadic.
 * @param fn The function object.
 * @param is_variadic True for variadic, false otherwise.
 * @discussion Convenience wrapper: FunctionSetVariadic(fn, is_variadic).
 */
void __builtin_ast_function_set_variadic(Obj *fn, bool is_variadic);

// Ticket #171: function prototype (forward declaration only, no body)
/*!
 * @function __builtin_ast_function_prototype
 * @abstract Create a function forward declaration (prototype) without a body.
 * @param vm The VM context.
 * @param name The function name.
 * @param return_type The return type.
 * @return The declaration Obj*, or NULL on error.
 * @discussion Use FunctionAddParam to add parameters and
 *             PublishNode to expose it in scope. A subsequent
 *             MakeFunction call with the same name will reuse this Obj and
 *             fill in the body.  Convenience wrapper: FunctionPrototype(name, return_type).
 */
Obj *__builtin_ast_function_prototype(VirtualMachine *vm, const char *name,
                                    Type *return_type);

// ============================================================================
// Programmatic Attribute Application — ticket #619
// ============================================================================

/*!
 * @function __builtin_ast_add_attribute
 * @abstract Apply an attribute string to a programmatically created function.
 * @param vm The VM context.
 * @param fn The function object to attribute (created by MakeFunction).
 * @param attr_text Attribute text as it would appear between [[ and ]] in source,
 *                  e.g. "cccc::test", "cccc::test(suite=\"gen\", timeout=5000)",
 *                  "nodiscard", "nodiscard(\"always check\")", or "@myattr".
 *                  GNU form __attribute__((…)) is also accepted as the full string.
 * @discussion Parses attr_text through the existing attribute pipeline and applies
 *             the result to fn.  Supports mode attrs (test/build/build_target/
 *             test_setup/test_teardown), standard C23/GNU attrs (nodiscard, noreturn,
 *             pure, aligned, …), and custom @attribute handlers.
 *             cccc::comptime cannot be applied this way and will error.
 *             Convenience wrapper: AddAttribute(fn, text).
 */
void __builtin_ast_add_attribute(VirtualMachine *vm, Obj *fn, const char *attr_text);

// Internal helper for MarkAsBuildTarget: composes "cccc::build_target(kind=…)"
// at runtime so the macro doesn't require string concatenation.
void __builtin_ast_add_build_target_attr(VirtualMachine *vm, Obj *fn, const char *kind);

// Ticket #171: struct/union/enum/typedef type builders

/*!
 * @function __builtin_ast_make_struct
 * @abstract Create and expose a new named struct type.
 * @param vm The VM context.
 * @param name The struct tag name.
 * @return The new struct Type*, or NULL on error.
 * @discussion Use StructAddField to add fields after creation.
 *             The type is immediately visible via FindType(name).
 *             Convenience wrapper: MakeStruct(name).
 */
Type *__builtin_ast_make_struct(VirtualMachine *vm, const char *name);

/*!
 * @function __builtin_ast_make_union
 * @abstract Create and expose a new named union type.
 * @param vm The VM context.
 * @param name The union tag name.
 * @return The new union Type*, or NULL on error.
 * @discussion Convenience wrapper: MakeUnion(name).
 */
Type *__builtin_ast_make_union(VirtualMachine *vm, const char *name);

/*!
 * @function __builtin_ast_struct_add_field
 * @abstract Append a field to a struct or union and recompute its layout.
 * @param vm The VM context.
 * @param ty The struct or union type to modify.
 * @param name The field name.
 * @param field_type The field's type.
 * @return ty on success, or NULL on error.
 * @discussion For struct types, offsets are recalculated from scratch after
 *             each field addition. For union types, all fields stay at offset 0
 *             and the union size is updated to the maximum field size.
 *             Convenience wrapper: StructAddField(ty, name, field_type).
 */
Type *__builtin_ast_struct_add_field(VirtualMachine *vm, Type *ty, const char *name,
                                   Type *field_type);

/*!
 * @function __builtin_ast_make_enum
 * @abstract Create and expose a new named enum type.
 * @param vm The VM context.
 * @param name The enum tag name.
 * @return The new enum Type*, or NULL on error.
 * @discussion Use EnumAddConstant to add constants after creation.
 *             Convenience wrapper: MakeEnum(name).
 */
Type *__builtin_ast_make_enum(VirtualMachine *vm, const char *name);

/*!
 * @function __builtin_ast_enum_add_constant
 * @abstract Add a named constant to an enum type and expose it in scope.
 * @param vm The VM context.
 * @param ty The enum type.
 * @param name The constant name.
 * @param value The constant integer value.
 * @discussion The constant is appended to ty->enum_constants and also pushed
 *             into the current scope so it is usable as an integer constant in
 *             subsequently compiled code.  Convenience wrapper:
 *             EnumAddConstant(ty, name, value).
 */
void __builtin_ast_enum_add_constant(VirtualMachine *vm, Type *ty, const char *name,
                                  int64_t value);

/*!
 * @function __builtin_ast_make_typedef
 * @abstract Register a typedef alias for a type and expose it in scope.
 * @param vm The VM context.
 * @param name The typedef name.
 * @param underlying The aliased type.
 * @return The aliased type after registration, or NULL on invalid arguments.
 * @discussion After this call, FindType(name) resolves to underlying and
 *             subsequently compiled code can use name as a type name.
 *             Convenience wrapper: MakeTypedef(name, underlying).
 */
Type *__builtin_ast_make_typedef(VirtualMachine *vm, const char *name, Type *underlying);

// ============================================================================
// Global Variable Generation (ticket #152)
// ============================================================================

/*!
 * @function __builtin_ast_global_var
 * @abstract Create a new named global variable definition.
 * @param vm   The VM context.
 * @param name The variable name (must be unique among globals).
 * @param ty   The variable type.  Use MakeArray(char_ty, len) for byte
 *             arrays so that the size matches the init_data length.
 * @return The new Obj*, or NULL on error.
 * @discussion The variable is registered in vm->compiler.globals.  For inline
 *             macros the capture loop will stash it in macro_globals and emit
 *             an extern declaration into every input file's token stream so
 *             the parser can resolve references.
 *             Convenience wrapper: GlobalVar(name, ty).
 */
Obj *__builtin_ast_global_var(VirtualMachine *vm, const char *name, Type *ty);

/*!
 * @function __builtin_ast_global_var_set_init_data
 * @abstract Set the initial data for a generated global variable.
 * @param vm   The VM context.
 * @param var  The global variable object.
 * @param data Pointer to the raw byte data.
 * @param len  Number of bytes to copy.  Must equal var->ty->size.
 * @discussion Convenience wrapper: GlobalVarSetInitData(var, data, len).
 */
void __builtin_ast_global_var_set_init_data(VirtualMachine *vm, Obj *var,
                                        const char *data, int len);

/*!
 * @function __builtin_ast_global_var_set_static
 * @abstract Set the static (internal linkage) flag on a generated global.
 * @param var       The global variable object.
 * @param is_static True for internal linkage (file-scope static).
 * @discussion Convenience wrapper: GlobalVarSetStatic(var, is_static).
 */
void __builtin_ast_global_var_set_static(Obj *var, bool is_static);

// ============================================================================
// Function-building context (ticket #148)
// ============================================================================

/*!
 * @function __builtin_ast_push_fn
 * @abstract Establish fn as the "function currently being built" so that
 *           Quote("return x;") applies the correct implicit return-type cast.
 * @param vm The VM context.
 * @param fn The generated function whose return type should be used.
 * @discussion Call __builtin_ast_pop_fn (or use the WithFn macro) to
 *             restore the previous context.  execute_pragma_macro always
 *             restores current_fn after the macro returns, so unmatched pushes
 *             cannot leak into the main parse/codegen pass.  There is no
 *             1:1 convenience macro; use the @c WithFn(fn) { ... } block
 *             helper to bracket push/pop pairs in a macro body.
 */
void __builtin_ast_push_fn(VirtualMachine *vm, Obj *fn);

/*!
 * @function __builtin_ast_pop_fn
 * @abstract Restore the function context saved by the most recent push.
 * @param vm The VM context.
 * @discussion Inverse of __builtin_ast_push_fn; typically used through the
 *             @c WithFn(fn) { ... } block helper, which performs the
 *             matching pop even on early exit.
 */
void __builtin_ast_pop_fn(VirtualMachine *vm);

/*!
 * @function __builtin_ast_push_block
 * @abstract Establish a block as the current statement-append context.
 * @param vm The VM context.
 * @param block The NK_BLOCK node being populated.
 * @discussion Use with WithBlock(block) so BlockAddStmt(stmt) appends to
 *             block without repeating the block pointer.
 */
void __builtin_ast_push_block(VirtualMachine *vm, Node *block);
void __builtin_ast_pop_block(VirtualMachine *vm);
Node *__builtin_ast_block_add_current_stmt(VirtualMachine *vm, Node *stmt);

/*!
 * @function __builtin_ast_push_struct
 * @abstract Establish a struct or union as the current field-add context.
 * @param vm The VM context.
 * @param ty The aggregate type being populated.
 * @discussion Use with WithStruct(ty) so StructAddField(name, ty) appends
 *             to the current aggregate.
 */
void __builtin_ast_push_struct(VirtualMachine *vm, Type *ty);
void __builtin_ast_pop_struct(VirtualMachine *vm);
Type *__builtin_ast_struct_add_current_field(VirtualMachine *vm, const char *name,
                                            Type *field_type);

/*!
 * @function __builtin_ast_push_switch
 * @abstract Establish a switch node as the current case/default context.
 * @param vm The VM context.
 * @param switch_node The switch node being populated.
 * @discussion Use with WithSwitch(sw) so SwitchAddCase(value, body) and
 *             SwitchSetDefault(body) append to the current switch.
 */
void __builtin_ast_push_switch(VirtualMachine *vm, Node *switch_node);
void __builtin_ast_pop_switch(VirtualMachine *vm);
void __builtin_ast_switch_add_current_case(VirtualMachine *vm, Node *value,
                                       Node *body);
void __builtin_ast_switch_set_current_default(VirtualMachine *vm, Node *body);

/*!
 * @function __builtin_ast_push_enum
 * @abstract Establish an enum as the current constant-add context.
 * @param vm The VM context.
 * @param ty The enum type being populated.
 * @discussion Use with WithEnum(ty) so EnumAddConstant(name, value)
 *             appends to the current enum.
 */
void __builtin_ast_push_enum(VirtualMachine *vm, Type *ty);
void __builtin_ast_pop_enum(VirtualMachine *vm);
void __builtin_ast_enum_add_current_constant(VirtualMachine *vm, const char *name,
                                         int value);

// ============================================================================
// AST Dump Functions (ticket #58) — Nim-style dumpTree / dumpAstGen
// ============================================================================

/*!
 * @function __builtin_dump_tree
 * @abstract Print a human-readable tree representation of a node to stdout.
 * @param vm The VM context.
 * @param node The root node to print.
 * @discussion Reuses the compiler's internal cc_dump_ast text renderer.
 *             Convenience wrapper: DumpTree(node).
 */
void __builtin_dump_tree(VirtualMachine *vm, Node *node);

/*!
 * @function __builtin_dump_tree_to_string
 * @abstract Render the tree representation to a heap-allocated string.
 * @param vm The VM context.
 * @param node The root node.
 * @return An arena-allocated NUL-terminated string, or NULL on error.
 * @discussion Convenience wrapper: DumpTreeToString(node).
 */
const char *__builtin_dump_tree_to_string(VirtualMachine *vm, Node *node);

/*!
 * @function __builtin_dump_ast_gen
 * @abstract Print __builtin_ast_*() builder calls that would reconstruct the node.
 * @param vm The VM context.
 * @param node The root node to emit builder calls for.
 * @discussion Covers all node kinds for which relfection.c has a builder.
 *             Unsupported kinds are emitted as C comments.
 *             Convenience wrapper: DumpAstGen(node).
 */
void __builtin_dump_ast_gen(VirtualMachine *vm, Node *node);

/*!
 * @function __builtin_dump_ast_gen_to_string
 * @abstract Render the __builtin_ast_*() builder call sequence to a string.
 * @param vm The VM context.
 * @param node The root node.
 * @return An arena-allocated NUL-terminated string, or NULL on error.
 * @discussion Convenience wrapper: DumpAstGenToString(node).
 */
const char *__builtin_dump_ast_gen_to_string(VirtualMachine *vm, Node *node);

// ============================================================================
// Convenience Macros (automatically pass VM)
// ============================================================================

// Quasi-quoting helpers (ticket #1, #172)
#define Quote(tmpl, ...) __builtin_quote(VM, tmpl, ##__VA_ARGS__)
#define QuoteN(tmpl, nodes, count) __builtin_quote_n(VM, tmpl, nodes, count)
// Build a ->next-linked chain from a compound-literal array for $@k splices:
//   NodeList((Node*[]){ a, b, c }, 3)
#define NodeList(nodes, count) __builtin_node_list(VM, nodes, count)

// Diagnostic helpers (ticket #78) — note: variadic macros require C99+
#define MacroErrorAt(node, ...) __builtin_macro_error_at(VM, node, __VA_ARGS__)
#define MacroWarningAt(node, ...) __builtin_macro_warning_at(VM, node, __VA_ARGS__)

// AST dump helpers (ticket #58)
#define DumpTree(node) __builtin_dump_tree(VM, node)
#define DumpTreeToString(node) __builtin_dump_tree_to_string(VM, node)
#define DumpAstGen(node) __builtin_dump_ast_gen(VM, node)
#define DumpAstGenToString(node) __builtin_dump_ast_gen_to_string(VM, node)
#define Gensym(prefix) __builtin_gensym(VM, prefix)
#define MacroExpand1(node) __builtin_macroexpand_1(VM, node)
#define MacroExpand(node) __builtin_macroexpand(VM, node)
#define VarargCount() __builtin_ast_vararg_count(VM)
#define VarargAt(i) __builtin_ast_vararg_at(VM, i)
#define VarargAsArray() __builtin_ast_varargs_as_array(VM)
#define VarargStrAt(i) __builtin_ast_vararg_str_at(VM, i)

#define __builtin_dispatch_2(_1, _2, which, ...) which(_1, _2)
#define __builtin_dispatch_3(_1, _2, _3, which, ...) which(_1, _2, _3)

#define CurrentToken() __builtin_ast_current_token(VM)
#define SyntheticToken(label) __builtin_ast_synthetic_token(VM, label)
#define TokenFromNode(node) __builtin_ast_token_from_node(node)
#define SetToken(node, tok) __builtin_ast_set_token(node, tok)
#define CopyLocation(dst, src) __builtin_ast_copy_location(dst, src)

#define FindType(name) __builtin_ast_find_type(VM, name)
#define TypeExists(name) __builtin_ast_type_exists(VM, name)
#define GetType(name) __builtin_ast_get_type(VM, name)

// Type introspection — no VM needed
#define GetTypeKind(ty)          __builtin_ast_type_kind(ty)
#define TypeSize(ty)          __builtin_ast_type_size(ty)
#define TypeAlign(ty)         __builtin_ast_type_align(ty)
#define TypeIsUnsigned(ty)   __builtin_ast_type_is_unsigned(ty)
#define TypeIsConst(ty)      __builtin_ast_type_is_const(ty)
#define TypeBase(ty)          __builtin_ast_type_base(ty)
#define TypeArrayLen(ty)     __builtin_ast_type_array_len(ty)
#define TypeReturnType(ty)   __builtin_ast_type_return_type(ty)
#define TypeParamCount(ty)   __builtin_ast_type_param_count(ty)
#define TypeParamAt(ty, i)   __builtin_ast_type_param_at(ty, i)
#define TypeIsVariadic(ty)   __builtin_ast_type_is_variadic(ty)
#define TypeName(ty)          __builtin_ast_type_name(ty)
#define TypeCName(ty)        __builtin_ast_type_c_name(VM, ty)

#define MakeIntLiteral(val) __builtin_ast_int_literal(VM, val)
#define MakeFloatLiteral(val) __builtin_ast_float_literal(VM, val)
#define MakeStringLiteral(str) __builtin_ast_string_literal(VM, str)
#define MakeVarRef(name) __builtin_ast_var_ref(VM, name)
#define MakeParamRef(fn, name) __builtin_ast_param_ref(VM, fn, name)

#define MakeBinary(op, l, r) __builtin_ast_binary(VM, op, l, r)
#define MakeUnary(op, operand) __builtin_ast_unary(VM, op, operand)
#define MakeCast(expr, ty) __builtin_ast_cast(VM, expr, ty)

// Ticket #171: new expression builders
// Ternary conditional: cond ? then_expr : else_expr
#define MakeCond(c, t, e) __builtin_ast_cond(VM, c, t, e)
// Typed null pointer: (void *)0
#define MakeNull() __builtin_ast_null(VM)
// sizeof(type) / _Alignof(type) as a compile-time integer literal
#define MakeSizeofType(ty) __builtin_ast_sizeof_type(VM, ty)
#define MakeAlignofType(ty) __builtin_ast_alignof_type(VM, ty)
// sizeof(expr): resolves expr's type then returns its size as an integer literal
#define MakeSizeofExpr(expr) __builtin_ast_sizeof_expr(VM, expr)
// Array subscript: arr[idx] (desugared as *(arr+idx))
#define MakeSubscript(arr, idx) __builtin_ast_subscript(VM, arr, idx)
// Comma expression: evaluate lhs (for side effects), yield rhs
#define MakeComma(lhs, rhs) __builtin_ast_comma(VM, lhs, rhs)

#define MakeReturn(expr) __builtin_ast_return(VM, expr)
#define MakeBlock(stmts, count) __builtin_ast_block(VM, stmts, count)
#define __builtin_block_add_stmt_1(stmt, _ignored)                          \
    __builtin_ast_block_add_current_stmt(VM, stmt)
#define __builtin_block_add_stmt_2(block, stmt)                              \
    __builtin_ast_block_add_stmt(VM, block, stmt)
#define BlockAddStmt(...)                                             \
    __builtin_dispatch_2(__VA_ARGS__, __builtin_block_add_stmt_2,                \
                     __builtin_block_add_stmt_1)
#define MakeIf(c, t, e) __builtin_ast_if(VM, c, t, e)
#define MakeSwitch(cond) __builtin_ast_switch(VM, cond)
#define __builtin_switch_add_case_2(v, b, _ignored)                          \
    __builtin_ast_switch_add_current_case(VM, v, b)
#define __builtin_switch_add_case_3(sw, v, b)                                \
    __builtin_ast_switch_add_case(VM, sw, v, b)
#define SwitchAddCase(...)                                            \
    __builtin_dispatch_3(__VA_ARGS__, __builtin_switch_add_case_3,               \
                     __builtin_switch_add_case_2)
#define __builtin_switch_set_default_1(b, _ignored)                          \
    __builtin_ast_switch_set_current_default(VM, b)
#define __builtin_switch_set_default_2(sw, b)                                \
    __builtin_ast_switch_set_default(VM, sw, b)
#define SwitchSetDefault(...)                                         \
    __builtin_dispatch_2(__VA_ARGS__, __builtin_switch_set_default_2,            \
                     __builtin_switch_set_default_1)
#define MakeExprStmt(expr) __builtin_ast_expr_stmt(VM, expr)
#define MakeLocalVar(name, ty) __builtin_ast_local_var(VM, name, ty)
#define MakeLocalVarUnique(ty) __builtin_ast_local_var_unique(VM, ty)
#define MakeAssign(target, value) __builtin_ast_assign(VM, target, value)
#define MakeMember(obj, name) __builtin_ast_member(VM, obj, name)
#define MakeFuncCall(callee, args, n) __builtin_ast_funcall(VM, callee, args, n)

// Ticket #235: thin AST wrappers over <string.h> functions, available via
// the implicit #include <string.h> at the top of this header.
#define Memcpy(dst, src, n)                                              \
    __builtin_ast_funcall(VM, __builtin_ast_var_ref(VM, "memcpy"),            \
        (Node *[]){(dst), (src), (n)}, 3)
#define Strlen(s)                                                        \
    __builtin_ast_funcall(VM, __builtin_ast_var_ref(VM, "strlen"),            \
        (Node *[]){(s)}, 1)
#define Strcmp(a, b)                                                     \
    __builtin_ast_funcall(VM, __builtin_ast_var_ref(VM, "strcmp"),            \
        (Node *[]){(a), (b)}, 2)
#define MakeWhile(cond, body) __builtin_ast_while(VM, cond, body)
#define MakeFor(init, cond, inc, body) __builtin_ast_for(VM, init, cond, inc, body)
#define MakeDoWhile(body, cond) __builtin_ast_do_while(VM, body, cond)

#define MakePointer(base) __builtin_ast_make_pointer(VM, base)
#define MakeArray(base, len) __builtin_ast_make_array(VM, base, len)
#define MakeFuncPtrType(ret, params, n) \
    __builtin_ast_make_func_ptr_type(VM, ret, params, n)

// Ticket #171: qualified type constructors
#define MakeConst(ty)    __builtin_ast_make_const(VM, ty)
#define MakeVolatile(ty) __builtin_ast_make_volatile(VM, ty)

#define EnumCount(ty) __builtin_ast_enum_count(VM, ty)
#define EnumAt(ty, i) __builtin_ast_enum_at(VM, ty, i)
#define EnumFind(ty, name) __builtin_ast_enum_find(VM, ty, name)
#define EnumConstantName(ec)   __builtin_ast_enum_constant_name(ec)
#define EnumConstantValue(ec)  __builtin_ast_enum_constant_value(ec)
#define EnumName(ty)            __builtin_ast_enum_name(ty)
#define EnumValueCount(ty)     __builtin_ast_enum_value_count(ty)
#define EnumValueName(ty, i)   __builtin_ast_enum_value_name(ty, i)
#define EnumValue(ty, i)        __builtin_ast_enum_value(ty, i)

// Ticket #235: EnumToString(ty, expr) / EnumFromString(ty, expr)
#define EnumToString(ty, expr)   __builtin_ast_enum_to_string_switch(VM, ty, expr)
#define EnumFromString(ty, expr) __builtin_ast_enum_from_string_chain(VM, ty, expr)

#define StructMemberCount(ty) __builtin_ast_struct_member_count(VM, ty)
#define StructMemberAt(ty, i) __builtin_ast_struct_member_at(VM, ty, i)
#define StructMemberFind(ty, name)                                   \
    __builtin_ast_struct_member_find(VM, ty, name)
#define MemberName(m)             __builtin_ast_member_name(m)
#define MemberType(m)             __builtin_ast_member_type(m)
#define MemberOffset(m)           __builtin_ast_member_offset(m)
#define MemberIsBitfield(m)      __builtin_ast_member_is_bitfield(m)
#define MemberBitfieldWidth(m)   __builtin_ast_member_bitfield_width(m)

// Ticket #235: ForeachMember(type, varname, body) — host-side (comptime
// C) loop over the members of a struct/union type. `varname` is bound to
// each Member* in turn; `body` is a compound statement run once per
// member at macro-evaluation time (typically building AST nodes via
// BlockAddStmt etc.). Two-layer __COUNTER__ indirection gives each
// call-site unique loop-variable names so ForeachMember can be nested.
#define __builtin_foreach_member_body(type, varname, body, uid)              \
    do {                                                                  \
        Type *__builtin_fem_ty_##uid = (type);                           \
        int __builtin_fem_n_##uid = StructMemberCount(__builtin_fem_ty_##uid); \
        for (int __builtin_fem_i_##uid = 0; __builtin_fem_i_##uid < __builtin_fem_n_##uid; \
             __builtin_fem_i_##uid++) {                                      \
            Member *varname =                                         \
                StructMemberAt(__builtin_fem_ty_##uid, __builtin_fem_i_##uid); \
            body                                                          \
        }                                                                 \
    } while (0)
#define __builtin_foreach_member_uid(type, varname, body, uid)               \
    __builtin_foreach_member_body(type, varname, body, uid)
#define ForeachMember(type, varname, body)                             \
    __builtin_foreach_member_uid(type, varname, body, __COUNTER__)

// Ticket #235: OffsetofChain(ty, "a", "b", ...) — offsetof(ty, a.b) as an
// MakeIntLiteral AST node.
#define OffsetofChain(type, ...)                                       \
    MakeIntLiteral(__builtin_ast_offsetof_chain(VM, type,                   \
        (const char *[]){__VA_ARGS__},                                  \
        (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(const char *))))

#define FindGlobal(name)        __builtin_ast_find_global(VM, name)
#define GlobalCount()           __builtin_ast_global_count(VM)
#define GlobalAt(i)             __builtin_ast_global_at(VM, i)
#define ObjName(obj)            __builtin_ast_obj_name(obj)
#define ObjType(obj)            __builtin_ast_obj_type(obj)
#define ObjIsFunction(obj)     __builtin_ast_obj_is_function(obj)
#define ObjIsDefinition(obj)   __builtin_ast_obj_is_definition(obj)
#define ObjIsStatic(obj)       __builtin_ast_obj_is_static(obj)
#define GetAttrTargetKind(target)  __builtin_attr_target_kind(target)
#define AttrTargetName(target)  __builtin_attr_target_name(target)
#define $ATTR_TARGET_TYPE(target)  __builtin_attr_target_type(target)
#define AttrTargetObj(target)   __builtin_attr_target_obj(target)
#define AttrTargetToken(target) __builtin_attr_target_token(target)

#define MakeFunction(name, ret_type)                                       \
    __builtin_ast_function(VM, name, ret_type)
#define PublishNode(decl)                                                  \
    _Generic((decl),                                                        \
        Obj *: __builtin_ast_publish,                                          \
        Type *: __builtin_ast_publish_type                                     \
    )(VM, (decl), 0)
#define PublishNodeAt(decl, tok)                                          \
    _Generic((decl),                                                        \
        Obj *: __builtin_ast_publish,                                          \
        Type *: __builtin_ast_publish_type                                     \
    )(VM, (decl), (tok))
#define EmitDirective(line) __builtin_emit_directive(VM, line)
#define FunctionAddParam(fn, name, type)                             \
    __builtin_ast_function_add_param(VM, fn, name, type)
#define FunctionSetBody(fn, body)                                    \
    __builtin_ast_function_set_body(VM, fn, body)
#define FunctionSetStatic(fn, is_static)                             \
    __builtin_ast_function_set_static(fn, is_static)
#define FunctionSetInline(fn, is_inline)                             \
    __builtin_ast_function_set_inline(fn, is_inline)
#define FunctionSetVariadic(fn, is_variadic)                         \
    __builtin_ast_function_set_variadic(fn, is_variadic)

// Ticket #171: function forward declaration / prototype builder
// Creates a declaration-only Obj (no body); use FunctionAddParam for
// parameters and PublishNode to make it visible in scope.
#define FunctionPrototype(name, ret)                                  \
    __builtin_ast_function_prototype(VM, name, ret)

// Mode attribute registration for AST-generated functions.
// Ticket #619: generic attribute application and convenience shorthands.
// Use AddAttribute(fn, "cccc::test(suite=\"s\", timeout=5000)") for fine-grained control.
#define AddAttribute(fn, text)          __builtin_ast_add_attribute(VM, fn, text)
#define MarkAsTest(fn)                  AddAttribute(fn, "cccc::test")
#define MarkAsBuild(fn)                 AddAttribute(fn, "cccc::build")
#define MarkAsBuildTarget(fn, kind)     __builtin_ast_add_build_target_attr(VM, fn, kind)

// Ticket #171: struct/union/enum/typedef type builders
// Build a new named aggregate and expose it so GetType(name) resolves it.
//
//   Type *s = MakeStruct("Point");
//   StructAddField(s, "x", GetType("int"));
//   StructAddField(s, "y", GetType("int"));
//
// StructAddField works for both struct and union types.
// MakeTypedef registers name as an alias for underlying and returns it.
// EnumAddConstant adds a constant to the enum AND to scope (usable as int).
#define MakeStruct(name)     __builtin_ast_make_struct(VM, name)
#define MakeUnion(name)      __builtin_ast_make_union(VM, name)
#define __builtin_struct_add_field_2(name, field_type, _ignored)             \
    __builtin_ast_struct_add_current_field(VM, name, field_type)
#define __builtin_struct_add_field_3(ty, name, field_type)                   \
    __builtin_ast_struct_add_field(VM, ty, name, field_type)
#define StructAddField(...)                                           \
    __builtin_dispatch_3(__VA_ARGS__, __builtin_struct_add_field_3,              \
                     __builtin_struct_add_field_2)
#define MakeEnum(name)       __builtin_ast_make_enum(VM, name)
#define __builtin_enum_add_constant_2(name, value, _ignored)                 \
    __builtin_ast_enum_add_current_constant(VM, name, value)
#define __builtin_enum_add_constant_3(ty, name, value)                       \
    __builtin_ast_enum_add_constant(VM, ty, name, value)
#define EnumAddConstant(...)                                          \
    __builtin_dispatch_3(__VA_ARGS__, __builtin_enum_add_constant_3,             \
                     __builtin_enum_add_constant_2)
#define MakeTypedef(name, underlying) \
    __builtin_ast_make_typedef(VM, name, underlying)

// Comptime variable access (ticket #188)
/*!
 * @function __builtin_get_comptime_int
 * @abstract Read an integer-typed @c #pragma comptime variable's value at
 *           compile time.
 * @param vm The VM context.
 * @param name The comptime variable's name.
 * @return The 64-bit integer value, or 0 if the variable is not defined.
 * @discussion Convenience wrapper: GetComptimeInt(name).
 */
int64_t __builtin_get_comptime_int(VirtualMachine *vm, const char *name);
/*!
 * @function __builtin_get_comptime_float
 * @abstract Read a float/double-typed @c #pragma comptime variable's value
 *           at compile time.
 * @param vm The VM context.
 * @param name The comptime variable's name.
 * @return The double value, or 0.0 if the variable is not defined.
 * @discussion Convenience wrapper: GetComptimeFloat(name).
 */
double __builtin_get_comptime_float(VirtualMachine *vm, const char *name);
/*!
 * @function __builtin_get_comptime_var
 * @abstract Read a comptime scalar variable as an AST literal node.
 * @param vm The VM context.
 * @param name The comptime variable's name.
 * @return A NK_NUM node representing the variable's value, or NULL on error.
 * @discussion Convenience wrapper: GetComptimeVar(name).
 */
Node *__builtin_get_comptime_var(VirtualMachine *vm, const char *name);
/*!
 * @function __builtin_get_comptime_ptr
 * @abstract Return the address of a comptime variable as a generated-code AST
 *           pointer node.
 * @param vm The VM context.
 * @param name The comptime variable's name.
 * @return An NK_ADDR node pointing at a static shadow copy of the evaluated
 *         comptime variable, or NULL on error.
 * @discussion Convenience wrapper: GetComptimePtr(name).
 */
Node *__builtin_get_comptime_ptr(VirtualMachine *vm, const char *name);
/*!
 * @function __builtin_get_comptime_member
 * @abstract Read a named field from a comptime struct variable as an AST
 *           literal node.
 * @param vm The VM context.
 * @param var_name The comptime struct variable's name.
 * @param field The field name to look up.
 * @return A NK_NUM node for the field's value, or NULL on error.
 * @discussion Convenience wrapper: GetComptimeMember(var_name, field).
 */
Node *__builtin_get_comptime_member(VirtualMachine *vm, const char *var_name,
                                  const char *field);

#define GetComptimeInt(name)           __builtin_get_comptime_int(VM, name)
#define GetComptimeFloat(name)         __builtin_get_comptime_float(VM, name)
#define GetComptimeVar(name)           __builtin_get_comptime_var(VM, name)
#define GetComptimePtr(name)           __builtin_get_comptime_ptr(VM, name)
#define GetComptimeMember(var, field)  __builtin_get_comptime_member(VM, var, field)

// Constexpr variable access (ticket #189)
/*!
 * @function __builtin_get_constexpr_value
 * @abstract Read the evaluated initializer of a global @c constexpr variable
 *           as an AST literal node.
 * @param vm The VM context.
 * @param name The constexpr variable's name.
 * @return A NK_NUM node (integer or float, depending on the variable's type),
 *         or NULL on error.
 * @discussion Errors at compile time if @a name does not refer to a visible
 *             @c constexpr variable.  Convenience wrapper:
 *             GetConstexprValue(name).
 */
Node *__builtin_get_constexpr_value(VirtualMachine *vm, const char *name);

#define GetConstexprValue(name)  __builtin_get_constexpr_value(VM, name)

// ============================================================================
// Initializer Builders (ticket #296)
// ============================================================================

/**
 * @abstract Build a positional compound literal: zero-initialise an anonymous
 *           local var then positionally assign @a inits via node_expand_init_splice.
 * @note Requires function scope (file-scope not supported in V1).
 */
Node *__builtin_ast_compound_literal(VirtualMachine *vm, Type *ty, Node **inits, int n);

/**
 * @abstract Build an array compound literal.  Element type is explicit to avoid
 *           long-inference surprises with MakeIntLiteral.
 */
Node *__builtin_ast_init_array(VirtualMachine *vm, Type *elem_ty, Node **elems, int n);

/**
 * @abstract Build a designated struct/union initializer.  Unmentioned fields
 *           are zero-initialised.  Partial init (n < member count) is allowed.
 */
Node *__builtin_ast_init_struct(VirtualMachine *vm, Type *ty, const char **fields,
                                Node **values, int n);

/** Positional compound literal — element count inferred from __VA_ARGS__. */
#define CompoundLiteral(ty, ...)                                      \
    __builtin_ast_compound_literal(VM, ty,                                 \
        (Node *[]){__VA_ARGS__},                                     \
        (int)(sizeof((Node *[]){__VA_ARGS__}) / sizeof(Node *)))

/** Array compound literal with explicit element type. */
#define InitArray(elem_ty, ...)                                       \
    __builtin_ast_init_array(VM, elem_ty,                                  \
        (Node *[]){__VA_ARGS__},                                     \
        (int)(sizeof((Node *[]){__VA_ARGS__}) / sizeof(Node *)))

/** Designated struct/union init — fields and values are separate arrays. */
#define InitStruct(ty, fields, values, n)                             \
    __builtin_ast_init_struct(VM, ty, fields, values, n)

// ============================================================================
// Serialization (ticket #235)
// ============================================================================

/**
 * @abstract Build a block of memcpy() calls copying expr (of type ty)
 *           byte-for-byte into buf (a void or char pointer). Struct/union
 *           types are copied member-by-member at their natural offsets,
 *           recursing into nested flat structs; scalar types are copied
 *           in one memcpy.
 * @note V1 placeholder: pointer-typed members are copied as raw pointer
 *       bytes, not followed.
 */
Node *__builtin_ast_serialize(VirtualMachine *vm, Type *ty, Node *expr, Node *buf);

/**
 * @abstract Build `*(ty*)buf` — reinterpret buf as a ty value.
 * @note V1 placeholder: inherits the host's alignment requirements for ty;
 *       if Serialize ever produces a packed/portable layout this must
 *       change to field-by-field reconstruction.
 */
Node *__builtin_ast_deserialize(VirtualMachine *vm, Type *ty, Node *buf);

#define Serialize(ty, expr, buf)   __builtin_ast_serialize(VM, ty, expr, buf)
#define Deserialize(ty, buf)       __builtin_ast_deserialize(VM, ty, buf)

// Global variable generation (ticket #152)
#define GlobalVar(name, ty)                                           \
    __builtin_ast_global_var(VM, name, ty)
#define GlobalVarSetInitData(var, data, len)                       \
    __builtin_ast_global_var_set_init_data(VM, var, data, len)
#define GlobalVarSetStatic(var, is_static)                          \
    __builtin_ast_global_var_set_static(var, is_static)

// Function-building context (ticket #148)
// Usage:
//   WithFn(fn) {
//       FunctionSetBody(fn, Quote("return 42;"));
//   }
// Inside the block, current_fn is set to fn so Quote("return x;") casts
// to the correct return type.  The pop always runs even on early exit.
#define WithFn(fn)                                                    \
    for (int _cccc_fn_ctx_ = (__builtin_ast_push_fn(VM, (fn)), 1);             \
         _cccc_fn_ctx_;                                                      \
         _cccc_fn_ctx_ = (__builtin_ast_pop_fn(VM), 0))

#define WithBlock(block)                                               \
    for (int _cccc_block_ctx_ = (__builtin_ast_push_block(VM, (block)), 1);  \
         _cccc_block_ctx_;                                                \
         _cccc_block_ctx_ = (__builtin_ast_pop_block(VM), 0))

#define WithStruct(ty)                                                 \
    for (int _cccc_struct_ctx_ = (__builtin_ast_push_struct(VM, (ty)), 1);   \
         _cccc_struct_ctx_;                                               \
         _cccc_struct_ctx_ = (__builtin_ast_pop_struct(VM), 0))

#define WithSwitch(sw)                                                 \
    for (int _cccc_switch_ctx_ = (__builtin_ast_push_switch(VM, (sw)), 1);   \
         _cccc_switch_ctx_;                                               \
         _cccc_switch_ctx_ = (__builtin_ast_pop_switch(VM), 0))

#define WithEnum(ty)                                                   \
    for (int _cccc_enum_ctx_ = (__builtin_ast_push_enum(VM, (ty)), 1);       \
         _cccc_enum_ctx_;                                                 \
         _cccc_enum_ctx_ = (__builtin_ast_pop_enum(VM), 0))

// ============================================================================
// Macro Standard Library Attributes (ticket #235)
// ============================================================================

// @serialize struct Foo {...}; publishes
//   int Foo_serialize(struct Foo *self, void *buf);
// which copies *self into buf via Serialize and returns sizeof(struct Foo).
@comptime(attribute("serialize"))
void __builtin_attr_serialize(AttrTarget *target) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_TYPE)
        MacroErrorAt(0, "@serialize expects a type (struct/union) target");

    Type *ty = $ATTR_TARGET_TYPE(target);
    const char *tname = AttrTargetName(target);
    if (!ty || !tname)
        MacroErrorAt(0, "@serialize target has no usable type/name");

    char fname[128];
    strcpy(fname, tname);
    strcat(fname, "_serialize");

    Obj *fn = MakeFunction(fname, GetType("int"));
    FunctionAddParam(fn, "self", MakePointer(ty));
    FunctionAddParam(fn, "buf", MakePointer(GetType("void")));
    WithFn(fn) {
        Node *self = MakeUnary(NK_DEREF, MakeParamRef(fn, "self"));
        Node *buf = MakeParamRef(fn, "buf");
        Node *block = Serialize(ty, self, buf);
        BlockAddStmt(block, MakeReturn(MakeIntLiteral(TypeSize(ty))));
        FunctionSetBody(fn, block);
    }
    PublishNode(fn);
}

// @deserialize struct Foo {...}; publishes
//   struct Foo Foo_deserialize(void *buf);
// which reconstructs a struct Foo from buf via Deserialize.
@comptime(attribute("deserialize"))
void __builtin_attr_deserialize(AttrTarget *target) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_TYPE)
        MacroErrorAt(0, "@deserialize expects a type (struct/union) target");

    Type *ty = $ATTR_TARGET_TYPE(target);
    const char *tname = AttrTargetName(target);
    if (!ty || !tname)
        MacroErrorAt(0, "@deserialize target has no usable type/name");

    char fname[128];
    strcpy(fname, tname);
    strcat(fname, "_deserialize");

    Obj *fn = MakeFunction(fname, ty);
    FunctionAddParam(fn, "buf", MakePointer(GetType("void")));
    WithFn(fn) {
        Node *buf = MakeParamRef(fn, "buf");
        FunctionSetBody(fn, MakeReturn(Deserialize(ty, buf)));
    }
    PublishNode(fn);
}

// @enum_to_string enum Color {...}; publishes
//   const char *Color_to_string(enum Color v);
// which switches over v and returns the matching constant's name, or "" if
// no constant matches.
@comptime(attribute("enum_to_string"))
void __builtin_attr_enum_to_string(AttrTarget *target) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_TYPE)
        MacroErrorAt(0, "@enum_to_string expects a type (enum) target");

    Type *ty = $ATTR_TARGET_TYPE(target);
    const char *tname = AttrTargetName(target);
    if (!ty || !tname)
        MacroErrorAt(0, "@enum_to_string target has no usable type/name");
    if (GetTypeKind(ty) != TK_ENUM)
        MacroErrorAt(0, "@enum_to_string expects an enum target");

    char fname[128];
    strcpy(fname, tname);
    strcat(fname, "_to_string");

    Type *cstr_ty = MakePointer(MakeConst(GetType("char")));
    Obj *fn = MakeFunction(fname, cstr_ty);
    FunctionAddParam(fn, "v", ty);
    WithFn(fn) {
        Node *v = MakeParamRef(fn, "v");
        FunctionSetBody(fn, EnumToString(ty, v));
    }
    PublishNode(fn);
}

// @enum_from_string enum Color {...}; publishes
//   enum Color Color_from_string(const char *s);
// which compares s against each constant's name and returns the matching
// value, or -1 if no constant matches.
@comptime(attribute("enum_from_string"))
void __builtin_attr_enum_from_string(AttrTarget *target) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_TYPE)
        MacroErrorAt(0, "@enum_from_string expects a type (enum) target");

    Type *ty = $ATTR_TARGET_TYPE(target);
    const char *tname = AttrTargetName(target);
    if (!ty || !tname)
        MacroErrorAt(0, "@enum_from_string target has no usable type/name");
    if (GetTypeKind(ty) != TK_ENUM)
        MacroErrorAt(0, "@enum_from_string expects an enum target");

    char fname[128];
    strcpy(fname, tname);
    strcat(fname, "_from_string");

    Type *cstr_ty = MakePointer(MakeConst(GetType("char")));
    Obj *fn = MakeFunction(fname, ty);
    FunctionAddParam(fn, "s", cstr_ty);
    WithFn(fn) {
        Node *s = MakeParamRef(fn, "s");
        FunctionSetBody(fn, EnumFromString(ty, s));
    }
    PublishNode(fn);
}

// Ticket #235: GenerateGetters/GenerateSetters/GenerateConstructor
// helpers. These are plain @macro functions (no attribute name), compiled
// into the same macro program as the @generate_* attribute handlers below
// and called from them as ordinary functions.

// __builtin_generate_getters(ty): for each member of struct/union `ty`,
// publishes `<MemberType> get_<field>(<ty> *self) { return self->field; }`.
@comptime
void __builtin_generate_getters(Type *ty) {
    ForeachMember(ty, m, {
        const char *field = MemberName(m);
        Type *fty = MemberType(m);

        char gname[128];
        strcpy(gname, "get_");
        strcat(gname, field);

        Obj *fn = MakeFunction(gname, fty);
        FunctionAddParam(fn, "self", MakePointer(ty));
        WithFn(fn) {
            Node *self = MakeUnary(NK_DEREF, MakeParamRef(fn, "self"));
            FunctionSetBody(fn, MakeReturn(MakeMember(self, field)));
        }
        PublishNode(fn);
    });
}

// __builtin_generate_setters(ty): for each member of struct/union `ty`,
// publishes `void set_<field>(<ty> *self, <MemberType> value) { self->field = value; }`.
@comptime
void __builtin_generate_setters(Type *ty) {
    ForeachMember(ty, m, {
        const char *field = MemberName(m);
        Type *fty = MemberType(m);

        char gname[128];
        strcpy(gname, "set_");
        strcat(gname, field);

        Obj *fn = MakeFunction(gname, GetType("void"));
        FunctionAddParam(fn, "self", MakePointer(ty));
        FunctionAddParam(fn, "value", fty);
        WithFn(fn) {
            Node *self = MakeUnary(NK_DEREF, MakeParamRef(fn, "self"));
            Node *value = MakeParamRef(fn, "value");
            FunctionSetBody(fn, MakeExprStmt(MakeAssign(MakeMember(self, field), value)));
        }
        PublishNode(fn);
    });
}

// __builtin_generate_constructor(ty, tname): publishes
// `<ty> <tname>_create(<member1>, <member2>, ...) { return (ty){ .member1 =
// member1, ... }; }` with one parameter per member of `ty`.
@comptime
void __builtin_generate_constructor(Type *ty, const char *tname) {
    char gname[128];
    strcpy(gname, tname);
    strcat(gname, "_create");

    Obj *fn = MakeFunction(gname, ty);

    ForeachMember(ty, m, {
        FunctionAddParam(fn, MemberName(m), MemberType(m));
    });

    WithFn(fn) {
        const char *fields[64];
        Node *values[64];
        int n = 0;
        ForeachMember(ty, m, {
            fields[n] = MemberName(m);
            values[n] = MakeParamRef(fn, MemberName(m));
            n++;
        });
        FunctionSetBody(fn, MakeReturn(InitStruct(ty, fields, values, n)));
    }
    PublishNode(fn);
}

// @generate_getters struct Foo {...}; publishes get_<field>(struct Foo *self)
// for each member, returning self->field.
@comptime(attribute("generate_getters"))
void __builtin_attr_generate_getters(AttrTarget *target) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_TYPE)
        MacroErrorAt(0, "@generate_getters expects a type (struct/union) target");

    Type *ty = $ATTR_TARGET_TYPE(target);
    if (!ty)
        MacroErrorAt(0, "@generate_getters target has no usable type");

    __builtin_generate_getters(ty);
}

// @generate_setters struct Foo {...}; publishes set_<field>(struct Foo *self,
// <FieldType> value) for each member, assigning self->field = value.
@comptime(attribute("generate_setters"))
void __builtin_attr_generate_setters(AttrTarget *target) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_TYPE)
        MacroErrorAt(0, "@generate_setters expects a type (struct/union) target");

    Type *ty = $ATTR_TARGET_TYPE(target);
    if (!ty)
        MacroErrorAt(0, "@generate_setters target has no usable type");

    __builtin_generate_setters(ty);
}

// @generate_constructor struct Foo {...}; publishes
//   struct Foo Foo_create(<member1>, <member2>, ...);
// returning a struct Foo initialized from the given member values.
@comptime(attribute("generate_constructor"))
void __builtin_attr_generate_constructor(AttrTarget *target) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_TYPE)
        MacroErrorAt(0, "@generate_constructor expects a type (struct/union) target");

    Type *ty = $ATTR_TARGET_TYPE(target);
    const char *tname = AttrTargetName(target);
    if (!ty || !tname)
        MacroErrorAt(0, "@generate_constructor target has no usable type/name");

    __builtin_generate_constructor(ty, tname);
}

// Ticket #235: GenerateSum/GenerateMap/GenerateReduce/GenerateFilter
// -- FP-style array generators. Each publishes a single function named after
// elem_ty's spelling (via TypeCName, since builtin scalar types have no
// TypeName): sum_<T>, map_<T>, reduce_<T>, filter_<T>. Implemented as plain
// C functions in reflection.c (registered via cc_register_cfunc) rather than
// @macro functions, so they're callable from any [[cccc::comptime]] context,
// not just from within reflection.h's own macro program.

/*!
 * @function __builtin_generate_sum
 * @abstract Publish `T sum_T(T *arr, size_t n)` summing all elements.
 * @discussion Convenience wrapper: GenerateSum(elem_type).
 */
void __builtin_generate_sum(VirtualMachine *vm, Type *elem_ty);

/*!
 * @function __builtin_generate_map
 * @abstract Publish `void map_T(T *arr, size_t n, T *out, T (*f)(T))`,
 *   writing `f(arr[i])` into `out[i]` for each element.
 * @discussion Convenience wrapper: GenerateMap(elem_type).
 */
void __builtin_generate_map(VirtualMachine *vm, Type *elem_ty);

/*!
 * @function __builtin_generate_reduce
 * @abstract Publish `T reduce_T(T *arr, size_t n, T init, T (*f)(T, T))`,
 *   folding `f` over the array starting from `init`.
 * @discussion Convenience wrapper: GenerateReduce(elem_type).
 */
void __builtin_generate_reduce(VirtualMachine *vm, Type *elem_ty);

/*!
 * @function __builtin_generate_filter
 * @abstract Publish `void filter_T(T *arr, size_t n, T *out, size_t *out_n,
 *   bool (*pred)(T))`, writing elements matching `pred` into `out` and
 *   setting `*out_n` to the match count.
 * @discussion Convenience wrapper: GenerateFilter(elem_type).
 */
void __builtin_generate_filter(VirtualMachine *vm, Type *elem_ty);

#define GenerateSum(elem_type)    __builtin_generate_sum(VM, elem_type)
#define GenerateMap(elem_type)    __builtin_generate_map(VM, elem_type)
#define GenerateReduce(elem_type) __builtin_generate_reduce(VM, elem_type)
#define GenerateFilter(elem_type) __builtin_generate_filter(VM, elem_type)

#ifdef __cplusplus
}
#endif

#endif // CCCC_REFLECTION_H
