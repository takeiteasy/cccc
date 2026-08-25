/*
 CCCC: Comprehensiev C Compensation Compiler

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

// Shared declarations for the src/parse_*.c translation units (ticket #717
// split of the former monolithic src/parse.c, itself originally part of
// chibicc by Rui Ueyama (MIT), https://github.com/rui314/chibicc). Nothing
// in here is public API; it exists only to let the split files call into
// each other.
//
// Together, src/parse_*.c contain a recursive descent parser for C.
//
// Most functions are named after the symbols they are supposed to read
// from an input token list. For example, stmt() is responsible for
// reading a statement from a token list. The function then constructs
// an AST node representing a statement.
//
// Each function conceptually returns two values, an AST node and
// remaining part of the input tokens. Since C doesn't support
// multiple return values, the remaining tokens are returned to the
// caller via a pointer argument.
//
// Input tokens are represented by a linked list. Unlike many recursive
// descent parsers, we don't have the notion of the "input token stream".
// Most parsing functions don't change the global state of the parser.
// So it is very easy to lookahead arbitrary number of tokens in this
// parser.
#ifndef CCCC_PARSE_INTERNAL_H
#define CCCC_PARSE_INTERNAL_H

#include "./internal.h"
#include <pthread.h> // pthread_once_t (see typename_map_once below)

#ifndef MAX
#define MAX(x, y) ((x) < (y) ? (y) : (x))
#endif
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

// Scope for local variables, global variables, typedefs
// or enum constants
typedef struct {
    Obj    *var;
    Type   *type_def;
    Type   *enum_ty;
    int64_t enum_val;
    bool    is_deprecated;
    char   *deprecated_msg;
    // #1095: mirrors EnumConstant.layout_ty/layout_is_align (cccc.h) --
    // copied here (enum-specifier parsing, parse_types.c) so that a later
    // *use* of the enumerator (an ND_NUM primary() synthesizes from this
    // scope entry) also carries the provenance, and flows through #1031's
    // own ND_NUM re-materialization in serialize_expr exactly like a bare
    // sizeof/_Alignof would. Without this, the enum body would print
    // "N = sizeof(struct statfs)" while every *use* of N still printed the
    // guest-folded literal -- the exact internal inconsistency #1095's own
    // ticket warns about, just between the body and its uses instead of
    // between a declaration and its initializer.
    Type *enum_layout_ty;
    bool  enum_layout_is_align;
    // #1155: every field above must appear in VarScopeNode (cccc.h), same
    // order, as a leading prefix -- push_scope() (parse_core.c) allocates a
    // VarScopeNode and callers cast it to VarScope*. See VarScopeNode's own
    // doc comment for the corruption this drifted into once before.
} VarScope;

// Variable attributes such as typedef or extern.
typedef struct {
    bool           is_typedef;
    bool           is_static;
    bool           is_extern;
    bool           is_inline;
    bool           is_tls;
    bool           is_constexpr;
    bool           is_block_var; // __block storage qualifier (Apple blocks)
    bool           is_auto; // C23 type inference (auto without explicit type)
    bool           is_maybe_unused;
    bool           is_deprecated;
    bool           is_noreturn;
    bool           is_nodiscard;
    bool           is_fallthrough;
    bool           is_pure;
    bool           is_func_const;
    char          *deprecated_msg;
    char          *nodiscard_msg;
    char          *attr_error_msg;   // __attribute__((error("msg")))
    char          *attr_warning_msg; // __attribute__((warning("msg")))
    Token         *attribute_tok;
    CustomAttrUse *custom_attrs;
    int            align;            // _Alignas(N): an assignment, can lower
    int gnu_align; // __attribute__((aligned(N))) / [[gnu::aligned(N)]]
                   // seen in declspec position: a floor, never lowers
                   // (#1160)

    // Per-function optimization level
    int  fn_optimize_level;
    bool fn_optimize_set;

    // Format string validation
    int format_style;         // 0=none/unvalidated, 1=printf, 2=scanf
    int format_string_index;  // 1-based index of format string arg
    int format_fmt_first_arg; // 1-based index of first variadic arg to check

    // Nonnull argument / return checking
    bool     nonnull_all;
    uint64_t nonnull_mask;
    bool     returns_nonnull;

    // NULL-terminated variadic argument check (__attribute__((sentinel[(N)])))
    bool is_sentinel;
    int  sentinel_pos;

    // __attribute__((alloc_size(n[,m]))) / __attribute__((malloc)) (#649)
    int  alloc_size_idx;
    int  alloc_size_idx2;
    bool is_malloc;

    // __attribute__((cleanup(fn)))
    Obj   *cleanup_fn;
    Token *cleanup_tok;

    // __attribute__((constructor[(priority)])) / ((destructor[(priority)]))
    bool is_constructor;
    bool is_destructor;
    int  init_priority; // CCCC_NO_INIT_PRIORITY if not explicitly given

    // __attribute__((vector_size(N))) / [[gnu::vector_size(N)]] (tracker #72)
    bool   has_vector_size;
    int    vector_size_bytes;
    Token *vector_size_tok; // for diagnostics
} VarAttr;

struct CustomAttrUse {
    char          *name;
    Token         *tok;
    Node          *args;
    int            arg_count;
    CustomAttrUse *next;
};

// This struct represents a variable initializer. Since initializers
// can be nested (e.g. `int x[2][2] = {{1, 2}, {3, 4}}`), this struct
// is a tree data structure.
typedef struct Initializer Initializer;
struct Initializer {
    Initializer *next;
    Type        *ty;
    Token       *tok;
    bool         is_flexible;
    bool is_set; // true once this slot has received an initializer value

    // If it's not an aggregate type and has an initializer,
    // `expr` has an initialization expression.
    Node *expr;

    // If it's an initializer for an aggregate type (e.g. array or struct),
    // `children` has initializers for its children.
    Initializer **children;

    // Only one member can be initialized for a union.
    // `mem` is used to clarify which member is initialized.
    Member *mem;
};

// For local variable initializer.
typedef struct InitDesg InitDesg;
struct InitDesg {
    InitDesg *next;
    int       idx;
    Member   *member;
    Obj      *var;
};

// __builtin_object_size helpers. Used from parse_analysis.c (the resolver
// functions) and parse_postfix.c (primary()'s __builtin_object_size call
// site).
typedef struct {
    int base_size;   // sizeof(base object); -1 = unknown
    int base_offset; // byte offset from start of base
    int sub_size;    // sizeof(nearest surrounding subobject); -1 = unknown
    int sub_offset;  // byte offset from start of nearest subobject
} ObjSizeInfo;

// #642: constant malloc-family allocation tracking for __builtin_object_size.
// A pending query on a malloc-tracked pointer var, resolved after the whole
// function body has been parsed (see resolve_objsize_queries) so that a
// reassignment or address-of appearing anywhere in the function -- including
// after the query textually, e.g. inside a loop back-edge -- can poison it.
// #697: `offset` is the compile-time-constant byte delta of an interior
// pointer expression (`p + k`, peeled from the builtin argument at
// registration time) into the tracked allocation; 0 for the original bare-var
// case. `var`'s poisoning (reassignment/address-of) still governs the whole
// query since the offset is measured from `var`'s own allocation. Used from
// parse_analysis.c and parse_decl.c/parse_postfix.c (registration sites).
struct ObjSizeQuery {
    Node *node;   // the ND_NUM node to (maybe) upgrade with the real size
    Obj  *var;    // the tracked pointer variable
    int   offset; // byte offset of the queried pointer into var's allocation
    struct ObjSizeQuery *next;
};

// Keyword-lookup table backing is_typename() (parse_stmt.c), lazily built by
// init_typename_map() (parse_init.c) via pthread_once -- see is_typename()'s
// own comment for why this is looked up through a hashmap rather than a
// switch. Defined in parse_init.c, used from parse_stmt.c.
extern HashMap        typename_map;
extern pthread_once_t typename_map_once;

// Types used in a cross-file function prototype below, so they must be
// visible here rather than staying local to their one natural home file.

typedef enum {
    DK_NONE = 0,
    // Storage class
    DK_TYPEDEF,
    DK_STATIC,
    DK_EXTERN,
    DK_INLINE,
    DK_TLS,
    DK_CONSTEXPR,
    DK_BLOCK_VAR,
    // Qualifiers
    DK_CONST,
    DK_VOLATILE,
    // Ignored
    DK_AUTO,
    DK_REGISTER,
    DK_RESTRICT,
    DK_NORETURN,
    // Special
    DK_ATOMIC,
    DK_ALIGNAS,
    // Composite
    DK_STRUCT,
    DK_UNION,
    DK_ENUM,
    DK_TYPEOF,
    DK_TYPEOF_UNQUAL,
    // Built-in types
    DK_VOID,
    DK_BOOL,
    DK_CHAR,
    DK_SHORT,
    DK_INT,
    DK_LONG,
    DK_FLOAT,
    DK_DOUBLE,
    DK_COMPLEX,
    DK_IMAGINARY,
    DK_SIGNED,
    DK_UNSIGNED,
    // C23 types
    DK_BITINT,
    DK_DECIMAL32,
    DK_DECIMAL64,
    DK_DECIMAL128,
    // GNU 128-bit integers (mapped onto _BitInt(128))
    DK_INT128,
} DeclKw;

// ========== Cross-file forward declarations ==========
//
// Every function below is used from at least one parse_*.c file other
// than the one that defines it, so each lost its `static` and gained a
// prototype here.

Type *abstract_declarator(VirtualMachine *vm, Token **rest, Token *tok,
                          Type *ty);
int align_down(int n, int align);
int align_to(int n, int align);
int effective_decl_align(VirtualMachine *vm, Token *tok, Type *ty,
                         VarAttr *attr);
int explicit_decl_align(VirtualMachine *vm, Token *tok, Type *ty,
                        VarAttr *attr);
void append_custom_attr(VirtualMachine *vm, CustomAttrUse **list,
                        Token *name_tok, Node *args, int arg_count);
void append_custom_attr_list(CustomAttrUse **dst, CustomAttrUse *src);
Token *apply_checked_ptr_attr(VirtualMachine *vm, Token *name_tok, Token *tok,
                              Type *ty, const char *name);
Type *apply_var_attrs_to_type(VirtualMachine *vm, Type *ty, VarAttr *attr);
Token *asm_label(VirtualMachine *vm, Token *tok, char **label);
Node *assign(VirtualMachine *vm, Token **rest, Token *tok);
Token *attribute_list(VirtualMachine *vm, Token *tok, Type *ty, VarAttr *attr);
Type *auto_deduced_type(VirtualMachine *vm, Type *ty);
Node *block_literal(VirtualMachine *vm, Token **rest, Token *tok);
Token *c23_attribute_list(VirtualMachine *vm, Token *tok, Type *ty,
                          VarAttr *attr);
Token *c23_attribute_list_ex(VirtualMachine *vm, Token *tok, Type *ty,
                             VarAttr *attr, bool allow_ty_align);
Node *cast(VirtualMachine *vm, Token **rest, Token *tok);
void check_may_return_null_summaries(VirtualMachine *vm);
void check_nonnull_flow(VirtualMachine *vm, Obj *fn);
int64_t classify_type_code(Type *ty);
Node *clone_bounds_node(VirtualMachine *vm, Node *n);
Node *compound_stmt(VirtualMachine *vm, Token **rest, Token *tok,
                    Token **close_tok);
Node *compute_vla_size(VirtualMachine *vm, Type *ty, Token *tok);
Node *conditional(VirtualMachine *vm, Token **rest, Token *tok);
Node *constexpr_expr_for_node(Node *node);
bool consume_end(Token **rest, Token *tok);
int count_auto_ptr_depth(Type *ty);
int count_ptr_depth(Type *ty);
Node *declaration(VirtualMachine *vm, Token **rest, Token *tok, Type *basety,
                  VarAttr *attr);
Type *declarator(VirtualMachine *vm, Token **rest, Token *tok, Type *ty);
Type *declspec(VirtualMachine *vm, Token **rest, Token *tok, VarAttr *attr);
DeclKw declspec_kw(Token *tok);
void enter_scope(VirtualMachine *vm);
int64_t eval(VirtualMachine *vm, Node *node);
int64_t eval2(VirtualMachine *vm, Node *node, char ***label);
void eval_decimal(VirtualMachine *vm, Node *node, int w, void *out);
double eval_double(VirtualMachine *vm, Node *node);
// Compile-time fold of a constant expression at arbitrary integer width
// (#1122). ty->size bytes are written to dst (little-endian words); ty must
// be an integer type. Reuses the src/stdlib/wide_bitint.c runtime helpers so
// the fold is bit-identical to the VM's own wide-_BitInt arithmetic.
void eval_wide(VirtualMachine *vm, Node *node, Type *ty, uint64_t *dst);
Node *expr(VirtualMachine *vm, Token **rest, Token *tok);
MacroFn *find_attribute_macro(VirtualMachine *vm, Token *tok);
MacroFn *find_macro_fn(VirtualMachine *vm, Token *tok);
TestFnRecord *find_neg_test_record(VirtualMachine *vm, const char *name);
Type *find_tag(VirtualMachine *vm, Token *tok);
Type *find_tag_in_current_scope(VirtualMachine *vm, Token *tok);
Type *find_typedef(VirtualMachine *vm, Token *tok);
VarScope *find_var(VirtualMachine *vm, Token *tok);
VarScope *find_var_in_current_scope(VirtualMachine *vm, char *name,
                                    int name_len);
Token *function(VirtualMachine *vm, Token *tok, Type *basety, VarAttr *attr);
Token *function_declaration_list(VirtualMachine *vm, Token *tok, Type *basety,
                                 VarAttr *attr);
char *get_ident(VirtualMachine *vm, Token *tok);
Member *get_struct_member(Type *ty, Token *tok);
int get_vm_size(Type *ty);
Token *global_variable(VirtualMachine *vm, Token *tok, Type *basety,
                       VarAttr *attr);
void gvar_initializer(VirtualMachine *vm, Token **rest, Token *tok, Obj *var);
bool has_custom_attrs(Type *ty, VarAttr *attr);
void init_typename_map(void);
bool is_attr_name(Token *tok, char *name);
bool is_compound_literal_head(VirtualMachine *vm, Token *tok);
bool is_const_expr(VirtualMachine *vm, Node *node);
bool is_decl_start(VirtualMachine *vm, Token *tok);
bool is_end(Token *tok);
bool is_function(VirtualMachine *vm, Token *tok, Type *basety);
bool is_function_decl_list(VirtualMachine *vm, Token *tok, Type *basety);
bool is_typename(VirtualMachine *vm, Token *tok);
void leave_scope(VirtualMachine *vm);
Node *lvar_initializer(VirtualMachine *vm, Token **rest, Token *tok, Obj *var);
void mark_addr_escapes(Node *node);
void mark_nested_captures(Obj *fn, Node *node);
Node *new_add(VirtualMachine *vm, Node *lhs, Node *rhs, Token *tok);
Obj *new_anon_gvar(VirtualMachine *vm, Type *ty);
Node *new_binary(VirtualMachine *vm, NodeKind kind, Node *lhs, Node *rhs,
                 Token *tok);
Node *new_complex_node(VirtualMachine *vm, Node *real, Node *imag, Type *ty,
                       Token *tok);
Obj *new_gvar(VirtualMachine *vm, char *name, int name_len, Type *ty);
Obj *new_implicit_function(VirtualMachine *vm, Token *tok);
Initializer *new_initializer(VirtualMachine *vm, Type *ty, bool is_flexible);
Node *new_long(VirtualMachine *vm, int64_t val, Token *tok);
Obj *new_lvar(VirtualMachine *vm, char *name, int name_len, Type *ty);
Node *new_node(VirtualMachine *vm, NodeKind kind, Token *tok);
Node *new_num(VirtualMachine *vm, int64_t val, Token *tok);
Obj *new_private_func_obj(VirtualMachine *vm, const char *name, Type *ty);
Obj *new_string_literal(VirtualMachine *vm, char *p, Type *ty);
Node *new_sub(VirtualMachine *vm, Node *lhs, Node *rhs, Token *tok);
Node *new_ulong(VirtualMachine *vm, long val, Token *tok);
Node *new_unary(VirtualMachine *vm, NodeKind kind, Node *expr, Token *tok);
char *new_unique_name(VirtualMachine *vm);
Obj *new_var(VirtualMachine *vm, char *name, int name_len, Type *ty);
Node *new_var_node(VirtualMachine *vm, Obj *var, Token *tok);
Node *new_vla_ptr(VirtualMachine *vm, Obj *var, Token *tok);
bool node_has_side_effects(Node *n);
bool nodes_structurally_equal(Node *a, Node *b);
char *obj_display_name(Obj *var);
bool objsize_alloc_from_call(VirtualMachine *vm, Node *rhs, int *out);
bool objsize_peel_offset_chain(VirtualMachine *vm, Node *node, Obj **out_base,
                               int *out_offset);
bool objsize_resolve_ptr(VirtualMachine *vm, Node *node, ObjSizeInfo *r);
Token *parse_custom_attr_args(VirtualMachine *vm, Token *tok, Node **args,
                              int *arg_count);
Token *parse_typedef(VirtualMachine *vm, Token *tok, Type *basety,
                     VarAttr *attr);
void propagate_checked_bounds(VirtualMachine *vm, Obj *fn);
VarScope *push_scope(VirtualMachine *vm, char *name, int name_len);
void push_tag_scope(VirtualMachine *vm, Token *tok, Type *ty);
void record_type_name(VirtualMachine *vm, Type *ty, char *name, int name_len,
                      bool is_tag, Token *decl_tok);
void mark_last_type_name_as_definition(VirtualMachine *vm, Type *ty); // #1010
void resolve_checked_bounds(VirtualMachine *vm, Obj *var);
void resolve_member_checked_bounds(VirtualMachine *vm, Member *members);
void resolve_objsize_queries(VirtualMachine *vm, Node *body);
void run_decl_custom_attrs(VirtualMachine *vm, Type *ty, VarAttr *attr,
                           AttrTargetKind kind, char *name, Type *target_ty,
                           Obj *obj, Token *tok);
void set_checked_deref_bounds(VirtualMachine *vm, Node *deref, Node *addr,
                              Token *tok);
Token *skip_to_decl_boundary(VirtualMachine *vm, Token *tok);
Token *skip_to_stmt_end(VirtualMachine *vm, Token *tok);
Token *static_assert_decl(VirtualMachine *vm, Token *tok, Node **out_cond,
                          char **out_msg, int *out_msg_len);
int static_branch_value(VirtualMachine *vm, Node *cond);
Node *stmt(VirtualMachine *vm, Token **rest, Token *tok);
Node *to_assign(VirtualMachine *vm, Node *binary);
Type *type_after_deprecated_use(VirtualMachine *vm, Type *ty);
bool type_has_restrict(Type *ty);
Type *typename(VirtualMachine *vm, Token **rest, Token *tok);
Node *unary(VirtualMachine *vm, Token **rest, Token *tok);
void validate_nonnull_call(VirtualMachine *vm, Type *func_ty, Node *args);
void validate_sentinel_call(VirtualMachine *vm, Token *tok, Type *func_ty,
                            Node *args);
Node *vector_lane_ref(VirtualMachine *vm, Node *vec_expr, Type *elem_ty,
                      Node *index, Token *tok);
void verify_checked_assign_bounds(VirtualMachine *vm, Obj *fn);
void warn_deprecated_use(VirtualMachine *vm, Token *tok, char *name,
                         char *message);
void warn_if_shadowing(VirtualMachine *vm, Token *tok);

#endif // CCCC_PARSE_INTERNAL_H
