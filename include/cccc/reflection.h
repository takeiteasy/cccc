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
 * Functions that need VM context read it internally from a process-global
 * set by the compiler for the duration of macro execution -- macro authors
 * never see or pass a VM object.
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

/*! @brief Opaque compiler/VM instance. Macro authors never see or pass this;
 *           functions that need it read it internally from a process-global
 *           set for the duration of macro execution. */
typedef struct VirtualMachine VirtualMachine;
typedef struct VirtualMachine VirtualMachine;

/*! @brief Opaque compiler type descriptor (primitive, pointer, array,
 *           function, struct/union, or enum). Inspect it with the
 *           @c GetTypeKind / @c Type* family of macros. */
typedef struct Type Type;

/*! @brief Opaque AST node (expression, statement, or literal). Built with
 *           the @c Make* family of macros and spliced into the compiled
 *           program by returning it from a pragma macro or via PublishNode. */
typedef struct Node Node;

/*! @brief Opaque global symbol (a function or global variable). Created by
 *           @c MakeFunction / @c GlobalVar, or looked up with @c FindGlobal /
 *           @c GlobalAt. */
typedef struct Obj Obj;

/*! @brief Opaque struct/union member descriptor, returned by
 *           @c StructMemberAt / @c StructMemberFind and inspected with the
 *           @c Member* family of macros. */
typedef struct Member Member;

/*! @brief Opaque enum constant descriptor, returned by @c EnumAt / @c EnumFind
 *           and inspected with @c EnumConstantName / @c EnumConstantValue. */
typedef struct EnumConstant EnumConstant;

/*! @brief Opaque source-location handle attached to AST nodes for
 *           diagnostics. Obtained via @c CurrentToken / @c SyntheticToken /
 *           @c TokenFromNode and attached with @c SetToken. */
typedef struct Token Token;

/*! @brief Opaque handle to the declaration a custom @c \@attribute handler
 *           was invoked on. Inspected with the @c AttrTarget* family of
 *           macros inside a function registered via
 *           @c \@comptime(attribute("...")). */
typedef struct AttrTarget AttrTarget;

/*! @brief Discriminates the kind of a Type: primitive, pointer, array,
 *           function, struct/union, or enum.
 * @details Mirrors the subset of cccc.h's internal @c TypeKind relevant to
 *             pragma macros. Query it via @c GetTypeKind(ty). */
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

/*! @brief Discriminates the operator/statement kind of a Node, e.g. as passed
 *           to @c MakeBinary / @c MakeUnary or matched against a node's
 *           @c ->kind field.
 * @details A subset of cccc.h's internal @c ND_* node kinds relevant to
 *             pragma macros; the numeric values match cccc.h's @c NodeKind
 *             exactly, so the gaps (33-36, 39, 41, 44-50) are internal-only
 *             kinds not exposed here, not omissions. */
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

/*! @brief Discriminates the kind of declaration an AttrTarget refers to,
 *           returned by @c GetAttrTargetKind(target) inside a custom
 *           @c \@attribute handler. */
typedef enum {
    ATTR_TARGET_TYPEDEF = 1,
    ATTR_TARGET_TYPE = 2,
    ATTR_TARGET_FUNCTION = 3,
    ATTR_TARGET_GLOBAL = 4,
} AttrTargetKind;

/*! @brief Generate a unique identifier string for macro-created symbols.
 * @param prefix Prefix for the generated name.
 * @return An arena-allocated string of the form "<prefix>__<n>".
 * @details Convenience wrapper: Gensym(prefix).
 */
const char *__builtin_gensym(const char *prefix);

/*! @brief Lisp-style single-step macro expansion (macroexpand-1 semantics).
 * @details If @a node is an @c NK_MACRO_CALL node, execute the macro once
 *             and return the resulting node without splicing it into the AST
 *             or recursing into nested macro calls. If @a node is not a macro
 *             call, it is returned unchanged (identity).
 *             Convenience wrapper: MacroExpand1(node).
 * @param node The node to (possibly) expand.
 * @return The expanded node, or @a node itself if it is not a macro call.
 */
Node *__builtin_macroexpand_1(Node *node);

/*! @brief Lisp-style full macro expansion.
 * @details Repeatedly calls @c __builtin_macroexpand_1 on the top-level node
 *             until it is no longer an @c NK_MACRO_CALL (i.e. the form is
 *             stable). Does not recurse into child nodes. Respects the VM's
 *             @c macro_recursion_limit.  Convenience wrapper: MacroExpand(node).
 * @param node The node to fully expand.
 * @return The fully expanded node, or @a node itself if it is not a macro call.
 */
Node *__builtin_macroexpand(Node *node);

/*! @brief Return the number of variadic arguments for the active macro call.
 * @return Number of arguments after the fixed parameters.
 */
int __builtin_ast_vararg_count(void);

/*! @brief Return an inline macro's variadic AST argument by zero-based index.
 * @param index Zero-based variadic argument index.
 * @return The argument node.
 * @details Emits a compile-time error if @a index is out of range or the
 *             active macro call is a global-generation string macro.
 */
Node *__builtin_ast_vararg_at(int index);

/*! @brief Return an inline macro's variadic AST arguments as an array.
 * @return Borrowed array of variadic argument nodes, or NULL when the active
 *         inline macro call has no variadic arguments.
 * @details The returned array is read-only and valid only for the current
 *             macro call's lifetime. It shares nodes with the original macro
 *             arguments and does not consume or clone them. Emits a
 *             compile-time error if the active macro call is a
 *             global-generation string macro.
 */
Node **__builtin_ast_varargs_as_array(void);

/*! @brief Return a global-generation macro's stringified variadic argument.
 * @param index Zero-based variadic argument index.
 * @return The stringified token argument.
 * @details Emits a compile-time error if @a index is out of range or the
 *             active macro call is an inline AST macro.
 */
const char *__builtin_ast_vararg_str_at(int index);

// ============================================================================
// Generated Node Source Locations (ticket #173)
// ============================================================================

/*! @brief Return the token for the macro invocation currently being executed.
 * @return Opaque token for the active macro call site, or NULL outside macro
 *         execution.
 * @details Convenience wrapper: CurrentToken().
 */
Token *__builtin_ast_current_token(void);

/*! @brief Create an opaque synthetic source token for generated AST nodes.
 * @param label Short diagnostic label for the synthetic location.
 * @return Arena-allocated synthetic token, or NULL on error.
 * @details Use this when a generated node should diagnose against a stable
 *             generated location instead of the macro call or an input node.
 *             Convenience wrapper: SyntheticToken(label).
 */
Token *__builtin_ast_synthetic_token(const char *label);

/*! @brief Return the opaque source token attached to a node.
 * @param node Node to inspect.
 * @return The node token, or NULL.
 * @details Convenience wrapper: TokenFromNode(node).
 */
Token *__builtin_ast_token_from_node(Node *node);

/*! @brief Attach an opaque source token to a node.
 * @param node Node to update.
 * @param tok Token from __builtin_ast_current_token(),
 *            __builtin_ast_synthetic_token(), or __builtin_ast_token_from_node().
 * @return node, for chaining.
 * @details Convenience wrapper: SetToken(node, tok).
 */
Node *__builtin_ast_set_token(Node *node, Token *tok);

/*! @brief Copy the source token from one node to another.
 * @param dst Generated node to update.
 * @param src Source node whose location should be reused.
 * @return dst, for chaining.
 * @details Convenience wrapper: CopyLocation(dst, src).
 */
Node *__builtin_ast_copy_location(Node *dst, Node *src);

// ============================================================================
// Macro Diagnostics (ticket #78)
// ============================================================================

/*! @brief Emit a compiler error pointing at the source location of a node.
 * @param node A node whose tok field provides file/line/col. May be NULL
 *             (falls back to a location-less error).
 * @param fmt printf-style format string, followed by format arguments.
 * @details Behaves like the compiler's error_tok(): in normal mode it
 *             prints the error with file/line/col and source snippet then
 *             aborts via longjmp or exit.  When vm->collect_errors is set
 *             it records the error and compilation may continue.
 *             Convenience wrapper: MacroErrorAt(node, ...).
 */
void __builtin_macro_error_at(Node *node, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/*! @brief Emit a compiler warning pointing at the source location of a node.
 * @param node A node whose tok field provides file/line/col. May be NULL.
 * @param fmt printf-style format string, followed by format arguments.
 * @details Emitted only when -Wcccc-macro is enabled. Non-fatal unless
 *             promoted with -Werror or -Werror=cccc-macro.
 *             Convenience wrapper: MacroWarningAt(node, ...).
 */
void __builtin_macro_warning_at(Node *node, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/*! @brief Parse a C code template string into an AST node, substituting
 *           splice points with the provided argument nodes.
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
 * @details Template is parsed and substituted at macro-execution (compile)
 *             time; there is no runtime overhead.  Expressions and statements
 *             are auto-detected. The variadic form supports up to 64 splice
 *             nodes; use __builtin_quote_n for larger node arrays.
 *             Convenience wrapper: Quote(tmpl, ...).
 */
Node *__builtin_quote(const char *tmpl, ...);

/*! @brief Array-form quasi-quote that validates the caller-provided splice
 *           count and supports larger node arrays than the variadic form.
 * @param tmpl A C expression or statement as a string literal with `$N` / `$@N`
 *             splice points.
 * @param nodes Array of Node* splice arguments.
 * @param count Length of the nodes array.  If any $K in the template exceeds
 *              count, a compile-time error is emitted.
 * @return The parsed and substituted AST node, or NULL on error.
 * @details Convenience wrapper: QuoteN(tmpl, nodes, count).
 */
Node *__builtin_quote_n(const char *tmpl, Node **nodes, int count);

/*! @brief Build a `->next`-linked node chain from an array, returning the
 *           head.  Use the result as the argument to a `$@k` list splice.
 * @param nodes Array of Node* to link together.  Linking stops at the first
 *              NULL element or at count, whichever comes first.
 * @param count Number of elements in the array.
 * @return Head of the chain, or NULL if count == 0 or nodes is NULL.
 * @details A single node is a valid chain of length 1.  An existing
 *             `->next` chain (e.g. `__builtin_ast_block(...)->body`) can also be
 *             passed directly as the splice argument without going through
 *             this helper.  Convenience wrapper: NodeList(nodes, count).
 */
Node *__builtin_node_list(Node **nodes, int count);

// ============================================================================
// Type Lookup and Introspection
// ============================================================================

/*! @brief Look up a type by tag name (struct/union/enum).
 * @param name The tag name to look up.
 * @return The matching Type*, or NULL if not found.
 * @details Convenience wrapper: FindType(name).
 */
Type *__builtin_ast_find_type(const char *name);

/*! @brief Check whether a type is currently in scope by name.
 * @param name The type name to look up.
 * @return True if the name resolves to a type, false otherwise.
 * @details Convenience wrapper: TypeExists(name).
 */
bool __builtin_ast_type_exists(const char *name);

/*! @brief Look up a type by name, falling back to the built-in primitives.
 * @param name The type name to look up.
 * @return The matching Type*, or NULL if not found.
 * @details Convenience wrapper: GetType(name).
 */
Type *__builtin_ast_get_type(const char *name);

/*! @brief Return the TypeKind tag of a type.
 * @param ty The type to inspect.
 * @return The type kind (TK_INT, TK_STRUCT, TK_PTR, ...).
 * @details Convenience wrapper: GetTypeKind(ty).
 */
TypeKind __builtin_ast_type_kind(Type *ty);

/*! @brief Return sizeof(ty) in bytes.
 * @param ty The type to inspect.
 * @return The size in bytes.
 * @details Convenience wrapper: TypeSize(ty).
 */
int __builtin_ast_type_size(Type *ty);

/*! @brief Return _Alignof(ty) in bytes.
 * @param ty The type to inspect.
 * @return The alignment in bytes.
 * @details Convenience wrapper: TypeAlign(ty).
 */
int __builtin_ast_type_align(Type *ty);

/*! @brief Test whether an integer type is unsigned.
 * @param ty The type to inspect.
 * @return True for unsigned integer types, false otherwise.
 * @details Convenience wrapper: TypeIsUnsigned(ty).
 */
bool __builtin_ast_type_is_unsigned(Type *ty);

/*! @brief Test whether a type is const-qualified.
 * @param ty The type to inspect.
 * @return True if ty has a const qualifier, false otherwise.
 * @details Convenience wrapper: TypeIsConst(ty).
 */
bool __builtin_ast_type_is_const(Type *ty);

/*! @brief Return the element type of a pointer or array.
 * @param ty The pointer/array type to inspect.
 * @return The base Type*, or NULL if ty is not a pointer or array.
 * @details Convenience wrapper: TypeBase(ty).
 */
Type *__builtin_ast_type_base(Type *ty);

/*! @brief Return the fixed length of an array type.
 * @param ty The array type to inspect.
 * @return The element count for TY_ARRAY types, -1 otherwise.
 * @details Convenience wrapper: TypeArrayLen(ty).
 */
int __builtin_ast_type_array_len(Type *ty);

/*! @brief Return the return type of a function type.
 * @param ty The function type to inspect.
 * @return The return Type*, or NULL if ty is not a function type.
 * @details Convenience wrapper: TypeReturnType(ty).
 */
Type *__builtin_ast_type_return_type(Type *ty);

/*! @brief Return the number of declared parameters of a function type.
 * @param ty The function type to inspect.
 * @return The parameter count for TY_FUNC types, -1 otherwise.
 * @details Convenience wrapper: TypeParamCount(ty).
 */
int __builtin_ast_type_param_count(Type *ty);

/*! @brief Return the type of the parameter at the given index.
 * @param ty The function type to inspect.
 * @param index Zero-based parameter index.
 * @return The parameter's Type*, or NULL on out-of-range or non-function ty.
 * @details Convenience wrapper: TypeParamAt(ty, index).
 */
Type *__builtin_ast_type_param_at(Type *ty, int index);

/*! @brief Test whether a function type is variadic.
 * @param ty The type to inspect.
 * @return True for variadic function types, false otherwise.
 * @details Convenience wrapper: TypeIsVariadic(ty).
 */
bool __builtin_ast_type_is_variadic(Type *ty);

/*! @brief Return the user-visible name of a type, if any.
 * @param ty The type to inspect.
 * @return A freshly-allocated NUL-terminated string, or NULL for anonymous types.
 * @details Convenience wrapper: TypeName(ty).
 */
const char *__builtin_ast_type_name(Type *ty);

/*! @brief Return a valid C identifier fragment naming `ty`.
 * @param ty The type to inspect.
 * @return TypeName(ty) for named types, or a builtin spelling ("int",
 *   "double", "ulong", ...) for builtin scalar types, or NULL.
 * @details Convenience wrapper: TypeCName(ty). Intended for naming
 *   generated functions, e.g. sum_<T> from GenerateSum(elem_type).
 */
const char *__builtin_ast_type_c_name(Type *ty);

/*! @brief Build a pointer-to-base type.
 * @param base The pointed-to type.
 * @return A Type* representing "base *", or NULL on error.
 * @details Convenience wrapper: MakePointer(base).
 */
Type *__builtin_ast_make_pointer(Type *base);

/*! @brief Build a fixed-length array type.
 * @param base The element type.
 * @param length The element count.
 * @return A Type* representing "base[length]", or NULL on error.
 * @details Convenience wrapper: MakeArray(base, length).
 */
Type *__builtin_ast_make_array(Type *base, int length);

/*! @brief Build a pointer-to-function type, e.g. "T (*)(T)".
 * @param return_ty The function's return type.
 * @param param_types Array of parameter types (each copy_type()'d internally).
 * @param nparams Number of entries in param_types (max 16).
 * @return A Type* representing "return_ty (*)(param_types...)", or NULL on error.
 * @details Convenience wrapper: MakeFuncPtrType(return_ty, param_types, nparams).
 */
Type *__builtin_ast_make_func_ptr_type(Type *return_ty,
                                        Type **param_types, int nparams);

// Ticket #171: qualified type constructors
/*! @brief Return a const-qualified copy of ty.
 * @param ty The type to qualify.
 * @return A const-qualified Type*, or NULL on error.
 * @details Convenience wrapper: MakeConst(ty).
 */
Type *__builtin_ast_make_const(Type *ty);
/*! @brief Return a volatile-qualified copy of ty.
 * @param ty The type to qualify.
 * @return A volatile-qualified Type*, or NULL on error.
 * @details Convenience wrapper: MakeVolatile(ty).
 */
Type *__builtin_ast_make_volatile(Type *ty);

// ============================================================================
// Enum Reflection
// ============================================================================

/*! @brief Return the number of constants in an enum type.
 * @param enum_type The enum type to inspect.
 * @return The constant count, or -1 if enum_type is not an enum.
 * @details Convenience wrapper: EnumCount(ty).
 */
int __builtin_ast_enum_count(Type *enum_type);

/*! @brief Return the enum constant at a given index.
 * @param enum_type The enum type to inspect.
 * @param index Zero-based index.
 * @return The EnumConstant*, or NULL on out-of-range or non-enum.
 * @details Convenience wrapper: EnumAt(ty, index).
 */
EnumConstant *__builtin_ast_enum_at(Type *enum_type, int index);

/*! @brief Look up an enum constant by name.
 * @param enum_type The enum type to search.
 * @param name The constant name to look up.
 * @return The matching EnumConstant*, or NULL if not found.
 * @details Convenience wrapper: EnumFind(ty, name).
 */
EnumConstant *__builtin_ast_enum_find(Type *enum_type,
                                    const char *name);

/*! @brief Return the name of an enum constant.
 * @param ec The enum constant.
 * @return A NUL-terminated string owned by ec.
 * @details Convenience wrapper: EnumConstantName(ec).
 */
const char *__builtin_ast_enum_constant_name(EnumConstant *ec);

/*! @brief Return the integer value of an enum constant.
 * @param ec The enum constant.
 * @return The constant's integer value (int64_t to support C23 wide underlying types).
 * @details Convenience wrapper: EnumConstantValue(ec).
 */
int64_t __builtin_ast_enum_constant_value(EnumConstant *ec);

/*! @brief Return the tag name of an enum type.
 * @param e The enum type to inspect.
 * @return A NUL-terminated string owned by e.
 * @details Convenience wrapper: EnumName(ty).
 */
const char *__builtin_ast_enum_name(Type *e);

/*! @brief Return the number of values in an enum type.
 * @param e The enum type to inspect.
 * @return The constant count, or -1 if e is not an enum.
 * @details Convenience wrapper: EnumValueCount(ty).
 */
int __builtin_ast_enum_value_count(Type *e);

/*! @brief Return the name of the enum constant at the given index.
 * @param e The enum type to inspect.
 * @param index Zero-based index.
 * @return A NUL-terminated string, or NULL on out-of-range.
 * @details Convenience wrapper: EnumValueName(ty, index).
 */
const char *__builtin_ast_enum_value_name(Type *e, int index);

/*! @brief Return the integer value of the enum constant at the given index.
 * @param e The enum type to inspect.
 * @param index Zero-based index.
 * @return The constant's integer value (int64_t), or -1 on out-of-range.
 * @details Convenience wrapper: EnumValue(ty, index).
 */
int64_t __builtin_ast_enum_value(Type *e, int index);

/*! @brief Build a `switch (expr) { case V0: return "Name0"; ...
 *           default: return ""; }` over the constants of an enum type.
 * @param ty The enum type.
 * @param expr The expression to switch on (the enum value).
 * @return An NK_SWITCH node. Caller wraps it in a function returning
 *         `const char *`.
 * @details Convenience wrapper: EnumToString(ty, expr).
 */
Node *__builtin_ast_enum_to_string_switch(Type *ty, Node *expr);

/*! @brief Build a block of `if (strcmp(expr, "Name0") == 0) return V0; ...
 *           return -1;` over the constants of an enum type.
 * @param ty The enum type.
 * @param expr The expression to compare (a `const char *`).
 * @return An NK_BLOCK node. Caller wraps it in a function returning the
 *         enum type (or an int).
 * @details Convenience wrapper: EnumFromString(ty, expr).
 */
Node *__builtin_ast_enum_from_string_chain(Type *ty, Node *expr);

// ============================================================================
// Struct/Union Member Introspection
// ============================================================================

/*! @brief Return the number of members of a struct or union type.
 * @param struct_type The struct or union type to inspect.
 * @return The member count, or -1 if struct_type is not a struct/union.
 * @details Convenience wrapper: StructMemberCount(ty).
 */
int __builtin_ast_struct_member_count(Type *struct_type);

/*! @brief Return the member at the given index.
 * @param struct_type The struct or union type to inspect.
 * @param index Zero-based member index.
 * @return The Member*, or NULL on out-of-range or non-aggregate.
 * @details Convenience wrapper: StructMemberAt(ty, index).
 */
Member *__builtin_ast_struct_member_at(Type *struct_type,
                                        int index);

/*! @brief Look up a struct or union member by name.
 * @param struct_type The struct or union type to search.
 * @param name The member name to look up.
 * @return The matching Member*, or NULL if not found.
 * @details Convenience wrapper: StructMemberFind(ty, name).
 */
Member *__builtin_ast_struct_member_find(Type *struct_type,
                                        const char *name);

/*! @brief Return the name of a struct/union member.
 * @param m The member to inspect.
 * @return A NUL-terminated string owned by m.
 * @details Convenience wrapper: MemberName(m).
 */
const char *__builtin_ast_member_name(Member *m);

/*! @brief Return the type of a struct/union member.
 * @param m The member to inspect.
 * @return The member's Type*.
 * @details Convenience wrapper: MemberType(m).
 */
Type *__builtin_ast_member_type(Member *m);

/*! @brief Return the byte offset of a struct/union member.
 * @param m The member to inspect.
 * @return The offset in bytes.
 * @details Convenience wrapper: MemberOffset(m).
 */
int __builtin_ast_member_offset(Member *m);

/*! @brief Test whether a member is a bitfield.
 * @param m The member to inspect.
 * @return True if the member is a bitfield, false otherwise.
 * @details Convenience wrapper: MemberIsBitfield(m).
 */
bool __builtin_ast_member_is_bitfield(Member *m);

/*! @brief Return the bit width of a bitfield member.
 * @param m The member to inspect.
 * @return The bit width for bitfield members, 0 otherwise.
 * @details Convenience wrapper: MemberBitfieldWidth(m).
 */
int __builtin_ast_member_bitfield_width(Member *m);

/*! @brief Compute the byte offset of a (possibly nested) member chain.
 * @param ty The starting struct/union type.
 * @param names An array of member names to walk, innermost last.
 * @param n The number of names in the chain.
 * @return The summed byte offset, or -1 if any name cannot be resolved.
 * @details Convenience wrapper: OffsetofChain(ty, "a", "b", ...).
 */
int64_t __builtin_ast_offsetof_chain(Type *ty,
                                   const char **names, int n);

// ============================================================================
// Global Symbol Introspection
// ============================================================================

/*! @brief Look up a global symbol by name.
 * @param name The global name to look up.
 * @return The matching Obj*, or NULL if not found.
 * @details Convenience wrapper: FindGlobal(name).
 */
Obj *__builtin_ast_find_global(const char *name);

/*! @brief Return the total number of global symbols.
 * @return The count of globals.
 * @details Convenience wrapper: GlobalCount().
 */
int __builtin_ast_global_count(void);

/*! @brief Return the global symbol at the given index.
 * @param index Zero-based global index.
 * @return The Obj* at the given slot, or NULL on out-of-range.
 * @details Convenience wrapper: GlobalAt(index).
 */
Obj *__builtin_ast_global_at(int index);

/*! @brief Return the name of a global object.
 * @param obj The object to inspect.
 * @return A NUL-terminated string owned by obj.
 * @details Convenience wrapper: ObjName(obj).
 */
const char *__builtin_ast_obj_name(Obj *obj);

/*! @brief Return the type of a global object.
 * @param obj The object to inspect.
 * @return The object's Type*.
 * @details Convenience wrapper: ObjType(obj).
 */
Type *__builtin_ast_obj_type(Obj *obj);

/*! @brief Test whether a global object is a function.
 * @param obj The object to inspect.
 * @return True for functions, false for variables.
 * @details Convenience wrapper: ObjIsFunction(obj).
 */
bool __builtin_ast_obj_is_function(Obj *obj);

/*! @brief Test whether a global object has a definition.
 * @param obj The object to inspect.
 * @return True for defined objects, false for declarations only.
 * @details Convenience wrapper: ObjIsDefinition(obj).
 */
bool __builtin_ast_obj_is_definition(Obj *obj);

/*! @brief Test whether a global object has internal (static) linkage.
 * @param obj The object to inspect.
 * @return True for static linkage, false for external.
 * @details Convenience wrapper: ObjIsStatic(obj).
 */
bool __builtin_ast_obj_is_static(Obj *obj);

/*! @brief Return the kind of declaration decorated by a custom attribute.
 */
int __builtin_attr_target_kind(AttrTarget *target);

/*! @brief Return the decorated declaration's source name, when available.
 */
const char *__builtin_attr_target_name(AttrTarget *target);

/*! @brief Return the decorated declaration's type.
 */
Type *__builtin_attr_target_type(AttrTarget *target);

/*! @brief Return the decorated function or global object, or NULL for type targets.
 */
Obj *__builtin_attr_target_obj(AttrTarget *target);

/*! @brief Return a source token for the decorated declaration.
 */
Token *__builtin_attr_target_token(AttrTarget *target);

// ============================================================================
// AST Node Construction - Literals
// ============================================================================

/*! @brief Build an integer literal AST node.
 * @param value The integer value.
 * @return An NK_NUM node for value.
 * @details Convenience wrapper: MakeIntLiteral(value).
 */
Node *__builtin_ast_int_literal(int64_t value);

/*! @brief Build a floating-point literal AST node.
 * @param value The floating-point value.
 * @return An NK_NUM node for value.
 * @details Convenience wrapper: MakeFloatLiteral(value).
 */
Node *__builtin_ast_float_literal(double value);

/*! @brief Build a string literal AST node.
 * @param str A NUL-terminated string.
 * @return An NK_NUM string-literal node.
 * @details Convenience wrapper: MakeStringLiteral(str).
 */
Node *__builtin_ast_string_literal(const char *str);

/*! @brief Build a variable reference AST node.
 * @param name The variable name.
 * @return An NK_VAR node referencing name.
 * @details Convenience wrapper: MakeVarRef(name).
 */
Node *__builtin_ast_var_ref(const char *name);

/*! @brief Build a reference to a function parameter by name.
 * @param fn The function object whose parameter is being referenced.
 * @param name The parameter name.
 * @return An NK_VAR node for the named parameter.
 * @details Use this when building function bodies to reference parameters
 *             by name.  Convenience wrapper: MakeParamRef(fn, name).
 */
Node *__builtin_ast_param_ref(Obj *fn, const char *name);

// ============================================================================
// AST Node Construction - Expressions
// ============================================================================

/*! @brief Build a binary operation AST node.
 * @param op The operator kind (NK_ADD, NK_SUB, ...).
 * @param left The left-hand operand.
 * @param right The right-hand operand.
 * @return The binary expression node.
 * @details Convenience wrapper: MakeBinary(op, left, right).
 */
Node *__builtin_ast_binary(NodeKind op, Node *left,
                            Node *right);

/*! @brief Build a unary operation AST node.
 * @param op The operator kind (NK_NEG, NK_DEREF, ...).
 * @param operand The operand expression.
 * @return The unary expression node.
 * @details Convenience wrapper: MakeUnary(op, operand).
 */
Node *__builtin_ast_unary(NodeKind op, Node *operand);

/*! @brief Build a type cast AST node.
 * @param expr The expression to cast.
 * @param target_type The type to cast to.
 * @return An NK_CAST node.
 * @details Convenience wrapper: MakeCast(expr, target_type).
 */
Node *__builtin_ast_cast(Node *expr, Type *target_type);

// Ticket #171: new expression builders

/*! @brief Build a ternary conditional expression node (cond ? then : else).
 * @param cond The condition expression.
 * @param then_expr The expression evaluated when cond is non-zero.
 * @param else_expr The expression evaluated when cond is zero.
 * @return An NK_COND node.
 * @details Convenience wrapper: MakeCond(cond, then_expr, else_expr).
 */
Node *__builtin_ast_cond(Node *cond, Node *then_expr,
                          Node *else_expr);

/*! @brief Build a typed null pointer node: (void *)0.
 * @return An NK_NUM node representing a typed NULL.
 * @details Convenience wrapper: MakeNull().
 */
Node *__builtin_ast_null(void);

/*! @brief Emit sizeof(ty) as a compile-time integer literal.
 * @param ty The type to measure.
 * @return An NK_NUM node holding sizeof(ty).
 * @details Convenience wrapper: MakeSizeofType(ty).
 */
Node *__builtin_ast_sizeof_type(Type *ty);

/*! @brief Emit _Alignof(ty) as a compile-time integer literal.
 * @param ty The type to measure.
 * @return An NK_NUM node holding _Alignof(ty).
 * @details Convenience wrapper: MakeAlignofType(ty).
 */
Node *__builtin_ast_alignof_type(Type *ty);

/*! @brief Emit sizeof(expr): resolve the expression's type then its size.
 * @param expr The expression whose type to measure.
 * @return An NK_NUM node holding sizeof(expr).
 * @details Convenience wrapper: MakeSizeofExpr(expr).
 */
Node *__builtin_ast_sizeof_expr(Node *expr);

/*! @brief Build an array subscript node: arr[idx], desugared as *(arr+idx).
 * @param arr The array (or pointer) expression.
 * @param idx The index expression.
 * @return An NK_ADD / NK_DEREF node pair representing the subscript.
 * @details Convenience wrapper: MakeSubscript(arr, idx).
 */
Node *__builtin_ast_subscript(Node *arr, Node *idx);

/*! @brief Build a comma expression: evaluate lhs, yield rhs.
 * @param lhs The expression evaluated for side effects.
 * @param rhs The expression whose value is the result.
 * @return An NK_COMMA node.
 * @details Convenience wrapper: MakeComma(lhs, rhs).
 */
Node *__builtin_ast_comma(Node *lhs, Node *rhs);

// ============================================================================
// AST Node Construction - Statements
// ============================================================================

/*! @brief Build a return statement node.
 * @param expr The value to return (may be NULL for `return;` in void functions).
 * @return An NK_RETURN node.
 * @details Convenience wrapper: MakeReturn(expr).
 */
Node *__builtin_ast_return(Node *expr);

/*! @brief Build a block (compound statement) node.
 * @param stmts Array of statement nodes, or NULL if count is 0.
 * @param count Number of statements in the array.
 * @return An NK_BLOCK node.
 * @details Convenience wrapper: MakeBlock(stmts, count).
 */
Node *__builtin_ast_block(Node **stmts, int count);

/*! @brief Append a statement to a block node.
 * @param block The NK_BLOCK node to modify.
 * @param stmt The statement to append.
 * @return The block on success, or NULL on invalid arguments.
 * @details Convenience wrapper: BlockAddStmt(block, stmt), or
 *             BlockAddStmt(stmt) inside WithBlock(block).
 */
Node *__builtin_ast_block_add_stmt(Node *block, Node *stmt);

/*! @brief Build an if statement node.
 * @param cond The condition expression.
 * @param then_body The body executed when cond is non-zero.
 * @param else_body The body executed when cond is zero, or NULL.
 * @return An NK_IF node.
 * @details Convenience wrapper: MakeIf(cond, then_body, else_body).
 */
Node *__builtin_ast_if(Node *cond, Node *then_body,
                        Node *else_body);

/*! @brief Build a switch statement node.
 * @param cond The expression to switch on.
 * @return An NK_SWITCH node.  Use __builtin_ast_switch_add_case and
 *         __builtin_ast_switch_set_default to populate it.
 * @details Convenience wrapper: MakeSwitch(cond).
 */
Node *__builtin_ast_switch(Node *cond);

/*! @brief Append a case to a switch statement.
 * @param switch_node The switch node returned by __builtin_ast_switch.
 * @param value The case value expression.
 * @param body The body statement for this case.
 * @details Convenience wrapper: SwitchAddCase(sw, value, body), or
 *             SwitchAddCase(value, body) inside WithSwitch(sw).
 */
void __builtin_ast_switch_add_case(Node *switch_node,
                                Node *value, Node *body);

/*! @brief Set the default case for a switch statement.
 * @param switch_node The switch node returned by __builtin_ast_switch.
 * @param body The default-case body statement.
 * @details Convenience wrapper: SwitchSetDefault(sw, body), or
 *             SwitchSetDefault(body) inside WithSwitch(sw).
 */
void __builtin_ast_switch_set_default(Node *switch_node,
                                    Node *body);

/*! @brief Build an expression statement node.
 * @param expr The expression to evaluate for side effects.
 * @return An NK_EXPR_STMT node.
 * @details Convenience wrapper: MakeExprStmt(expr).
 */
Node *__builtin_ast_expr_stmt(Node *expr);

// ============================================================================
// AST Node Construction - Local Variable Injection (ticket #77)
// ============================================================================

/*! @brief Declare a named local variable in the current function scope
 *           and return a variable-reference node for it.
 * @param name The variable name (user-visible).
 * @param ty The variable type.
 * @return A NK_VAR node referencing the new local, or NULL if called
 *         outside a function body or on invalid arguments.
 * @note  The variable is injected into the current function's locals list
 *        and will receive a stack offset when the function is compiled.
 *        For temporaries that must not capture user names, prefer
 *        __builtin_ast_local_var_unique().
 * @details Convenience wrapper: MakeLocalVar(name, ty).
 */
Node *__builtin_ast_local_var(const char *name, Type *ty);

/*! @brief Declare a hygienic (gensym'd) local variable in the current
 *           function scope and return a variable-reference node for it.
 * @param ty The variable type.
 * @return A NK_VAR node referencing the new local, or NULL on error.
 * @note  The generated name begins with ".L.." and is therefore not
 *        expressible as a user identifier — guaranteed no name capture.
 *        This is the safe default for macro temporaries.
 * @details Convenience wrapper: MakeLocalVarUnique(ty).
 */
Node *__builtin_ast_local_var_unique(Type *ty);

/*! @brief Build an assignment node (target = value).
 * @param target The lvalue expression being assigned to.
 * @param value The rvalue expression to assign.
 * @return An NK_ASSIGN node, or NULL on error.
 * @details Convenience wrapper: MakeAssign(target, value).
 */
Node *__builtin_ast_assign(Node *target, Node *value);

/*! @brief Create a struct/union member access node (obj.name).
 * @param obj An expression node whose type must be a struct or union.
 * @param name The member name as a NUL-terminated string.
 * @return A NK_MEMBER node, or NULL if the member is not found or
 *         obj is not a struct/union type.
 * @note The callee is responsible for dereferencing pointers first;
 *       pass the struct value directly (use __builtin_ast_unary(NK_DEREF,…)
 *       for pointer-to-struct access).
 * @details Convenience wrapper: MakeMember(obj, name).
 */
Node *__builtin_ast_member(Node *obj, const char *name);

/*! @brief Create a function call node.
 * @param callee An expression node that evaluates to a function (or function
 *               pointer). The callee's lhs field holds this expression.
 * @param args Array of argument nodes (may be NULL if n == 0).
 * @param n Number of arguments.
 * @return A NK_FUNCALL node, or NULL on error.
 * @details Convenience wrapper: MakeFuncCall(callee, args, n).
 */
Node *__builtin_ast_funcall(Node *callee, Node **args, int n);

/*! @brief Create a while loop node.
 * @param cond The loop condition expression.
 * @param body The loop body statement.
 * @return A NK_FOR node (CCCC represents while as for with no init/inc),
 *         or NULL on error.
 * @details Convenience wrapper: MakeWhile(cond, body).
 */
Node *__builtin_ast_while(Node *cond, Node *body);

/*! @brief Create a for loop node.
 * @param init Initialiser expression/statement (may be NULL).
 * @param cond Loop condition (may be NULL for infinite loop).
 * @param inc Increment expression (may be NULL).
 * @param body Loop body.
 * @return A NK_FOR node, or NULL on error.
 * @details Convenience wrapper: MakeFor(init, cond, inc, body).
 */
Node *__builtin_ast_for(Node *init, Node *cond,
                       Node *inc, Node *body);

/*! @brief Create a do-while loop node.
 * @param body The loop body.
 * @param cond The loop condition (tested after each iteration).
 * @return A NK_DO node, or NULL on error.
 * @details Convenience wrapper: MakeDoWhile(body, cond).
 */
Node *__builtin_ast_do_while(Node *body, Node *cond);

// ============================================================================
// Function Generation
// ============================================================================

/*! @brief Create a new function object.
 * @param name The function name.
 * @param return_type The return type.
 * @return The newly created function object, or NULL on error.
 * @details The function is automatically added to the globals list
 *             and will be compiled when the main program is compiled.
 *             Convenience wrapper: MakeFunction(name, return_type).
 */
Obj *__builtin_ast_function(const char *name,
                            Type *return_type);

/*! @brief Make a generated object visible at the current source position.
 * @param obj A function or global variable object created by the AST builders.
 * @param tok Optional representative token for diagnostics, or NULL.
 * @return A no-op Node on success, or NULL on invalid arguments.
 * @details Top-level explicit macro calls run at their source position.
 *             Call this after creating a function or global variable when
 *             later macro-generated code at the same parse point should be
 *             able to reference it without a handwritten declaration.
 *             Convenience wrapper: PublishNode(obj) / PublishNodeAt(obj, tok).
 */
Node *__builtin_ast_publish(Obj *obj, Token *tok);

/*! @brief Accept a generated type declaration as already published.
 * @param ty A type created by MakeStruct, MakeUnion,
 *           MakeEnum, or MakeTypedef.
 * @param tok Optional representative token for diagnostics, or NULL.
 * @return A no-op Node on success, or NULL on invalid arguments.
 * @details Generated type builders self-register in tag or typedef scope.
 *             This function lets PublishNode(type) be used uniformly; there
 *             is no separate convenience macro for this entry point.
 */
Node *__builtin_ast_publish_type(Type *ty, Token *tok);

/*! @brief Emit one raw preprocessor directive line into generated output.
 * @param line Complete directive text, for example "#ifdef _WIN32".
 * @details Convenience wrapper: EmitDirective(line).
 */
void __builtin_emit_directive(const char *line);


/*! @brief Add a parameter to a function.
 * @param fn The function object.
 * @param name The parameter name.
 * @param type The parameter type.
 * @details Parameters are added in order. Call this multiple times
 *             for multiple parameters.  Convenience wrapper:
 *             FunctionAddParam(fn, name, type).
 */
void __builtin_ast_function_add_param(Obj *fn, const char *name,
                                Type *type);

/*! @brief Set the body of a function.
 * @param fn The function object.
 * @param body The function body (a statement or block node).
 * @details If body is not already a NK_BLOCK, it will be wrapped in one.
 *             Convenience wrapper: FunctionSetBody(fn, body).
 */
void __builtin_ast_function_set_body(Obj *fn, Node *body);

/*! @brief Set whether a function has static linkage.
 * @param fn The function object.
 * @param is_static True for static linkage, false for external.
 * @details Convenience wrapper: FunctionSetStatic(fn, is_static).
 */
void __builtin_ast_function_set_static(Obj *fn, bool is_static);

/*! @brief Set whether a function is inline.
 * @param fn The function object.
 * @param is_inline True for inline, false otherwise.
 * @details Convenience wrapper: FunctionSetInline(fn, is_inline).
 */
void __builtin_ast_function_set_inline(Obj *fn, bool is_inline);

/*! @brief Set whether a function is variadic.
 * @param fn The function object.
 * @param is_variadic True for variadic, false otherwise.
 * @details Convenience wrapper: FunctionSetVariadic(fn, is_variadic).
 */
void __builtin_ast_function_set_variadic(Obj *fn, bool is_variadic);

// Ticket #171: function prototype (forward declaration only, no body)
/*! @brief Create a function forward declaration (prototype) without a body.
 * @param name The function name.
 * @param return_type The return type.
 * @return The declaration Obj*, or NULL on error.
 * @details Use FunctionAddParam to add parameters and
 *             PublishNode to expose it in scope. A subsequent
 *             MakeFunction call with the same name will reuse this Obj and
 *             fill in the body.  Convenience wrapper: FunctionPrototype(name, return_type).
 */
Obj *__builtin_ast_function_prototype(const char *name,
                                    Type *return_type);

// ============================================================================
// Programmatic Attribute Application — ticket #619
// ============================================================================

/*! @brief Apply an attribute string to a programmatically created function.
 * @param fn The function object to attribute (created by MakeFunction).
 * @param attr_text Attribute text as it would appear between [[ and ]] in source,
 *                  e.g. "cccc::test", "cccc::test(suite=\"gen\", timeout=5000)",
 *                  "nodiscard", "nodiscard(\"always check\")", or "@myattr".
 *                  GNU form __attribute__((…)) is also accepted as the full string.
 * @details Parses attr_text through the existing attribute pipeline and applies
 *             the result to fn.  Supports mode attrs (test/build/build_target/
 *             test_setup/test_teardown), standard C23/GNU attrs (nodiscard, noreturn,
 *             pure, aligned, …), and custom `@attribute` handlers.
 *             cccc::comptime cannot be applied this way and will error.
 *             Convenience wrapper: AddAttribute(fn, text).
 */
void __builtin_ast_add_attribute(Obj *fn, const char *attr_text);

// Internal helper for MarkAsBuildTarget: composes "cccc::build_target(kind=…)"
// at runtime so the macro doesn't require string concatenation.
void __builtin_ast_add_build_target_attr(Obj *fn, const char *kind);

// Ticket #171: struct/union/enum/typedef type builders

/*! @brief Create and expose a new named struct type.
 * @param name The struct tag name.
 * @return The new struct Type*, or NULL on error.
 * @details Use StructAddField to add fields after creation.
 *             The type is immediately visible via FindType(name).
 *             Convenience wrapper: MakeStruct(name).
 */
Type *__builtin_ast_make_struct(const char *name);

/*! @brief Create and expose a new named union type.
 * @param name The union tag name.
 * @return The new union Type*, or NULL on error.
 * @details Convenience wrapper: MakeUnion(name).
 */
Type *__builtin_ast_make_union(const char *name);

/*! @brief Append a field to a struct or union and recompute its layout.
 * @param ty The struct or union type to modify.
 * @param name The field name.
 * @param field_type The field's type.
 * @return ty on success, or NULL on error.
 * @details For struct types, offsets are recalculated from scratch after
 *             each field addition. For union types, all fields stay at offset 0
 *             and the union size is updated to the maximum field size.
 *             Convenience wrapper: StructAddField(ty, name, field_type).
 */
Type *__builtin_ast_struct_add_field(Type *ty, const char *name,
                                   Type *field_type);

/*! @brief Create and expose a new named enum type.
 * @param name The enum tag name.
 * @return The new enum Type*, or NULL on error.
 * @details Use EnumAddConstant to add constants after creation.
 *             Convenience wrapper: MakeEnum(name).
 */
Type *__builtin_ast_make_enum(const char *name);

/*! @brief Add a named constant to an enum type and expose it in scope.
 * @param ty The enum type.
 * @param name The constant name.
 * @param value The constant integer value.
 * @details The constant is appended to ty->enum_constants and also pushed
 *             into the current scope so it is usable as an integer constant in
 *             subsequently compiled code.  Convenience wrapper:
 *             EnumAddConstant(ty, name, value).
 */
void __builtin_ast_enum_add_constant(Type *ty, const char *name,
                                  int64_t value);

/*! @brief Register a typedef alias for a type and expose it in scope.
 * @param name The typedef name.
 * @param underlying The aliased type.
 * @return The aliased type after registration, or NULL on invalid arguments.
 * @details After this call, FindType(name) resolves to underlying and
 *             subsequently compiled code can use name as a type name.
 *             Convenience wrapper: MakeTypedef(name, underlying).
 */
Type *__builtin_ast_make_typedef(const char *name, Type *underlying);

// ============================================================================
// Global Variable Generation (ticket #152)
// ============================================================================

/*! @brief Create a new named global variable definition.
 * @param name The variable name (must be unique among globals).
 * @param ty   The variable type.  Use MakeArray(char_ty, len) for byte
 *             arrays so that the size matches the init_data length.
 * @return The new Obj*, or NULL on error.
 * @details The variable is registered in vm->compiler.globals.  For inline
 *             macros the capture loop will stash it in macro_globals and emit
 *             an extern declaration into every input file's token stream so
 *             the parser can resolve references.
 *             Convenience wrapper: GlobalVar(name, ty).
 */
Obj *__builtin_ast_global_var(const char *name, Type *ty);

/*! @brief Set the initial data for a generated global variable.
 * @param var  The global variable object.
 * @param data Pointer to the raw byte data.
 * @param len  Number of bytes to copy.  Must equal var->ty->size.
 * @details Convenience wrapper: GlobalVarSetInitData(var, data, len).
 */
void __builtin_ast_global_var_set_init_data(Obj *var,
                                        const char *data, int len);

/*! @brief Set the static (internal linkage) flag on a generated global.
 * @param var       The global variable object.
 * @param is_static True for internal linkage (file-scope static).
 * @details Convenience wrapper: GlobalVarSetStatic(var, is_static).
 */
void __builtin_ast_global_var_set_static(Obj *var, bool is_static);

// ============================================================================
// Function-building context (ticket #148)
// ============================================================================

/*! @brief Establish fn as the "function currently being built" so that
 *           Quote("return x;") applies the correct implicit return-type cast.
 * @param fn The generated function whose return type should be used.
 * @details Call __builtin_ast_pop_fn (or use the WithFn macro) to
 *             restore the previous context.  execute_pragma_macro always
 *             restores current_fn after the macro returns, so unmatched pushes
 *             cannot leak into the main parse/codegen pass.  There is no
 *             1:1 convenience macro; use the @c WithFn(fn) { ... } block
 *             helper to bracket push/pop pairs in a macro body.
 */
void __builtin_ast_push_fn(Obj *fn);

/*! @brief Restore the function context saved by the most recent push.
 * @details Inverse of __builtin_ast_push_fn; typically used through the
 *             @c WithFn(fn) { ... } block helper, which performs the
 *             matching pop even on early exit.
 */
void __builtin_ast_pop_fn(void);

/*! @brief Establish a block as the current statement-append context.
 * @param block The NK_BLOCK node being populated.
 * @details Use with WithBlock(block) so BlockAddStmt(stmt) appends to
 *             block without repeating the block pointer.
 */
void __builtin_ast_push_block(Node *block);
void __builtin_ast_pop_block(void);
Node *__builtin_ast_block_add_current_stmt(Node *stmt);

/*! @brief Establish a struct or union as the current field-add context.
 * @param ty The aggregate type being populated.
 * @details Use with WithStruct(ty) so StructAddField(name, ty) appends
 *             to the current aggregate.
 */
void __builtin_ast_push_struct(Type *ty);
void __builtin_ast_pop_struct(void);
Type *__builtin_ast_struct_add_current_field(const char *name,
                                            Type *field_type);

/*! @brief Establish a switch node as the current case/default context.
 * @param switch_node The switch node being populated.
 * @details Use with WithSwitch(sw) so SwitchAddCase(value, body) and
 *             SwitchSetDefault(body) append to the current switch.
 */
void __builtin_ast_push_switch(Node *switch_node);
void __builtin_ast_pop_switch(void);
void __builtin_ast_switch_add_current_case(Node *value,
                                       Node *body);
void __builtin_ast_switch_set_current_default(Node *body);

/*! @brief Establish an enum as the current constant-add context.
 * @param ty The enum type being populated.
 * @details Use with WithEnum(ty) so EnumAddConstant(name, value)
 *             appends to the current enum.
 */
void __builtin_ast_push_enum(Type *ty);
void __builtin_ast_pop_enum(void);
void __builtin_ast_enum_add_current_constant(const char *name,
                                         int value);

// ============================================================================
// AST Dump Functions (ticket #58) — Nim-style dumpTree / dumpAstGen
// ============================================================================

/*! @brief Print a human-readable tree representation of a node to stdout.
 * @param node The root node to print.
 * @details Reuses the compiler's internal cc_dump_ast text renderer.
 *             Convenience wrapper: DumpTree(node).
 */
void __builtin_dump_tree(Node *node);

/*! @brief Render the tree representation to a heap-allocated string.
 * @param node The root node.
 * @return An arena-allocated NUL-terminated string, or NULL on error.
 * @details Convenience wrapper: DumpTreeToString(node).
 */
const char *__builtin_dump_tree_to_string(Node *node);

/*! @brief Print __builtin_ast_*() builder calls that would reconstruct the node.
 * @param node The root node to emit builder calls for.
 * @details Covers all node kinds for which relfection.c has a builder.
 *             Unsupported kinds are emitted as C comments.
 *             Convenience wrapper: DumpAstGen(node).
 */
void __builtin_dump_ast_gen(Node *node);

/*! @brief Render the __builtin_ast_*() builder call sequence to a string.
 * @param node The root node.
 * @return An arena-allocated NUL-terminated string, or NULL on error.
 * @details Convenience wrapper: DumpAstGenToString(node).
 */
const char *__builtin_dump_ast_gen_to_string(Node *node);

// ============================================================================
// Convenience Macros (automatically pass VM)
// ============================================================================

// Quasi-quoting helpers (ticket #1, #172)
/*! @def Quote
 * @brief Parse a C code template string into an AST node, substituting @c $N / @c $@N splice points with the given argument nodes.
 * @param tmpl A C expression or statement as a string literal with splice points.
 * @param ... Node* arguments corresponding to the splice points (up to 64; use QuoteN for more). */
#define Quote(tmpl, ...) __builtin_quote(tmpl, ##__VA_ARGS__)
/*! @def QuoteN
 * @brief Array-form quasi-quote that validates the splice count and supports more nodes than Quote's variadic form.
 * @param tmpl Template string with @c $N / @c $@N splice points.
 * @param nodes Array of Node* splice arguments.
 * @param count Length of the nodes array. */
#define QuoteN(tmpl, nodes, count) __builtin_quote_n(tmpl, nodes, count)
// Build a ->next-linked chain from a compound-literal array for $@k splices:
//   NodeList((Node*[]){ a, b, c }, 3)
/*! @def NodeList
 * @brief Build a @c ->next-linked node chain from an array, for use as a @c $@k list-splice argument.
 * @param nodes Array of Node* to link together.
 * @param count Number of elements in the array. */
#define NodeList(nodes, count) __builtin_node_list(nodes, count)

// Diagnostic helpers (ticket #78) — note: variadic macros require C99+
/*! @def MacroErrorAt
 * @brief Emit a compiler error pointing at @a node's source location.
 * @param node A node whose tok field provides file/line/col, or NULL.
 * @param ... printf-style format string, followed by format arguments. */
#define MacroErrorAt(node, ...) __builtin_macro_error_at(node, __VA_ARGS__)
/*! @def MacroWarningAt
 * @brief Emit a compiler warning pointing at @a node's source location (only under @c -Wcccc-macro).
 * @param node A node whose tok field provides file/line/col, or NULL.
 * @param ... printf-style format string, followed by format arguments. */
#define MacroWarningAt(node, ...) __builtin_macro_warning_at(node, __VA_ARGS__)

// AST dump helpers (ticket #58)
/*! @def DumpTree
 * @brief Print a human-readable tree representation of @a node to stdout.
 * @param node The root node to print. */
#define DumpTree(node) __builtin_dump_tree(node)
/*! @def DumpTreeToString
 * @brief Render the tree representation of @a node to a heap-allocated string.
 * @param node The root node. */
#define DumpTreeToString(node) __builtin_dump_tree_to_string(node)
/*! @def DumpAstGen
 * @brief Print @c __builtin_ast_*() builder calls that would reconstruct @a node.
 * @param node The root node to emit builder calls for. */
#define DumpAstGen(node) __builtin_dump_ast_gen(node)
/*! @def DumpAstGenToString
 * @brief Render the @c __builtin_ast_*() builder call sequence for @a node to a string.
 * @param node The root node. */
#define DumpAstGenToString(node) __builtin_dump_ast_gen_to_string(node)
/*! @def Gensym
 * @brief Generate a unique identifier string for macro-created symbols.
 * @param prefix Prefix for the generated name. */
#define Gensym(prefix) __builtin_gensym(prefix)
/*! @def MacroExpand1
 * @brief Lisp-style single-step macro expansion (@c macroexpand-1 semantics).
 * @param node The node to (possibly) expand. */
#define MacroExpand1(node) __builtin_macroexpand_1(node)
/*! @def MacroExpand
 * @brief Lisp-style full macro expansion; repeats MacroExpand1 until the form is stable.
 * @param node The node to fully expand. */
#define MacroExpand(node) __builtin_macroexpand(node)
/*! @def VarargCount
 * @brief Return the number of variadic arguments for the active macro call. */
#define VarargCount() __builtin_ast_vararg_count()
/*! @def VarargAt
 * @brief Return an inline macro's variadic AST argument by zero-based index.
 * @param i Zero-based variadic argument index. */
#define VarargAt(i) __builtin_ast_vararg_at(i)
/*! @def VarargAsArray
 * @brief Return an inline macro's variadic AST arguments as a borrowed array. */
#define VarargAsArray() __builtin_ast_varargs_as_array()
/*! @def VarargStrAt
 * @brief Return a global-generation macro's stringified variadic argument by index.
 * @param i Zero-based variadic argument index. */
#define VarargStrAt(i) __builtin_ast_vararg_str_at(i)

#define __builtin_dispatch_2(_1, _2, which, ...) which(_1, _2)
#define __builtin_dispatch_3(_1, _2, _3, which, ...) which(_1, _2, _3)

/*! @def CurrentToken
 * @brief Return the token for the macro invocation currently being executed. */
#define CurrentToken() __builtin_ast_current_token()
/*! @def SyntheticToken
 * @brief Create an opaque synthetic source token for generated AST nodes.
 * @param label Short diagnostic label for the synthetic location. */
#define SyntheticToken(label) __builtin_ast_synthetic_token(label)
/*! @def TokenFromNode
 * @brief Return the opaque source token attached to @a node.
 * @param node Node to inspect. */
#define TokenFromNode(node) __builtin_ast_token_from_node(node)
/*! @def SetToken
 * @brief Attach an opaque source token to @a node.
 * @param node Node to update.
 * @param tok Token from CurrentToken(), SyntheticToken(), or TokenFromNode(). */
#define SetToken(node, tok) __builtin_ast_set_token(node, tok)
/*! @def CopyLocation
 * @brief Copy the source token from @a src to @a dst.
 * @param dst Generated node to update.
 * @param src Source node whose location should be reused. */
#define CopyLocation(dst, src) __builtin_ast_copy_location(dst, src)

/*! @def FindType
 * @brief Look up a type by tag name (struct/union/enum).
 * @param name The tag name to look up. */
#define FindType(name) __builtin_ast_find_type(name)
/*! @def TypeExists
 * @brief Check whether a type is currently in scope by name.
 * @param name The type name to look up. */
#define TypeExists(name) __builtin_ast_type_exists(name)
/*! @def GetType
 * @brief Look up a type by name, falling back to the built-in primitives.
 * @param name The type name to look up. */
#define GetType(name) __builtin_ast_get_type(name)

// Type introspection — no VM needed
/*! @def GetTypeKind
 * @brief Return the TypeKind tag of a type.
 * @param ty The type to inspect. */
#define GetTypeKind(ty)          __builtin_ast_type_kind(ty)
/*! @def TypeSize
 * @brief Return @c sizeof(ty) in bytes.
 * @param ty The type to inspect. */
#define TypeSize(ty)          __builtin_ast_type_size(ty)
/*! @def TypeAlign
 * @brief Return @c _Alignof(ty) in bytes.
 * @param ty The type to inspect. */
#define TypeAlign(ty)         __builtin_ast_type_align(ty)
/*! @def TypeIsUnsigned
 * @brief Test whether an integer type is unsigned.
 * @param ty The type to inspect. */
#define TypeIsUnsigned(ty)   __builtin_ast_type_is_unsigned(ty)
/*! @def TypeIsConst
 * @brief Test whether a type is const-qualified.
 * @param ty The type to inspect. */
#define TypeIsConst(ty)      __builtin_ast_type_is_const(ty)
/*! @def TypeBase
 * @brief Return the element type of a pointer or array.
 * @param ty The pointer/array type to inspect. */
#define TypeBase(ty)          __builtin_ast_type_base(ty)
/*! @def TypeArrayLen
 * @brief Return the fixed length of an array type.
 * @param ty The array type to inspect. */
#define TypeArrayLen(ty)     __builtin_ast_type_array_len(ty)
/*! @def TypeReturnType
 * @brief Return the return type of a function type.
 * @param ty The function type to inspect. */
#define TypeReturnType(ty)   __builtin_ast_type_return_type(ty)
/*! @def TypeParamCount
 * @brief Return the number of declared parameters of a function type.
 * @param ty The function type to inspect. */
#define TypeParamCount(ty)   __builtin_ast_type_param_count(ty)
/*! @def TypeParamAt
 * @brief Return the type of the parameter at the given index.
 * @param ty The function type to inspect.
 * @param i Zero-based parameter index. */
#define TypeParamAt(ty, i)   __builtin_ast_type_param_at(ty, i)
/*! @def TypeIsVariadic
 * @brief Test whether a function type is variadic.
 * @param ty The type to inspect. */
#define TypeIsVariadic(ty)   __builtin_ast_type_is_variadic(ty)
/*! @def TypeName
 * @brief Return the user-visible name of a type, if any.
 * @param ty The type to inspect. */
#define TypeName(ty)          __builtin_ast_type_name(ty)
/*! @def TypeCName
 * @brief Return a valid C identifier fragment naming @a ty (for naming generated functions).
 * @param ty The type to inspect. */
#define TypeCName(ty)        __builtin_ast_type_c_name(ty)

/*! @def MakeIntLiteral
 * @brief Build an integer literal AST node.
 * @param val The integer value. */
#define MakeIntLiteral(val) __builtin_ast_int_literal(val)
/*! @def MakeFloatLiteral
 * @brief Build a floating-point literal AST node.
 * @param val The floating-point value. */
#define MakeFloatLiteral(val) __builtin_ast_float_literal(val)
/*! @def MakeStringLiteral
 * @brief Build a string literal AST node.
 * @param str A NUL-terminated string. */
#define MakeStringLiteral(str) __builtin_ast_string_literal(str)
/*! @def MakeVarRef
 * @brief Build a variable reference AST node.
 * @param name The variable name. */
#define MakeVarRef(name) __builtin_ast_var_ref(name)
/*! @def MakeParamRef
 * @brief Build a reference to a function parameter by name.
 * @param fn The function object whose parameter is being referenced.
 * @param name The parameter name. */
#define MakeParamRef(fn, name) __builtin_ast_param_ref(fn, name)

/*! @def MakeBinary
 * @brief Build a binary operation AST node.
 * @param op The operator kind (NK_ADD, NK_SUB, ...).
 * @param l The left-hand operand.
 * @param r The right-hand operand. */
#define MakeBinary(op, l, r) __builtin_ast_binary(op, l, r)
/*! @def MakeUnary
 * @brief Build a unary operation AST node.
 * @param op The operator kind (NK_NEG, NK_DEREF, ...).
 * @param operand The operand expression. */
#define MakeUnary(op, operand) __builtin_ast_unary(op, operand)
/*! @def MakeCast
 * @brief Build a type cast AST node.
 * @param expr The expression to cast.
 * @param ty The type to cast to. */
#define MakeCast(expr, ty) __builtin_ast_cast(expr, ty)

// Ticket #171: new expression builders
// Ternary conditional: cond ? then_expr : else_expr
/*! @def MakeCond
 * @brief Build a ternary conditional expression node (@c c @c ? @c t @c : @c e).
 * @param c The condition expression.
 * @param t The expression evaluated when c is non-zero.
 * @param e The expression evaluated when c is zero. */
#define MakeCond(c, t, e) __builtin_ast_cond(c, t, e)
// Typed null pointer: (void *)0
/*! @def MakeNull
 * @brief Build a typed null pointer node: @c (void @c *)0. */
#define MakeNull() __builtin_ast_null()
// sizeof(type) / _Alignof(type) as a compile-time integer literal
/*! @def MakeSizeofType
 * @brief Emit @c sizeof(ty) as a compile-time integer literal.
 * @param ty The type to measure. */
#define MakeSizeofType(ty) __builtin_ast_sizeof_type(ty)
/*! @def MakeAlignofType
 * @brief Emit @c _Alignof(ty) as a compile-time integer literal.
 * @param ty The type to measure. */
#define MakeAlignofType(ty) __builtin_ast_alignof_type(ty)
// sizeof(expr): resolves expr's type then returns its size as an integer literal
/*! @def MakeSizeofExpr
 * @brief Emit @c sizeof(expr): resolves the expression's type then its size.
 * @param expr The expression whose type to measure. */
#define MakeSizeofExpr(expr) __builtin_ast_sizeof_expr(expr)
// Array subscript: arr[idx] (desugared as *(arr+idx))
/*! @def MakeSubscript
 * @brief Build an array subscript node: @c arr[idx], desugared as @c *(arr+idx).
 * @param arr The array (or pointer) expression.
 * @param idx The index expression. */
#define MakeSubscript(arr, idx) __builtin_ast_subscript(arr, idx)
// Comma expression: evaluate lhs (for side effects), yield rhs
/*! @def MakeComma
 * @brief Build a comma expression: evaluate @a lhs, yield @a rhs.
 * @param lhs The expression evaluated for side effects.
 * @param rhs The expression whose value is the result. */
#define MakeComma(lhs, rhs) __builtin_ast_comma(lhs, rhs)

/*! @def MakeReturn
 * @brief Build a return statement node.
 * @param expr The value to return (may be NULL for a void return). */
#define MakeReturn(expr) __builtin_ast_return(expr)
/*! @def MakeBlock
 * @brief Build a block (compound statement) node.
 * @param stmts Array of statement nodes, or NULL if count is 0.
 * @param count Number of statements in the array. */
#define MakeBlock(stmts, count) __builtin_ast_block(stmts, count)
#define __builtin_block_add_stmt_1(stmt, _ignored)                          \
    __builtin_ast_block_add_current_stmt(stmt)
#define __builtin_block_add_stmt_2(block, stmt)                              \
    __builtin_ast_block_add_stmt(block, stmt)
/*! @def BlockAddStmt
 * @brief Append a statement to a block.
 * @param ... Either @c (stmt) inside a @c WithBlock(block) block, or @c (block, stmt) to name the block explicitly.
 * @details Dispatches on argument count via __builtin_dispatch_2. */
#define BlockAddStmt(...)                                             \
    __builtin_dispatch_2(__VA_ARGS__, __builtin_block_add_stmt_2,                \
                     __builtin_block_add_stmt_1)
/*! @def MakeIf
 * @brief Build an if statement node.
 * @param c The condition expression.
 * @param t The body executed when c is non-zero.
 * @param e The body executed when c is zero, or NULL. */
#define MakeIf(c, t, e) __builtin_ast_if(c, t, e)
/*! @def MakeSwitch
 * @brief Build a switch statement node.
 * @param cond The expression to switch on.
 * @details Use SwitchAddCase / SwitchSetDefault (or WithSwitch) to populate it. */
#define MakeSwitch(cond) __builtin_ast_switch(cond)
#define __builtin_switch_add_case_2(v, b, _ignored)                          \
    __builtin_ast_switch_add_current_case(v, b)
#define __builtin_switch_add_case_3(sw, v, b)                                \
    __builtin_ast_switch_add_case(sw, v, b)
/*! @def SwitchAddCase
 * @brief Append a case to a switch statement.
 * @param ... Either @c (value, body) inside a @c WithSwitch(sw) block, or @c (sw, value, body) to name the switch explicitly.
 * @details Dispatches on argument count via __builtin_dispatch_3. */
#define SwitchAddCase(...)                                            \
    __builtin_dispatch_3(__VA_ARGS__, __builtin_switch_add_case_3,               \
                     __builtin_switch_add_case_2)
#define __builtin_switch_set_default_1(b, _ignored)                          \
    __builtin_ast_switch_set_current_default(b)
#define __builtin_switch_set_default_2(sw, b)                                \
    __builtin_ast_switch_set_default(sw, b)
/*! @def SwitchSetDefault
 * @brief Set the default case for a switch statement.
 * @param ... Either @c (body) inside a @c WithSwitch(sw) block, or @c (sw, body) to name the switch explicitly.
 * @details Dispatches on argument count via __builtin_dispatch_2. */
#define SwitchSetDefault(...)                                         \
    __builtin_dispatch_2(__VA_ARGS__, __builtin_switch_set_default_2,            \
                     __builtin_switch_set_default_1)
/*! @def MakeExprStmt
 * @brief Build an expression statement node.
 * @param expr The expression to evaluate for side effects. */
#define MakeExprStmt(expr) __builtin_ast_expr_stmt(expr)
/*! @def MakeLocalVar
 * @brief Declare a named local variable in the current function scope.
 * @param name The variable name (user-visible).
 * @param ty The variable type.
 * @details For temporaries that must not capture user names, prefer MakeLocalVarUnique. */
#define MakeLocalVar(name, ty) __builtin_ast_local_var(name, ty)
/*! @def MakeLocalVarUnique
 * @brief Declare a hygienic (gensym'd) local variable in the current function scope.
 * @param ty The variable type.
 * @details The safe default for macro temporaries; the generated name cannot collide with a user identifier. */
#define MakeLocalVarUnique(ty) __builtin_ast_local_var_unique(ty)
/*! @def MakeAssign
 * @brief Build an assignment node (@c target @c = @c value).
 * @param target The lvalue expression being assigned to.
 * @param value The rvalue expression to assign. */
#define MakeAssign(target, value) __builtin_ast_assign(target, value)
/*! @def MakeMember
 * @brief Create a struct/union member access node (@c obj.name).
 * @param obj An expression node whose type must be a struct or union.
 * @param name The member name as a NUL-terminated string.
 * @details Dereference pointers first (e.g. via @c MakeUnary(NK_DEREF, ...)); this does not do it for you. */
#define MakeMember(obj, name) __builtin_ast_member(obj, name)
/*! @def MakeFuncCall
 * @brief Create a function call node.
 * @param callee An expression node that evaluates to a function (or function pointer).
 * @param args Array of argument nodes (may be NULL if n == 0).
 * @param n Number of arguments. */
#define MakeFuncCall(callee, args, n) __builtin_ast_funcall(callee, args, n)

// Ticket #235: thin AST wrappers over <string.h> functions, available via
// the implicit #include <string.h> at the top of this header.
/*! @def Memcpy
 * @brief Build a call node to @c memcpy(dst, src, n).
 * @param dst Destination expression.
 * @param src Source expression.
 * @param n Byte count expression. */
#define Memcpy(dst, src, n)                                              \
    __builtin_ast_funcall(__builtin_ast_var_ref("memcpy"),            \
        (Node *[]){(dst), (src), (n)}, 3)
/*! @def Strlen
 * @brief Build a call node to @c strlen(s).
 * @param s String expression. */
#define Strlen(s)                                                        \
    __builtin_ast_funcall(__builtin_ast_var_ref("strlen"),            \
        (Node *[]){(s)}, 1)
/*! @def Strcmp
 * @brief Build a call node to @c strcmp(a, b).
 * @param a First string expression.
 * @param b Second string expression. */
#define Strcmp(a, b)                                                     \
    __builtin_ast_funcall(__builtin_ast_var_ref("strcmp"),            \
        (Node *[]){(a), (b)}, 2)
/*! @def MakeWhile
 * @brief Create a while loop node.
 * @param cond The loop condition expression.
 * @param body The loop body statement. */
#define MakeWhile(cond, body) __builtin_ast_while(cond, body)
/*! @def MakeFor
 * @brief Create a for loop node.
 * @param init Initialiser expression/statement (may be NULL).
 * @param cond Loop condition (may be NULL for infinite loop).
 * @param inc Increment expression (may be NULL).
 * @param body Loop body. */
#define MakeFor(init, cond, inc, body) __builtin_ast_for(init, cond, inc, body)
/*! @def MakeDoWhile
 * @brief Create a do-while loop node.
 * @param body The loop body.
 * @param cond The loop condition (tested after each iteration). */
#define MakeDoWhile(body, cond) __builtin_ast_do_while(body, cond)

/*! @def MakePointer
 * @brief Build a pointer-to-base type.
 * @param base The pointed-to type. */
#define MakePointer(base) __builtin_ast_make_pointer(base)
/*! @def MakeArray
 * @brief Build a fixed-length array type.
 * @param base The element type.
 * @param len The element count. */
#define MakeArray(base, len) __builtin_ast_make_array(base, len)
/*! @def MakeFuncPtrType
 * @brief Build a pointer-to-function type, e.g. @c "T (*)(T)".
 * @param ret The function's return type.
 * @param params Array of parameter types.
 * @param n Number of entries in params (max 16). */
#define MakeFuncPtrType(ret, params, n) \
    __builtin_ast_make_func_ptr_type(ret, params, n)

// Ticket #171: qualified type constructors
/*! @def MakeConst
 * @brief Return a const-qualified copy of @a ty.
 * @param ty The type to qualify. */
#define MakeConst(ty)    __builtin_ast_make_const(ty)
/*! @def MakeVolatile
 * @brief Return a volatile-qualified copy of @a ty.
 * @param ty The type to qualify. */
#define MakeVolatile(ty) __builtin_ast_make_volatile(ty)

/*! @def EnumCount
 * @brief Return the number of constants in an enum type.
 * @param ty The enum type to inspect. */
#define EnumCount(ty) __builtin_ast_enum_count(ty)
/*! @def EnumAt
 * @brief Return the enum constant at a given index.
 * @param ty The enum type to inspect.
 * @param i Zero-based index. */
#define EnumAt(ty, i) __builtin_ast_enum_at(ty, i)
/*! @def EnumFind
 * @brief Look up an enum constant by name.
 * @param ty The enum type to search.
 * @param name The constant name to look up. */
#define EnumFind(ty, name) __builtin_ast_enum_find(ty, name)
/*! @def EnumConstantName
 * @brief Return the name of an enum constant.
 * @param ec The enum constant. */
#define EnumConstantName(ec)   __builtin_ast_enum_constant_name(ec)
/*! @def EnumConstantValue
 * @brief Return the integer value of an enum constant.
 * @param ec The enum constant. */
#define EnumConstantValue(ec)  __builtin_ast_enum_constant_value(ec)
/*! @def EnumName
 * @brief Return the tag name of an enum type.
 * @param ty The enum type to inspect. */
#define EnumName(ty)            __builtin_ast_enum_name(ty)
/*! @def EnumValueCount
 * @brief Return the number of values in an enum type.
 * @param ty The enum type to inspect. */
#define EnumValueCount(ty)     __builtin_ast_enum_value_count(ty)
/*! @def EnumValueName
 * @brief Return the name of the enum constant at the given index.
 * @param ty The enum type to inspect.
 * @param i Zero-based index. */
#define EnumValueName(ty, i)   __builtin_ast_enum_value_name(ty, i)
/*! @def EnumValue
 * @brief Return the integer value of the enum constant at the given index.
 * @param ty The enum type to inspect.
 * @param i Zero-based index. */
#define EnumValue(ty, i)        __builtin_ast_enum_value(ty, i)

// Ticket #235: EnumToString(ty, expr) / EnumFromString(ty, expr)
/*! @def EnumToString
 * @brief Build a @c switch over @a ty's constants returning each constant's name as a string.
 * @param ty The enum type.
 * @param expr The expression to switch on (the enum value).
 * @details Wrap the result in a function returning const char *. */
#define EnumToString(ty, expr)   __builtin_ast_enum_to_string_switch(ty, expr)
/*! @def EnumFromString
 * @brief Build an if-chain comparing @a expr against each constant's name, returning the matching value.
 * @param ty The enum type.
 * @param expr The expression to compare (a const char *).
 * @details Wrap the result in a function returning the enum type (or an int). */
#define EnumFromString(ty, expr) __builtin_ast_enum_from_string_chain(ty, expr)

/*! @def StructMemberCount
 * @brief Return the number of members of a struct or union type.
 * @param ty The struct or union type to inspect. */
#define StructMemberCount(ty) __builtin_ast_struct_member_count(ty)
/*! @def StructMemberAt
 * @brief Return the member at the given index.
 * @param ty The struct or union type to inspect.
 * @param i Zero-based member index. */
#define StructMemberAt(ty, i) __builtin_ast_struct_member_at(ty, i)
/*! @def StructMemberFind
 * @brief Look up a struct or union member by name.
 * @param ty The struct or union type to search.
 * @param name The member name to look up. */
#define StructMemberFind(ty, name)                                   \
    __builtin_ast_struct_member_find(ty, name)
/*! @def MemberName
 * @brief Return the name of a struct/union member.
 * @param m The member to inspect. */
#define MemberName(m)             __builtin_ast_member_name(m)
/*! @def MemberType
 * @brief Return the type of a struct/union member.
 * @param m The member to inspect. */
#define MemberType(m)             __builtin_ast_member_type(m)
/*! @def MemberOffset
 * @brief Return the byte offset of a struct/union member.
 * @param m The member to inspect. */
#define MemberOffset(m)           __builtin_ast_member_offset(m)
/*! @def MemberIsBitfield
 * @brief Test whether a member is a bitfield.
 * @param m The member to inspect. */
#define MemberIsBitfield(m)      __builtin_ast_member_is_bitfield(m)
/*! @def MemberBitfieldWidth
 * @brief Return the bit width of a bitfield member.
 * @param m The member to inspect. */
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
/*! @def ForeachMember
 * @brief Host-side loop over the members of a struct/union type, evaluated at macro-execution (compile) time.
 * @param type The struct/union Type* to iterate.
 * @param varname Identifier bound to each Member* in turn.
 * @param body Compound statement run once per member (typically builds AST nodes via BlockAddStmt etc.).
 * @details Nestable: a two-layer __COUNTER__ indirection gives each call site unique loop-variable names. */
#define ForeachMember(type, varname, body)                             \
    __builtin_foreach_member_uid(type, varname, body, __COUNTER__)

// Ticket #235: OffsetofChain(ty, "a", "b", ...) — offsetof(ty, a.b) as an
// MakeIntLiteral AST node.
/*! @def OffsetofChain
 * @brief Compute the byte offset of a (possibly nested) member chain as a MakeIntLiteral AST node.
 * @param type The starting struct/union type.
 * @param ... Member names to walk, innermost last. */
#define OffsetofChain(type, ...)                                       \
    MakeIntLiteral(__builtin_ast_offsetof_chain(type,                   \
        (const char *[]){__VA_ARGS__},                                  \
        (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(const char *))))

/*! @def FindGlobal
 * @brief Look up a global symbol by name.
 * @param name The global name to look up. */
#define FindGlobal(name)        __builtin_ast_find_global(name)
/*! @def GlobalCount
 * @brief Return the total number of global symbols. */
#define GlobalCount()           __builtin_ast_global_count()
/*! @def GlobalAt
 * @brief Return the global symbol at the given index.
 * @param i Zero-based global index. */
#define GlobalAt(i)             __builtin_ast_global_at(i)
/*! @def ObjName
 * @brief Return the name of a global object.
 * @param obj The object to inspect. */
#define ObjName(obj)            __builtin_ast_obj_name(obj)
/*! @def ObjType
 * @brief Return the type of a global object.
 * @param obj The object to inspect. */
#define ObjType(obj)            __builtin_ast_obj_type(obj)
/*! @def ObjIsFunction
 * @brief Test whether a global object is a function.
 * @param obj The object to inspect. */
#define ObjIsFunction(obj)     __builtin_ast_obj_is_function(obj)
/*! @def ObjIsDefinition
 * @brief Test whether a global object has a definition.
 * @param obj The object to inspect. */
#define ObjIsDefinition(obj)   __builtin_ast_obj_is_definition(obj)
/*! @def ObjIsStatic
 * @brief Test whether a global object has internal (static) linkage.
 * @param obj The object to inspect. */
#define ObjIsStatic(obj)       __builtin_ast_obj_is_static(obj)
/*! @def GetAttrTargetKind
 * @brief Return the kind of declaration decorated by a custom attribute.
 * @param target The attribute target to inspect. */
#define GetAttrTargetKind(target)  __builtin_attr_target_kind(target)
/*! @def AttrTargetName
 * @brief Return the decorated declaration's source name, when available.
 * @param target The attribute target to inspect. */
#define AttrTargetName(target)  __builtin_attr_target_name(target)
/*! @def AttrTargetType
 * @brief Return the decorated declaration's type.
 * @param target The attribute target to inspect. */
#define AttrTargetType(target)  __builtin_attr_target_type(target)
/*! @def AttrTargetObj
 * @brief Return the decorated function or global object, or NULL for type targets.
 * @param target The attribute target to inspect. */
#define AttrTargetObj(target)   __builtin_attr_target_obj(target)
/*! @def AttrTargetToken
 * @brief Return a source token for the decorated declaration.
 * @param target The attribute target to inspect. */
#define AttrTargetToken(target) __builtin_attr_target_token(target)

/*! @def MakeFunction
 * @brief Create a new function object, automatically added to the globals list.
 * @param name The function name.
 * @param ret_type The return type. */
#define MakeFunction(name, ret_type)                                       \
    __builtin_ast_function(name, ret_type)
/*! @def PublishNode
 * @brief Make a generated declaration visible at the current source position.
 * @param decl An Obj* (function/global) or Type* created by the AST builders.
 * @details Dispatches on the argument's type via _Generic: Obj* goes to __builtin_ast_publish, Type* to __builtin_ast_publish_type. */
#define PublishNode(decl)                                                  \
    _Generic((decl),                                                        \
        Obj *: __builtin_ast_publish,                                          \
        Type *: __builtin_ast_publish_type                                     \
    )((decl), 0)
/*! @def PublishNodeAt
 * @brief Like PublishNode, but attaches an explicit diagnostic token.
 * @param decl An Obj* (function/global) or Type* created by the AST builders.
 * @param tok Representative token for diagnostics, or NULL. */
#define PublishNodeAt(decl, tok)                                          \
    _Generic((decl),                                                        \
        Obj *: __builtin_ast_publish,                                          \
        Type *: __builtin_ast_publish_type                                     \
    )((decl), (tok))
/*! @def EmitDirective
 * @brief Emit one raw preprocessor directive line into generated output.
 * @param line Complete directive text, for example "#ifdef _WIN32". */
#define EmitDirective(line) __builtin_emit_directive(line)
/*! @def FunctionAddParam
 * @brief Add a parameter to a function.
 * @param fn The function object.
 * @param name The parameter name.
 * @param type The parameter type.
 * @details Call once per parameter, in order. */
#define FunctionAddParam(fn, name, type)                             \
    __builtin_ast_function_add_param(fn, name, type)
/*! @def FunctionSetBody
 * @brief Set the body of a function.
 * @param fn The function object.
 * @param body The function body (a statement or block node). */
#define FunctionSetBody(fn, body)                                    \
    __builtin_ast_function_set_body(fn, body)
/*! @def FunctionSetStatic
 * @brief Set whether a function has static linkage.
 * @param fn The function object.
 * @param is_static True for static linkage, false for external. */
#define FunctionSetStatic(fn, is_static)                             \
    __builtin_ast_function_set_static(fn, is_static)
/*! @def FunctionSetInline
 * @brief Set whether a function is inline.
 * @param fn The function object.
 * @param is_inline True for inline, false otherwise. */
#define FunctionSetInline(fn, is_inline)                             \
    __builtin_ast_function_set_inline(fn, is_inline)
/*! @def FunctionSetVariadic
 * @brief Set whether a function is variadic.
 * @param fn The function object.
 * @param is_variadic True for variadic, false otherwise. */
#define FunctionSetVariadic(fn, is_variadic)                         \
    __builtin_ast_function_set_variadic(fn, is_variadic)

// Ticket #171: function forward declaration / prototype builder
// Creates a declaration-only Obj (no body); use FunctionAddParam for
// parameters and PublishNode to make it visible in scope.
/*! @def FunctionPrototype
 * @brief Create a function forward declaration (prototype) without a body.
 * @param name The function name.
 * @param ret The return type.
 * @details Use FunctionAddParam for parameters and PublishNode to expose it in scope. */
#define FunctionPrototype(name, ret)                                  \
    __builtin_ast_function_prototype(name, ret)

// Mode attribute registration for AST-generated functions.
// Ticket #619: generic attribute application and convenience shorthands.
// Use AddAttribute(fn, "cccc::test(suite=\"s\", timeout=5000)") for fine-grained control.
/*! @def AddAttribute
 * @brief Apply an attribute string to a programmatically created function.
 * @param fn The function object to attribute (created by MakeFunction).
 * @param text Attribute text as it would appear between [[ and ]], e.g. "cccc::test", "nodiscard", or "@myattr". */
#define AddAttribute(fn, text)          __builtin_ast_add_attribute(fn, text)
/*! @def MarkAsTest
 * @brief Shorthand for @c AddAttribute(fn, "cccc::test").
 * @param fn The function object. */
#define MarkAsTest(fn)                  AddAttribute(fn, "cccc::test")
/*! @def MarkAsBuild
 * @brief Shorthand for @c AddAttribute(fn, "cccc::build").
 * @param fn The function object. */
#define MarkAsBuild(fn)                 AddAttribute(fn, "cccc::build")
/*! @def MarkAsBuildTarget
 * @brief Mark @a fn as a @c [[cccc::build_target]] factory of the given kind.
 * @param fn The function object.
 * @param kind The build-target kind string. */
#define MarkAsBuildTarget(fn, kind)     __builtin_ast_add_build_target_attr(fn, kind)

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
/*! @def MakeStruct
 * @brief Create and expose a new named struct type.
 * @param name The struct tag name.
 * @details Use StructAddField to add fields after creation. */
#define MakeStruct(name)     __builtin_ast_make_struct(name)
/*! @def MakeUnion
 * @brief Create and expose a new named union type.
 * @param name The union tag name. */
#define MakeUnion(name)      __builtin_ast_make_union(name)
#define __builtin_struct_add_field_2(name, field_type, _ignored)             \
    __builtin_ast_struct_add_current_field(name, field_type)
#define __builtin_struct_add_field_3(ty, name, field_type)                   \
    __builtin_ast_struct_add_field(ty, name, field_type)
/*! @def StructAddField
 * @brief Append a field to a struct or union and recompute its layout.
 * @param ... Either @c (name, field_type) inside a @c WithStruct(ty) block, or @c (ty, name, field_type) to name the type explicitly.
 * @details Works for both struct and union types; dispatches on argument count via __builtin_dispatch_3. */
#define StructAddField(...)                                           \
    __builtin_dispatch_3(__VA_ARGS__, __builtin_struct_add_field_3,              \
                     __builtin_struct_add_field_2)
/*! @def MakeEnum
 * @brief Create and expose a new named enum type.
 * @param name The enum tag name.
 * @details Use EnumAddConstant to add constants after creation. */
#define MakeEnum(name)       __builtin_ast_make_enum(name)
#define __builtin_enum_add_constant_2(name, value, _ignored)                 \
    __builtin_ast_enum_add_current_constant(name, value)
#define __builtin_enum_add_constant_3(ty, name, value)                       \
    __builtin_ast_enum_add_constant(ty, name, value)
/*! @def EnumAddConstant
 * @brief Add a named constant to an enum type and expose it in scope.
 * @param ... Either @c (name, value) inside a @c WithEnum(ty) block, or @c (ty, name, value) to name the type explicitly.
 * @details Dispatches on argument count via __builtin_dispatch_3. */
#define EnumAddConstant(...)                                          \
    __builtin_dispatch_3(__VA_ARGS__, __builtin_enum_add_constant_3,             \
                     __builtin_enum_add_constant_2)
/*! @def MakeTypedef
 * @brief Register a typedef alias for a type and expose it in scope.
 * @param name The typedef name.
 * @param underlying The aliased type. */
#define MakeTypedef(name, underlying) \
    __builtin_ast_make_typedef(name, underlying)

// Comptime variable access (ticket #188)
/*! @brief Read an integer-typed @c \#pragma comptime variable's value at
 *           compile time.
 * @param name The comptime variable's name.
 * @return The 64-bit integer value, or 0 if the variable is not defined.
 * @details Convenience wrapper: GetComptimeInt(name).
 */
int64_t __builtin_get_comptime_int(const char *name);
/*! @brief Read a float/double-typed @c \#pragma comptime variable's value
 *           at compile time.
 * @param name The comptime variable's name.
 * @return The double value, or 0.0 if the variable is not defined.
 * @details Convenience wrapper: GetComptimeFloat(name).
 */
double __builtin_get_comptime_float(const char *name);
/*! @brief Read a comptime scalar variable as an AST literal node.
 * @param name The comptime variable's name.
 * @return A NK_NUM node representing the variable's value, or NULL on error.
 * @details Convenience wrapper: GetComptimeVar(name).
 */
Node *__builtin_get_comptime_var(const char *name);
/*! @brief Return the address of a comptime variable as a generated-code AST
 *           pointer node.
 * @param name The comptime variable's name.
 * @return An NK_ADDR node pointing at a static shadow copy of the evaluated
 *         comptime variable, or NULL on error.
 * @details Convenience wrapper: GetComptimePtr(name).
 */
Node *__builtin_get_comptime_ptr(const char *name);
/*! @brief Read a named field from a comptime struct variable as an AST
 *           literal node.
 * @param var_name The comptime struct variable's name.
 * @param field The field name to look up.
 * @return A NK_NUM node for the field's value, or NULL on error.
 * @details Convenience wrapper: GetComptimeMember(var_name, field).
 */
Node *__builtin_get_comptime_member(const char *var_name,
                                  const char *field);

/*! @def GetComptimeInt
 * @brief Read an integer-typed @c #pragma comptime variable's value at compile time.
 * @param name The comptime variable's name. */
#define GetComptimeInt(name)           __builtin_get_comptime_int(name)
/*! @def GetComptimeFloat
 * @brief Read a float/double-typed @c #pragma comptime variable's value at compile time.
 * @param name The comptime variable's name. */
#define GetComptimeFloat(name)         __builtin_get_comptime_float(name)
/*! @def GetComptimeVar
 * @brief Read a comptime scalar variable as an AST literal node.
 * @param name The comptime variable's name. */
#define GetComptimeVar(name)           __builtin_get_comptime_var(name)
/*! @def GetComptimePtr
 * @brief Return the address of a comptime variable as a generated-code AST pointer node.
 * @param name The comptime variable's name. */
#define GetComptimePtr(name)           __builtin_get_comptime_ptr(name)
/*! @def GetComptimeMember
 * @brief Read a named field from a comptime struct variable as an AST literal node.
 * @param var The comptime struct variable's name.
 * @param field The field name to look up. */
#define GetComptimeMember(var, field)  __builtin_get_comptime_member(var, field)

// Constexpr variable access (ticket #189)
/*! @brief Read the evaluated initializer of a global @c constexpr variable
 *           as an AST literal node.
 * @param name The constexpr variable's name.
 * @return A NK_NUM node (integer or float, depending on the variable's type),
 *         or NULL on error.
 * @details Errors at compile time if @a name does not refer to a visible
 *             @c constexpr variable.  Convenience wrapper:
 *             GetConstexprValue(name).
 */
Node *__builtin_get_constexpr_value(const char *name);

/*! @def GetConstexprValue
 * @brief Read the evaluated initializer of a global constexpr variable as an AST literal node.
 * @param name The constexpr variable's name.
 * @details Errors at compile time if name does not refer to a visible constexpr variable. */
#define GetConstexprValue(name)  __builtin_get_constexpr_value(name)

// ============================================================================
// Initializer Builders (ticket #296)
// ============================================================================

/**
 * @brief Build a positional compound literal: zero-initialise an anonymous
 *           local var then positionally assign @a inits via node_expand_init_splice.
 * @note Requires function scope (file-scope not supported in V1).
 */
Node *__builtin_ast_compound_literal(Type *ty, Node **inits, int n);

/**
 * @brief Build an array compound literal.  Element type is explicit to avoid
 *           long-inference surprises with MakeIntLiteral.
 */
Node *__builtin_ast_init_array(Type *elem_ty, Node **elems, int n);

/**
 * @brief Build a designated struct/union initializer.  Unmentioned fields
 *           are zero-initialised.  Partial init (n < member count) is allowed.
 */
Node *__builtin_ast_init_struct(Type *ty, const char **fields,
                                Node **values, int n);

/** Positional compound literal — element count inferred from __VA_ARGS__. */
/*! @def CompoundLiteral
 * @brief Build a positional compound literal; element count is inferred from the argument list.
 * @param ty The compound literal's type.
 * @param ... Node* initializer values, assigned positionally.
 * @details Requires function scope (file-scope is not supported). */
#define CompoundLiteral(ty, ...)                                      \
    __builtin_ast_compound_literal(ty,                                 \
        (Node *[]){__VA_ARGS__},                                     \
        (int)(sizeof((Node *[]){__VA_ARGS__}) / sizeof(Node *)))

/** Array compound literal with explicit element type. */
/*! @def InitArray
 * @brief Build an array compound literal with an explicit element type.
 * @param elem_ty The array element type.
 * @param ... Node* element values; count is inferred from the argument list. */
#define InitArray(elem_ty, ...)                                       \
    __builtin_ast_init_array(elem_ty,                                  \
        (Node *[]){__VA_ARGS__},                                     \
        (int)(sizeof((Node *[]){__VA_ARGS__}) / sizeof(Node *)))

/** Designated struct/union init — fields and values are separate arrays. */
/*! @def InitStruct
 * @brief Build a designated struct/union initializer; unmentioned fields are zero-initialised.
 * @param ty The struct/union type.
 * @param fields Array of field-name strings.
 * @param values Array of Node* values, parallel to fields.
 * @param n Number of (field, value) pairs.
 * @details Partial init (n less than the member count) is allowed. */
#define InitStruct(ty, fields, values, n)                             \
    __builtin_ast_init_struct(ty, fields, values, n)

// ============================================================================
// Serialization (ticket #235)
// ============================================================================

/**
 * @brief Build a block of memcpy() calls copying expr (of type ty)
 *           byte-for-byte into buf (a void or char pointer). Struct/union
 *           types are copied member-by-member at their natural offsets,
 *           recursing into nested flat structs; scalar types are copied
 *           in one memcpy.
 * @note V1 placeholder: pointer-typed members are copied as raw pointer
 *       bytes, not followed.
 */
Node *__builtin_ast_serialize(Type *ty, Node *expr, Node *buf);

/**
 * @brief Build `*(ty*)buf` — reinterpret buf as a ty value.
 * @note V1 placeholder: inherits the host's alignment requirements for ty;
 *       if Serialize ever produces a packed/portable layout this must
 *       change to field-by-field reconstruction.
 */
Node *__builtin_ast_deserialize(Type *ty, Node *buf);

/*! @def Serialize
 * @brief Build a block of memcpy() calls copying @a expr (of type @a ty) byte-for-byte into @a buf.
 * @param ty The type of expr.
 * @param expr The expression to serialize.
 * @param buf A void or char pointer destination.
 * @details Struct/union types are copied member-by-member at their natural offsets; pointer members are copied as raw bytes, not followed. */
#define Serialize(ty, expr, buf)   __builtin_ast_serialize(ty, expr, buf)
/*! @def Deserialize
 * @brief Build @c *(ty*)buf — reinterpret @a buf as a @a ty value.
 * @param ty The type to reinterpret as.
 * @param buf The source buffer.
 * @details Inherits the host's alignment requirements for ty. */
#define Deserialize(ty, buf)       __builtin_ast_deserialize(ty, buf)

// Global variable generation (ticket #152)
/*! @def GlobalVar
 * @brief Create a new named global variable definition.
 * @param name The variable name (must be unique among globals).
 * @param ty The variable type. */
#define GlobalVar(name, ty)                                           \
    __builtin_ast_global_var(name, ty)
/*! @def GlobalVarSetInitData
 * @brief Set the initial data for a generated global variable.
 * @param var The global variable object.
 * @param data Pointer to the raw byte data.
 * @param len Number of bytes to copy (must equal var's type size). */
#define GlobalVarSetInitData(var, data, len)                       \
    __builtin_ast_global_var_set_init_data(var, data, len)
/*! @def GlobalVarSetStatic
 * @brief Set the static (internal linkage) flag on a generated global.
 * @param var The global variable object.
 * @param is_static True for internal (file-scope static) linkage. */
#define GlobalVarSetStatic(var, is_static)                          \
    __builtin_ast_global_var_set_static(var, is_static)

// Function-building context (ticket #148)
// Usage:
//   WithFn(fn) {
//       FunctionSetBody(fn, Quote("return 42;"));
//   }
// Inside the block, current_fn is set to fn so Quote("return x;") casts
// to the correct return type.  The pop always runs even on early exit.
/*! @def WithFn
 * @brief Block helper: establishes @a fn as the current function-building context so @c Quote("return x;") applies the correct return-type cast.
 * @param fn The generated function whose return type should be used.
 * @details Implemented as a single-iteration for loop; the pop runs in the loop increment. Exit the block with `continue`, not `break` — `break` skips the increment and leaves the context unrestored. */
#define WithFn(fn)                                                    \
    for (int _cccc_fn_ctx_ = (__builtin_ast_push_fn((fn)), 1);             \
         _cccc_fn_ctx_;                                                      \
         _cccc_fn_ctx_ = (__builtin_ast_pop_fn(), 0))

/*! @def WithBlock
 * @brief Block helper: establishes @a block as the current statement-append context so @c BlockAddStmt(stmt) appends to it.
 * @param block The NK_BLOCK node being populated.
 * @details Implemented as a single-iteration for loop; use `continue` rather than `break` to exit early so the pop still runs. */
#define WithBlock(block)                                               \
    for (int _cccc_block_ctx_ = (__builtin_ast_push_block((block)), 1);  \
         _cccc_block_ctx_;                                                \
         _cccc_block_ctx_ = (__builtin_ast_pop_block(), 0))

/*! @def WithStruct
 * @brief Block helper: establishes @a ty as the current field-add context so @c StructAddField(name, ty) appends to it.
 * @param ty The aggregate type being populated.
 * @details Implemented as a single-iteration for loop; use `continue` rather than `break` to exit early so the pop still runs. */
#define WithStruct(ty)                                                 \
    for (int _cccc_struct_ctx_ = (__builtin_ast_push_struct((ty)), 1);   \
         _cccc_struct_ctx_;                                               \
         _cccc_struct_ctx_ = (__builtin_ast_pop_struct(), 0))

/*! @def WithSwitch
 * @brief Block helper: establishes @a sw as the current case/default context so @c SwitchAddCase / @c SwitchSetDefault populate it.
 * @param sw The switch node being populated.
 * @details Implemented as a single-iteration for loop; use `continue` rather than `break` to exit early so the pop still runs. */
#define WithSwitch(sw)                                                 \
    for (int _cccc_switch_ctx_ = (__builtin_ast_push_switch((sw)), 1);   \
         _cccc_switch_ctx_;                                               \
         _cccc_switch_ctx_ = (__builtin_ast_pop_switch(), 0))

/*! @def WithEnum
 * @brief Block helper: establishes @a ty as the current constant-add context so @c EnumAddConstant(name, value) appends to it.
 * @param ty The enum type being populated.
 * @details Implemented as a single-iteration for loop; use `continue` rather than `break` to exit early so the pop still runs. */
#define WithEnum(ty)                                                   \
    for (int _cccc_enum_ctx_ = (__builtin_ast_push_enum((ty)), 1);       \
         _cccc_enum_ctx_;                                                 \
         _cccc_enum_ctx_ = (__builtin_ast_pop_enum(), 0))

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

    Type *ty = AttrTargetType(target);
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

    Type *ty = AttrTargetType(target);
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

    Type *ty = AttrTargetType(target);
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

    Type *ty = AttrTargetType(target);
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

    Type *ty = AttrTargetType(target);
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

    Type *ty = AttrTargetType(target);
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

    Type *ty = AttrTargetType(target);
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

/*! @brief Publish `T sum_T(T *arr, size_t n)` summing all elements.
 * @details Convenience wrapper: GenerateSum(elem_type).
 */
void __builtin_generate_sum(Type *elem_ty);

/*! @brief Publish `void map_T(T *arr, size_t n, T *out, T (*f)(T))`,
 *   writing `f(arr[i])` into `out[i]` for each element.
 * @details Convenience wrapper: GenerateMap(elem_type).
 */
void __builtin_generate_map(Type *elem_ty);

/*! @brief Publish `T reduce_T(T *arr, size_t n, T init, T (*f)(T, T))`,
 *   folding `f` over the array starting from `init`.
 * @details Convenience wrapper: GenerateReduce(elem_type).
 */
void __builtin_generate_reduce(Type *elem_ty);

/*! @brief Publish `void filter_T(T *arr, size_t n, T *out, size_t *out_n,
 *   bool (*pred)(T))`, writing elements matching `pred` into `out` and
 *   setting `*out_n` to the match count.
 * @details Convenience wrapper: GenerateFilter(elem_type).
 */
void __builtin_generate_filter(Type *elem_ty);

/*! @def GenerateSum
 * @brief Publish @c T @c sum_T(T @c *arr, size_t @c n) summing all elements.
 * @param elem_type The element type T. */
#define GenerateSum(elem_type)    __builtin_generate_sum(elem_type)
/*! @def GenerateMap
 * @brief Publish @c void @c map_T(T @c *arr, size_t @c n, T @c *out, T @c (*f)(T)).
 * @param elem_type The element type T. */
#define GenerateMap(elem_type)    __builtin_generate_map(elem_type)
/*! @def GenerateReduce
 * @brief Publish @c T @c reduce_T(T @c *arr, size_t @c n, T @c init, T @c (*f)(T, T)).
 * @param elem_type The element type T. */
#define GenerateReduce(elem_type) __builtin_generate_reduce(elem_type)
/*! @def GenerateFilter
 * @brief Publish @c void @c filter_T(T @c *arr, size_t @c n, T @c *out, size_t @c *out_n, bool @c (*pred)(T)).
 * @param elem_type The element type T. */
#define GenerateFilter(elem_type) __builtin_generate_filter(elem_type)

#ifdef __cplusplus
}
#endif

#endif // CCCC_REFLECTION_H
