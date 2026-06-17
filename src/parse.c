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

 This file was original part of chibicc by Rui Ueyama (MIT)
 https://github.com/rui314/chibicc
*/

// This file contains a recursive descent parser for C.
//
// Most functions in this file are named after the symbols they are
// supposed to read from an input token list. For example, stmt() is
// responsible for reading a statement from a token list. The function
// then construct an AST node representing a statement.
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

#include "./internal.h"
#include "cccc.h"
#include <limits.h>

#ifndef MAX
#define MAX(x, y) ((x) < (y) ? (y) : (x))
#endif
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

// Scope for local variables, global variables, typedefs
// or enum constants
typedef struct {
    Obj *var;
    Type *type_def;
    Type *enum_ty;
    int64_t enum_val;
    bool is_deprecated;
    char *deprecated_msg;
} VarScope;

// Variable attributes such as typedef or extern.
typedef struct {
    bool is_typedef;
    bool is_static;
    bool is_extern;
    bool is_inline;
    bool is_tls;
    bool is_constexpr;
    bool is_block_var; // __block storage qualifier (Apple blocks)
    bool is_auto;      // C23 type inference (auto without explicit type)
    bool is_maybe_unused;
    bool is_deprecated;
    bool is_noreturn;
    bool is_nodiscard;
    bool is_fallthrough;
    bool is_pure;
    bool is_func_const;
    char *deprecated_msg;
    char *nodiscard_msg;
    Token *attribute_tok;
    CustomAttrUse *custom_attrs;
    int align;

    // Per-function optimization level
    int  fn_optimize_level;
    bool fn_optimize_set;

    // Format string validation
    int format_style;          // 0=none, 1=printf, 2=scanf
    int format_string_index;   // 1-based index of format string arg
    int format_fmt_first_arg;  // 1-based index of first variadic arg to check
} VarAttr;

struct CustomAttrUse {
    char *name;
    Token *tok;
    Node *args;
    int arg_count;
    CustomAttrUse *next;
};

// This struct represents a variable initializer. Since initializers
// can be nested (e.g. `int x[2][2] = {{1, 2}, {3, 4}}`), this struct
// is a tree data structure.
typedef struct Initializer Initializer;
struct Initializer {
    Initializer *next;
    Type *ty;
    Token *tok;
    bool is_flexible;

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
    int idx;
    Member *member;
    Obj *var;
};

// Error placeholder variable for recovery
static Obj error_var_obj = {
    .name = "<error>",
    .ty = NULL, // Will be set to ty_error during initialization
    .is_local = false,
};
static Obj *error_var = &error_var_obj;

static bool is_typename(VirtualMachine *vm, Token *tok);
static Type *declspec(VirtualMachine *vm, Token **rest, Token *tok, VarAttr *attr);
static Type *typename(VirtualMachine *vm, Token **rest, Token *tok);
static Type *enum_specifier(VirtualMachine *vm, Token **rest, Token *tok);
static Type *typeof_specifier(VirtualMachine *vm, Token **rest, Token *tok);
static Type *typeof_unqual_specifier(VirtualMachine *vm, Token **rest, Token *tok);
static Type *type_suffix(VirtualMachine *vm, Token **rest, Token *tok, Type *ty);
static Type *declarator(VirtualMachine *vm, Token **rest, Token *tok, Type *ty);
static Token *attribute_list(VirtualMachine *vm, Token *tok, Type *ty, VarAttr *attr);
static Token *c23_attribute_list(VirtualMachine *vm, Token *tok, Type *ty, VarAttr *attr);
static void inherit_semantic_attrs(Type *dst, Type *src);
static Type *apply_var_attrs_to_type(VirtualMachine *vm, Type *ty, VarAttr *attr);
static Node *declaration(VirtualMachine *vm, Token **rest, Token *tok, Type *basety,
                         VarAttr *attr);
static Token *function_declaration_list(VirtualMachine *vm, Token *tok,
                                        Type *basety, VarAttr *attr);
static void array_initializer2(VirtualMachine *vm, Token **rest, Token *tok,
                               Initializer *init, int i);
static void struct_initializer2(VirtualMachine *vm, Token **rest, Token *tok,
                                Initializer *init, Member *mem);
static void initializer2(VirtualMachine *vm, Token **rest, Token *tok, Initializer *init);
static Initializer *initializer(VirtualMachine *vm, Token **rest, Token *tok, Type *ty,
                                Type **new_ty);
static Node *lvar_initializer(VirtualMachine *vm, Token **rest, Token *tok, Obj *var);
static void gvar_initializer(VirtualMachine *vm, Token **rest, Token *tok, Obj *var);
static Node *create_vla_init(VirtualMachine *vm, Initializer *init, Type *ty, Obj *var,
                             Token *tok);
static Node *compound_stmt(VirtualMachine *vm, Token **rest, Token *tok, Token **close_tok);
static Node *stmt(VirtualMachine *vm, Token **rest, Token *tok);
static Node *expr_stmt(VirtualMachine *vm, Token **rest, Token *tok);
static Node *expr(VirtualMachine *vm, Token **rest, Token *tok);
static int64_t eval(VirtualMachine *vm, Node *node);
static int64_t eval2(VirtualMachine *vm, Node *node, char ***label);
static int64_t eval_rval(VirtualMachine *vm, Node *node, char ***label);
static bool is_const_expr(VirtualMachine *vm, Node *node);
static bool is_constexpr_object_type(Type *ty);
static void validate_constexpr_object_type(VirtualMachine *vm, Token *tok, Type *ty);
static void validate_constexpr_initializer(VirtualMachine *vm, Obj *var, Initializer *init,
                                           Token *tok);
static Token *static_assert_decl(VirtualMachine *vm, Token *tok);
static bool expr_has_no_side_effects(Node *n);
static bool nodes_structurally_equal(Node *a, Node *b);
static Node *assign(VirtualMachine *vm, Token **rest, Token *tok);
static Node *logor(VirtualMachine *vm, Token **rest, Token *tok);
static double eval_double(VirtualMachine *vm, Node *node);
static Node *conditional(VirtualMachine *vm, Token **rest, Token *tok);
static Node *logand(VirtualMachine *vm, Token **rest, Token *tok);
static Node *bitor(VirtualMachine *vm, Token **rest, Token *tok);
static Node *bitxor(VirtualMachine *vm, Token **rest, Token *tok);
static Node *bitand(VirtualMachine *vm, Token **rest, Token *tok);
static Node *equality(VirtualMachine *vm, Token **rest, Token *tok);
static Node *relational(VirtualMachine *vm, Token **rest, Token *tok);
static Node *shift(VirtualMachine *vm, Token **rest, Token *tok);
static Node *add(VirtualMachine *vm, Token **rest, Token *tok);
static Node *new_add(VirtualMachine *vm, Node *lhs, Node *rhs, Token *tok);
static Node *new_sub(VirtualMachine *vm, Node *lhs, Node *rhs, Token *tok);
static Node *mul(VirtualMachine *vm, Token **rest, Token *tok);
static Node *cast(VirtualMachine *vm, Token **rest, Token *tok);
static Member *get_struct_member(Type *ty, Token *tok);
static Type *struct_decl(VirtualMachine *vm, Token **rest, Token *tok);
static Type *union_decl(VirtualMachine *vm, Token **rest, Token *tok);
static bool is_compound_literal_head(VirtualMachine *vm, Token *tok);
static Node *postfix(VirtualMachine *vm, Token **rest, Token *tok);
static Node *funcall(VirtualMachine *vm, Token **rest, Token *tok, Node *node);
static Node *unary(VirtualMachine *vm, Token **rest, Token *tok);
static Node *primary(VirtualMachine *vm, Token **rest, Token *tok);
static Token *parse_typedef(VirtualMachine *vm, Token *tok, Type *basety, VarAttr *attr);
static bool falls_through(Node *n);
static void warn_switch_fallthrough(VirtualMachine *vm, Node *sw);
static bool is_function(VirtualMachine *vm, Token *tok, Type *basety);
static bool is_function_decl_list(VirtualMachine *vm, Token *tok, Type *basety);
static Token *function(VirtualMachine *vm, Token *tok, Type *basety, VarAttr *attr);
static Token *global_variable(VirtualMachine *vm, Token *tok, Type *basety, VarAttr *attr);

static int align_to(int n, int align) {
    return (int)(((long long)n + align - 1) / align * align);
}

// Return the TestFnRecord for this name if it is a negative test (has error_pat), else NULL.
static TestFnRecord *find_neg_test_record(VirtualMachine *vm, const char *name) {
    if (!name) return NULL;
    for (TestFnRecord *r = vm->compiler.test_fns; r; r = r->next)
        if (r->error_pat && strcmp(r->name, name) == 0)
            return r;
    return NULL;
}

static int align_down(int n, int align) {
    return align_to(n - align + 1, align);
}

static void enter_scope(VirtualMachine *vm) {
    Scope *sc = arena_alloc(&vm->compiler.parser_arena, sizeof(Scope));
    memset(sc, 0, sizeof(Scope));
    sc->next = vm->compiler.scope;
    vm->compiler.scope = sc;
}

static bool type_has_restrict(Type *ty) {
    for (; ty; ty = ty->base) {
        if (ty->is_restrict)
            return true;
    }
    return false;
}

static char *obj_display_name(Obj *var) {
    return var->display_name ? var->display_name : var->name;
}

static void warn_deprecated_use(VirtualMachine *vm, Token *tok, char *name,
                                char *message) {
    if (message)
        warn_tok(vm, tok, CCCC_WARN_DEPRECATED, "'%s' is deprecated: %s", name,
                 message);
    else
        warn_tok(vm, tok, CCCC_WARN_DEPRECATED, "'%s' is deprecated", name);
}

static Type *type_after_deprecated_use(VirtualMachine *vm, Type *ty) {
    Type *copy = copy_type(vm, ty);
    copy->is_deprecated = false;
    copy->deprecated_msg = NULL;
    return copy;
}

static void warn_unused_scope(VirtualMachine *vm, Scope *sc) {
    for (VarScopeNode *node = sc->vars; node; node = node->next) {
        Obj *var = node->var;
        if (!var || !var->is_local_symbol || !var->tok || var->is_used ||
            var->is_maybe_unused)
            continue;
        warn_tok(vm, var->tok, CCCC_WARN_UNUSED, "unused %s '%s'",
                 var->is_param ? "parameter" : "variable",
                 obj_display_name(var));
    }
}

static void leave_scope(VirtualMachine *vm) {
    warn_unused_scope(vm, vm->compiler.scope);
    hashmap_deinit_borrowed(&vm->compiler.scope->var_map);
    hashmap_deinit_borrowed(&vm->compiler.scope->tag_map);
    vm->compiler.scope = vm->compiler.scope->next;
}

// Find a variable by name.
static VarScope *find_var(VirtualMachine *vm, Token *tok) {
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        if (sc->var_map.buckets) {
            VarScopeNode *node = hashmap_get2(&sc->var_map, tok->loc, tok->len);
            if (node)
                return (VarScope *)node;
        } else {
            for (VarScopeNode *node = sc->vars; node; node = node->next) {
                if (node->name_len == tok->len &&
                    strncmp(node->name, tok->loc, tok->len) == 0)
                    return (VarScope *)node;
            }
        }
    }
    return NULL;
}

static void warn_if_shadowing(VirtualMachine *vm, Token *tok) {
    if (!tok || !vm->compiler.current_fn || !vm->compiler.scope)
        return;

    for (Scope *sc = vm->compiler.scope->next; sc; sc = sc->next) {
        VarScopeNode *node = sc->var_map.buckets
                                 ? hashmap_get2(&sc->var_map, tok->loc, tok->len)
                                 : NULL;
        if (!node) {
            for (node = sc->vars; node; node = node->next)
                if (node->name_len == tok->len &&
                    !strncmp(node->name, tok->loc, tok->len))
                    break;
        }
        if (!node)
            continue;
        if (node->var && !node->var->is_function)
            warn_tok(vm, tok, CCCC_WARN_SHADOW,
                     "declaration of '%.*s' shadows an outer variable",
                     tok->len, tok->loc);
        return;
    }
}

// Find a macro function by name
static MacroFn *find_macro_fn(VirtualMachine *vm, Token *tok) {
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next) {
        if (pm->is_macro_entry && strlen(pm->name) == tok->len &&
            strncmp(pm->name, tok->loc, tok->len) == 0) {
            return pm;
        }
    }
    return NULL;
}

static MacroFn *find_attribute_macro(VirtualMachine *vm, Token *tok) {
    if (vm->compiler.in_macro_mode)
        return NULL;
    if (!tok || tok->kind != TK_IDENT)
        return NULL;
    // Ticket #235: make sure reflection.h's built-in @macro(attribute(...))
    // handlers (e.g. __cccc_attr_serialize) are registered before searching.
    ensure_reflection_attrs_registered(vm);
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next) {
        if (!pm->is_attribute_handler || !pm->attribute_name)
            continue;
        if (strlen(pm->attribute_name) == tok->len &&
            strncmp(pm->attribute_name, tok->loc, tok->len) == 0)
            return pm;
    }
    return NULL;
}

static void append_custom_attr(VirtualMachine *vm, CustomAttrUse **list, Token *name_tok,
                               Node *args, int arg_count) {
    if (!list || !name_tok)
        return;
    CustomAttrUse *use =
        arena_alloc(&vm->compiler.parser_arena, sizeof(CustomAttrUse));
    memset(use, 0, sizeof(CustomAttrUse));
    use->name = arena_strndup(vm, name_tok->loc, name_tok->len);
    use->tok = name_tok;
    use->args = args;
    use->arg_count = arg_count;

    if (!*list) {
        *list = use;
        return;
    }
    CustomAttrUse *tail = *list;
    while (tail->next)
        tail = tail->next;
    tail->next = use;
}

static Token *skip_paren_group(VirtualMachine *vm, Token *tok) {
    tok = skip(vm, tok, "(");
    int depth = 1;
    while (tok && tok->kind != TK_EOF && depth > 0) {
        if (equal(tok, "("))
            depth++;
        else if (equal(tok, ")"))
            depth--;
        tok = tok->next;
    }
    return tok;
}

static Token *skip_c23_attr_group(Token *tok) {
    if (!equal(tok, "[") || !tok->next || !equal(tok->next, "["))
        return tok;
    tok = tok->next->next;
    while (tok && tok->kind != TK_EOF) {
        if (equal(tok, "]") && tok->next && equal(tok->next, "]"))
            return tok->next->next;
        tok = tok->next;
    }
    return tok;
}

static bool is_decl_start(VirtualMachine *vm, Token *tok) {
    for (;;) {
        if (equal(tok, "__attribute__")) {
            Token *p = tok->next;
            if (!p || !equal(p, "(") || !p->next || !equal(p->next, "("))
                return false;
            p = skip_paren_group(vm, p);
            if (!p)
                return false;
            tok = p;
            continue;
        }
        if (equal(tok, "[") && tok->next && equal(tok->next, "[")) {
            tok = skip_c23_attr_group(tok);
            continue;
        }
        if (tok && tok->kind == TK_IDENT && tok->next && equal(tok->next, ":"))
            return false;
        return is_typename(vm, tok);
    }
}

static Token *parse_custom_attr_args(VirtualMachine *vm, Token *tok, Node **args,
                                     int *arg_count) {
    *args = NULL;
    *arg_count = 0;
    if (!equal(tok, "("))
        return tok;

    if (vm->compiler.in_type_lookahead)
        return skip_paren_group(vm, tok);

    tok = tok->next;
    Node head = {};
    Node *cur = &head;
    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(vm, tok, ",");
        Node *arg = assign(vm, &tok, tok);
        cur = cur->next = arg;
        (*arg_count)++;
    }
    *args = head.next;
    return tok->next;
}

static void run_custom_attrs(VirtualMachine *vm, CustomAttrUse *attrs,
                             AttrTargetKind kind, char *name, Type *ty,
                             Obj *obj, Token *tok) {
    // Ticket #235: make sure reflection.h's built-in @macro(attribute(...))
    // handlers (e.g. __cccc_attr_serialize) are registered before searching.
    if (attrs)
        ensure_reflection_attrs_registered(vm);
    for (CustomAttrUse *use = attrs; use; use = use->next) {
        MacroFn *pm = NULL;
        for (MacroFn *m = vm->compiler.macro_fns; m; m = m->next) {
            if (m->is_attribute_handler && m->attribute_name &&
                !strcmp(m->attribute_name, use->name)) {
                pm = m;
                break;
            }
        }
        if (!pm)
            error_tok(vm, use->tok, "undefined custom attribute '%s'",
                      use->name);

        AttrTarget *target =
            arena_alloc(&vm->compiler.parser_arena, sizeof(AttrTarget));
        memset(target, 0, sizeof(AttrTarget));
        target->kind = kind;
        target->name = name;
        target->ty = ty;
        target->obj = obj;
        target->tok = tok ? tok : use->tok;
        cc_execute_attribute_macro(vm, pm, use->tok, target, use->args,
                                   use->arg_count);
    }
}

static bool has_custom_attrs(Type *ty, VarAttr *attr) {
    return (ty && ty->custom_attrs) || (attr && attr->custom_attrs);
}

static void run_decl_custom_attrs(VirtualMachine *vm, Type *ty, VarAttr *attr,
                                  AttrTargetKind kind, char *name,
                                  Type *target_ty, Obj *obj, Token *tok) {
    if (attr && attr->custom_attrs)
        run_custom_attrs(vm, attr->custom_attrs, kind, name, target_ty, obj,
                         tok);
    if (ty && ty->custom_attrs)
        run_custom_attrs(vm, ty->custom_attrs, kind, name, target_ty, obj,
                         tok);
}

static void append_custom_attr_list(CustomAttrUse **dst, CustomAttrUse *src) {
    if (!dst || !src)
        return;
    if (!*dst) {
        *dst = src;
        return;
    }
    CustomAttrUse *tail = *dst;
    while (tail->next)
        tail = tail->next;
    tail->next = src;
}

static Type *find_tag(VirtualMachine *vm, Token *tok) {
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        if (sc->tag_map.buckets) {
            TagScopeNode *node = hashmap_get2(&sc->tag_map, tok->loc, tok->len);
            if (node)
                return node->ty;
        } else {
            for (TagScopeNode *node = sc->tags; node; node = node->next) {
                if (node->name_len == tok->len &&
                    strncmp(node->name, tok->loc, tok->len) == 0)
                    return node->ty;
            }
        }
    }
    return NULL;
}

static Type *find_tag_in_current_scope(VirtualMachine *vm, Token *tok) {
    Scope *sc = vm->compiler.scope;
    if (!sc)
        return NULL;
    if (sc->tag_map.buckets) {
        TagScopeNode *node = hashmap_get2(&sc->tag_map, tok->loc, tok->len);
        return node ? node->ty : NULL;
    }
    for (TagScopeNode *node = sc->tags; node; node = node->next)
        if (node->name_len == tok->len &&
            strncmp(node->name, tok->loc, tok->len) == 0)
            return node->ty;
    return NULL;
}

static VarScope *find_var_in_current_scope(VirtualMachine *vm, char *name, int name_len) {
    Scope *sc = vm->compiler.scope;
    if (!sc)
        return NULL;
    if (sc->var_map.buckets) {
        VarScopeNode *node = hashmap_get2(&sc->var_map, name, name_len);
        return node ? (VarScope *)node : NULL;
    }
    for (VarScopeNode *node = sc->vars; node; node = node->next)
        if (node->name_len == name_len &&
            strncmp(node->name, name, name_len) == 0)
            return (VarScope *)node;
    return NULL;
}

static Node *new_node(VirtualMachine *vm, NodeKind kind, Token *tok) {
    Node *node = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    memset(node, 0, sizeof(Node));
    node->kind = kind;
    node->tok = tok;
    return node;
}

static Node *new_binary(VirtualMachine *vm, NodeKind kind, Node *lhs, Node *rhs,
                        Token *tok) {
    Node *node = new_node(vm, kind, tok);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

static Node *new_unary(VirtualMachine *vm, NodeKind kind, Node *expr, Token *tok) {
    Node *node = new_node(vm, kind, tok);
    node->lhs = expr;
    return node;
}

static Node *new_num(VirtualMachine *vm, int64_t val, Token *tok) {
    Node *node = new_node(vm, ND_NUM, tok);
    node->val = val;
    node->ty = ty_int;
    return node;
}

static Node *new_long(VirtualMachine *vm, int64_t val, Token *tok) {
    Node *node = new_node(vm, ND_NUM, tok);
    node->val = val;
    node->ty = ty_long;
    return node;
}

static Node *new_ulong(VirtualMachine *vm, long val, Token *tok) {
    Node *node = new_node(vm, ND_NUM, tok);
    node->val = val;
    node->ty = ty_ulong;
    return node;
}

static Node *new_complex_node(VirtualMachine *vm, Node *real, Node *imag, Type *ty,
                              Token *tok) {
    Node *node = new_node(vm, ND_COMPLEX, tok);
    node->lhs = real;
    node->rhs = imag;
    node->ty = ty;
    return node;
}

static Node *new_var_node(VirtualMachine *vm, Obj *var, Token *tok) {
    Node *node = new_node(vm, ND_VAR, tok);
    node->var = var;
    return node;
}

static Node *new_vla_ptr(VirtualMachine *vm, Obj *var, Token *tok) {
    Node *node = new_node(vm, ND_VLA_PTR, tok);
    node->var = var;
    return node;
}

Node *new_cast(VirtualMachine *vm, Node *expr, Type *ty) {
    add_type(vm, expr);
    Node *node = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    memset(node, 0, sizeof(Node));
    node->kind = ND_CAST;
    node->tok = expr->tok;
    node->lhs = expr;
    node->ty = copy_type(vm, ty);
    return node;
}

static VarScope *push_scope(VirtualMachine *vm, char *name, int name_len) {
    VarScopeNode *node =
        arena_alloc(&vm->compiler.parser_arena, sizeof(VarScopeNode));
    memset(node, 0, sizeof(VarScopeNode));
    node->name = name;
    node->name_len = name_len;
    node->next = vm->compiler.scope->vars;
    vm->compiler.scope->vars = node;
    hashmap_put2_borrowed(&vm->compiler.scope->var_map, name, name_len, node);
    return (VarScope *)node;
}

static void record_type_name(VirtualMachine *vm, Type *ty, char *name, int name_len,
                             bool is_tag) {
    if (!ty || !name || name_len <= 0)
        return;

    TypeNameRecord *rec =
        arena_alloc(&vm->compiler.parser_arena, sizeof(TypeNameRecord));
    memset(rec, 0, sizeof(TypeNameRecord));
    rec->ty = ty;
    rec->name = name;
    rec->name_len = name_len;
    rec->owner_fn = vm->compiler.current_fn;
    rec->is_tag = is_tag;
    rec->next = vm->compiler.type_names;
    vm->compiler.type_names = rec;
}

static Initializer *new_initializer(VirtualMachine *vm, Type *ty, bool is_flexible) {
    Initializer *init =
        arena_alloc(&vm->compiler.parser_arena, sizeof(Initializer));
    memset(init, 0, sizeof(Initializer));
    init->ty = ty;

    if (ty->kind == TY_ARRAY) {
        if (is_flexible && ty->size < 0) {
            init->is_flexible = true;
            return init;
        }

        init->children = arena_alloc(&vm->compiler.parser_arena,
                                     ty->array_len * sizeof(Initializer *));
        memset(init->children, 0, ty->array_len * sizeof(Initializer *));
        for (int i = 0; i < ty->array_len; i++)
            init->children[i] = new_initializer(vm, ty->base, false);
        return init;
    }

    // VLA initialization: treat like flexible array - will be sized during
    // parsing
    if (ty->kind == TY_VLA) {
        init->is_flexible = true;
        return init;
    }

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        // Count the number of struct members.
        int len = 0;
        for (Member *mem = ty->members; mem; mem = mem->next)
            len++;

        init->children = arena_alloc(&vm->compiler.parser_arena,
                                     len * sizeof(Initializer *));
        memset(init->children, 0, len * sizeof(Initializer *));

        for (Member *mem = ty->members; mem; mem = mem->next) {
            if (is_flexible && ty->is_flexible && !mem->next) {
                Initializer *child = arena_alloc(&vm->compiler.parser_arena,
                                                 sizeof(Initializer));
                memset(child, 0, sizeof(Initializer));
                child->ty = mem->ty;
                child->is_flexible = true;
                init->children[mem->idx] = child;
            } else {
                init->children[mem->idx] = new_initializer(vm, mem->ty, false);
            }
        }
        return init;
    }

    return init;
}

static Obj *new_var(VirtualMachine *vm, char *name, int name_len, Type *ty) {
    Obj *var = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(var, 0, sizeof(Obj));
    var->name = name;
    var->ty = ty;
    var->asm_label = ty->asm_label;
    var->align = ty->align;
    var->tok = ty->name;
    var->display_name =
        ty->name ? arena_strndup(vm, ty->name->loc, ty->name->len) : name;
    var->is_maybe_unused = ty->is_maybe_unused;
    var->is_deprecated = ty->is_deprecated;
    var->is_noreturn = ty->is_noreturn;
    var->is_pure = ty->is_pure;
    var->is_func_const = ty->is_func_const;
    var->fn_optimize_level = ty->fn_optimize_level;
    var->fn_optimize_set = ty->fn_optimize_set;
    var->deprecated_msg = ty->deprecated_msg;
    push_scope(vm, name, name_len)->var = var;
    return var;
}

static Obj *new_lvar(VirtualMachine *vm, char *name, int name_len, Type *ty) {
    warn_if_shadowing(vm, ty->name);
    Obj *var = new_var(vm, name, name_len, ty);
    var->is_local = true;
    var->is_local_symbol = ty->name && name_len > 0;
    var->next = vm->compiler.locals;
    vm->compiler.locals = var;
    return var;
}

static Obj *new_gvar(VirtualMachine *vm, char *name, int name_len, Type *ty) {
    Obj *var = new_var(vm, name, name_len, ty);
    var->next = vm->compiler.globals;
    var->is_static = true;
    var->is_definition = true;
    vm->compiler.globals = var;
    return var;
}

static Obj *new_implicit_function(VirtualMachine *vm, Token *tok) {
    Type *ty = func_type(vm, ty_int);
    ty->is_variadic = true;

    Scope *saved_scope = vm->compiler.scope;
    while (vm->compiler.scope->next)
        vm->compiler.scope = vm->compiler.scope->next;
    Obj *fn = new_gvar(vm, arena_strndup(vm, tok->loc, tok->len), tok->len, ty);
    vm->compiler.scope = saved_scope;

    fn->is_function = true;
    fn->is_definition = false;
    fn->is_static = false;
    fn->is_implicit = true;
    return fn;
}

static char *new_unique_name(VirtualMachine *vm) {
    return arena_format(vm, ".L..%d", vm->compiler.unique_name_counter++);
}

static Obj *new_anon_gvar(VirtualMachine *vm, Type *ty) {
    char *name = new_unique_name(vm);
    return new_gvar(vm, name, strlen(name), ty);
}

static Obj *new_string_literal(VirtualMachine *vm, char *p, Type *ty) {
    Obj *var = new_anon_gvar(vm, ty);
    var->init_data = p;
    return var;
}

static char *get_ident(VirtualMachine *vm, Token *tok) {
    if (tok->kind != TK_IDENT)
        error_tok(vm, tok, "expected an identifier, found '%.*s'", tok->len,
                  tok->loc);
    char *s = arena_alloc(&vm->compiler.parser_arena, tok->len + 1);
    memcpy(s, tok->loc, tok->len);
    s[tok->len] = '\0';
    return s;
}

// Error recovery helper: Skip to end of statement (semicolon or closing brace)
static Token *skip_to_stmt_end(VirtualMachine *vm, Token *tok) {
    int paren_depth = 0, brace_depth = 0;

    while (tok->kind != TK_EOF) {
        if (equal(tok, "("))
            paren_depth++;
        if (equal(tok, ")") && paren_depth > 0)
            paren_depth--;
        if (equal(tok, "{"))
            brace_depth++;
        if (equal(tok, "}")) {
            if (brace_depth > 0) {
                brace_depth--;
            } else {
                return tok; // Found unmatched closing brace
            }
        }

        // Only treat semicolon as end if we're at same nesting level
        if (paren_depth == 0 && brace_depth == 0 && equal(tok, ";"))
            return tok->next;

        tok = tok->next;
    }
    return tok;
}

// Error recovery helper: Skip to next declarator boundary
static Token *skip_to_decl_boundary(VirtualMachine *vm, Token *tok) {
    int paren_depth = 0;

    while (tok->kind != TK_EOF) {
        if (equal(tok, "("))
            paren_depth++;
        if (equal(tok, ")") && paren_depth > 0)
            paren_depth--;

        if (paren_depth == 0) {
            if (equal(tok, ","))
                return tok->next; // Next declarator
            if (equal(tok, ";"))
                return tok->next; // End of declaration
            if (equal(tok, "{"))
                return tok; // Function body start
        }

        tok = tok->next;
    }
    return tok;
}

static Type *find_typedef(VirtualMachine *vm, Token *tok) {
    if (tok->kind == TK_IDENT) {
        VarScope *sc = find_var(vm, tok);
        if (sc)
            return sc->type_def;
    }
    return NULL;
}

static void push_tag_scope(VirtualMachine *vm, Token *tok, Type *ty) {
    TagScopeNode *node =
        arena_alloc(&vm->compiler.parser_arena, sizeof(TagScopeNode));
    node->name = tok->loc;
    node->name_len = tok->len;
    node->ty = ty;
    node->next = vm->compiler.scope->tags;
    vm->compiler.scope->tags = node;
    hashmap_put2_borrowed(&vm->compiler.scope->tag_map, tok->loc, tok->len, node);
    record_type_name(vm, ty, tok->loc, tok->len, true);
}

typedef enum {
    DK_NONE = 0,
    // Storage class
    DK_TYPEDEF, DK_STATIC, DK_EXTERN, DK_INLINE, DK_TLS, DK_CONSTEXPR, DK_BLOCK_VAR,
    // Qualifiers
    DK_CONST, DK_VOLATILE,
    // Ignored
    DK_AUTO, DK_REGISTER, DK_RESTRICT, DK_NORETURN,
    // Special
    DK_ATOMIC, DK_ALIGNAS,
    // Composite
    DK_STRUCT, DK_UNION, DK_ENUM, DK_TYPEOF, DK_TYPEOF_UNQUAL,
    // Built-in types
    DK_VOID, DK_BOOL, DK_CHAR, DK_SHORT, DK_INT, DK_LONG,
    DK_FLOAT, DK_DOUBLE, DK_COMPLEX, DK_IMAGINARY, DK_SIGNED, DK_UNSIGNED,
    // C23 types
    DK_BITINT, DK_DECIMAL32, DK_DECIMAL64, DK_DECIMAL128,
} DeclKw;

static DeclKw declspec_kw(Token *tok) {
    const char *s = tok->loc;
    switch (tok->len) {
    case 3:  // int
        if (s[0]=='i' && s[1]=='n' && s[2]=='t') return DK_INT;
        break;
    case 4:  // auto, bool, char, enum, long, void
        switch (s[0]) {
        case 'a': if (s[1]=='u' && s[2]=='t' && s[3]=='o') return DK_AUTO;  break;
        // bool is only a keyword in C23; below C23 it is downgraded to
        // TK_IDENT in convert_pp_tokens so it can be used as an identifier.
        case 'b': if (s[1]=='o' && s[2]=='o' && s[3]=='l')
            return tok->kind == TK_KEYWORD ? DK_BOOL : DK_NONE; break;
        case 'c': if (s[1]=='h' && s[2]=='a' && s[3]=='r') return DK_CHAR;  break;
        case 'e': if (s[1]=='n' && s[2]=='u' && s[3]=='m') return DK_ENUM;  break;
        case 'l': if (s[1]=='o' && s[2]=='n' && s[3]=='g') return DK_LONG;  break;
        case 'v': if (s[1]=='o' && s[2]=='i' && s[3]=='d') return DK_VOID;  break;
        }
        break;
    case 5:  // _Bool, const, float, short, union
        switch (s[0]) {
        case '_': if (memcmp(s+1,"Bool",4)==0) return DK_BOOL;     break;
        case 'c': if (memcmp(s+1,"onst",4)==0) return DK_CONST;    break;
        case 'f': if (memcmp(s+1,"loat",4)==0) return DK_FLOAT;    break;
        case 's': if (memcmp(s+1,"hort",4)==0) return DK_SHORT;    break;
        case 'u': if (memcmp(s+1,"nion",4)==0) return DK_UNION;    break;
        }
        break;
    case 6:  // double, extern, inline, signed, static, struct, typeof
        switch (s[0]) {
        case 'd': if (memcmp(s+1,"ouble",5)==0) return DK_DOUBLE;  break;
        case 'e': if (memcmp(s+1,"xtern",5)==0) return DK_EXTERN;  break;
        case 'i': if (memcmp(s+1,"nline",5)==0) return DK_INLINE;  break;
        case 's':
            if (memcmp(s+1,"igned",5)==0) return DK_SIGNED;
            if (memcmp(s+1,"tatic",5)==0) return DK_STATIC;
            if (memcmp(s+1,"truct",5)==0) return DK_STRUCT;
            break;
        case 't': if (memcmp(s+1,"ypeof",5)==0) return DK_TYPEOF;  break;
        }
        break;
    case 7:  // _Atomic, __block, _BitInt, typedef
        if (s[0] == '_') {
            if (s[1]=='A' && memcmp(s+2,"tomic",5)==0) return DK_ATOMIC;
            if (s[1]=='_' && memcmp(s+2,"block",5)==0) return DK_BLOCK_VAR;
            if (s[1]=='B' && memcmp(s+2,"itInt",5)==0) return DK_BITINT;
        } else if (s[0]=='t' && memcmp(s+1,"ypedef",6)==0) {
            return DK_TYPEDEF;
        }
        break;
    case 8:  // _Alignas, _Complex, __thread, register, restrict, unsigned, volatile
        switch (s[0]) {
        case '_':
            switch (s[1]) {
            case 'A': if (memcmp(s+2,"lignas",6)==0) return DK_ALIGNAS;  break;
            case 'C': if (memcmp(s+2,"omplex",6)==0) return DK_COMPLEX;  break;
            case '_': if (memcmp(s+2,"thread",6)==0) return DK_TLS;      break;
            }
            break;
        case 'r':
            if (memcmp(s+1,"egister",7)==0) return DK_REGISTER;
            if (memcmp(s+1,"estrict",7)==0) return DK_RESTRICT;
            break;
        case 'u': if (memcmp(s+1,"nsigned",7)==0) return DK_UNSIGNED;   break;
        case 'v': if (memcmp(s+1,"olatile",7)==0) return DK_VOLATILE;   break;
        }
        break;
    case 9:  // _Noreturn, constexpr
        switch (s[0]) {
        case '_': if (memcmp(s+1,"Noreturn",8)==0) return DK_NORETURN;   break;
        // constexpr is only a keyword in C23; below C23 it is downgraded to
        // TK_IDENT in convert_pp_tokens so it can be used as an identifier.
        case 'c': if (memcmp(s+1,"onstexpr",8)==0)
            return tok->kind == TK_KEYWORD ? DK_CONSTEXPR : DK_NONE; break;
        }
        break;
    case 10:  // __restrict, _Imaginary, _Decimal32, _Decimal64
        if (s[0]=='_') {
            if (s[1]=='_' && memcmp(s+2,"restrict",8)==0) return DK_RESTRICT;
            if (s[1]=='I' && memcmp(s+2,"maginary",8)==0) return DK_IMAGINARY;
            if (s[1]=='D' && memcmp(s+2,"ecimal32",8)==0) return DK_DECIMAL32;
            if (s[1]=='D' && memcmp(s+2,"ecimal64",8)==0) return DK_DECIMAL64;
        }
        break;
    case 11:  // _Decimal128
        if (s[0]=='_' && s[1]=='D' && memcmp(s+2,"ecimal128",9)==0) return DK_DECIMAL128;
        break;
    case 12:  // __restrict__, thread_local
        if (memcmp(s,"__restrict__",12)==0) return DK_RESTRICT;
        if (memcmp(s, "thread_local", 12) == 0)
            return tok->kind == TK_KEYWORD ? DK_TLS : DK_NONE;
        break;
    case 13:  // _Thread_local, typeof_unqual
        switch (s[0]) {
        case '_': if (memcmp(s+1,"Thread_local",12)==0) return DK_TLS;           break;
        case 't': if (memcmp(s+1,"ypeof_unqual",12)==0) return DK_TYPEOF_UNQUAL; break;
        }
        break;
    }
    return DK_NONE;
}

// declspec = ("void" | "_Bool" | "char" | "short" | "int" | "long"
//             | "typedef" | "static" | "extern" | "inline"
//             | "_Thread_local" | "__thread"
//             | "signed" | "unsigned"
//             | struct-decl | union-decl | typedef-name
//             | enum-specifier | typeof-specifier
//             | "const" | "volatile" | "auto" | "register" | "restrict"
//             | "__restrict" | "__restrict__" | "_Noreturn")+
//
// The order of typenames in a type-specifier doesn't matter. For
// example, `int long static` means the same as `static long int`.
// That can also be written as `static long` because you can omit
// `int` if `long` or `short` are specified. However, something like
// `char int` is not a valid type specifier. We have to accept only a
// limited combinations of the typenames.
//
// In this function, we count the number of occurrences of each typename
// while keeping the "current" type object that the typenames up
// until that point represent. When we reach a non-typename token,
// we returns the current type object.
static Type *declspec(VirtualMachine *vm, Token **rest, Token *tok, VarAttr *attr) {
    Token *start = tok;

    // We use a single integer as counters for all typenames.
    // For example, bits 0 and 1 represents how many times we saw the
    // keyword "void" so far. With this, we can use a switch statement
    // as you can see below.
    enum {
        VOID = 1 << 0,
        BOOL = 1 << 2,
        CHAR = 1 << 4,
        SHORT = 1 << 6,
        INT = 1 << 8,
        LONG = 1 << 10,
        FLOAT = 1 << 12,
        DOUBLE = 1 << 14,
        OTHER = 1 << 16,
        SIGNED = 1 << 17,
        UNSIGNED = 1 << 18,
        COMPLEX = 1 << 19,
        IMAGINARY = 1 << 20,
    };

    Type *ty = ty_int;
    int counter = 0;
    int bitint_width = 0;
    bool is_atomic = false;
    bool is_const = false;
    bool is_volatile = false;

    while (is_typename(vm, tok) || equal(tok, "__attribute__") ||
           (equal(tok, "[") && equal(tok->next, "["))) {
        if (equal(tok, "__attribute__")) {
            tok = attribute_list(vm, tok, NULL, attr);
            continue;
        }
        if (equal(tok, "[") && equal(tok->next, "[")) {
            tok = c23_attribute_list(vm, tok, NULL, attr);
            continue;
        }

        DeclKw dk = declspec_kw(tok);
        switch (dk) {
        case DK_TYPEDEF: case DK_STATIC: case DK_EXTERN: case DK_INLINE:
        case DK_TLS: case DK_CONSTEXPR: case DK_BLOCK_VAR:
            if (!attr)
                error_tok(vm, tok,
                          "storage class specifier is not allowed in this context");
            if      (dk == DK_TYPEDEF)   attr->is_typedef   = true;
            else if (dk == DK_STATIC)    attr->is_static    = true;
            else if (dk == DK_EXTERN)    attr->is_extern    = true;
            else if (dk == DK_INLINE) {
                if (vm->compiler.c_std < CCCC_STD_C99)
                    error_tok(vm, tok, "'inline' is not available before C99");
                attr->is_inline = true;
            }
            else if (dk == DK_CONSTEXPR) attr->is_constexpr = true;
            else if (dk == DK_BLOCK_VAR) attr->is_block_var = true;
            else {
                attr->is_tls = true;
            }
            if (attr->is_typedef && attr->is_static + attr->is_extern +
                                        attr->is_inline + attr->is_tls > 1)
                error_tok(vm, tok,
                          "typedef may not be used together with static,"
                          " extern, inline, __thread or _Thread_local");
            if (attr->is_typedef && attr->is_constexpr)
                error_tok(vm, tok,
                          "typedef may not be used together with constexpr");
            if (attr->is_block_var && (attr->is_static || attr->is_extern || attr->is_tls))
                error_tok(vm, tok,
                          "__block may not be used together with static,"
                          " extern, __thread or _Thread_local");
            tok = tok->next;
            continue;
        case DK_CONST:
            is_const = true;
            tok = tok->next;
            continue;
        case DK_VOLATILE:
            is_volatile = true;
            tok = tok->next;
            continue;
        case DK_AUTO:
            if (vm->compiler.c_std >= CCCC_STD_C23) {
                if (counter != 0)
                    error_tok(vm, tok,
                              "cannot combine 'auto' with other type specifiers");
                if (attr)
                    attr->is_auto = true;
                ty = ty_auto;
                counter = OTHER;
            }
            tok = tok->next;
            continue;
        case DK_REGISTER: case DK_RESTRICT:
            tok = tok->next;
            continue;
        case DK_NORETURN:
            attr->is_noreturn = true;
            tok = tok->next;
            continue;
        case DK_ATOMIC:
            warn_tok(vm, tok, CCCC_WARN_IGNORED_FEATURES,
                     "'_Atomic' is parsed but non-atomic — "
                     "loads and stores are not atomic");
            tok = tok->next;
            if (equal(tok, "(")) {
                ty = typename(vm, &tok, tok->next);
                tok = skip(vm, tok, ")");
                counter = OTHER;
            }
            is_atomic = true;
            continue;
        case DK_ALIGNAS:
            if (!attr)
                error_tok(vm, tok, "_Alignas is not allowed in this context");
            tok = skip(vm, tok->next, "(");
            if (is_typename(vm, tok))
                attr->align = typename(vm, &tok, tok)->align;
            else
                attr->align = const_expr(vm, &tok, tok);
            tok = skip(vm, tok, ")");
            continue;
        case DK_BITINT: {
            // _BitInt(N) — must be C23; unsigned/signed must precede _BitInt
            Token *bitint_tok = tok;
            tok = tok->next;  // consume _BitInt
            tok = skip(vm, tok, "(");
            bitint_width = const_expr(vm, &tok, tok);
            tok = skip(vm, tok, ")");
            ty = bitint_type(vm, bitint_tok, bitint_width, (bool)(counter & UNSIGNED));
            counter = OTHER;
            continue;
        }
        case DK_STRUCT: case DK_UNION: case DK_ENUM:
        case DK_TYPEOF: case DK_TYPEOF_UNQUAL: case DK_NONE:
        case DK_DECIMAL32: case DK_DECIMAL64: case DK_DECIMAL128: {
            Type *ty2 = (dk == DK_NONE) ? find_typedef(vm, tok) : NULL;
            if (counter) goto declspec_done;
            if      (dk == DK_STRUCT)        ty = struct_decl(vm, &tok, tok->next);
            else if (dk == DK_UNION)         ty = union_decl(vm, &tok, tok->next);
            else if (dk == DK_ENUM)          ty = enum_specifier(vm, &tok, tok->next);
            else if (dk == DK_TYPEOF)        ty = typeof_specifier(vm, &tok, tok->next);
            else if (dk == DK_TYPEOF_UNQUAL) ty = typeof_unqual_specifier(vm, &tok, tok->next);
            else if (dk == DK_DECIMAL32)  { ty = ty_decimal32;  tok = tok->next; }
            else if (dk == DK_DECIMAL64)  { ty = ty_decimal64;  tok = tok->next; }
            else if (dk == DK_DECIMAL128) { ty = ty_decimal128; tok = tok->next; }
            else {
                VarScope *sc = find_var(vm, tok);
                if (!vm->compiler.in_type_lookahead && sc &&
                    sc->is_deprecated)
                    warn_deprecated_use(
                        vm, tok, arena_strndup(vm, tok->loc, tok->len),
                        sc->deprecated_msg);
                ty = sc && sc->is_deprecated
                         ? type_after_deprecated_use(vm, ty2)
                         : ty2;
                tok = tok->next;
            }
            counter += OTHER;
            continue;
        }
        case DK_VOID:      counter += VOID;      break;
        case DK_BOOL:      counter += BOOL;      break;
        case DK_CHAR:      counter += CHAR;      break;
        case DK_SHORT:     counter += SHORT;     break;
        case DK_INT:       counter += INT;       break;
        case DK_LONG:
            if ((counter & LONG) && vm->compiler.c_std < CCCC_STD_C99 &&
                !vm->compiler.in_type_lookahead)
                warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                         "'long long' is a C99 extension");
            counter += LONG;
            break;
        case DK_FLOAT:     counter += FLOAT;     break;
        case DK_DOUBLE:    counter += DOUBLE;    break;
        case DK_COMPLEX:   counter += COMPLEX;   break;
        case DK_IMAGINARY: counter += IMAGINARY; break;
        case DK_SIGNED:    counter |= SIGNED;    break;
        case DK_UNSIGNED:  counter |= UNSIGNED;  break;
        default: unreachable();
        }

        switch (counter) {
        case VOID:
            ty = ty_void;
            break;
        case BOOL:
            ty = ty_bool;
            break;
        case CHAR:
        case SIGNED + CHAR:
            ty = ty_char;
            break;
        case UNSIGNED + CHAR:
            ty = ty_uchar;
            break;
        case SHORT:
        case SHORT + INT:
        case SIGNED + SHORT:
        case SIGNED + SHORT + INT:
            ty = ty_short;
            break;
        case UNSIGNED + SHORT:
        case UNSIGNED + SHORT + INT:
            ty = ty_ushort;
            break;
        case INT:
        case SIGNED:
        case SIGNED + INT:
            ty = ty_int;
            break;
        case UNSIGNED:
        case UNSIGNED + INT:
            ty = ty_uint;
            break;
        case LONG:
        case LONG + INT:
        case LONG + LONG:
        case LONG + LONG + INT:
        case SIGNED + LONG:
        case SIGNED + LONG + INT:
        case SIGNED + LONG + LONG:
        case SIGNED + LONG + LONG + INT:
            ty = ty_long;
            break;
        case UNSIGNED + LONG:
        case UNSIGNED + LONG + INT:
        case UNSIGNED + LONG + LONG:
        case UNSIGNED + LONG + LONG + INT:
            ty = ty_ulong;
            break;
        case FLOAT:
            ty = ty_float;
            break;
        case FLOAT + COMPLEX:
        case FLOAT + IMAGINARY:
            ty = ty_fcomplex;
            break;
        case DOUBLE:
            ty = ty_double;
            break;
        case DOUBLE + COMPLEX:
        case DOUBLE + IMAGINARY:
        case COMPLEX:
        case IMAGINARY:
            ty = ty_dcomplex;
            break;
        case LONG + DOUBLE:
            ty = ty_ldouble;
            break;
        case LONG + DOUBLE + COMPLEX:
        case LONG + DOUBLE + IMAGINARY:
            ty = ty_ldcomplex;
            break;
        default:
            error_tok(vm, tok, "invalid type");
        }

        tok = tok->next;
    }
declspec_done:
    if (counter == 0 && !vm->compiler.in_type_lookahead)
        warn_tok(vm, start, CCCC_WARN_IMPLICIT_INT,
                 "type specifier missing, defaults to 'int'");

    if (attr && (attr->is_maybe_unused || attr->is_deprecated)) {
        ty = copy_type(vm, ty);
        ty->is_maybe_unused = attr->is_maybe_unused;
        ty->is_deprecated = attr->is_deprecated;
        ty->deprecated_msg = attr->deprecated_msg;
    }

    if (is_atomic) {
        ty = copy_type(vm, ty);
        ty->is_atomic = true;
    }

    if (is_const) {
        ty = copy_type(vm, ty);
        ty->is_const = true;
    }

    if (is_volatile) {
        ty = copy_type(vm, ty);
        ty->is_volatile = true;
    }

    if (attr && attr->is_constexpr) {
        ty = copy_type(vm, ty);
        ty->is_const = true;
    }

    *rest = tok;
    return ty;
}

// func-params = ("void" | param ("," param)* ("," "...")?)? ")"
// param       = declspec declarator
static Type *func_params(VirtualMachine *vm, Token **rest, Token *tok, Type *ty) {
    if (equal(tok, "void") && equal(tok->next, ")")) {
        *rest = tok->next->next;
        return func_type(vm, ty);
    }

    Type head = {};
    Type *cur = &head;
    bool is_variadic = false;

    // Open a temporary prototype scope so that each parameter is visible to
    // subsequent parameters' VLA size expressions (C99 §6.7.6.3p12).
    // e.g. void f(int n, int a[n]) — 'n' must be in scope when parsing a[n].
    enter_scope(vm);

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(vm, tok, ",");

        if (equal(tok, "...")) {
            is_variadic = true;
            tok = tok->next;
            skip(vm, tok, ")");
            break;
        }

        VarAttr attr = {};
        Type *ty2 = declspec(vm, &tok, tok, &attr);
        ty2 = declarator(vm, &tok, tok, ty2);
        ty2 = apply_var_attrs_to_type(vm, ty2, &attr);
        if (has_custom_attrs(ty2, &attr))
            error_tok(vm, ty2->name ? ty2->name : tok,
                      "custom attributes are only supported on file-scope declarations");

        Token *name = ty2->name;

        if (ty2->kind == TY_ARRAY) {
            // "array of T" is converted to "pointer to T" only in the parameter
            // context. For example, *argv[] is converted to **argv by this.
            // Qualifiers inside [...] apply to the resulting pointer per C99 §6.7.6.3p7.
            int  saved_static_min  = ty2->static_min;
            bool saved_is_const    = ty2->is_const;
            bool saved_is_volatile = ty2->is_volatile;
            bool saved_is_restrict = ty2->is_restrict;
            ty2 = pointer_to(vm, ty2->base);
            ty2->name        = name;
            ty2->static_min  = saved_static_min;
            ty2->is_const    = saved_is_const;
            ty2->is_volatile = saved_is_volatile;
            ty2->is_restrict = saved_is_restrict;
        } else if (ty2->kind == TY_VLA) {
            // VLA parameters also adjust to pointer-to-element (C99 §6.7.6.3p7).
            // Qualifiers from the brackets transfer to the resulting pointer.
            bool saved_is_const    = ty2->is_const;
            bool saved_is_volatile = ty2->is_volatile;
            bool saved_is_restrict = ty2->is_restrict;
            ty2 = pointer_to(vm, ty2->base);
            ty2->name        = name;
            ty2->is_const    = saved_is_const;
            ty2->is_volatile = saved_is_volatile;
            ty2->is_restrict = saved_is_restrict;
        } else if (ty2->kind == TY_FUNC) {
            // Likewise, a function is converted to a pointer to a function
            // only in the parameter context.
            ty2 = pointer_to(vm, ty2);
            ty2->name = name;
        }

        // Register this parameter in the prototype scope so subsequent
        // parameters can reference it in VLA dimension expressions.
        if (name) {
            Obj *dummy = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
            memset(dummy, 0, sizeof(Obj));
            dummy->ty    = ty2;
            dummy->align = ty2->align;
            dummy->is_local = true;
            push_scope(vm, name->loc, name->len)->var = dummy;
        }

        cur = cur->next = copy_type(vm, ty2);
    }

    leave_scope(vm);

    if (cur == &head) {
        if (vm->compiler.c_std < CCCC_STD_C23) {
            is_variadic = true;
            if (!vm->compiler.in_type_lookahead)
                warn_tok(vm, tok, CCCC_WARN_STRICT_PROTOTYPES,
                         "function declaration is not a prototype");
        }
        // C23: empty () is a prototype accepting no args, identical to (void).
    }

    ty = func_type(vm, ty);
    ty->params = head.next;
    ty->is_variadic = is_variadic;
    *rest = tok->next;
    return ty;
}

// array-dimensions = ("static" | "restrict" | "const" | "volatile" | "_Atomic")* const-expr? "]" type-suffix
static Type *array_dimensions(VirtualMachine *vm, Token **rest, Token *tok, Type *ty) {
    bool saw_static   = false;
    bool saw_const    = false;
    bool saw_volatile = false;
    bool saw_restrict = false;
    while (equal(tok, "static") || equal(tok, "restrict") ||
           equal(tok, "const")  || equal(tok, "volatile") || equal(tok, "_Atomic")) {
        if (equal(tok, "static"))   saw_static   = true;
        if (equal(tok, "const"))    saw_const    = true;
        if (equal(tok, "volatile")) saw_volatile = true;
        if (equal(tok, "restrict")) saw_restrict = true;
        tok = tok->next;
    }

    if (equal(tok, "]")) {
        ty = type_suffix(vm, rest, tok->next, ty);
        Type *arr = array_of(vm, ty, -1);
        if (saw_const)    arr->is_const    = true;
        if (saw_volatile) arr->is_volatile = true;
        if (saw_restrict) arr->is_restrict = true;
        return arr;
    }

    Token *expr_tok = tok;
    Node *expr = conditional(vm, &tok, tok);
    tok = skip(vm, tok, "]");
    ty = type_suffix(vm, rest, tok, ty);

    if (ty->kind == TY_VLA || !is_const_expr(vm, expr)) {
        if (vm->compiler.c_std < CCCC_STD_C99)
            warn_tok(vm, expr_tok, CCCC_WARN_PEDANTIC,
                     "variable-length arrays are a C99 extension");
        return vla_of(vm, ty, expr);
    }
    Type *arr = array_of(vm, ty, eval(vm, expr));
    if (saw_static)   arr->static_min  = arr->array_len;
    if (saw_const)    arr->is_const    = true;
    if (saw_volatile) arr->is_volatile = true;
    if (saw_restrict) arr->is_restrict = true;
    return arr;
}

// type-suffix = "(" func-params
//             | "[" array-dimensions
//             | ε
static Type *type_suffix(VirtualMachine *vm, Token **rest, Token *tok, Type *ty) {
    if (equal(tok, "("))
        return func_params(vm, rest, tok->next, ty);

    if (equal(tok, "[") && !equal(tok->next, "["))
        return array_dimensions(vm, rest, tok->next, ty);

    *rest = tok;
    return ty;
}

static bool is_asm_label_tok(Token *tok) {
    return equal(tok, "asm") || equal(tok, "__asm") || equal(tok, "__asm__");
}

static Token *asm_label(VirtualMachine *vm, Token *tok, char **label) {
    if (!is_asm_label_tok(tok))
        return tok;

    tok = skip(vm, tok->next, "(");
    if (tok->kind != TK_STR || !tok->ty || !tok->ty->base ||
        tok->ty->base->kind != TY_CHAR)
        error_tok(vm, tok, "expected string literal in asm label");
    if (label)
        *label = arena_strdup(vm, tok->str);
    return skip(vm, tok->next, ")");
}

// pointers = ("*" ("const" | "volatile" | "restrict")*)*
static Type *pointers(VirtualMachine *vm, Token **rest, Token *tok, Type *ty) {
    while (consume(vm, &tok, tok, "*")) {
        ty = pointer_to(vm, ty);
        // Handle const/volatile qualification on the pointer itself
        // Example: "int *const p" makes the pointer const, not the pointee
        // Example: "int *volatile p" makes the pointer volatile
        while (equal(tok, "const") || equal(tok, "volatile") ||
               equal(tok, "restrict") || equal(tok, "__restrict") ||
               equal(tok, "__restrict__")) {
            if (equal(tok, "const")) {
                ty = copy_type(vm, ty);
                ty->is_const = true;
            } else if (equal(tok, "volatile")) {
                ty = copy_type(vm, ty);
                ty->is_volatile = true;
            } else {
                ty = copy_type(vm, ty);
                ty->is_restrict = true;
            }
            tok = tok->next;
        }
    }
    *rest = tok;
    return ty;
}

// declarator = attribute? pointers ("(" ident ")" | "(" declarator ")" | ident)
// type-suffix attribute?
static Type *declarator(VirtualMachine *vm, Token **rest, Token *tok, Type *ty) {
    // Handle __attribute__ before declarator
    VarAttr prefix_attr = {};
    tok = attribute_list(vm, tok, NULL, &prefix_attr);
    tok = c23_attribute_list(vm, tok, NULL, &prefix_attr);
    append_custom_attr_list(&ty->custom_attrs, prefix_attr.custom_attrs);
    ty = apply_var_attrs_to_type(vm, ty, &prefix_attr);

    ty = pointers(vm, &tok, tok, ty);

    // Handle block type: int (^name)(params)
    // The ^ indicates this is a block type, not a function pointer
    if (equal(tok, "(") && equal(tok->next, "^")) {
        // Token *start = tok;
        tok = tok->next->next; // Skip '(' and '^'

        Token *name = NULL;
        Token *name_pos = tok;

        if (tok->kind == TK_IDENT) {
            name = tok;
            tok = tok->next;
        }

        tok = skip(vm, tok, ")");

        // Now parse the parameter list: (params)
        // This creates the function signature that the block will have
        Type *func_ty = type_suffix(vm, rest, tok, ty);

        // Create a block type instead of function pointer
        Type *block_ty = block_type(vm, func_ty->return_ty, func_ty->params);
        inherit_semantic_attrs(block_ty, ty);
        block_ty->name = name;
        block_ty->name_pos = name_pos;

        return block_ty;
    }

    if (equal(tok, "(")) {
        Token *start = tok;
        Type dummy = {};
        declarator(vm, &tok, start->next, &dummy);
        tok = skip(vm, tok, ")");
        ty = type_suffix(vm, rest, tok, ty);
        *rest = asm_label(vm, *rest, &ty->asm_label);
        return declarator(vm, &tok, start->next, ty);
    }

    Token *name = NULL;
    Token *name_pos = tok;

    if (tok->kind == TK_IDENT) {
        name = tok;
        tok = tok->next;
    }

    Type *inner_ty = ty;
    ty = type_suffix(vm, rest, tok, ty);
    inherit_semantic_attrs(ty, inner_ty);

    // Propagate noreturn from prefix __attribute__ to function type
    if (prefix_attr.is_noreturn && ty->kind == TY_FUNC)
        ty->is_noreturn = true;

    // Propagate nodiscard from prefix [[nodiscard]] to function type
    if (prefix_attr.is_nodiscard && ty->kind == TY_FUNC) {
        ty->is_nodiscard = true;
        if (prefix_attr.nodiscard_msg)
            ty->nodiscard_msg = prefix_attr.nodiscard_msg;
    }

    // Handle __attribute__ after declarator
    VarAttr suffix_attr = {};
    tok = attribute_list(vm, *rest, NULL, &suffix_attr);
    tok = c23_attribute_list(vm, tok, NULL, &suffix_attr);
    append_custom_attr_list(&ty->custom_attrs, suffix_attr.custom_attrs);
    ty = apply_var_attrs_to_type(vm, ty, &suffix_attr);
    tok = asm_label(vm, tok, &ty->asm_label);

    ty->name = name;
    ty->name_pos = name_pos;
    *rest = tok;
    return ty;
}

// abstract-declarator = attribute? pointers ("(" abstract-declarator ")")?
// type-suffix attribute?
static Type *abstract_declarator(VirtualMachine *vm, Token **rest, Token *tok, Type *ty) {
    // Handle __attribute__ before abstract declarator
    VarAttr prefix_attr = {};
    tok = attribute_list(vm, tok, NULL, &prefix_attr);
    tok = c23_attribute_list(vm, tok, NULL, &prefix_attr);
    ty = apply_var_attrs_to_type(vm, ty, &prefix_attr);

    ty = pointers(vm, &tok, tok, ty);

    // Handle block type: int (^)(params) in abstract declarators (for casts)
    if (equal(tok, "(") && equal(tok->next, "^")) {
        tok = tok->next->next; // Skip '(' and '^'
        tok = skip(vm, tok, ")");

        // Parse the parameter list
        Type *func_ty = type_suffix(vm, rest, tok, ty);

        // Create a block type
        return block_type(vm, func_ty->return_ty, func_ty->params);
    }

    if (equal(tok, "(")) {
        Token *start = tok;
        Type dummy = {};
        abstract_declarator(vm, &tok, start->next, &dummy);
        tok = skip(vm, tok, ")");
        ty = type_suffix(vm, rest, tok, ty);
        return abstract_declarator(vm, &tok, start->next, ty);
    }

    return type_suffix(vm, rest, tok, ty);
}

// type-name = declspec abstract-declarator
static Type *typename(VirtualMachine *vm, Token **rest, Token *tok) {
    Type *ty = declspec(vm, &tok, tok, NULL);
    return abstract_declarator(vm, rest, tok, ty);
}

static bool is_end(Token *tok) {
    return equal(tok, "}") || (equal(tok, ",") && equal(tok->next, "}"));
}

static bool consume_end(Token **rest, Token *tok) {
    if (equal(tok, "}")) {
        *rest = tok->next;
        return true;
    }

    if (equal(tok, ",") && equal(tok->next, "}")) {
        *rest = tok->next->next;
        return true;
    }

    return false;
}

static bool same_optional_name(Token *a, Token *b) {
    if (!a || !b)
        return a == b;
    return a->len == b->len && strncmp(a->loc, b->loc, a->len) == 0;
}

static bool same_type_exact(Type *a, Type *b) {
    if (a == b)
        return true;
    if (!a || !b || a->kind != b->kind ||
        a->is_unsigned != b->is_unsigned ||
        a->is_atomic != b->is_atomic ||
        a->is_const != b->is_const ||
        a->is_volatile != b->is_volatile ||
        a->is_restrict != b->is_restrict)
        return false;

    switch (a->kind) {
    case TY_PTR:
        return same_type_exact(a->base, b->base);
    case TY_ARRAY:
        return a->array_len == b->array_len &&
               same_type_exact(a->base, b->base);
    case TY_FUNC: {
        if (!same_type_exact(a->return_ty, b->return_ty) ||
            a->is_variadic != b->is_variadic)
            return false;
        Type *pa = a->params;
        Type *pb = b->params;
        for (; pa && pb; pa = pa->next, pb = pb->next)
            if (!same_type_exact(pa, pb))
                return false;
        return !pa && !pb;
    }
    case TY_STRUCT:
    case TY_UNION:
    case TY_ENUM:
        return a == b || (a->name && b->name &&
                          same_optional_name(a->name, b->name));
    case TY_COMPLEX:
        return same_type_exact(a->base, b->base);
    case TY_BITINT:
        return a->bit_width == b->bit_width;
    default:
        return true;
    }
}

static bool same_member_shape(Member *a, Member *b) {
    if (!same_optional_name(a->name, b->name) ||
        a->align != b->align ||
        a->is_bitfield != b->is_bitfield ||
        a->bit_width != b->bit_width ||
        !same_type_exact(a->ty, b->ty))
        return false;
    return true;
}

static bool same_struct_members(Type *a, Type *b) {
    Member *ma = a->members;
    Member *mb = b->members;
    for (; ma && mb; ma = ma->next, mb = mb->next)
        if (!same_member_shape(ma, mb))
            return false;
    return !ma && !mb;
}

static Member *find_union_member_by_name(Type *ty, Member *needle) {
    for (Member *mem = ty->members; mem; mem = mem->next) {
        if (!needle->name || !mem->name) {
            if (needle->name == mem->name)
                return mem;
            continue;
        }
        if (same_optional_name(needle->name, mem->name))
            return mem;
    }
    return NULL;
}

static bool same_union_members(Type *a, Type *b) {
    int count_a = 0;
    int count_b = 0;
    for (Member *ma = a->members; ma; ma = ma->next) {
        count_a++;
        Member *mb = find_union_member_by_name(b, ma);
        if (!mb || !same_member_shape(ma, mb))
            return false;
    }
    for (Member *mb = b->members; mb; mb = mb->next)
        count_b++;
    return count_a == count_b;
}

static EnumConstant *find_enum_constant(Type *ty, char *name) {
    for (EnumConstant *ec = ty->enum_constants; ec; ec = ec->next)
        if (!strcmp(ec->name, name))
            return ec;
    return NULL;
}

static bool same_enum_constants(Type *a, Type *b) {
    int count_a = 0;
    int count_b = 0;
    for (EnumConstant *ea = a->enum_constants; ea; ea = ea->next) {
        count_a++;
        EnumConstant *eb = find_enum_constant(b, ea->name);
        if (!eb || eb->value != ea->value)
            return false;
    }
    for (EnumConstant *eb = b->enum_constants; eb; eb = eb->next)
        count_b++;
    return count_a == count_b;
}

static bool compatible_tag_redeclaration(Type *old, Type *new) {
    if (!old || !new || old->kind != new->kind)
        return false;
    if (old->kind == TY_ENUM) {
        if (!same_type_exact(old->enum_base_type, new->enum_base_type))
            return false;
        return same_enum_constants(old, new);
    }
    if (old->kind == TY_STRUCT)
        return same_struct_members(old, new);
    if (old->kind == TY_UNION)
        return same_union_members(old, new);
    return false;
}

static Type *install_tag_definition(VirtualMachine *vm, Token *tag, Type *ty,
                                    char *kind_name) {
    if (!tag)
        return ty;

    Type *existing = find_tag_in_current_scope(vm, tag);
    if (!existing) {
        push_tag_scope(vm, tag, ty);
        return ty;
    }

    if (existing->kind != ty->kind)
        error_tok(vm, tag, "tag redeclared as different kind");

    if (existing->size < 0 ||
        (existing->kind == TY_ENUM && !existing->enum_constants &&
         ty->enum_constants)) {
        *existing = *ty;
        return existing;
    }

    if (vm->compiler.c_std < CCCC_STD_C23)
        error_tok(vm, tag, "redefinition of %s '%.*s'",
                  kind_name, tag->len, tag->loc);

    if (!compatible_tag_redeclaration(existing, ty))
        error_tok(vm, tag, "incompatible redeclaration of %s '%.*s'",
                  kind_name, tag->len, tag->loc);

    return existing;
}

// enum-specifier = ident? (":" typename)? "{" enum-list? "}"
//                | ident (":" typename)?
//
// enum-list      = ident ("=" num)? ("," ident ("=" num)?)* ","?
//
// C23: optional ": integer-type" specifies the underlying type.
// C23: "enum tag : underlying-type ;" is a forward declaration (complete type).
static Type *enum_specifier(VirtualMachine *vm, Token **rest, Token *tok) {
    Type *ty = enum_type(vm);

    // Read a tag.
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        ty->name = tag;
        ty->name_pos = tag;
        ty->enum_tag = tag;
        tok = tok->next;
    }

    // C23: optional underlying type `: integer-type`
    if (equal(tok, ":")) {
        tok = tok->next;
        Token *base_tok = tok;
        Type *base_ty = typename(vm, &tok, tok);
        if (!is_integer(base_ty) || base_ty->kind == TY_BOOL || base_ty->kind == TY_ENUM)
            error_tok(vm, base_tok,
                      "enum underlying type must be a non-bool integer type");
        ty->enum_base_type = base_ty;
        ty->size  = base_ty->size;
        ty->align = base_ty->align;
        ty->is_unsigned = base_ty->is_unsigned;
    }

    if (tag && !equal(tok, "{")) {
        Type *existing = find_tag(vm, tag);
        if (existing) {
            if (existing->kind != TY_ENUM)
                error_tok(vm, tag, "not an enum tag");
            if (existing->is_deprecated)
                warn_deprecated_use(vm, tag, get_ident(vm, tag),
                                    existing->deprecated_msg);
            *rest = tok;
            return existing->is_deprecated
                       ? type_after_deprecated_use(vm, existing)
                       : existing;
        }
        // Forward declaration requires an underlying type (C23 §6.7.2.2)
        if (!ty->enum_base_type)
            error_tok(vm, tag,
                      "enum forward declaration requires an underlying type");
        push_tag_scope(vm, tag, ty);
        *rest = tok;
        return ty;
    }

    Type *existing_tag = tag ? find_tag_in_current_scope(vm, tag) : NULL;
    if (existing_tag && existing_tag->kind != TY_ENUM)
        error_tok(vm, tag, "not an enum tag");

    tok = skip(vm, tok, "{");

    // Read an enum-list.
    int i = 0;
    int64_t val = 0;
    struct EnumConstant *enum_tail = NULL;
    while (!consume_end(rest, tok)) {
        if (i++ > 0)
            tok = skip(vm, tok, ",");

        char *name = get_ident(vm, tok);
        int name_len = tok->len;
        tok = tok->next;

        VarAttr enum_attr = {};
        tok = attribute_list(vm, tok, NULL, &enum_attr);
        tok = c23_attribute_list(vm, tok, NULL, &enum_attr);

        if (equal(tok, "="))
            val = const_expr(vm, &tok, tok->next);

        bool duplicate_from_same_enum = false;
        VarScope *old_sc = find_var_in_current_scope(vm, name, name_len);
        if (old_sc && old_sc->enum_ty) {
            EnumConstant *old_ec =
                existing_tag ? find_enum_constant(existing_tag, name) : NULL;
            duplicate_from_same_enum = old_ec && old_ec->value == val &&
                                       old_sc->enum_ty == existing_tag;
            if (!duplicate_from_same_enum)
                error_tok(vm, tok, "redeclaration of enumerator '%s'", name);
        }

        if (!duplicate_from_same_enum) {
            VarScope *sc = push_scope(vm, name, name_len);
            sc->enum_ty = ty;
            sc->enum_val = val;
            sc->is_deprecated = enum_attr.is_deprecated;
            sc->deprecated_msg = enum_attr.deprecated_msg;
        }

        // Store enum constant in Type structure for code emission
        struct EnumConstant *ec = arena_alloc(&vm->compiler.parser_arena,
                                              sizeof(struct EnumConstant));
        memset(ec, 0, sizeof(struct EnumConstant));
        ec->name = name;
        ec->value = val;
        ec->next = NULL;

        if (enum_tail) {
            enum_tail->next = ec;
        } else {
            ty->enum_constants = ec;
        }
        enum_tail = ec;

        val++;
    }

    return install_tag_definition(vm, tag, ty, "enum");
}

// typeof-specifier = "(" (expr | typename) ")"
static Type *typeof_specifier(VirtualMachine *vm, Token **rest, Token *tok) {
    tok = skip(vm, tok, "(");

    Type *ty;
    if (is_typename(vm, tok)) {
        ty = typename(vm, &tok, tok);
    } else {
        Node *node = expr(vm, &tok, tok);
        add_type(vm, node);
        ty = node->ty;
    }
    *rest = skip(vm, tok, ")");
    return ty;
}

// typeof_unqual - C23 version of typeof that removes qualifiers
static Type *typeof_unqual_specifier(VirtualMachine *vm, Token **rest, Token *tok) {
    Type *ty = typeof_specifier(vm, rest, tok);
    // Copy the type to avoid mutating the original
    ty = copy_type(vm, ty);
    // Remove all qualifiers
    ty->is_const = false;
    ty->is_volatile = false;
    return ty;
}

// C23 auto type inference: given an initializer expression type, return the
// deduced type (array-to-pointer decay, function-to-pointer decay already done
// by add_type for ND_VAR, then strip top-level qualifiers like typeof_unqual).
static Type *auto_deduced_type(VirtualMachine *vm, Type *ty) {
    if (ty->kind == TY_ARRAY)
        ty = pointer_to(vm, ty->base);
    if (ty->is_const || ty->is_volatile || ty->is_restrict) {
        ty = copy_type(vm, ty);
        ty->is_const = false;
        ty->is_volatile = false;
        ty->is_restrict = false;
    }
    return ty;
}

// Walk the declarator result type down to the ty_auto sentinel counting TY_PTR
// hops.  Returns the depth (0 for plain `auto x`), or -1 if a non-PTR type
// other than ty_auto is encountered (e.g. array declarator).
static int count_auto_ptr_depth(Type *ty) {
    int depth = 0;
    while (ty != ty_auto) {
        if (ty->kind != TY_PTR)
            return -1;
        depth++;
        ty = ty->base;
    }
    return depth;
}

// Count how many TY_PTR layers are at the top of a type.
static int count_ptr_depth(Type *ty) {
    int depth = 0;
    while (ty->kind == TY_PTR) {
        depth++;
        ty = ty->base;
    }
    return depth;
}

// Get size for a type (no adjustment needed - types are already correct)
static int get_vm_size(Type *ty) { return ty->size; }

// Generate code for computing a VLA size.
static Node *compute_vla_size(VirtualMachine *vm, Type *ty, Token *tok) {
    Node *node = new_node(vm, ND_NULL_EXPR, tok);
    if (ty->base)
        node = new_binary(vm, ND_COMMA, node,
                          compute_vla_size(vm, ty->base, tok), tok);

    if (ty->kind != TY_VLA)
        return node;

    Node *base_sz;
    if (ty->base->kind == TY_VLA)
        base_sz = new_var_node(vm, ty->base->vla_size, tok);
    else
        base_sz =
            new_num(vm, get_vm_size(ty->base), tok); // Use VM-adjusted size

    ty->vla_size = new_lvar(vm, "", 0, ty_ulong);
    Node *expr =
        new_binary(vm, ND_ASSIGN, new_var_node(vm, ty->vla_size, tok),
                   new_binary(vm, ND_MUL, ty->vla_len, base_sz, tok), tok);
    return new_binary(vm, ND_COMMA, node, expr, tok);
}

static Node *new_alloca(VirtualMachine *vm, Node *sz) {
    Node *node = new_unary(
        vm, ND_FUNCALL, new_var_node(vm, vm->compiler.builtin_alloca, sz->tok),
        sz->tok);
    node->func_ty = vm->compiler.builtin_alloca->ty;
    node->ty = vm->compiler.builtin_alloca->ty->return_ty;
    node->args = sz;
    add_type(vm, sz);
    return node;
}

// declaration = declspec (declarator ("=" expr)? ("," declarator ("="
// expr)?)*)? ";"
static Node *declaration(VirtualMachine *vm, Token **rest, Token *tok, Type *basety,
                         VarAttr *attr) {
    Node head = {};
    Node *cur = &head;
    int i = 0;

    while (!equal(tok, ";")) {
        if (i++ > 0)
            tok = skip(vm, tok, ",");

        Type *ty = declarator(vm, &tok, tok, basety);

        if (has_custom_attrs(ty, attr) || (basety && basety->custom_attrs))
            error_tok(vm, ty->name ? ty->name : tok,
                      "custom attributes are only supported on file-scope declarations");

        if (ty->kind == TY_VOID) {
            if (vm->collect_errors &&
                error_tok_recover(vm, tok, "variable declared void")) {
                // Skip to next declarator or end of declaration
                tok = skip_to_decl_boundary(vm, tok);
                if (equal(tok, ";"))
                    break;
                if (equal(tok, ","))
                    continue;
                break;
            }
            error_tok(vm, tok, "variable declared void");
        }

        if (!ty->name) {
            if (vm->collect_errors &&
                error_tok_recover(vm, ty->name_pos, "variable name omitted")) {
                // Skip to next declarator or end of declaration
                tok = skip_to_decl_boundary(vm, tok);
                if (equal(tok, ";"))
                    break;
                if (equal(tok, ","))
                    continue;
                break;
            }
            error_tok(vm, ty->name_pos, "variable name omitted");
        }

        if (attr && attr->is_constexpr && ty->kind == TY_VLA)
            error_tok(vm, ty->name, "constexpr object may not have variable length array type");

        // C23 auto type inference
        if (attr && attr->is_auto) {
            Token *name_tok = ty->name;
            int decl_depth = count_auto_ptr_depth(ty);
            if (decl_depth < 0)
                error_tok(vm, name_tok,
                          "cannot use 'auto' with array or function declarator");
            if (!equal(tok, "="))
                error_tok(vm, name_tok,
                          "declaration of variable '%.*s' with deduced type 'auto' requires an initializer",
                          (int)name_tok->len, name_tok->loc);
            if (equal(tok->next, "{"))
                error_tok(vm, tok->next, "cannot use 'auto' with array in C");

            // Parse initializer expression to infer type
            Token *eq_tok = tok;
            Node *init_expr = assign(vm, &tok, tok->next);
            add_type(vm, init_expr);
            Type *deduced = auto_deduced_type(vm, init_expr->ty);

            // Validate pointer depth: declarator stars must not exceed inferred type depth
            if (count_ptr_depth(deduced) < decl_depth) {
                char stars[16] = "";
                for (int i = 0; i < decl_depth && i < 15; i++)
                    stars[i] = '*';
                error_tok(vm, name_tok,
                          "variable '%.*s' with type 'auto%s%s' has incompatible initializer",
                          (int)name_tok->len, name_tok->loc,
                          decl_depth > 0 ? " " : "", stars);
            }

            if (attr->is_static) {
                warn_if_shadowing(vm, name_tok);
                Obj *var = new_anon_gvar(vm, deduced);
                var->tok = name_tok;
                var->display_name = get_ident(vm, name_tok);
                var->is_local_symbol = true;
                var->is_maybe_unused = ty->is_maybe_unused;
                var->is_deprecated = ty->is_deprecated;
                var->deprecated_msg = ty->deprecated_msg;
                push_scope(vm, get_ident(vm, name_tok), name_tok->len)->var = var;
                Token *tmp = eq_tok;
                gvar_initializer(vm, &tmp, eq_tok->next, var);
                continue;
            }

            Obj *var = new_lvar(vm, get_ident(vm, name_tok), name_tok->len, deduced);
            if (attr->align)
                var->align = attr->align;
            if (attr->is_block_var)
                var->is_block_var = true;

            vm->compiler.initializing_var = var;
            Node *lhs = new_var_node(vm, var, name_tok);
            add_type(vm, lhs);
            Node *asgn = new_binary(vm, ND_ASSIGN, lhs, init_expr, name_tok);
            add_type(vm, asgn);
            cur = cur->next = new_unary(vm, ND_EXPR_STMT, asgn, name_tok);
            continue;
        }

        if (attr && attr->is_static) {
            // static local variable
            warn_if_shadowing(vm, ty->name);
            Obj *var = new_anon_gvar(vm, ty);
            var->tok = ty->name;
            var->display_name = get_ident(vm, ty->name);
            var->is_local_symbol = true;
            var->is_constexpr = attr->is_constexpr;
            var->is_maybe_unused = ty->is_maybe_unused;
            var->is_deprecated = ty->is_deprecated;
            var->deprecated_msg = ty->deprecated_msg;
            push_scope(vm, get_ident(vm, ty->name), ty->name->len)->var = var;
            if (equal(tok, "="))
                gvar_initializer(vm, &tok, tok->next, var);
            else if (attr->is_constexpr)
                error_tok(vm, ty->name, "constexpr object requires an initializer");
            continue;
        }

        // Generate code for computing a VLA size. We need to do this
        // even if ty is not VLA because ty may be a pointer to VLA
        // (e.g. int (*foo)[n][m] where n and m are variables.)
        cur = cur->next =
            new_unary(vm, ND_EXPR_STMT, compute_vla_size(vm, ty, tok), tok);

        if (ty->kind == TY_VLA) {
            // Variable length arrays (VLAs) are translated to alloca() calls.
            // For example, `int x[n+2]` is translated to `tmp = n + 2,
            // x = alloca(tmp)`.
            Obj *var = new_lvar(vm, get_ident(vm, ty->name), ty->name->len, ty);
            Token *tok_local = ty->name;
            Node *expr = new_binary(
                vm, ND_ASSIGN, new_vla_ptr(vm, var, tok_local),
                new_alloca(vm, new_var_node(vm, ty->vla_size, tok_local)),
                tok_local);

            cur = cur->next = new_unary(vm, ND_EXPR_STMT, expr, tok_local);

            // Handle VLA initialization if present
            if (equal(tok, "=")) {
                tok = tok->next;
                Type *new_ty;
                Initializer *init = initializer(vm, &tok, tok, ty, &new_ty);
                Node *init_node = create_vla_init(vm, init, ty, var, tok_local);
                if (init_node)
                    cur = cur->next =
                        new_unary(vm, ND_EXPR_STMT, init_node, tok_local);
            }

            continue;
        }

        Obj *var = new_lvar(vm, get_ident(vm, ty->name), ty->name->len, ty);
        if (attr && attr->is_constexpr) {
            var->is_constexpr = true;
        }
        if (attr && attr->align)
            var->align = attr->align;
        if (attr && attr->is_block_var)
            var->is_block_var = true;

        if (equal(tok, "=")) {
            // Mark this variable as being initialized (allows const
            // initialization) NOTE: Don't clear this until after add_type is
            // called on the function body For now, just set it and it will be
            // cleared when next variable is initialized This works because
            // initializations happen sequentially
            vm->compiler.initializing_var = var;
            Node *expr = lvar_initializer(vm, &tok, tok->next, var);
            cur = cur->next = new_unary(vm, ND_EXPR_STMT, expr, tok);
            // Don't clear here - will be cleared by next init or at end of
            // parsing
        } else if (var->is_constexpr) {
            error_tok(vm, ty->name, "constexpr object requires an initializer");
        }

        if (var->ty->size < 0) {
            if (vm->collect_errors &&
                error_tok_recover(vm, ty->name,
                                  "variable has incomplete type")) {
                // Set a default size to allow parsing to continue
                var->ty->size = 1;
                continue;
            }
            error_tok(vm, ty->name, "variable has incomplete type");
        }

        if (var->ty->kind == TY_VOID) {
            if (vm->collect_errors &&
                error_tok_recover(vm, ty->name, "variable declared void")) {
                // Already reported earlier, just continue
                continue;
            }
            error_tok(vm, ty->name, "variable declared void");
        }
    }

    Node *node = new_node(vm, ND_BLOCK, tok);
    node->body = head.next;
    *rest = tok->next;
    return node;
}

static Token *skip_excess_element(VirtualMachine *vm, Token *tok) {
    if (equal(tok, "{")) {
        tok = skip_excess_element(vm, tok->next);
        return skip(vm, tok, "}");
    }

    assign(vm, &tok, tok);
    return tok;
}

// string-initializer = string-literal
static void string_initializer(VirtualMachine *vm, Token **rest, Token *tok,
                               Initializer *init) {
    if (init->is_flexible)
        *init = *new_initializer(
            vm, array_of(vm, init->ty->base, tok->ty->array_len), false);

    int len = MIN(init->ty->array_len, tok->ty->array_len);

    switch (init->ty->base->size) {
    case 1: {
        char *str = tok->str;
        for (int i = 0; i < len; i++)
            init->children[i]->expr = new_num(vm, str[i], tok);
        break;
    }
    case 2: {
        uint16_t *str = (uint16_t *)tok->str;
        for (int i = 0; i < len; i++)
            init->children[i]->expr = new_num(vm, str[i], tok);
        break;
    }
    case 4: {
        uint32_t *str = (uint32_t *)tok->str;
        for (int i = 0; i < len; i++)
            init->children[i]->expr = new_num(vm, str[i], tok);
        break;
    }
    default:
        unreachable();
    }

    *rest = tok->next;
}

// array-designator = "[" const-expr "]"
//
// C99 added the designated initializer to the language, which allows
// programmers to move the "cursor" of an initializer to any element.
// The syntax looks like this:
//
//   int x[10] = { 1, 2, [5]=3, 4, 5, 6, 7 };
//
// `[5]` moves the cursor to the 5th element, so the 5th element of x
// is set to 3. Initialization then continues forward in order, so
// 6th, 7th, 8th and 9th elements are initialized with 4, 5, 6 and 7,
// respectively. Unspecified elements (in this case, 3rd and 4th
// elements) are initialized with zero.
//
// Nesting is allowed, so the following initializer is valid:
//
//   int x[5][10] = { [5][8]=1, 2, 3 };
//
// It sets x[5][8], x[5][9] and x[6][0] to 1, 2 and 3, respectively.
//
// Use `.fieldname` to move the cursor for a struct initializer. E.g.
//
//   struct { int a, b, c; } x = { .c=5 };
//
// The above initializer sets x.c to 5.
static void array_designator(VirtualMachine *vm, Token **rest, Token *tok, Type *ty,
                             int *begin, int *end) {
    *begin = const_expr(vm, &tok, tok->next);
    if (*begin >= ty->array_len)
        error_tok(vm, tok, "array designator index exceeds array bounds");

    if (equal(tok, "...")) {
        *end = const_expr(vm, &tok, tok->next);
        if (*end >= ty->array_len)
            error_tok(vm, tok, "array designator index exceeds array bounds");
        if (*end < *begin)
            error_tok(vm, tok, "array designator range [%d, %d] is empty",
                      *begin, *end);
    } else {
        *end = *begin;
    }

    *rest = skip(vm, tok, "]");
}

// struct-designator = "." ident
static Member *struct_designator(VirtualMachine *vm, Token **rest, Token *tok, Type *ty) {
    Token *start = tok;
    tok = skip(vm, tok, ".");
    if (tok->kind != TK_IDENT)
        error_tok(vm, tok, "expected a field designator");

    for (Member *mem = ty->members; mem; mem = mem->next) {
        // Anonymous struct member
        if (mem->ty->kind == TY_STRUCT && !mem->name) {
            if (get_struct_member(mem->ty, tok)) {
                *rest = start;
                return mem;
            }
            continue;
        }

        // Regular struct member
        if (mem->name->len == tok->len &&
            !strncmp(mem->name->loc, tok->loc, tok->len)) {
            *rest = tok->next;
            return mem;
        }
    }

    error_tok(vm, tok, "struct has no such member");
    return NULL;
}

// designation = ("[" const-expr "]" | "." ident)* "="? initializer
static void designation(VirtualMachine *vm, Token **rest, Token *tok, Initializer *init) {
    if (equal(tok, "[")) {
        if (vm->compiler.c_std < CCCC_STD_C99)
            warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                     "designated initializers are a C99 extension");
        if (init->ty->kind != TY_ARRAY)
            error_tok(vm, tok, "array index in non-array initializer");

        int begin, end;
        array_designator(vm, &tok, tok, init->ty, &begin, &end);

        Token *tok2;
        for (int i = begin; i <= end; i++)
            designation(vm, &tok2, tok, init->children[i]);
        array_initializer2(vm, rest, tok2, init, begin + 1);
        return;
    }

    if (equal(tok, ".") && init->ty->kind == TY_STRUCT) {
        if (vm->compiler.c_std < CCCC_STD_C99)
            warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                     "designated initializers are a C99 extension");
        Member *mem = struct_designator(vm, &tok, tok, init->ty);
        designation(vm, &tok, tok, init->children[mem->idx]);
        init->expr = NULL;

        // Only continue with struct_initializer2 if we're not immediately
        // followed by another designator (which might re-designate the same
        // nested struct) This allows {.tl.x = 10, .tl.y = 20} to work correctly
        if (!equal(tok, ",") || !equal(tok->next, ".")) {
            struct_initializer2(vm, rest, tok, init, mem->next);
        } else {
            *rest = tok;
        }
        return;
    }

    if (equal(tok, ".") && init->ty->kind == TY_UNION) {
        if (vm->compiler.c_std < CCCC_STD_C99)
            warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                     "designated initializers are a C99 extension");
        Member *mem = struct_designator(vm, &tok, tok, init->ty);
        init->mem = mem;
        designation(vm, rest, tok, init->children[mem->idx]);
        return;
    }

    if (equal(tok, "."))
        error_tok(vm, tok, "field name not in struct or union initializer");

    if (equal(tok, "="))
        tok = tok->next;
    initializer2(vm, rest, tok, init);
}

// An array length can be omitted if an array has an initializer
// (e.g. `int x[] = {1,2,3}`). If it's omitted, count the number
// of initializer elements.
static int count_array_init_elements(VirtualMachine *vm, Token *tok, Type *ty) {
    bool first = true;
    Initializer *dummy = new_initializer(vm, ty->base, true);

    int i = 0, max = 0;

    while (!consume_end(&tok, tok)) {
        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        if (equal(tok, "[")) {
            i = const_expr(vm, &tok, tok->next);
            if (equal(tok, "..."))
                i = const_expr(vm, &tok, tok->next);
            tok = skip(vm, tok, "]");
            designation(vm, &tok, tok, dummy);
        } else {
            initializer2(vm, &tok, tok, dummy);
        }

        i++;
        max = MAX(max, i);
    }
    return max;
}

// array-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void array_initializer1(VirtualMachine *vm, Token **rest, Token *tok,
                               Initializer *init) {
    tok = skip(vm, tok, "{");

    if (init->is_flexible) {
        int len = count_array_init_elements(vm, tok, init->ty);
        // For VLA, keep the VLA type but allocate children for initializer
        // elements
        if (init->ty->kind == TY_VLA) {
            init->is_flexible = false;
            init->children = arena_alloc(&vm->compiler.parser_arena,
                                         len * sizeof(Initializer *));
            memset(init->children, 0, len * sizeof(Initializer *));
            for (int i = 0; i < len; i++)
                init->children[i] = new_initializer(vm, init->ty->base, false);
        } else {
            // For flexible arrays, create a fixed-size array type
            *init =
                *new_initializer(vm, array_of(vm, init->ty->base, len), false);
        }
    }

    bool first = true;

    for (int i = 0; !consume_end(rest, tok); i++) {
        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        if (equal(tok, "[")) {
            if (vm->compiler.c_std < CCCC_STD_C99)
                warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                         "designated initializers are a C99 extension");
            int begin, end;
            array_designator(vm, &tok, tok, init->ty, &begin, &end);

            Token *tok2;
            for (int j = begin; j <= end; j++)
                designation(vm, &tok2, tok, init->children[j]);
            tok = tok2;
            i = end;
            continue;
        }

        // For VLA, check if children[i] exists; for regular arrays, check
        // array_len
        if (init->ty->kind == TY_VLA) {
            if (init->children && init->children[i])
                initializer2(vm, &tok, tok, init->children[i]);
            else
                tok = skip_excess_element(vm, tok);
        } else {
            if (i < init->ty->array_len)
                initializer2(vm, &tok, tok, init->children[i]);
            else
                tok = skip_excess_element(vm, tok);
        }
    }
}

// array-initializer2 = initializer ("," initializer)*
static void array_initializer2(VirtualMachine *vm, Token **rest, Token *tok,
                               Initializer *init, int i) {
    if (init->is_flexible) {
        int len = count_array_init_elements(vm, tok, init->ty);
        // For VLA, keep the VLA type but allocate children for initializer
        // elements
        if (init->ty->kind == TY_VLA) {
            init->is_flexible = false;
            init->children = arena_alloc(&vm->compiler.parser_arena,
                                         len * sizeof(Initializer *));
            memset(init->children, 0, len * sizeof(Initializer *));
            for (int j = 0; j < len; j++)
                init->children[j] = new_initializer(vm, init->ty->base, false);
        } else {
            // For flexible arrays, create a fixed-size array type
            *init =
                *new_initializer(vm, array_of(vm, init->ty->base, len), false);
        }
    }

    // For VLA, loop until children run out; for regular arrays, use array_len
    int limit = (init->ty->kind == TY_VLA) ? INT_MAX : init->ty->array_len;
    for (; i < limit && !is_end(tok); i++) {
        Token *start = tok;
        if (i > 0)
            tok = skip(vm, tok, ",");

        if (equal(tok, "[") || equal(tok, ".")) {
            *rest = start;
            return;
        }

        initializer2(vm, &tok, tok, init->children[i]);
    }
    *rest = tok;
}

// struct-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void struct_initializer1(VirtualMachine *vm, Token **rest, Token *tok,
                                Initializer *init) {
    tok = skip(vm, tok, "{");

    Member *mem = init->ty->members;
    bool first = true;

    while (!consume_end(rest, tok)) {
        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        if (equal(tok, ".")) {
            if (vm->compiler.c_std < CCCC_STD_C99)
                warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                         "designated initializers are a C99 extension");
            mem = struct_designator(vm, &tok, tok, init->ty);
            designation(vm, &tok, tok, init->children[mem->idx]);
            mem = mem->next;
            continue;
        }

        if (mem) {
            initializer2(vm, &tok, tok, init->children[mem->idx]);
            mem = mem->next;
        } else {
            tok = skip_excess_element(vm, tok);
        }
    }
}

// struct-initializer2 = initializer ("," initializer)*
static void struct_initializer2(VirtualMachine *vm, Token **rest, Token *tok,
                                Initializer *init, Member *mem) {
    bool first = true;

    for (; mem && !is_end(tok); mem = mem->next) {
        Token *start = tok;

        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        if (equal(tok, "[") || equal(tok, ".")) {
            *rest = start;
            return;
        }

        initializer2(vm, &tok, tok, init->children[mem->idx]);
    }
    *rest = tok;
}

static void union_initializer(VirtualMachine *vm, Token **rest, Token *tok,
                              Initializer *init) {
    // Unlike structs, union initializers take only one initializer,
    // and that initializes the first union member by default.
    // You can initialize other member using a designated initializer.
    if (equal(tok, "{") && equal(tok->next, ".")) {
        Member *mem = struct_designator(vm, &tok, tok->next, init->ty);
        init->mem = mem;
        designation(vm, &tok, tok, init->children[mem->idx]);
        *rest = skip(vm, tok, "}");
        return;
    }

    init->mem = init->ty->members;
    if (!init->mem) {
        if (equal(tok, "{")) {
            tok = skip(vm, tok->next, "}");
            *rest = tok;
            return;
        }
        error_tok(vm, tok, "empty union initializer must be empty");
    }

    if (equal(tok, "{")) {
        initializer2(vm, &tok, tok->next, init->children[0]);
        consume(vm, &tok, tok, ",");
        *rest = skip(vm, tok, "}");
    } else {
        initializer2(vm, rest, tok, init->children[0]);
    }
}

// initializer = string-initializer | array-initializer
//             | struct-initializer | union-initializer
//             | assign
static void initializer2(VirtualMachine *vm, Token **rest, Token *tok, Initializer *init) {
    if (init->ty->kind == TY_ARRAY && tok->kind == TK_STR) {
        string_initializer(vm, rest, tok, init);
        return;
    }

    if (init->ty->kind == TY_ARRAY) {
        if (equal(tok, "{"))
            array_initializer1(vm, rest, tok, init);
        else
            array_initializer2(vm, rest, tok, init, 0);
        return;
    }

    // VLA initialization uses same syntax as array initialization
    if (init->ty->kind == TY_VLA) {
        if (equal(tok, "{"))
            array_initializer1(vm, rest, tok, init);
        else
            array_initializer2(vm, rest, tok, init, 0);
        return;
    }

    if (init->ty->kind == TY_STRUCT) {
        if (equal(tok, "{")) {
            struct_initializer1(vm, rest, tok, init);
            return;
        }

        // A struct can be initialized with another struct. E.g.
        // `struct T x = y;` where y is a variable of type `struct T`.
        // Handle that case first.
        Node *expr = assign(vm, rest, tok);
        add_type(vm, expr);
        if (expr->ty->kind == TY_STRUCT || expr->kind == ND_MACRO_CALL) {
            init->expr = expr;
            return;
        }

        struct_initializer2(vm, rest, tok, init, init->ty->members);
        return;
    }

    if (init->ty->kind == TY_UNION) {
        if (equal(tok, "{")) {
            union_initializer(vm, rest, tok, init);
            return;
        }

        // A union can be initialized with another union. E.g.
        // `union T x = y;` where y is a variable or expression of type `union
        // T`. Handle that case first.
        Node *expr = assign(vm, rest, tok);
        add_type(vm, expr);
        if (expr->ty->kind == TY_UNION) {
            init->expr = expr;
            return;
        }

        // Otherwise, initialize the first member
        union_initializer(vm, rest, tok, init);
        return;
    }

    if (equal(tok, "{")) {
        // An initializer for a scalar variable can be surrounded by
        // braces. E.g. `int x = {3};`. Handle that case.
        initializer2(vm, &tok, tok->next, init);
        *rest = skip(vm, tok, "}");
        return;
    }

    init->expr = assign(vm, rest, tok);
}

static Type *copy_struct_type(VirtualMachine *vm, Type *ty) {
    ty = copy_type(vm, ty);

    Member head = {};
    Member *cur = &head;
    for (Member *mem = ty->members; mem; mem = mem->next) {
        Member *m = arena_alloc(&vm->compiler.parser_arena, sizeof(Member));
        memset(m, 0, sizeof(Member));
        *m = *mem;
        cur = cur->next = m;
    }

    ty->members = head.next;
    return ty;
}

static Initializer *initializer(VirtualMachine *vm, Token **rest, Token *tok, Type *ty,
                                Type **new_ty) {
    Initializer *init = new_initializer(vm, ty, true);
    initializer2(vm, rest, tok, init);

    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->is_flexible) {
        ty = copy_struct_type(vm, ty);

        Member *mem = ty->members;
        while (mem->next)
            mem = mem->next;
        mem->ty = init->children[mem->idx]->ty;
        ty->size += mem->ty->size;

        *new_ty = ty;
        return init;
    }

    *new_ty = init->ty;
    return init;
}

static bool is_constexpr_object_type(Type *ty) {
    if (!ty)
        return false;
    if (ty->kind == TY_FUNC || ty->kind == TY_VLA || ty->is_atomic ||
        ty->is_volatile || ty->is_restrict)
        return false;
    if (ty->kind == TY_ARRAY)
        return ty->size >= 0 && is_constexpr_object_type(ty->base);
    if (ty->kind == TY_PTR)
        return !type_has_restrict(ty);
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        for (Member *mem = ty->members; mem; mem = mem->next)
            if (!is_constexpr_object_type(mem->ty))
                return false;
    }
    return true;
}

static void validate_constexpr_object_type(VirtualMachine *vm, Token *tok, Type *ty) {
    if (!is_constexpr_object_type(ty))
        error_tok(vm, tok, "constexpr object has unsupported type or qualifiers");
}

static bool initializer_is_constexpr(VirtualMachine *vm, Initializer *init, Type *ty) {
    if (!init)
        return true;
    if (init->expr)
        return is_const_expr(vm, init->expr);
    if (ty->kind == TY_ARRAY) {
        for (int i = 0; i < ty->array_len; i++)
            if (!initializer_is_constexpr(vm, init->children[i], ty->base))
                return false;
        return true;
    }
    if (ty->kind == TY_STRUCT) {
        for (Member *mem = ty->members; mem; mem = mem->next)
            if (!initializer_is_constexpr(vm, init->children[mem->idx], mem->ty))
                return false;
        return true;
    }
    if (ty->kind == TY_UNION)
        return !init->mem ||
               initializer_is_constexpr(vm, init->children[init->mem->idx],
                                        init->mem->ty);
    return true;
}

static void validate_constexpr_initializer(VirtualMachine *vm, Obj *var, Initializer *init,
                                           Token *tok) {
    validate_constexpr_object_type(vm, var->tok ? var->tok : tok, var->ty);
    if (!initializer_is_constexpr(vm, init, var->ty))
        error_tok(vm, tok, "constexpr initializer is not a constant expression");
    var->constexpr_init = init;
    if (init && init->expr)
        var->init_expr = init->expr;
}

static Node *init_desg_expr(VirtualMachine *vm, InitDesg *desg, Token *tok) {
    if (desg->var)
        return new_var_node(vm, desg->var, tok);

    if (desg->member) {
        Node *node =
            new_unary(vm, ND_MEMBER, init_desg_expr(vm, desg->next, tok), tok);
        node->member = desg->member;
        return node;
    }

    Node *lhs = init_desg_expr(vm, desg->next, tok);
    Node *rhs = new_num(vm, desg->idx, tok);
    return new_unary(vm, ND_DEREF, new_add(vm, lhs, rhs, tok), tok);
}

static Node *create_lvar_init(VirtualMachine *vm, Initializer *init, Type *ty,
                              InitDesg *desg, Token *tok) {
    if (ty->kind == TY_ARRAY) {
        Node *node = new_node(vm, ND_NULL_EXPR, tok);
        for (int i = 0; i < ty->array_len; i++) {
            InitDesg desg2 = {desg, i};
            Node *rhs =
                create_lvar_init(vm, init->children[i], ty->base, &desg2, tok);
            node = new_binary(vm, ND_COMMA, node, rhs, tok);
        }
        return node;
    }

    if (ty->kind == TY_STRUCT && !init->expr) {
        Node *node = new_node(vm, ND_NULL_EXPR, tok);

        for (Member *mem = ty->members; mem; mem = mem->next) {
            InitDesg desg2 = {desg, 0, mem};
            Node *rhs = create_lvar_init(vm, init->children[mem->idx], mem->ty,
                                         &desg2, tok);
            node = new_binary(vm, ND_COMMA, node, rhs, tok);
        }
        return node;
    }

    if (ty->kind == TY_UNION && !init->expr) {
        Member *mem = init->mem ? init->mem : ty->members;
        if (!mem)
            return new_node(vm, ND_NULL_EXPR, tok);
        InitDesg desg2 = {desg, 0, mem};
        return create_lvar_init(vm, init->children[mem->idx], mem->ty, &desg2,
                                tok);
    }

    if (!init->expr)
        return new_node(vm, ND_NULL_EXPR, tok);

    Node *lhs = init_desg_expr(vm, desg, tok);
    return new_binary(vm, ND_ASSIGN, lhs, init->expr, tok);
}

static bool is_init_splice_expr(Node *expr) {
    return expr && expr->kind == ND_VAR && expr->var &&
           expr->var->is_splice_placeholder;
}

static int init_tail_count(Node *tail) {
    int n = 0;
    for (; tail; tail = tail->next)
        n++;
    return n;
}

static Member *member_at_index(Type *ty, int idx) {
    for (Member *mem = ty->members; mem; mem = mem->next)
        if (mem->idx == idx)
            return mem;
    return NULL;
}

static Node *init_lhs_at(VirtualMachine *vm, Obj *var, Type *ty, int idx, Token *tok) {
    InitDesg base_desg = {NULL, 0, NULL, var};
    if (ty->kind == TY_STRUCT) {
        Member *mem = member_at_index(ty, idx);
        if (!mem)
            return NULL;
        InitDesg desg = {&base_desg, 0, mem};
        return init_desg_expr(vm, &desg, tok);
    }

    if (ty->kind == TY_ARRAY) {
        InitDesg desg = {&base_desg, idx};
        return init_desg_expr(vm, &desg, tok);
    }

    return NULL;
}

static Node *append_init_assignment(VirtualMachine *vm, Node *result, Obj *var, Type *ty,
                                    int idx, Node *rhs, Token *tok) {
    Node *lhs = init_lhs_at(vm, var, ty, idx, tok);
    if (!lhs)
        error_tok(vm, tok, "$@k initializer splice target is out of range");
    rhs->next = NULL;
    return new_binary(vm, ND_COMMA, result,
                      new_binary(vm, ND_ASSIGN, lhs, rhs, tok), tok);
}

// Called from reflection.c quote_substitute to expand ND_INIT_SPLICE nodes.
// Builds positional ND_ASSIGN chains for a splice chain plus any ordinary
// initializer tail that followed the $@k placeholder in the source template.
Node *node_expand_init_splice(VirtualMachine *vm, Node *splice, Node *chain) {
    Obj *var = splice->var;
    Type *ty = var->ty;
    Token *tok = splice->tok;
    Node *result = new_node(vm, ND_NULL_EXPR, tok);
    Node *elem = chain;
    int start = splice->init_start_index;
    int splice_count = 0;
    int tail_count = init_tail_count(splice->init_tail);

    for (Node *n = chain; n; n = n->next)
        splice_count++;

    int capacity = 0;
    if (ty->kind == TY_STRUCT) {
        for (Member *mem = ty->members; mem; mem = mem->next)
            capacity++;
    } else if (ty->kind == TY_ARRAY) {
        if (splice->init_inferred_array) {
            int len = start + splice_count + tail_count;
            var->ty = ty = array_of(vm, ty->base, len);
        }
        capacity = ty->array_len;
    } else {
        return result;
    }

    int total = splice_count + tail_count;
    if (total < capacity - start)
        error_tok(vm, tok,
                  "$@k initializer splice: too few elements (got %d, need %d)",
                  total, capacity - start);
    if (total > capacity - start)
        error_tok(vm, tok,
                  "$@k initializer splice: too many elements (got %d, need %d)",
                  total, capacity - start);

    int idx = start;
    while (elem) {
        Node *next = elem->next;
        elem->next = NULL;
        result = append_init_assignment(vm, result, var, ty, idx++, elem, tok);
        elem = next;
    }

    for (Node *tail = splice->init_tail; tail; ) {
        Node *next = tail->next;
        tail->next = NULL;
        result = append_init_assignment(vm, result, var, ty, idx++, tail, tok);
        tail = next;
    }

    return result;
}

// Generate initialization for VLA
// Unlike create_lvar_init which uses ty->array_len, VLAs have
// runtime-determined size We generate assignments based on the number of
// initializer elements (known at parse time)
static Node *create_vla_init(VirtualMachine *vm, Initializer *init, Type *ty, Obj *var,
                             Token *tok) {
    if (!init || ty->kind != TY_VLA)
        return NULL;

    // Count how many elements are in the initializer
    if (!init->children)
        return NULL;

    int init_count = 0;
    // Count non-null children (initializer elements)
    while (init->children[init_count])
        init_count++;

    if (init_count == 0)
        return NULL;

    // Generate assignments: arr[0] = val0, arr[1] = val1, ...
    Node *node = new_node(vm, ND_NULL_EXPR, tok);
    InitDesg desg = {NULL, 0, NULL, var};

    for (int i = 0; i < init_count; i++) {
        InitDesg desg2 = {&desg, i, NULL, NULL};
        if (init->children[i]) {
            Node *rhs =
                create_lvar_init(vm, init->children[i], ty->base, &desg2, tok);
            node = new_binary(vm, ND_COMMA, node, rhs, tok);
        }
    }

    return node;
}

// A variable definition with an initializer is a shorthand notation
// for a variable definition followed by assignments. This function
// generates assignment expressions for an initializer. For example,
// `int x[2][2] = {{6, 7}, {8, 9}}` is converted to the following
// expressions:
//
//   x[0][0] = 6;
//   x[0][1] = 7;
//   x[1][0] = 8;
//   x[1][1] = 9;
static Node *append_init_tail(Node *tail, Node *expr) {
    if (!expr)
        return tail;
    expr->next = NULL;
    if (!tail)
        return expr;
    Node *cur = tail;
    while (cur->next)
        cur = cur->next;
    cur->next = expr;
    return tail;
}

static bool build_deferred_init_splice(VirtualMachine *vm, Initializer *init, Obj *var,
                                       bool inferred_array, Token *tok,
                                       Node **out) {
    Type *ty = var->ty;
    if ((ty->kind != TY_STRUCT && ty->kind != TY_ARRAY) || !init->children)
        return false;

    int len = 0;
    if (ty->kind == TY_ARRAY) {
        len = ty->array_len;
    } else {
        for (Member *mem = ty->members; mem; mem = mem->next)
            len++;
    }

    int splice_idx = -1;
    Node *placeholder = NULL;
    for (int i = 0; i < len; i++) {
        Initializer *child = init->children[i];
        if (!child || !is_init_splice_expr(child->expr))
            continue;
        if (splice_idx >= 0)
            error_tok(vm, tok, "only one $@k initializer splice is supported");
        splice_idx = i;
        placeholder = child->expr;
    }
    if (splice_idx < 0)
        return false;

    Node *node = new_node(vm, ND_MEMZERO, tok);
    node->var = var;

    InitDesg base_desg = {NULL, 0, NULL, var};
    for (int i = 0; i < splice_idx; i++) {
        Initializer *child = init->children[i];
        if (!child)
            continue;
        InitDesg desg = {&base_desg, i};
        if (ty->kind == TY_STRUCT)
            desg.member = member_at_index(ty, i);
        Node *rhs = create_lvar_init(vm, child, child->ty, &desg, tok);
        node = new_binary(vm, ND_COMMA, node, rhs, tok);
    }

    Node *tail = NULL;
    for (int i = splice_idx + 1; i < len; i++) {
        Initializer *child = init->children[i];
        if (child && child->expr)
            tail = append_init_tail(tail, child->expr);
    }

    Node *splice = new_node(vm, ND_INIT_SPLICE, tok);
    splice->var = var;
    splice->lhs = placeholder;
    splice->init_tail = tail;
    splice->init_start_index = splice_idx;
    splice->init_inferred_array = inferred_array;

    *out = new_binary(vm, ND_COMMA, node, splice, tok);
    return true;
}

static Node *lvar_initializer(VirtualMachine *vm, Token **rest, Token *tok, Obj *var) {
    bool inferred_array = var->ty->kind == TY_ARRAY && var->ty->size < 0;
    Initializer *init = initializer(vm, rest, tok, var->ty, &var->ty);

    if (var->is_constexpr)
        validate_constexpr_initializer(vm, var, init, tok);

    // $@k splice in compound-literal context: defer final positional lowering
    // until quote_substitute knows the caller-provided chain length.
    Node *deferred = NULL;
    if (build_deferred_init_splice(vm, init, var, inferred_array, tok, &deferred))
        return deferred;

    InitDesg desg = {NULL, 0, NULL, var};

    // If a partial initializer list is given, the standard requires that
    // unspecified elements/members are set to 0. We zero-initialize the entire
    // memory region of the variable before applying the user-supplied values.
    //
    // Only aggregates (struct/union/array) can be partially initialized, so the
    // pre-zero is needed for them alone. A scalar's single initializer always
    // fully defines its value, so create_lvar_init's assignment overwrites the
    // whole object — a leading ND_MEMZERO would be dead. Emitting it anyway is
    // not just wasteful: ND_MEMZERO lowers to MSET, which clobbers REG_A0/A2,
    // so every redundant memzero widens the call-argument clobber surface (see
    // contains_funcall in codegen.c). Restrict it to the types that need it.
    Node *rhs = create_lvar_init(vm, init, var->ty, &desg, tok);
    Type *t = var->ty;
    bool is_aggregate = t->kind == TY_STRUCT || t->kind == TY_UNION ||
                        t->kind == TY_ARRAY;
    if (!is_aggregate)
        return rhs;

    Node *lhs = new_node(vm, ND_MEMZERO, tok);
    lhs->var = var;
    return new_binary(vm, ND_COMMA, lhs, rhs, tok);
}

static uint64_t read_buf(char *buf, int sz) {
    if (sz == 1)
        return *buf;
    if (sz == 2)
        return *(uint16_t *)buf;
    if (sz == 4)
        return *(uint32_t *)buf;
    if (sz == 8)
        return *(uint64_t *)buf;
    unreachable();
    return 0;
}

static void write_buf(char *buf, uint64_t val, int sz) {
    if (sz == 1)
        *buf = val;
    else if (sz == 2)
        *(uint16_t *)buf = val;
    else if (sz == 4)
        *(uint32_t *)buf = val;
    else if (sz == 8)
        *(uint64_t *)buf = val;
    else
        unreachable();
}

static Relocation *write_gvar_data(VirtualMachine *vm, Relocation *cur, Initializer *init,
                                   Type *ty, char *buf, int offset) {
    if (ty->kind == TY_ARRAY) {
        int sz = ty->base->size;
        for (int i = 0; i < ty->array_len; i++)
            cur = write_gvar_data(vm, cur, init->children[i], ty->base, buf,
                                  offset + sz * i);
        return cur;
    }

    if (ty->kind == TY_STRUCT) {
        for (Member *mem = ty->members; mem; mem = mem->next) {
            if (mem->is_bitfield) {
                Node *expr = init->children[mem->idx]->expr;
                if (!expr)
                    break;

                char *loc = buf + offset + mem->offset;
                uint64_t oldval = read_buf(loc, mem->ty->size);
                uint64_t newval = eval(vm, expr);
                uint64_t mask = (1L << mem->bit_width) - 1;
                uint64_t combined =
                    oldval | ((newval & mask) << mem->bit_offset);
                write_buf(loc, combined, mem->ty->size);
            } else {
                cur = write_gvar_data(vm, cur, init->children[mem->idx],
                                      mem->ty, buf, offset + mem->offset);
            }
        }
        return cur;
    }

    if (ty->kind == TY_UNION) {
        if (!init->mem)
            return cur;
        return write_gvar_data(vm, cur, init->children[init->mem->idx],
                               init->mem->ty, buf, offset);
    }

    if (!init->expr)
        return cur;

    if (ty->kind == TY_FLOAT) {
        *(float *)(buf + offset) = eval_double(vm, init->expr);
        return cur;
    }

    if (ty->kind == TY_DOUBLE) {
        *(double *)(buf + offset) = eval_double(vm, init->expr);
        return cur;
    }

    char **label = NULL;
    uint64_t val = eval2(vm, init->expr, &label);

    if (!label) {
        write_buf(buf + offset, val, ty->size);
        return cur;
    }

    Relocation *rel =
        arena_alloc(&vm->compiler.parser_arena, sizeof(Relocation));
    memset(rel, 0, sizeof(Relocation));
    rel->offset = offset;
    rel->label = label;
    rel->addend = val;
    cur->next = rel;
    return cur->next;
}

// Initializers for global variables are evaluated at compile-time and
// embedded to .data section. This function serializes Initializer
// objects to a flat byte array. It is a compile error if an
// initializer list contains a non-constant expression.
static void gvar_initializer(VirtualMachine *vm, Token **rest, Token *tok, Obj *var) {
    Initializer *init = initializer(vm, rest, tok, var->ty, &var->ty);

    // For constexpr variables, save the initializer expression for compile-time
    // evaluation
    if (var->is_constexpr)
        validate_constexpr_initializer(vm, var, init, tok);

    Relocation head = {};
    char *buf = arena_alloc(&vm->compiler.parser_arena, var->ty->size);
    memset(buf, 0, var->ty->size);
    write_gvar_data(vm, &head, init, var->ty, buf, 0);
    var->init_data = buf;
    var->rel = head.next;
}

// Returns true if a given token represents a type.
static bool is_typename(VirtualMachine *vm, Token *tok) {
    static HashMap map;

    if (map.capacity == 0) {
        static char *kw[] = {
            "void",          "_Bool",        "char",          "short",
            "int",           "long",         "struct",        "union",
            "typedef",       "enum",         "static",        "extern",
            "_Alignas",      "signed",       "unsigned",      "const",
            "volatile",      "auto",         "register",      "restrict",
            "__restrict",    "__restrict__", "_Noreturn",     "float",
            "double",        "typeof",       "typeof_unqual", "inline",
            "_Thread_local", "__thread",     "_Atomic",       "constexpr",
            "__block",       "_Complex",     "_Imaginary",
            "_BitInt",       "_Decimal32",   "_Decimal64",   "_Decimal128",
        };

        for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
            hashmap_put_borrowed(&map, kw[i], (void *)1);
    }

    // "bool" is only a typename when it was actually classified as a C23
    // keyword; in pre-C23 modes it is downgraded to TK_IDENT and may be
    // used as an ordinary identifier (e.g. without <stdbool.h>), so it is
    // deliberately not added to the hashmap above (which matches by text
    // regardless of token kind).
    return hashmap_get2(&map, tok->loc, tok->len) || find_typedef(vm, tok) ||
           (tok->kind == TK_KEYWORD &&
            (equal(tok, "bool") || equal(tok, "thread_local")));
}

// asm-stmt = "asm" ("volatile" | "inline")* "(" string-literal ")"
static Node *asm_stmt(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = new_node(vm, ND_ASM, tok);
    tok = tok->next;

    while (equal(tok, "volatile") || equal(tok, "inline"))
        tok = tok->next;

    tok = skip(vm, tok, "(");
    if (tok->kind != TK_STR || tok->ty->base->kind != TY_CHAR)
        error_tok(vm, tok, "expected string literal, found '%.*s'", tok->len,
                  tok->loc);
    node->asm_str = tok->str;
    *rest = skip(vm, tok->next, ")");
    return node;
}

// stmt = "return" expr? ";"
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "switch" "(" expr ")" stmt
//      | "case" const-expr ("..." const-expr)? ":" stmt
//      | "default" ":" stmt
//      | "for" "(" expr-stmt expr? ";" expr? ")" stmt
//      | "while" "(" expr ")" stmt
//      | "do" stmt "while" "(" expr ")" ";"
//      | "asm" asm-stmt
//      | "goto" (ident | "*" expr) ";"
//      | "break" ";"
//      | "continue" ";"
//      | ident ":" stmt
//      | "{" compound-stmt
//      | expr-stmt
static Node *stmt(VirtualMachine *vm, Token **rest, Token *tok);

// C23 §6.8.1: a label may precede a declaration at block scope.
// Pre-C23 bare declarations after labels are a hard error.
// Limitation: only handles object declarations; typedef/function-def after a
// label are not routed here.
static Node *stmt_or_decl(VirtualMachine *vm, Token **rest, Token *tok) {
    if (is_decl_start(vm, tok) && !equal(tok->next, ":")) {
        if (vm->compiler.c_std < CCCC_STD_C23)
            error_tok(vm, tok,
                      "a declaration may not appear directly after a label "
                      "(use --std=c23 or later)");
        VarAttr attr = {};
        Type *basety = declspec(vm, &tok, tok, &attr);
        return declaration(vm, rest, tok, basety, &attr);
    }
    return stmt(vm, rest, tok);
}

static Token *static_assert_decl(VirtualMachine *vm, Token *tok) {
    bool c23_static_assert = equal(tok, "static_assert");
    tok = skip(vm, tok->next, "(");
    long long val = const_expr(vm, &tok, tok);
    char *message = "static assertion failed";

    if (consume(vm, &tok, tok, ",")) {
        if (tok->kind != TK_STR)
            error_tok(vm, tok, "expected string literal, found '%.*s'",
                      tok->len, tok->loc);
        message = tok->str;
        tok = tok->next;
    } else if (!c23_static_assert || vm->compiler.c_std < CCCC_STD_C23) {
        error_tok(vm, tok, "expected ','");
    }

    if (!val)
        error_tok(vm, tok, "%s", message);
    tok = skip(vm, tok, ")");
    return skip(vm, tok, ";");
}

static Node *stmt(VirtualMachine *vm, Token **rest, Token *tok) {
    if (equal(tok, "_Static_assert") || equal(tok, "static_assert")) {
        *rest = static_assert_decl(vm, tok);
        return new_node(vm, ND_BLOCK, tok);
    }

    if (equal(tok, "return")) {
        Node *node = new_node(vm, ND_RETURN, tok);

        // Warn if this is a noreturn function attempting to return
        if (vm->compiler.current_fn && vm->compiler.current_fn->is_noreturn)
            warn_tok(vm, tok, CCCC_WARN_RETURN_TYPE,
                     "noreturn function should not return a value");

        if (consume(vm, rest, tok->next, ";")) {
            if (vm->compiler.current_fn) {
                Type *ty = vm->compiler.current_fn->ty->return_ty;
                if (ty->kind != TY_VOID) {
                    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
                        error_tok(vm, tok,
                                  "non-void aggregate function should return a value");
                    warn_tok(vm, tok, CCCC_WARN_RETURN_TYPE,
                             "non-void function should return a value");
                    node->lhs = new_cast(vm, new_num(vm, 0, tok), ty);
                }
            }
            return node;
        }

        Node *exp = expr(vm, &tok, tok->next);
        *rest = skip(vm, tok, ";");

        add_type(vm, exp);
        // current_fn may be NULL when a $quote template is parsed at file scope
        // (e.g. inside a top-level pragma macro call that uses $quote("return x;")).
        // Guard the implicit return-type cast; types will be resolved by add_type
        // later, or by the caller establishing context via $with_fn.
        if (vm->compiler.current_fn) {
            Type *ty = vm->compiler.current_fn->ty->return_ty;
            if (ty->kind == TY_VOID) {
                warn_tok(vm, node->tok, CCCC_WARN_RETURN_TYPE,
                         "void function should not return a value");
            } else if (ty->kind != TY_STRUCT && ty->kind != TY_UNION) {
                warn_implicit_conversion(vm, exp, ty, node->tok);
                exp = new_cast(vm, exp, ty);
            }
        }

        node->lhs = exp;
        return node;
    }

    if (equal(tok, "if")) {
        Node *node = new_node(vm, ND_IF, tok);
        tok = skip(vm, tok->next, "(");
        node->cond = expr(vm, &tok, tok);
        tok = skip(vm, tok, ")");
        node->then = stmt(vm, &tok, tok);
        if (equal(tok, "else"))
            node->els = stmt(vm, &tok, tok->next);
        *rest = tok;

        if (node->els && (vm->compiler.warnings & CCCC_WARN_DUPLICATED_BRANCHES) &&
            nodes_structurally_equal(node->then, node->els))
            warn_tok(vm, node->tok, CCCC_WARN_DUPLICATED_BRANCHES,
                     "both branches of 'if' statement are identical");

        if (vm->compiler.warnings & CCCC_WARN_DUPLICATED_COND) {
            Node *conds[64]; int nconds = 0;
            for (Node *chain = node; chain && chain->kind == ND_IF; chain = chain->els) {
                for (int i = 0; i < nconds; i++) {
                    if (nodes_structurally_equal(conds[i], chain->cond)) {
                        warn_tok(vm, chain->tok, CCCC_WARN_DUPLICATED_COND,
                                 "duplicated condition in 'if'/'else if' chain");
                        break;
                    }
                }
                if (nconds < 64) conds[nconds++] = chain->cond;
            }
        }

        return node;
    }

    if (equal(tok, "switch")) {
        Node *node = new_node(vm, ND_SWITCH, tok);
        tok = skip(vm, tok->next, "(");
        node->cond = expr(vm, &tok, tok);
        tok = skip(vm, tok, ")");

        if (vm->compiler.warnings & CCCC_WARN_SWITCH_BOOL) {
            add_type(vm, node->cond);
            if (node->cond->ty && node->cond->ty->kind == TY_BOOL)
                warn_tok(vm, node->tok, CCCC_WARN_SWITCH_BOOL,
                         "switch condition has boolean type");
        }

        Node *sw = vm->compiler.current_switch;
        vm->compiler.current_switch = node;

        char *brk = vm->compiler.brk_label;
        vm->compiler.brk_label = node->brk_label = new_unique_name(vm);

        node->then = stmt(vm, rest, tok);

        warn_switch_fallthrough(vm, node);

        if (vm->compiler.warnings & (CCCC_WARN_SWITCH | CCCC_WARN_SWITCH_ENUM)) {
            add_type(vm, node->cond);
            Type *cond_ty = node->cond->ty;
            if (cond_ty && cond_ty->kind == TY_ENUM && cond_ty->enum_constants) {
                bool has_default = node->default_case != NULL;
                bool check_sw   = (vm->compiler.warnings & CCCC_WARN_SWITCH) && !has_default;
                bool check_se   = !!(vm->compiler.warnings & CCCC_WARN_SWITCH_ENUM);
                if (check_sw || check_se) {
                    for (EnumConstant *ec = cond_ty->enum_constants; ec; ec = ec->next) {
                        bool covered = false;
                        for (Node *c = node->case_next; c; c = c->case_next) {
                            if (ec->value >= c->begin && ec->value <= c->end) {
                                covered = true;
                                break;
                            }
                        }
                        if (!covered) {
                            CCCCWarning which = (check_se && has_default)
                                               ? CCCC_WARN_SWITCH_ENUM : CCCC_WARN_SWITCH;
                            warn_tok(vm, node->tok, which,
                                     "enumeration value '%s' not handled in switch", ec->name);
                        }
                    }
                }
            }
        }

        if ((vm->compiler.warnings & CCCC_WARN_SWITCH_DEFAULT) &&
            !node->default_case)
            warn_tok(vm, node->tok, CCCC_WARN_SWITCH_DEFAULT,
                     "switch statement has no default case");

        vm->compiler.current_switch = sw;
        vm->compiler.brk_label = brk;
        return node;
    }

    if (equal(tok, "case")) {
        if (!vm->compiler.current_switch) {
            if (!error_tok_recover(vm, tok, "stray case")) {
                *rest = tok->next;
                return new_node(vm, ND_NULL_EXPR, tok);
            }
            // Skip to end of statement and return empty node
            tok = skip_to_stmt_end(vm, tok);
            *rest = tok;
            return new_node(vm, ND_NULL_EXPR, tok);
        }

        Node *node = new_node(vm, ND_CASE, tok);
        int begin = const_expr(vm, &tok, tok->next);
        int end;

        if (equal(tok, "...")) {
            // [GNU] Case ranges, e.g. "case 1 ... 5:"
            end = const_expr(vm, &tok, tok->next);
            if (end < begin)
                error_tok(vm, tok, "empty case range specified");
        } else {
            end = begin;
        }

        tok = skip(vm, tok, ":");
        node->label = new_unique_name(vm);
        node->lhs = stmt_or_decl(vm, rest, tok);
        node->begin = begin;
        node->end = end;
        node->case_next = vm->compiler.current_switch->case_next;
        vm->compiler.current_switch->case_next = node;
        return node;
    }

    if (equal(tok, "default")) {
        if (!vm->compiler.current_switch) {
            if (!error_tok_recover(vm, tok, "stray default")) {
                *rest = tok->next;
                return new_node(vm, ND_NULL_EXPR, tok);
            }
            // Skip to end of statement and return empty node
            tok = skip_to_stmt_end(vm, tok);
            *rest = tok;
            return new_node(vm, ND_NULL_EXPR, tok);
        }

        Node *node = new_node(vm, ND_CASE, tok);
        tok = skip(vm, tok->next, ":");
        node->label = new_unique_name(vm);
        node->lhs = stmt_or_decl(vm, rest, tok);
        vm->compiler.current_switch->default_case = node;
        return node;
    }

    if (equal(tok, "for")) {
        Node *node = new_node(vm, ND_FOR, tok);
        tok = skip(vm, tok->next, "(");

        enter_scope(vm);

        char *brk = vm->compiler.brk_label;
        char *cont = vm->compiler.cont_label;
        vm->compiler.brk_label = node->brk_label = new_unique_name(vm);
        vm->compiler.cont_label = node->cont_label = new_unique_name(vm);

        if (is_decl_start(vm, tok)) {
            Type *basety = declspec(vm, &tok, tok, NULL);
            node->init = declaration(vm, &tok, tok, basety, NULL);
        } else {
            node->init = expr_stmt(vm, &tok, tok);
        }

        if (!equal(tok, ";"))
            node->cond = expr(vm, &tok, tok);
        tok = skip(vm, tok, ";");

        if (!equal(tok, ")"))
            node->inc = expr(vm, &tok, tok);
        tok = skip(vm, tok, ")");

        node->then = stmt(vm, rest, tok);

        leave_scope(vm);
        vm->compiler.brk_label = brk;
        vm->compiler.cont_label = cont;
        return node;
    }

    if (equal(tok, "while")) {
        Node *node = new_node(vm, ND_FOR, tok);
        tok = skip(vm, tok->next, "(");
        node->cond = expr(vm, &tok, tok);
        tok = skip(vm, tok, ")");

        char *brk = vm->compiler.brk_label;
        char *cont = vm->compiler.cont_label;
        vm->compiler.brk_label = node->brk_label = new_unique_name(vm);
        vm->compiler.cont_label = node->cont_label = new_unique_name(vm);

        node->then = stmt(vm, rest, tok);

        vm->compiler.brk_label = brk;
        vm->compiler.cont_label = cont;
        return node;
    }

    if (equal(tok, "do")) {
        Node *node = new_node(vm, ND_DO, tok);

        char *brk = vm->compiler.brk_label;
        char *cont = vm->compiler.cont_label;
        vm->compiler.brk_label = node->brk_label = new_unique_name(vm);
        vm->compiler.cont_label = node->cont_label = new_unique_name(vm);

        node->then = stmt(vm, &tok, tok->next);

        vm->compiler.brk_label = brk;
        vm->compiler.cont_label = cont;

        tok = skip(vm, tok, "while");
        tok = skip(vm, tok, "(");
        node->cond = expr(vm, &tok, tok);
        tok = skip(vm, tok, ")");
        *rest = skip(vm, tok, ";");
        return node;
    }

    if (equal(tok, "asm"))
        return asm_stmt(vm, rest, tok);

    if (equal(tok, "goto")) {
        if (equal(tok->next, "*")) {
            // [GNU] `goto *ptr` jumps to the address specified by `ptr`.
            Node *node = new_node(vm, ND_GOTO_EXPR, tok);
            node->lhs = expr(vm, &tok, tok->next->next);
            *rest = skip(vm, tok, ";");
            return node;
        }

        Node *node = new_node(vm, ND_GOTO, tok);
        node->label = get_ident(vm, tok->next);
        node->goto_next = vm->compiler.gotos;
        vm->compiler.gotos = node;
        *rest = skip(vm, tok->next->next, ";");
        return node;
    }

    if (equal(tok, "break")) {
        if (!vm->compiler.brk_label) {
            if (!error_tok_recover(vm, tok, "stray break")) {
                *rest = tok->next;
                return new_node(vm, ND_NULL_EXPR, tok);
            }
            // Skip to end of statement and return empty node
            tok = skip_to_stmt_end(vm, tok);
            *rest = tok;
            return new_node(vm, ND_NULL_EXPR, tok);
        }
        Node *node = new_node(vm, ND_GOTO, tok);
        node->unique_label = vm->compiler.brk_label;
        *rest = skip(vm, tok->next, ";");
        return node;
    }

    if (equal(tok, "continue")) {
        if (!vm->compiler.cont_label) {
            if (!error_tok_recover(vm, tok, "stray continue")) {
                *rest = tok->next;
                return new_node(vm, ND_NULL_EXPR, tok);
            }
            // Skip to end of statement and return empty node
            tok = skip_to_stmt_end(vm, tok);
            *rest = tok;
            return new_node(vm, ND_NULL_EXPR, tok);
        }
        Node *node = new_node(vm, ND_GOTO, tok);
        node->unique_label = vm->compiler.cont_label;
        *rest = skip(vm, tok->next, ";");
        return node;
    }

    VarAttr label_attr = {};
    tok = attribute_list(vm, tok, NULL, &label_attr);
    tok = c23_attribute_list(vm, tok, NULL, &label_attr);

    if (label_attr.is_fallthrough) {
        if (equal(tok, ";")) {
            *rest = tok->next;
            Node *node = new_node(vm, ND_BLOCK, tok);
            node->is_fallthrough = true;
            return node;
        }
    }

    if (tok->kind == TK_IDENT && equal(tok->next, ":")) {
        Node *node = new_node(vm, ND_LABEL, tok);
        node->label = arena_strndup(vm, tok->loc, tok->len);
        node->unique_label = new_unique_name(vm);
        Token *body_tok = tok->next->next;
        body_tok = attribute_list(vm, body_tok, NULL, &label_attr);
        body_tok = c23_attribute_list(vm, body_tok, NULL, &label_attr);
        node->label_maybe_unused = label_attr.is_maybe_unused;
        node->lhs = stmt_or_decl(vm, rest, body_tok);
        node->goto_next = vm->compiler.labels;
        vm->compiler.labels = node;
        return node;
    }

    if (equal(tok, "{"))
        return compound_stmt(vm, rest, tok->next, NULL);

    return expr_stmt(vm, rest, tok);
}

// compound-stmt = (typedef | declaration | stmt)* "}"
static Node *compound_stmt(VirtualMachine *vm, Token **rest, Token *tok, Token **close_tok) {
    Node *node = new_node(vm, ND_BLOCK, tok);
    Node head = {};
    Node *cur = &head;

    enter_scope(vm);

    bool seen_stmt = false;
    while (!equal(tok, "}")) {
        if (is_decl_start(vm, tok) && !equal(tok->next, ":")) {
            if (seen_stmt && vm->compiler.c_std < CCCC_STD_C99)
                warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                         "mixing declarations and code is a C99 extension");
            VarAttr attr = {};
            Type *basety = declspec(vm, &tok, tok, &attr);

            if (attr.is_typedef) {
                if (has_custom_attrs(basety, &attr))
                    error_tok(vm, tok,
                              "custom attributes are only supported on file-scope declarations");
                tok = parse_typedef(vm, tok, basety, &attr);
                continue;
            }

            if (is_function(vm, tok, basety)) {
                tok = is_function_decl_list(vm, tok, basety)
                          ? function_declaration_list(vm, tok, basety, &attr)
                          : function(vm, tok, basety, &attr);
                continue;
            }

            if (attr.is_extern) {
                tok = global_variable(vm, tok, basety, &attr);
                continue;
            }

            cur = cur->next = declaration(vm, &tok, tok, basety, &attr);
        } else {
            // Clear initializing_var when we start parsing statements
            // (non-declarations) This ensures const variables can be
            // initialized but not assigned later
            vm->compiler.initializing_var = NULL;
            cur = cur->next = stmt(vm, &tok, tok);
            seen_stmt = true;
        }
        add_type(vm, cur);
    }

    // Also clear at end in case there are no statements after declarations
    vm->compiler.initializing_var = NULL;

    leave_scope(vm);

    node->body = head.next;
    if (close_tok)
        *close_tok = tok;
    *rest = tok->next;
    return node;
}

// Returns true if the expression has no observable side effects and its result
// can be safely discarded. Conservative: returns false for unknown node kinds.
static bool expr_has_no_side_effects(Node *n) {
    if (!n) return true;
    switch (n->kind) {
    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
    case ND_BITAND: case ND_BITOR: case ND_BITXOR: case ND_SHL: case ND_SHR:
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
    case ND_LOGAND: case ND_LOGOR:
        return expr_has_no_side_effects(n->lhs) && expr_has_no_side_effects(n->rhs);
    case ND_NEG: case ND_NOT: case ND_BITNOT: case ND_CAST: case ND_ADDR:
        return expr_has_no_side_effects(n->lhs);
    case ND_DEREF:
        return expr_has_no_side_effects(n->lhs);
    case ND_COND:
        return expr_has_no_side_effects(n->cond) &&
               expr_has_no_side_effects(n->then) &&
               expr_has_no_side_effects(n->els);
    case ND_COMMA:
        return expr_has_no_side_effects(n->lhs) && expr_has_no_side_effects(n->rhs);
    case ND_MEMBER:
        return expr_has_no_side_effects(n->lhs);
    case ND_NUM:
    case ND_VAR:
        return true;
    default:
        return false;
    }
}

// Conservative structural equality for -Wduplicated-branches / -Wduplicated-cond.
// Returns false for unrecognised node kinds to avoid false positives.
static bool nodes_structurally_equal(Node *a, Node *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case ND_NUM:
        return a->val == b->val;
    case ND_VAR:
        return a->var == b->var;
    case ND_MEMBER:
        return a->member == b->member && nodes_structurally_equal(a->lhs, b->lhs);
    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
    case ND_BITAND: case ND_BITOR: case ND_BITXOR: case ND_SHL: case ND_SHR:
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
    case ND_LOGAND: case ND_LOGOR: case ND_ASSIGN: case ND_COMMA:
        return nodes_structurally_equal(a->lhs, b->lhs) &&
               nodes_structurally_equal(a->rhs, b->rhs);
    case ND_NEG: case ND_NOT: case ND_BITNOT: case ND_CAST: case ND_ADDR:
    case ND_DEREF: case ND_EXPR_STMT:
        return nodes_structurally_equal(a->lhs, b->lhs);
    case ND_COND:
        return nodes_structurally_equal(a->cond, b->cond) &&
               nodes_structurally_equal(a->then, b->then) &&
               nodes_structurally_equal(a->els, b->els);
    case ND_BLOCK: {
        Node *pa = a->body, *pb = b->body;
        while (pa && pb) {
            if (!nodes_structurally_equal(pa, pb)) return false;
            pa = pa->next; pb = pb->next;
        }
        return !pa && !pb;
    }
    case ND_RETURN:
        return nodes_structurally_equal(a->lhs, b->lhs);
    case ND_NULL_EXPR:
        return true;
    default:
        return false;
    }
}

// expr-stmt = expr? ";"
static Node *expr_stmt(VirtualMachine *vm, Token **rest, Token *tok) {
    if (equal(tok, ";")) {
        *rest = tok->next;
        return new_node(vm, ND_BLOCK, tok);
    }

    Node *node = new_node(vm, ND_EXPR_STMT, tok);
    node->lhs = expr(vm, &tok, tok);
    *rest = skip(vm, tok, ";");

    add_type(vm, node->lhs);
    if (node->lhs && !(node->lhs->kind == ND_CAST &&
                       node->lhs->ty && node->lhs->ty->kind == TY_VOID)) {
        bool nodiscard = false;
        const char *what = NULL;
        if (node->lhs->kind == ND_FUNCALL && node->lhs->func_ty &&
            node->lhs->func_ty->is_nodiscard) {
            nodiscard = true;
            what = "function";
        } else if (node->lhs->ty && node->lhs->ty->is_nodiscard) {
            nodiscard = true;
            what = "type";
        }
        if (nodiscard) {
            if (node->lhs->func_ty && node->lhs->func_ty->nodiscard_msg)
                warn_tok(vm, node->tok, CCCC_WARN_NODISCARD,
                         "ignoring return value of %s declared with 'nodiscard': %s",
                         what, node->lhs->func_ty->nodiscard_msg);
            else if (node->lhs->ty && node->lhs->ty->nodiscard_msg)
                warn_tok(vm, node->tok, CCCC_WARN_NODISCARD,
                         "ignoring return value of %s declared with 'nodiscard': %s",
                         what, node->lhs->ty->nodiscard_msg);
            else
                warn_tok(vm, node->tok, CCCC_WARN_NODISCARD,
                         "ignoring return value of %s declared with 'nodiscard'",
                         what);
        }

        if ((vm->compiler.warnings & CCCC_WARN_UNUSED_VALUE) &&
            expr_has_no_side_effects(node->lhs))
            warn_tok(vm, node->tok, CCCC_WARN_UNUSED_VALUE,
                     "expression result unused");
    }

    return node;
}

// Returns true if control can fall through to the statement after `n`.
static bool falls_through(Node *n) {
    if (!n) return true;
    switch (n->kind) {
    case ND_RETURN:
    case ND_GOTO:
    case ND_GOTO_EXPR:
    case ND_UNREACHABLE:
        return false;
    case ND_IF:
        if (!n->els) return true;
        return falls_through(n->then) || falls_through(n->els);
    case ND_BLOCK:
    case ND_STMT_EXPR:
        if (!n->body) return true;
        { Node *last = n->body;
          while (last->next) last = last->next;
          return falls_through(last); }
    default:
        return true;
    }
}

static void warn_switch_fallthrough(VirtualMachine *vm, Node *sw) {
    if (!sw || sw->kind != ND_SWITCH || !sw->then) return;
    if (sw->then->kind != ND_BLOCK || !sw->then->body) return;

    Node *body = sw->then->body;
    Node *group_case = NULL; // current case group's label node
    Node *annotated = NULL; // last annotated [[fallthrough]] node in group

    for (Node *cur = body; cur; cur = cur->next) {
        if (cur->kind == ND_CASE) {
            if (group_case && annotated != (Node *)1) {
                // Check if the previous case group reaches the end
                // (annotated == non-NULL and also NOT sentinel-1 means fallthrough annotated)
            }
            group_case = cur;
            annotated = NULL;

            // Unwind nested case labels to find first real statement
            Node *c = cur;
            while (c && c->kind == ND_CASE && c->lhs && c->lhs->kind == ND_CASE)
                c = c->lhs;
            if (c && c->kind == ND_CASE && c->lhs) {
                if (c->lhs->is_fallthrough)
                    annotated = c->lhs;
            }
        } else {
            if (!group_case) continue;
            if (cur->is_fallthrough)
                annotated = cur;
        }
    }

    // Reset and redo properly with group_reaches_end tracking
    annotated = NULL;
    group_case = NULL;
    bool group_reaches_end = true;

    for (Node *cur = body; cur; cur = cur->next) {
        if (cur->kind == ND_CASE) {
            if (group_case && group_reaches_end && !annotated) {
                // The previous case group reaches the end (falls through to this label)
                // and it's not annotated with [[fallthrough]]
                warn_tok(vm, group_case->tok, CCCC_WARN_FALLTHROUGH,
                         "unannotated fallthrough between case labels");
            }
            group_case = cur;
            annotated = NULL;
            group_reaches_end = true;

            // Unwind nested case labels to find the first real statement
            Node *c = cur;
            while (c && c->kind == ND_CASE && c->lhs && c->lhs->kind == ND_CASE)
                c = c->lhs;
            if (c && c->kind == ND_CASE && c->lhs) {
                if (falls_through(c->lhs)) {
                    if (c->lhs->is_fallthrough)
                        annotated = c->lhs;
                } else {
                    group_reaches_end = false;
                }
            }
        } else {
            if (!group_case) continue;
            if (!group_reaches_end) continue;

            if (cur->is_fallthrough) {
                annotated = cur;
            }
            if (!falls_through(cur))
                group_reaches_end = false;
        }
    }
}

// expr = assign ("," expr)?
static Node *expr(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = assign(vm, &tok, tok);

    if (equal(tok, ","))
        return new_binary(vm, ND_COMMA, node, expr(vm, rest, tok->next), tok);

    *rest = tok;
    return node;
}

static int64_t eval(VirtualMachine *vm, Node *node) { return eval2(vm, node, NULL); }

static Initializer *constexpr_init_for_node(Node *node) {
    if (!node)
        return NULL;
    if (node->kind == ND_VAR && node->var && node->var->is_constexpr)
        return (Initializer *)node->var->constexpr_init;
    if (node->kind == ND_MEMBER) {
        Initializer *base = constexpr_init_for_node(node->lhs);
        if (!base || !node->member)
            return NULL;
        if (base->ty->kind == TY_UNION) {
            if (base->mem != node->member)
                return NULL;
            return base->children[node->member->idx];
        }
        if (base->ty->kind != TY_STRUCT)
            return NULL;
        return base->children[node->member->idx];
    }
    return NULL;
}

static Node *constexpr_expr_for_node(Node *node) {
    Initializer *init = constexpr_init_for_node(node);
    return init ? init->expr : NULL;
}

// Evaluate a given node as a constant expression.
//
// A constant expression is either just a number or ptr+n where ptr
// is a pointer to a global variable and n is a postiive/negative
// number. The latter form is accepted only as an initialization
// expression for a global variable.
static int64_t eval2(VirtualMachine *vm, Node *node, char ***label) {
    add_type(vm, node);

    if (is_flonum(node->ty))
        return eval_double(vm, node);

    switch (node->kind) {
    case ND_ADD:
        return eval2(vm, node->lhs, label) + eval(vm, node->rhs);
    case ND_SUB:
        return eval2(vm, node->lhs, label) - eval(vm, node->rhs);
    case ND_MUL:
        return eval(vm, node->lhs) * eval(vm, node->rhs);
    case ND_DIV: {
        int64_t rhs = eval(vm, node->rhs);
        if (rhs == 0)
            error_tok(vm, node->rhs->tok, "division by zero in constant expression");
        if (node->ty->is_unsigned)
            return (uint64_t)eval(vm, node->lhs) / (uint64_t)rhs;
        return eval(vm, node->lhs) / rhs;
    }
    case ND_NEG:
        return -eval(vm, node->lhs);
    case ND_MOD: {
        int64_t rhs = eval(vm, node->rhs);
        if (rhs == 0)
            error_tok(vm, node->rhs->tok, "division by zero in constant expression");
        if (node->ty->is_unsigned)
            return (uint64_t)eval(vm, node->lhs) % (uint64_t)rhs;
        return eval(vm, node->lhs) % rhs;
    }
    case ND_BITAND:
        return eval(vm, node->lhs) & eval(vm, node->rhs);
    case ND_BITOR:
        return eval(vm, node->lhs) | eval(vm, node->rhs);
    case ND_BITXOR:
        return eval(vm, node->lhs) ^ eval(vm, node->rhs);
    case ND_SHL:
        return eval(vm, node->lhs) << eval(vm, node->rhs);
    case ND_SHR:
        if (node->ty->is_unsigned && node->ty->size == 8)
            return (uint64_t)eval(vm, node->lhs) >> eval(vm, node->rhs);
        return eval(vm, node->lhs) >> eval(vm, node->rhs);
    case ND_EQ:
        return eval(vm, node->lhs) == eval(vm, node->rhs);
    case ND_NE:
        return eval(vm, node->lhs) != eval(vm, node->rhs);
    case ND_LT:
        if (node->lhs->ty->is_unsigned)
            return (uint64_t)eval(vm, node->lhs) < eval(vm, node->rhs);
        return eval(vm, node->lhs) < eval(vm, node->rhs);
    case ND_LE:
        if (node->lhs->ty->is_unsigned)
            return (uint64_t)eval(vm, node->lhs) <= eval(vm, node->rhs);
        return eval(vm, node->lhs) <= eval(vm, node->rhs);
    case ND_COND:
        return eval(vm, node->cond) ? eval2(vm, node->then, label)
                                    : eval2(vm, node->els, label);
    case ND_COMMA:
        return eval2(vm, node->rhs, label);
    case ND_NOT:
        return !eval(vm, node->lhs);
    case ND_BITNOT:
        return ~eval(vm, node->lhs);
    case ND_LOGAND:
        return eval(vm, node->lhs) && eval(vm, node->rhs);
    case ND_LOGOR:
        return eval(vm, node->lhs) || eval(vm, node->rhs);
    case ND_CAST: {
        int64_t val = eval2(vm, node->lhs, label);
        if (is_integer(node->ty)) {
            switch (node->ty->size) {
            case 1:
                return node->ty->is_unsigned ? (uint8_t)val : (int8_t)val;
            case 2:
                return node->ty->is_unsigned ? (uint16_t)val : (int16_t)val;
            case 4:
                return node->ty->is_unsigned ? (uint32_t)val : (int32_t)val;
            }
        }
        return val;
    }
    case ND_ADDR:
        return eval_rval(vm, node->lhs, label);
    case ND_LABEL_VAL:
        *label = &node->unique_label;
        return 0;
    case ND_MEMBER:
        {
            Initializer *init = constexpr_init_for_node(node);
            if (init)
                return init->expr ? eval(vm, init->expr) : 0;
        }
        if (!label)
            error_tok(vm, node->tok,
                      "not a compile-time constant (member access)");
        if (node->ty->kind != TY_ARRAY)
            error_tok(vm, node->tok,
                      "invalid initializer (member is not an array)");
        return eval_rval(vm, node->lhs, label) + node->member->offset;
    case ND_VAR:
        if (node->var->is_constexpr) {
            Node *expr = constexpr_expr_for_node(node);
            if (!expr)
                error_tok(vm, node->tok,
                          "not a scalar compile-time constant");
            return eval(vm, expr);
        }
        if (!label)
            error_tok(vm, node->tok,
                      "not a compile-time constant (variable reference)");
        if (node->var->ty->kind != TY_ARRAY && node->var->ty->kind != TY_FUNC)
            error_tok(vm, node->tok,
                      "invalid initializer (expected address of array or function)");
        *label = &node->var->name;
        return 0;
    case ND_NUM:
        return node->val;
    default:
        error_tok(vm, node->tok,
                  "not a compile-time constant (expression)");
        return 0;
    }
}

static int64_t eval_rval(VirtualMachine *vm, Node *node, char ***label) {
    switch (node->kind) {
    case ND_VAR:
        if (node->var->is_local)
            error_tok(vm, node->tok,
                      "not a compile-time constant (local variable)");
        *label = &node->var->name;
        return 0;
    case ND_DEREF:
        return eval2(vm, node->lhs, label);
    case ND_MEMBER:
        return eval_rval(vm, node->lhs, label) + node->member->offset;
    default:
        error_tok(vm, node->tok, "invalid initializer");
        return 0;
    }
}

static bool is_const_expr(VirtualMachine *vm, Node *node) {
    add_type(vm, node);

    switch (node->kind) {
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_BITAND:
    case ND_BITOR:
    case ND_BITXOR:
    case ND_SHL:
    case ND_SHR:
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_LOGAND:
    case ND_LOGOR:
        return is_const_expr(vm, node->lhs) && is_const_expr(vm, node->rhs);
    case ND_COND:
        if (!is_const_expr(vm, node->cond))
            return false;
        return is_const_expr(vm, eval(vm, node->cond) ? node->then : node->els);
    case ND_COMMA:
        return is_const_expr(vm, node->rhs);
    case ND_NEG:
    case ND_NOT:
    case ND_BITNOT:
    case ND_CAST:
        return is_const_expr(vm, node->lhs);
    case ND_NUM:
        return true;
    case ND_VAR:
    case ND_MEMBER:
        return constexpr_init_for_node(node) != NULL;
    default:
        return false;
    }
}

int64_t const_expr(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = conditional(vm, rest, tok);
    return eval(vm, node);
}

// Returns true when `expr` is an integer constant expression whose value fits
// within the range of `to` without truncation.  Used to suppress -Wconversion
// false positives such as `char c = 0;` or `char c = 1 + 1;`.
bool node_int_const_fits(VirtualMachine *vm, Node *expr, Type *to) {
    if (!expr || !to || !is_integer(to))
        return false;
    if (!is_const_expr(vm, expr))
        return false;
    int64_t val = eval(vm, expr);
    if (to->is_unsigned) {
        // Unsigned destination: value must be in [0, 2^bits-1].
        uint64_t max = (to->size >= 8) ? UINT64_MAX : ((uint64_t)1 << (to->size * 8)) - 1;
        return (uint64_t)val <= max;
    } else {
        // Signed destination: value must be in [-(2^(bits-1)), 2^(bits-1)-1].
        int64_t min = (to->size >= 8) ? INT64_MIN : -(int64_t)((uint64_t)1 << (to->size * 8 - 1));
        int64_t max = (to->size >= 8) ? INT64_MAX : (int64_t)(((uint64_t)1 << (to->size * 8 - 1)) - 1);
        return val >= min && val <= max;
    }
}

static double eval_double(VirtualMachine *vm, Node *node) {
    add_type(vm, node);

    if (is_integer(node->ty)) {
        if (node->ty->is_unsigned)
            return (unsigned long)eval(vm, node);
        return eval(vm, node);
    }

    switch (node->kind) {
    case ND_ADD:
        return eval_double(vm, node->lhs) + eval_double(vm, node->rhs);
    case ND_SUB:
        return eval_double(vm, node->lhs) - eval_double(vm, node->rhs);
    case ND_MUL:
        return eval_double(vm, node->lhs) * eval_double(vm, node->rhs);
    case ND_DIV:
        return eval_double(vm, node->lhs) / eval_double(vm, node->rhs);
    case ND_NEG:
        return -eval_double(vm, node->lhs);
    case ND_COND:
        return eval_double(vm, node->cond) ? eval_double(vm, node->then)
                                           : eval_double(vm, node->els);
    case ND_COMMA:
        return eval_double(vm, node->rhs);
    case ND_CAST:
        if (is_flonum(node->lhs->ty))
            return eval_double(vm, node->lhs);
        return eval(vm, node->lhs);
    case ND_VAR:
    case ND_MEMBER: {
        Node *expr = constexpr_expr_for_node(node);
        if (!expr)
            error_tok(vm, node->tok, "not a compile-time constant");
        return eval_double(vm, expr);
    }
    case ND_NUM:
        return node->fval;
    default:
        error_tok(vm, node->tok, "not a compile-time constant");
        return 0;
    }
}

// Convert op= operators to expressions containing an assignment.
//
// In general, `A op= C` is converted to ``tmp = &A, *tmp = *tmp op B`.
// However, if a given expression is of form `A.x op= C`, the input is
// converted to `tmp = &A, (*tmp).x = (*tmp).x op C` to handle assignments
// to bitfields.
static Node *to_assign(VirtualMachine *vm, Node *binary) {
    add_type(vm, binary->lhs);
    add_type(vm, binary->rhs);
    Token *tok = binary->tok;

    // Convert `A.x op= C` to `tmp = &A, (*tmp).x = (*tmp).x op C`.
    if (binary->lhs->kind == ND_MEMBER) {
        Obj *var = new_lvar(vm, "", 0, pointer_to(vm, binary->lhs->lhs->ty));

        Node *expr1 =
            new_binary(vm, ND_ASSIGN, new_var_node(vm, var, tok),
                       new_unary(vm, ND_ADDR, binary->lhs->lhs, tok), tok);

        Node *expr2 = new_unary(
            vm, ND_MEMBER,
            new_unary(vm, ND_DEREF, new_var_node(vm, var, tok), tok), tok);
        expr2->member = binary->lhs->member;

        Node *expr3 = new_unary(
            vm, ND_MEMBER,
            new_unary(vm, ND_DEREF, new_var_node(vm, var, tok), tok), tok);
        expr3->member = binary->lhs->member;

        Node *expr4 = new_binary(
            vm, ND_ASSIGN, expr2,
            new_binary(vm, binary->kind, expr3, binary->rhs, tok), tok);

        return new_binary(vm, ND_COMMA, expr1, expr4, tok);
    }

    // If A is an atomic type, Convert `A op= B` to
    //
    // ({
    //   T1 *addr = &A; T2 val = (B); T1 old = *addr; T1 new;
    //   do {
    //    new = old op val;
    //   } while (!atomic_compare_exchange_strong(addr, &old, new));
    //   new;
    // })
    if (binary->lhs->ty->is_atomic) {
        Node head = {};
        Node *cur = &head;

        Obj *addr = new_lvar(vm, "", 0, pointer_to(vm, binary->lhs->ty));
        Obj *val = new_lvar(vm, "", 0, binary->rhs->ty);
        Obj *old = new_lvar(vm, "", 0, binary->lhs->ty);
        Obj *new = new_lvar(vm, "", 0, binary->lhs->ty);

        cur = cur->next =
            new_unary(vm, ND_EXPR_STMT,
                      new_binary(vm, ND_ASSIGN, new_var_node(vm, addr, tok),
                                 new_unary(vm, ND_ADDR, binary->lhs, tok), tok),
                      tok);

        cur = cur->next =
            new_unary(vm, ND_EXPR_STMT,
                      new_binary(vm, ND_ASSIGN, new_var_node(vm, val, tok),
                                 binary->rhs, tok),
                      tok);

        cur = cur->next = new_unary(
            vm, ND_EXPR_STMT,
            new_binary(
                vm, ND_ASSIGN, new_var_node(vm, old, tok),
                new_unary(vm, ND_DEREF, new_var_node(vm, addr, tok), tok), tok),
            tok);

        Node *loop = new_node(vm, ND_DO, tok);
        loop->brk_label = new_unique_name(vm);
        loop->cont_label = new_unique_name(vm);

        Node *body =
            new_binary(vm, ND_ASSIGN, new_var_node(vm, new, tok),
                       new_binary(vm, binary->kind, new_var_node(vm, old, tok),
                                  new_var_node(vm, val, tok), tok),
                       tok);

        loop->then = new_node(vm, ND_BLOCK, tok);
        loop->then->body = new_unary(vm, ND_EXPR_STMT, body, tok);

        Node *cas = new_node(vm, ND_CAS, tok);
        cas->cas_addr = new_var_node(vm, addr, tok);
        cas->cas_old = new_unary(vm, ND_ADDR, new_var_node(vm, old, tok), tok);
        cas->cas_new = new_var_node(vm, new, tok);
        loop->cond = new_unary(vm, ND_NOT, cas, tok);

        cur = cur->next = loop;
        cur = cur->next =
            new_unary(vm, ND_EXPR_STMT, new_var_node(vm, new, tok), tok);

        Node *node = new_node(vm, ND_STMT_EXPR, tok);
        node->body = head.next;
        return node;
    }

    // Convert `A op= B` to ``tmp = &A, *tmp = *tmp op B`.
    Obj *var = new_lvar(vm, "", 0, pointer_to(vm, binary->lhs->ty));

    Node *expr1 = new_binary(vm, ND_ASSIGN, new_var_node(vm, var, tok),
                             new_unary(vm, ND_ADDR, binary->lhs, tok), tok);

    Node *expr2 = new_binary(
        vm, ND_ASSIGN, new_unary(vm, ND_DEREF, new_var_node(vm, var, tok), tok),
        new_binary(vm, binary->kind,
                   new_unary(vm, ND_DEREF, new_var_node(vm, var, tok), tok),
                   binary->rhs, tok),
        tok);

    return new_binary(vm, ND_COMMA, expr1, expr2, tok);
}

// assign    = conditional (assign-op assign)?
// assign-op = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^="
//           | "<<=" | ">>="
static Node *assign(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = conditional(vm, &tok, tok);

    if (equal(tok, "="))
        return new_binary(vm, ND_ASSIGN, node, assign(vm, rest, tok->next),
                          tok);

    if (equal(tok, "+="))
        return to_assign(vm,
                         new_add(vm, node, assign(vm, rest, tok->next), tok));

    if (equal(tok, "-="))
        return to_assign(vm,
                         new_sub(vm, node, assign(vm, rest, tok->next), tok));

    if (equal(tok, "*="))
        return to_assign(
            vm, new_binary(vm, ND_MUL, node, assign(vm, rest, tok->next), tok));

    if (equal(tok, "/="))
        return to_assign(
            vm, new_binary(vm, ND_DIV, node, assign(vm, rest, tok->next), tok));

    if (equal(tok, "%="))
        return to_assign(
            vm, new_binary(vm, ND_MOD, node, assign(vm, rest, tok->next), tok));

    if (equal(tok, "&="))
        return to_assign(vm, new_binary(vm, ND_BITAND, node,
                                        assign(vm, rest, tok->next), tok));

    if (equal(tok, "|="))
        return to_assign(vm, new_binary(vm, ND_BITOR, node,
                                        assign(vm, rest, tok->next), tok));

    if (equal(tok, "^="))
        return to_assign(vm, new_binary(vm, ND_BITXOR, node,
                                        assign(vm, rest, tok->next), tok));

    if (equal(tok, "<<="))
        return to_assign(
            vm, new_binary(vm, ND_SHL, node, assign(vm, rest, tok->next), tok));

    if (equal(tok, ">>="))
        return to_assign(
            vm, new_binary(vm, ND_SHR, node, assign(vm, rest, tok->next), tok));

    *rest = tok;
    return node;
}

// conditional = logor ("?" expr? ":" conditional)?
static Node *conditional(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *cond = logor(vm, &tok, tok);

    if (!equal(tok, "?")) {
        *rest = tok;
        return cond;
    }

    if (equal(tok->next, ":")) {
        // [GNU] Compile `a ?: b` as `tmp = a, tmp ? tmp : b`.
        add_type(vm, cond);
        Obj *var = new_lvar(vm, "", 0, cond->ty);
        Node *lhs =
            new_binary(vm, ND_ASSIGN, new_var_node(vm, var, tok), cond, tok);
        Node *rhs = new_node(vm, ND_COND, tok);
        rhs->cond = new_var_node(vm, var, tok);
        rhs->then = new_var_node(vm, var, tok);
        rhs->els = conditional(vm, rest, tok->next->next);
        return new_binary(vm, ND_COMMA, lhs, rhs, tok);
    }

    Node *node = new_node(vm, ND_COND, tok);
    node->cond = cond;
    node->then = expr(vm, &tok, tok->next);

    // Try to recover if ':' is missing
    if (!equal(tok, ":")) {
        if (vm->collect_errors &&
            error_tok_recover(vm, tok, "expected ':' in ternary operator")) {
            // Use 'then' expression as 'else' placeholder
            node->els = node->then;
            *rest = tok;
            return node;
        }
        tok = skip(vm, tok, ":");
    } else {
        tok = tok->next;
    }

    node->els = conditional(vm, rest, tok);
    return node;
}

// logor = logand ("||" logand)*
static Node *logor(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = logand(vm, &tok, tok);
    while (equal(tok, "||")) {
        Token *start = tok;
        Node *rhs = logand(vm, &tok, tok->next);
        if (vm->compiler.warnings & CCCC_WARN_LOGICAL_OP) {
            if (is_const_expr(vm, node))
                warn_tok(vm, start, CCCC_WARN_LOGICAL_OP,
                         "left operand of '||' is a constant expression");
            else if (is_const_expr(vm, rhs))
                warn_tok(vm, start, CCCC_WARN_LOGICAL_OP,
                         "right operand of '||' is a constant expression");
        }
        node = new_binary(vm, ND_LOGOR, node, rhs, start);
    }
    *rest = tok;
    return node;
}

// logand = bitor ("&&" bitor)*
static Node *logand(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = bitor(vm, &tok, tok);
    while (equal(tok, "&&")) {
        Token *start = tok;
        Node *rhs = bitor(vm, &tok, tok->next);
        if (vm->compiler.warnings & CCCC_WARN_LOGICAL_OP) {
            if (is_const_expr(vm, node))
                warn_tok(vm, start, CCCC_WARN_LOGICAL_OP,
                         "left operand of '&&' is a constant expression");
            else if (is_const_expr(vm, rhs))
                warn_tok(vm, start, CCCC_WARN_LOGICAL_OP,
                         "right operand of '&&' is a constant expression");
        }
        node = new_binary(vm, ND_LOGAND, node, rhs, start);
    }
    *rest = tok;
    return node;
}

// bitor = bitxor ("|" bitxor)*
static Node *bitor(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = bitxor(vm, &tok, tok);
    while (equal(tok, "|")) {
        Token *start = tok;
        node =
            new_binary(vm, ND_BITOR, node, bitxor(vm, &tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

// bitxor = bitand ("^" bitand)*
static Node *bitxor(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = bitand(vm, &tok, tok);
    while (equal(tok, "^")) {
        Token *start = tok;
        node =
            new_binary(vm, ND_BITXOR, node, bitand(vm, &tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

// bitand = equality ("&" equality)*
static Node *bitand(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = equality(vm, &tok, tok);
    while (equal(tok, "&")) {
        Token *start = tok;
        node = new_binary(vm, ND_BITAND, node, equality(vm, &tok, tok->next),
                          start);
    }
    *rest = tok;
    return node;
}

// equality = relational ("==" relational | "!=" relational)*
static Node *equality(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = relational(vm, &tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "==")) {
            Node *rhs = relational(vm, &tok, tok->next);
            if (vm->compiler.warnings & CCCC_WARN_FLOAT_EQUAL) {
                add_type(vm, node);
                add_type(vm, rhs);
                if (is_flonum(node->ty) && is_flonum(rhs->ty))
                    warn_tok(vm, start, CCCC_WARN_FLOAT_EQUAL,
                             "comparing floating-point values with == is unreliable");
            }
            if ((vm->compiler.warnings & CCCC_WARN_TAUTOLOGICAL_COMPARE) &&
                nodes_structurally_equal(node, rhs))
                warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                         "self-comparison always evaluates to true");
            if (vm->compiler.warnings & CCCC_WARN_ENUM_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (node->ty && rhs->ty &&
                    node->ty->kind == TY_ENUM && rhs->ty->kind == TY_ENUM &&
                    node->ty != rhs->ty && node->ty->enum_tag && rhs->ty->enum_tag)
                    warn_tok(vm, start, CCCC_WARN_ENUM_COMPARE,
                             "comparison between values of different enum types '%.*s' and '%.*s'",
                             node->ty->enum_tag->len, node->ty->enum_tag->loc,
                             rhs->ty->enum_tag->len, rhs->ty->enum_tag->loc);
            }
            node = new_binary(vm, ND_EQ, node, rhs, start);
            continue;
        }

        if (equal(tok, "!=")) {
            Node *rhs = relational(vm, &tok, tok->next);
            if (vm->compiler.warnings & CCCC_WARN_FLOAT_EQUAL) {
                add_type(vm, node);
                add_type(vm, rhs);
                if (is_flonum(node->ty) && is_flonum(rhs->ty))
                    warn_tok(vm, start, CCCC_WARN_FLOAT_EQUAL,
                             "comparing floating-point values with != is unreliable");
            }
            if ((vm->compiler.warnings & CCCC_WARN_TAUTOLOGICAL_COMPARE) &&
                nodes_structurally_equal(node, rhs))
                warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                         "self-comparison always evaluates to false");
            if (vm->compiler.warnings & CCCC_WARN_ENUM_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (node->ty && rhs->ty &&
                    node->ty->kind == TY_ENUM && rhs->ty->kind == TY_ENUM &&
                    node->ty != rhs->ty && node->ty->enum_tag && rhs->ty->enum_tag)
                    warn_tok(vm, start, CCCC_WARN_ENUM_COMPARE,
                             "comparison between values of different enum types '%.*s' and '%.*s'",
                             node->ty->enum_tag->len, node->ty->enum_tag->loc,
                             rhs->ty->enum_tag->len, rhs->ty->enum_tag->loc);
            }
            node = new_binary(vm, ND_NE, node, rhs, start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static Node *relational(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = shift(vm, &tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "<")) {
            Node *rhs = shift(vm, &tok, tok->next);
            if (vm->compiler.warnings & CCCC_WARN_TAUTOLOGICAL_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (nodes_structurally_equal(node, rhs))
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "self-comparison always evaluates to false");
                else if (is_integer(node->ty) && node->ty->is_unsigned &&
                         is_const_expr(vm, rhs) && eval(vm, rhs) == 0)
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "comparison of unsigned expression < 0 is always false");
            }
            if (vm->compiler.warnings & CCCC_WARN_ENUM_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (node->ty && rhs->ty &&
                    node->ty->kind == TY_ENUM && rhs->ty->kind == TY_ENUM &&
                    node->ty != rhs->ty && node->ty->enum_tag && rhs->ty->enum_tag)
                    warn_tok(vm, start, CCCC_WARN_ENUM_COMPARE,
                             "comparison between values of different enum types '%.*s' and '%.*s'",
                             node->ty->enum_tag->len, node->ty->enum_tag->loc,
                             rhs->ty->enum_tag->len, rhs->ty->enum_tag->loc);
            }
            node = new_binary(vm, ND_LT, node, rhs, start);
            continue;
        }

        if (equal(tok, "<=")) {
            Node *rhs = shift(vm, &tok, tok->next);
            if (vm->compiler.warnings & CCCC_WARN_TAUTOLOGICAL_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (nodes_structurally_equal(node, rhs))
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "self-comparison always evaluates to true");
            }
            if (vm->compiler.warnings & CCCC_WARN_ENUM_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (node->ty && rhs->ty &&
                    node->ty->kind == TY_ENUM && rhs->ty->kind == TY_ENUM &&
                    node->ty != rhs->ty && node->ty->enum_tag && rhs->ty->enum_tag)
                    warn_tok(vm, start, CCCC_WARN_ENUM_COMPARE,
                             "comparison between values of different enum types '%.*s' and '%.*s'",
                             node->ty->enum_tag->len, node->ty->enum_tag->loc,
                             rhs->ty->enum_tag->len, rhs->ty->enum_tag->loc);
            }
            node = new_binary(vm, ND_LE, node, rhs, start);
            continue;
        }

        if (equal(tok, ">")) {
            Node *rhs = shift(vm, &tok, tok->next);
            if (vm->compiler.warnings & CCCC_WARN_TAUTOLOGICAL_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (nodes_structurally_equal(node, rhs))
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "self-comparison always evaluates to false");
                else if (is_integer(rhs->ty) && rhs->ty->is_unsigned &&
                         is_const_expr(vm, node) && eval(vm, node) == 0)
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "comparison of 0 > unsigned expression is always false");
            }
            if (vm->compiler.warnings & CCCC_WARN_ENUM_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (node->ty && rhs->ty &&
                    node->ty->kind == TY_ENUM && rhs->ty->kind == TY_ENUM &&
                    node->ty != rhs->ty && node->ty->enum_tag && rhs->ty->enum_tag)
                    warn_tok(vm, start, CCCC_WARN_ENUM_COMPARE,
                             "comparison between values of different enum types '%.*s' and '%.*s'",
                             node->ty->enum_tag->len, node->ty->enum_tag->loc,
                             rhs->ty->enum_tag->len, rhs->ty->enum_tag->loc);
            }
            // note: a > b is stored as b < a
            node = new_binary(vm, ND_LT, rhs, node, start);
            continue;
        }

        if (equal(tok, ">=")) {
            Node *rhs = shift(vm, &tok, tok->next);
            if (vm->compiler.warnings & CCCC_WARN_TAUTOLOGICAL_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (nodes_structurally_equal(node, rhs))
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "self-comparison always evaluates to true");
                else if (is_integer(node->ty) && node->ty->is_unsigned &&
                         is_const_expr(vm, rhs) && eval(vm, rhs) == 0)
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "comparison of unsigned expression >= 0 is always true");
            }
            if (vm->compiler.warnings & CCCC_WARN_ENUM_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (node->ty && rhs->ty &&
                    node->ty->kind == TY_ENUM && rhs->ty->kind == TY_ENUM &&
                    node->ty != rhs->ty && node->ty->enum_tag && rhs->ty->enum_tag)
                    warn_tok(vm, start, CCCC_WARN_ENUM_COMPARE,
                             "comparison between values of different enum types '%.*s' and '%.*s'",
                             node->ty->enum_tag->len, node->ty->enum_tag->loc,
                             rhs->ty->enum_tag->len, rhs->ty->enum_tag->loc);
            }
            // note: a >= b is stored as b <= a
            node = new_binary(vm, ND_LE, rhs, node, start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// shift = add ("<<" add | ">>" add)*
static Node *shift(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = add(vm, &tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "<<")) {
            Node *rhs = add(vm, &tok, tok->next);
            // Check for error types
            add_type(vm, node);
            add_type(vm, rhs);
            if (is_error_type(node->ty) || is_error_type(rhs->ty)) {
                node = new_binary(vm, ND_SHL, node, rhs, start);
                node->ty = ty_error;
                continue;
            }
            if (is_const_expr(vm, rhs)) {
                int64_t rv = eval(vm, rhs);
                if ((vm->compiler.warnings & CCCC_WARN_SHIFT_NEGATIVE_VALUE) && rv < 0)
                    warn_tok(vm, start, CCCC_WARN_SHIFT_NEGATIVE_VALUE,
                             "left shift by negative amount %lld is undefined behaviour", rv);
                if ((vm->compiler.warnings & CCCC_WARN_SHIFT_OVERFLOW) && rv >= 0) {
                    // integer promotion: types smaller than int promote to int (4 bytes)
                    int bw = (node->ty->size < 4 ? 4 : node->ty->size) * 8;
                    if (rv >= bw)
                        warn_tok(vm, start, CCCC_WARN_SHIFT_OVERFLOW,
                                 "left shift amount %lld >= width of type (%d bits)", rv, bw);
                }
            }
            node = new_binary(vm, ND_SHL, node, rhs, start);
            continue;
        }

        if (equal(tok, ">>")) {
            Node *rhs = add(vm, &tok, tok->next);
            // Check for error types
            add_type(vm, node);
            add_type(vm, rhs);
            if (is_error_type(node->ty) || is_error_type(rhs->ty)) {
                node = new_binary(vm, ND_SHR, node, rhs, start);
                node->ty = ty_error;
                continue;
            }
            if (is_const_expr(vm, rhs)) {
                int64_t rv = eval(vm, rhs);
                if ((vm->compiler.warnings & CCCC_WARN_SHIFT_NEGATIVE_VALUE) && rv < 0)
                    warn_tok(vm, start, CCCC_WARN_SHIFT_NEGATIVE_VALUE,
                             "right shift by negative amount %lld is undefined behaviour", rv);
                if ((vm->compiler.warnings & CCCC_WARN_SHIFT_OVERFLOW) && rv >= 0) {
                    int bw = (node->ty->size < 4 ? 4 : node->ty->size) * 8;
                    if (rv >= bw)
                        warn_tok(vm, start, CCCC_WARN_SHIFT_OVERFLOW,
                                 "right shift amount %lld >= width of type (%d bits)", rv, bw);
                }
            }
            node = new_binary(vm, ND_SHR, node, rhs, start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// In C, `+` operator is overloaded to perform the pointer arithmetic.
// If p is a pointer, p+n adds not n but sizeof(*p)*n to the value of p,
// so that p+n points to the location n elements (not bytes) ahead of p.
// In other words, we need to scale an integer value before adding to a
// pointer value. This function takes care of the scaling.
static Node *new_add(VirtualMachine *vm, Node *lhs, Node *rhs, Token *tok) {
    add_type(vm, lhs);
    add_type(vm, rhs);

    // Early exit for error types to prevent cascading errors
    if (is_error_type(lhs->ty) || is_error_type(rhs->ty)) {
        Node *node = new_binary(vm, ND_ADD, lhs, rhs, tok);
        node->ty = ty_error;
        return node;
    }

    // num + num
    if (is_numeric(lhs->ty) && is_numeric(rhs->ty))
        return new_binary(vm, ND_ADD, lhs, rhs, tok);

    if (lhs->ty->base && rhs->ty->base)
        error_tok(vm, tok, "cannot add two pointers");

    // Canonicalize `num + ptr` to `ptr + num`.
    if (!lhs->ty->base && rhs->ty->base) {
        Node *tmp = lhs;
        lhs = rhs;
        rhs = tmp;
    }

    if (!lhs->ty->base)
        error_tok(vm, tok, "invalid operands to + (expected pointer and integer)");

    // void* arithmetic is a GNU extension; we allow it for compatibility
    if (lhs->ty->base->kind == TY_VOID) {
        warn_tok(vm, tok, CCCC_WARN_POINTER_ARITH,
                 "pointer of type 'void *' used in arithmetic");
        rhs = new_binary(vm, ND_MUL, rhs,
                         new_long(vm, get_vm_size(lhs->ty->base), tok), tok);
        return new_binary(vm, ND_ADD, lhs, rhs, tok);
    }

    // VLA + num
    if (lhs->ty->base->kind == TY_VLA) {
        rhs = new_binary(vm, ND_MUL, rhs,
                         new_var_node(vm, lhs->ty->base->vla_size, tok), tok);
        return new_binary(vm, ND_ADD, lhs, rhs, tok);
    }

    // Function pointer arithmetic is a GNU extension
    if (lhs->ty->base->kind == TY_FUNC)
        warn_tok(vm, tok, CCCC_WARN_POINTER_ARITH,
                 "pointer to a function used in arithmetic");

    // ptr + num
    rhs = new_binary(vm, ND_MUL, rhs,
                     new_long(vm, get_vm_size(lhs->ty->base), tok), tok);
    return new_binary(vm, ND_ADD, lhs, rhs, tok);
}

// Like `+`, `-` is overloaded for the pointer type.
static Node *new_sub(VirtualMachine *vm, Node *lhs, Node *rhs, Token *tok) {
    add_type(vm, lhs);
    add_type(vm, rhs);

    // Early exit for error types to prevent cascading errors
    if (is_error_type(lhs->ty) || is_error_type(rhs->ty)) {
        Node *node = new_binary(vm, ND_SUB, lhs, rhs, tok);
        node->ty = ty_error;
        return node;
    }

    // num - num
    if (is_numeric(lhs->ty) && is_numeric(rhs->ty))
        return new_binary(vm, ND_SUB, lhs, rhs, tok);

    if (!lhs->ty->base)
        error_tok(vm, tok, "invalid operands to - (left operand is not a pointer)");

    // VLA + num
    if (lhs->ty->base->kind == TY_VLA) {
        rhs = new_binary(vm, ND_MUL, rhs,
                         new_var_node(vm, lhs->ty->base->vla_size, tok), tok);
        add_type(vm, rhs);
        Node *node = new_binary(vm, ND_SUB, lhs, rhs, tok);
        node->ty = lhs->ty;
        return node;
    }

    // ptr - num
    if (lhs->ty->base && is_integer(rhs->ty)) {
        if (lhs->ty->base->kind == TY_VOID)
            warn_tok(vm, tok, CCCC_WARN_POINTER_ARITH,
                     "pointer of type 'void *' used in arithmetic");
        else if (lhs->ty->base->kind == TY_FUNC)
            warn_tok(vm, tok, CCCC_WARN_POINTER_ARITH,
                     "pointer to a function used in arithmetic");
        rhs = new_binary(vm, ND_MUL, rhs,
                         new_long(vm, get_vm_size(lhs->ty->base), tok), tok);
        add_type(vm, rhs);
        Node *node = new_binary(vm, ND_SUB, lhs, rhs, tok);
        node->ty = lhs->ty;
        return node;
    }

    // ptr - ptr, which returns how many elements are between the two.
    if (lhs->ty->base && rhs->ty->base) {
        if (lhs->ty->base->kind == TY_VOID || rhs->ty->base->kind == TY_VOID)
            warn_tok(vm, tok, CCCC_WARN_POINTER_ARITH,
                     "pointer of type 'void *' used in arithmetic");
        else if (lhs->ty->base->kind == TY_FUNC || rhs->ty->base->kind == TY_FUNC)
            warn_tok(vm, tok, CCCC_WARN_POINTER_ARITH,
                     "pointer to a function used in arithmetic");
        Node *node = new_binary(vm, ND_SUB, lhs, rhs, tok);
        node->ty = ty_long;
        return new_binary(vm, ND_DIV, node,
                          new_num(vm, lhs->ty->base->size, tok), tok);
    }

    error_tok(vm, tok, "invalid operands to -");
    return NULL;
}

// add = mul ("+" mul | "-" mul)*
static Node *add(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = mul(vm, &tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "+")) {
            node = new_add(vm, node, mul(vm, &tok, tok->next), start);
            continue;
        }

        if (equal(tok, "-")) {
            node = new_sub(vm, node, mul(vm, &tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// mul = cast ("*" cast | "/" cast | "%" cast)*
static Node *mul(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = cast(vm, &tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "*")) {
            Node *rhs = cast(vm, &tok, tok->next);
            // Check for error types
            add_type(vm, node);
            add_type(vm, rhs);
            if (is_error_type(node->ty) || is_error_type(rhs->ty)) {
                node = new_binary(vm, ND_MUL, node, rhs, start);
                node->ty = ty_error;
                continue;
            }
            node = new_binary(vm, ND_MUL, node, rhs, start);
            continue;
        }

        if (equal(tok, "/")) {
            Node *rhs = cast(vm, &tok, tok->next);
            // Check for error types
            add_type(vm, node);
            add_type(vm, rhs);
            if (is_error_type(node->ty) || is_error_type(rhs->ty)) {
                node = new_binary(vm, ND_DIV, node, rhs, start);
                node->ty = ty_error;
                continue;
            }
            node = new_binary(vm, ND_DIV, node, rhs, start);
            continue;
        }

        if (equal(tok, "%")) {
            Node *rhs = cast(vm, &tok, tok->next);
            // Check for error types
            add_type(vm, node);
            add_type(vm, rhs);
            if (is_error_type(node->ty) || is_error_type(rhs->ty)) {
                node = new_binary(vm, ND_MOD, node, rhs, start);
                node->ty = ty_error;
                continue;
            }
            node = new_binary(vm, ND_MOD, node, rhs, start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// cast = "(" type-name ")" cast | unary
static Node *cast(VirtualMachine *vm, Token **rest, Token *tok) {
    if (is_compound_literal_head(vm, tok))
        return unary(vm, rest, tok);

    if (equal(tok, "(") && is_typename(vm, tok->next)) {
        Token *start = tok;
        Type *ty = typename(vm, &tok, tok->next);
        tok = skip(vm, tok, ")");

        Node *expr = cast(vm, &tok, tok);

        // Warn when a cast discards the _Atomic qualifier from a pointer type.
        if ((vm->flags & CCCC_THREAD_SAFETY) &&
            !vm->compiler.in_type_lookahead) {
            add_type(vm, expr);
            if (expr->ty && ty &&
                expr->ty->kind == TY_PTR && ty->kind == TY_PTR &&
                expr->ty->base && ty->base &&
                expr->ty->base->is_atomic && !ty->base->is_atomic) {
                warn_tok(vm, start, CCCC_WARN_DISCARDED_QUALIFIERS,
                         "cast discards '_Atomic' qualifier from pointer type; "
                         "non-atomic access to atomic object may cause data races");
            }
        }

        // type cast
        Node *node = new_cast(vm, expr, ty);
        node->tok = start;
        *rest = tok;
        return node;
    }

    return unary(vm, rest, tok);
}

// ========== Block Literal Support (Apple Blocks Extension) ==========

// Recursively collect variables from outer scopes that are referenced in an
// expression
static void collect_captures_in_node(VirtualMachine *vm, Node *node, Obj *outer_locals,
                                     Obj ***captures, int *num_captures,
                                     int *cap_capacity) {
    if (!node)
        return;

    if (node->kind == ND_VAR && node->var && node->var->is_local) {
        // Check if this variable belongs to an outer function (in outer_locals
        // list)
        bool is_outer = false;
        for (Obj *local = outer_locals; local; local = local->next) {
            if (local == node->var) {
                is_outer = true;
                break;
            }
        }

        if (is_outer) {
            // Check if already captured
            for (int i = 0; i < *num_captures; i++) {
                if ((*captures)[i] == node->var)
                    return; // Already captured
            }

            // Add to captures list
            if (*num_captures >= *cap_capacity) {
                *cap_capacity = (*cap_capacity == 0) ? 8 : *cap_capacity * 2;
                Obj **new_caps = arena_alloc(&vm->compiler.parser_arena,
                                             sizeof(Obj *) * (*cap_capacity));
                for (int i = 0; i < *num_captures; i++)
                    new_caps[i] = (*captures)[i];
                *captures = new_caps;
            }
            (*captures)[(*num_captures)++] = node->var;
            node->var->is_captured = true;
        }
    }

    // Recursively check all children
    collect_captures_in_node(vm, node->lhs, outer_locals, captures,
                             num_captures, cap_capacity);
    collect_captures_in_node(vm, node->rhs, outer_locals, captures,
                             num_captures, cap_capacity);
    collect_captures_in_node(vm, node->cond, outer_locals, captures,
                             num_captures, cap_capacity);
    collect_captures_in_node(vm, node->then, outer_locals, captures,
                             num_captures, cap_capacity);
    collect_captures_in_node(vm, node->els, outer_locals, captures,
                             num_captures, cap_capacity);
    collect_captures_in_node(vm, node->init, outer_locals, captures,
                             num_captures, cap_capacity);
    collect_captures_in_node(vm, node->inc, outer_locals, captures,
                             num_captures, cap_capacity);

    for (Node *n = node->body; n; n = n->next)
        collect_captures_in_node(vm, n, outer_locals, captures, num_captures,
                                 cap_capacity);
    for (Node *n = node->args; n; n = n->next)
        collect_captures_in_node(vm, n, outer_locals, captures, num_captures,
                                 cap_capacity);

    // Recurse into nested block literals so intermediate blocks pick up
    // transitive captures (e.g. outer block sees x used inside inner block).
    if (node->kind == ND_BLOCK_LITERAL && node->block_fn)
        collect_captures_in_node(vm, node->block_fn->body, outer_locals, captures,
                                 num_captures, cap_capacity);
}

// Parse a block literal: ^{ ... } or ^(params){ ... } or ^returntype(params){
// ... }
static Node *block_literal(VirtualMachine *vm, Token **rest, Token *tok) {
    Token *start = tok;
    tok = tok->next; // Skip ^

    // Determine return type and parameters
    Type *return_ty = ty_void;
    Type *params = NULL;
    // bool has_params = false;

    // Check for explicit return type (anything before '(' that's a type)
    if (!equal(tok, "{") && !equal(tok, "(") && is_typename(vm, tok)) {
        return_ty = typename(vm, &tok, tok);
    }

    // Check for parameter list
    if (equal(tok, "(")) {
        tok = tok->next;
        // has_params = true;

        if (!equal(tok, ")")) {
            // Parse parameters using declspec + declarator (like func_params)
            Type head = {};
            Type *cur = &head;
            while (!equal(tok, ")")) {
                if (cur != &head)
                    tok = skip(vm, tok, ",");

                VarAttr attr = {};
                Type *param_ty = declspec(vm, &tok, tok, &attr);
                param_ty = declarator(vm, &tok, tok, param_ty);
                param_ty = apply_var_attrs_to_type(vm, param_ty, &attr);
                if (has_custom_attrs(param_ty, &attr))
                    error_tok(vm, param_ty->name ? param_ty->name : tok,
                              "custom attributes are only supported on file-scope declarations");

                // Convert array and function parameters to pointers
                if (param_ty->kind == TY_ARRAY) {
                    Token *name = param_ty->name;
                    param_ty = pointer_to(vm, param_ty->base);
                    param_ty->name = name;
                } else if (param_ty->kind == TY_FUNC) {
                    Token *name = param_ty->name;
                    param_ty = pointer_to(vm, param_ty);
                    param_ty->name = name;
                }

                cur = cur->next = copy_type(vm, param_ty);
            }
            params = head.next;
        }
        tok = skip(vm, tok, ")");
    }

    // Now we must have a compound statement
    if (!equal(tok, "{"))
        error_tok(vm, tok, "expected '{' in block literal");

    // Save current function context
    Obj *outer_fn = vm->compiler.current_fn;
    Obj *saved_locals = vm->compiler.locals;

    // Create a synthetic function for this block
    char *block_name = new_unique_name(vm);
    Type *block_func_ty = func_type(vm, return_ty);
    block_func_ty->params = params;

    Obj *block_fn = new_gvar(vm, block_name, strlen(block_name), block_func_ty);
    block_fn->is_function = true;
    block_fn->is_definition = true;
    block_fn->is_static = true;
    block_fn->is_block = true;
    block_fn->parent_fn = outer_fn;
    block_fn->is_nested =
        true; // Treat blocks like nested functions for codegen
    block_fn->nesting_depth = outer_fn ? outer_fn->nesting_depth + 1 : 1;
    // Store parent's locals snapshot before entering our own scope so nested
    // blocks can walk the ancestry chain during transitive capture collection.
    block_fn->block_outer_locals = saved_locals;

    // Set up block function context
    vm->compiler.current_fn = block_fn;
    vm->compiler.locals = NULL;

    enter_scope(vm);

    // Create params in correct order for calling convention:
    // A0 = __static_link (descriptor), A1 = first user param, A2 = second, etc.
    // Since new_lvar prepends, we need to add in REVERSE order:
    // add last user param, then prev, ..., then first user param, then
    // __static_link

    // Count user params and store in array for reverse iteration
    int param_count = 0;
    for (Type *p = params; p; p = p->next)
        param_count++;

    Type **param_array = NULL;
    if (param_count > 0) {
        param_array = arena_alloc(&vm->compiler.parser_arena,
                                  sizeof(Type *) * param_count);
        int idx = 0;
        for (Type *p = params; p; p = p->next) {
            param_array[idx++] = p;
        }
    }

    // Add user params in reverse order (last first)
    for (int i = param_count - 1; i >= 0; i--) {
        Type *p = param_array[i];
        if (p->name) {
            Obj *param =
                new_lvar(vm, get_ident(vm, p->name), p->name->len, p);
            param->is_param = true;
        }
    }

    // Add __static_link LAST so it ends up FIRST in the list (receives A0)
    new_lvar(vm, "__static_link", 13, pointer_to(vm, ty_void));

    block_fn->params = vm->compiler.locals;
    block_fn->alloca_bottom =
        new_lvar(vm, "__alloca_size__", 15, pointer_to(vm, ty_char));

    // Now parse the block body - params are visible in scope
    tok = skip(vm, tok, "{");
    block_fn->body = compound_stmt(vm, &tok, tok, NULL);
    block_fn->locals = vm->compiler.locals;

    leave_scope(vm);

    // Collect captured variables from the parsed body.
    // Walk all ancestor scopes so that variables from grandparent+ scopes are
    // captured transitively (inner block gets x from outer block's descriptor).
    Obj **captures = NULL;
    int num_captures = 0, cap_capacity = 0;

    // Level 0: immediate parent's locals
    if (saved_locals)
        collect_captures_in_node(vm, block_fn->body, saved_locals, &captures,
                                 &num_captures, &cap_capacity);
    // Levels 1+: walk block ancestor chain via block_outer_locals snapshots
    for (Obj *anc = outer_fn; anc && anc->is_block && anc->block_outer_locals;
         anc = anc->parent_fn)
        collect_captures_in_node(vm, block_fn->body, anc->block_outer_locals,
                                 &captures, &num_captures, &cap_capacity);

    block_fn->captures = captures;
    block_fn->num_captures = num_captures;
    // Descriptor offsets are computed per-block at codegen time via
    // find_capture_index so no per-Obj offset field is needed.

    // Restore outer function context
    vm->compiler.current_fn = outer_fn;
    vm->compiler.locals = saved_locals;

    // Allocate descriptor storage on the enclosing function's stack frame.
    // Layout: [invoke_ptr(0) | desc_size(8) | cap0(16) | cap1(24) | ...]
    // Per-frame stack allocation ensures each invocation gets its own descriptor,
    // so multiple calls to a function returning the same block literal are independent.
    int desc_slots = 2 + num_captures;
    Type *desc_arr_ty = array_of(vm, ty_long, desc_slots);
    Obj *desc_var = new_lvar(vm, "", 0, desc_arr_ty);

    // Create the block literal node
    Node *node = new_node(vm, ND_BLOCK_LITERAL, start);
    node->block_fn = block_fn;
    node->block_captures = captures;
    node->num_block_captures = num_captures;
    node->block_desc_var = desc_var;

    // Block type: pointer to function type (blocks are first-class callable
    // values)
    node->ty = block_type(vm, return_ty, params);

    *rest = tok;
    return node;
}

// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | ("++" | "--") unary
//       | "&&" ident
//       | "^" block-literal  (Apple blocks extension)
//       | postfix
static Node *unary(VirtualMachine *vm, Token **rest, Token *tok) {
    // Apple blocks: ^{ ... } or ^(params){ ... } or ^returntype(params){ ... }
    if (equal(tok, "^") && tok->next &&
        (equal(tok->next, "{") || equal(tok->next, "(") ||
         is_typename(vm, tok->next))) {
        return block_literal(vm, rest, tok);
    }

    if (equal(tok, "+"))
        return cast(vm, rest, tok->next);

    if (equal(tok, "-"))
        return new_unary(vm, ND_NEG, cast(vm, rest, tok->next), tok);

    if (equal(tok, "&")) {
        Node *lhs = cast(vm, rest, tok->next);
        add_type(vm, lhs);
        if (lhs->kind == ND_MEMBER && lhs->member->is_bitfield) {
            if (vm->collect_errors &&
                error_tok_recover(vm, tok, "cannot take address of bitfield")) {
                // Return the member itself as an error placeholder
                return lhs;
            }
            error_tok(vm, tok, "cannot take address of bitfield");
        }
        return new_unary(vm, ND_ADDR, lhs, tok);
    }

    if (equal(tok, "*")) {
        // [https://www.sigbus.info/n1570#6.5.3.2p4] This is an oddity
        // in the C spec, but dereferencing a function shouldn't do
        // anything. If foo is a function, `*foo`, `**foo` or `*****foo`
        // are all equivalent to just `foo`.
        Node *node = cast(vm, rest, tok->next);
        add_type(vm, node);
        if (node->ty->kind == TY_FUNC)
            return node;
        return new_unary(vm, ND_DEREF, node, tok);
    }

    if (equal(tok, "!"))
        return new_unary(vm, ND_NOT, cast(vm, rest, tok->next), tok);

    if (equal(tok, "~"))
        return new_unary(vm, ND_BITNOT, cast(vm, rest, tok->next), tok);

    // Read ++i as i+=1
    if (equal(tok, "++"))
        return to_assign(vm, new_add(vm, unary(vm, rest, tok->next),
                                     new_num(vm, 1, tok), tok));

    // Read --i as i-=1
    if (equal(tok, "--"))
        return to_assign(vm, new_sub(vm, unary(vm, rest, tok->next),
                                     new_num(vm, 1, tok), tok));

    // [GNU] labels-as-values
    if (equal(tok, "&&")) {
        Node *node = new_node(vm, ND_LABEL_VAL, tok);
        node->label = get_ident(vm, tok->next);
        node->goto_next = vm->compiler.gotos;
        vm->compiler.gotos = node;
        *rest = tok->next->next;
        return node;
    }

    return postfix(vm, rest, tok);
}

// struct-members = (declspec declarator (","  declarator)* ";")*
static void struct_members(VirtualMachine *vm, Token **rest, Token *tok, Type *ty) {
    Member head = {};
    Member *cur = &head;
    int idx = 0;

    while (!equal(tok, "}")) {
        VarAttr attr = {};
        Type *basety = declspec(vm, &tok, tok, &attr);
        if (has_custom_attrs(basety, &attr))
            error_tok(vm, tok,
                      "custom attributes are only supported on file-scope declarations");
        bool first = true;

        // Anonymous struct member
        Token *anon_tok = tok;
        if ((basety->kind == TY_STRUCT || basety->kind == TY_UNION) &&
            consume(vm, &tok, tok, ";")) {
            if (vm->compiler.c_std < CCCC_STD_C11)
                warn_tok(vm, anon_tok, CCCC_WARN_PEDANTIC,
                         "anonymous structs/unions are a C11 extension");
            Member *mem =
                arena_alloc(&vm->compiler.parser_arena, sizeof(Member));
            memset(mem, 0, sizeof(Member));
            mem->ty = basety;
            mem->idx = idx++;
            mem->align = attr.align ? attr.align : mem->ty->align;
            cur = cur->next = mem;
            continue;
        }

        // Regular struct members
        while (!consume(vm, &tok, tok, ";")) {
            if (!first)
                tok = skip(vm, tok, ",");
            first = false;

            Member *mem =
                arena_alloc(&vm->compiler.parser_arena, sizeof(Member));
            memset(mem, 0, sizeof(Member));
            mem->ty = declarator(vm, &tok, tok, basety);
            if (has_custom_attrs(mem->ty, NULL))
                error_tok(vm, mem->name ? mem->name : tok,
                          "custom attributes are only supported on file-scope declarations");
            mem->name = mem->ty->name;
            mem->idx = idx++;
            mem->align = attr.align ? attr.align : mem->ty->align;

            if (consume(vm, &tok, tok, ":")) {
                mem->is_bitfield = true;
                mem->bit_width = const_expr(vm, &tok, tok);
                if (mem->bit_width < 0)
                    error_tok(vm, tok, "negative bit-field width");
                if (mem->bit_width > mem->ty->size * CHAR_BIT)
                    error_tok(vm, tok, "bit-field width exceeds its type");
            }

            cur = cur->next = mem;
        }
    }

    // If the last element is an array of incomplete type, it's
    // called a "flexible array member". It should behave as if
    // if were a zero-sized array.
    if (cur != &head && cur->ty->kind == TY_ARRAY && cur->ty->array_len < 0) {
        if (vm->compiler.c_std < CCCC_STD_C99)
            error_tok(vm, cur->name ? cur->name : *rest,
                      "flexible array members are not available before C99");
        cur->ty = array_of(vm, cur->ty->base, 0);
        ty->is_flexible = true;
    }

    *rest = tok->next;
    ty->members = head.next;
}

static bool is_attr_name(Token *tok, char *name) {
    if (equal(tok, name))
        return true;
    int len = strlen(name);
    return tok->len == len + 4 && !memcmp(tok->loc, "__", 2) &&
           !memcmp(tok->loc + 2, name, len) &&
           !memcmp(tok->loc + 2 + len, "__", 2);
}

static void apply_semantic_attr(Type *ty, VarAttr *attr, Token *tok,
                                bool unused, bool deprecated, bool nodiscard,
                                char *message) {
    if (ty) {
        ty->is_maybe_unused |= unused;
        ty->is_deprecated |= deprecated;
        ty->is_nodiscard |= nodiscard;
        if (message) {
            if (deprecated)
                ty->deprecated_msg = message;
            if (nodiscard)
                ty->nodiscard_msg = message;
        }
    }
    if (attr) {
        attr->is_maybe_unused |= unused;
        attr->is_deprecated |= deprecated;
        attr->is_nodiscard |= nodiscard;
        if (message) {
            if (deprecated)
                attr->deprecated_msg = message;
            if (nodiscard)
                attr->nodiscard_msg = message;
        }
        if (unused || deprecated || nodiscard)
            attr->attribute_tok = tok;
    }
}

static Type *apply_var_attrs_to_type(VirtualMachine *vm, Type *ty, VarAttr *attr) {
    if (!attr || (!attr->is_maybe_unused && !attr->is_deprecated &&
                  !attr->is_noreturn && !attr->is_nodiscard &&
                  !attr->is_pure && !attr->is_func_const &&
                  !attr->format_style))
        return ty;
    ty = copy_type(vm, ty);
    apply_semantic_attr(ty, NULL, attr->attribute_tok, attr->is_maybe_unused,
                        attr->is_deprecated, attr->is_nodiscard,
                        attr->deprecated_msg ? attr->deprecated_msg
                                             : attr->nodiscard_msg);
    if (attr->is_noreturn && ty->kind == TY_FUNC)
        ty->is_noreturn = true;
    if (attr->is_pure && ty->kind == TY_FUNC)
        ty->is_pure = true;
    if (attr->is_func_const && ty->kind == TY_FUNC)
        ty->is_func_const = true;
    if (attr->format_style && ty->kind == TY_FUNC) {
        ty->format_style = attr->format_style;
        ty->format_string_index = attr->format_string_index;
        ty->format_fmt_first_arg = attr->format_fmt_first_arg;
    }
    return ty;
}

static void inherit_semantic_attrs(Type *dst, Type *src) {
    if (!dst || !src)
        return;
    dst->is_maybe_unused |= src->is_maybe_unused;
    dst->is_deprecated |= src->is_deprecated;
    dst->is_nodiscard |= src->is_nodiscard;
    dst->is_noreturn |= src->is_noreturn;
    dst->is_pure |= src->is_pure;
    dst->is_func_const |= src->is_func_const;
    if (!dst->deprecated_msg)
        dst->deprecated_msg = src->deprecated_msg;
    if (!dst->nodiscard_msg)
        dst->nodiscard_msg = src->nodiscard_msg;
    if (src->format_style) {
        dst->format_style = src->format_style;
        dst->format_string_index = src->format_string_index;
        dst->format_fmt_first_arg = src->format_fmt_first_arg;
    }
}

// Parse optimize attribute argument: (N) where N is an integer 0-4, or ("ON") where
// the string is "O0".."O4" or "-O0".."-O4" (GCC-compatible form).
// Sets the optimize fields on ty and/or attr and marks have_fn_opt_attrs on the
// compiler. Returns the token after the closing ')'.
static Token *parse_optimize_attr(VirtualMachine *vm, Token *tok,
                                  Type *ty, VarAttr *attr) {
    tok = skip(vm, tok, "(");

    int level = -1;

    if (tok->kind == TK_NUM || tok->kind == TK_PP_NUM) {
        // Integer form: [[cccc::optimize(2)]] / __attribute__((optimize(2)))
        // Use tok->val if available (TK_NUM); fall back to strtol on raw text.
        long long val;
        if (tok->kind == TK_NUM) {
            val = tok->val;
        } else {
            char *ep = NULL;
            val = strtoll(tok->loc, &ep, 10);
            if (ep == tok->loc)
                error_tok(vm, tok,
                          "optimize level must be an integer 0-4 (got '%.*s')",
                          tok->len, tok->loc);
        }
        if (val < 0 || val > 4)
            error_tok(vm, tok,
                      "optimize level must be an integer 0-4 (got %lld)", val);
        level = (int)val;
        tok = tok->next;
    } else if (tok->kind == TK_STR) {
        // String form: __attribute__((optimize("O2"))) or [[cccc::optimize("-O2")]]
        const char *s = tok->str;
        if (*s == '-') s++;          // skip optional leading '-'
        if (*s != 'O' && *s != 'o')
            error_tok(vm, tok,
                      "optimize string must be 'O0'–'O4' or '-O0'–'-O4' (got '%s')",
                      tok->str);
        s++;
        if (*s < '0' || *s > '4' || *(s + 1) != '\0')
            error_tok(vm, tok,
                      "optimize string must be 'O0'–'O4' or '-O0'–'-O4' (got '%s')",
                      tok->str);
        level = (int)(*s - '0');
        tok = tok->next;
    } else {
        error_tok(vm, tok,
                  "optimize attribute expects an integer 0-4 or a string "
                  "like \"O2\" or \"-O2\"");
    }

    if (ty) {
        ty->fn_optimize_level = level;
        ty->fn_optimize_set   = true;
    }
    if (attr) {
        attr->fn_optimize_level = level;
        attr->fn_optimize_set   = true;
    }
    if (!vm->compiler.in_type_lookahead)
        vm->compiler.have_fn_opt_attrs = true;

    tok = skip(vm, tok, ")");
    return tok;
}

// attribute = ("__attribute__" "(" "(" attribute-list ")" ")")*
static Token *attribute_list(VirtualMachine *vm, Token *tok, Type *ty, VarAttr *attr) {
    while (consume(vm, &tok, tok, "__attribute__")) {
        tok = skip(vm, tok, "(");
        tok = skip(vm, tok, "(");

        bool first = true;

        while (!consume(vm, &tok, tok, ")")) {
            if (!first)
                tok = skip(vm, tok, ",");
            first = false;

            Token *attr_tok = tok;

            // Handle packed attribute
            if (consume(vm, &tok, tok, "packed")) {
                if (ty)
                    ty->is_packed = true;
                continue;
            }

            // Handle aligned attribute
            if (consume(vm, &tok, tok, "aligned")) {
                if (equal(tok, "(")) {
                    tok = skip(vm, tok, "(");
                    int align = const_expr(vm, &tok, tok);
                    if (ty)
                        ty->align = align;
                    tok = skip(vm, tok, ")");
                }
                continue;
            }

            bool unused = is_attr_name(tok, "unused");
            bool deprecated = is_attr_name(tok, "deprecated");
            if (unused || deprecated) {
                tok = tok->next;
                char *message = NULL;
                if (equal(tok, "(")) {
                    int depth = 1;
                    tok = tok->next;
                    if (deprecated && tok->kind == TK_STR)
                        message = tok->str;
                    while (depth > 0) {
                        if (equal(tok, "("))
                            depth++;
                        else if (equal(tok, ")"))
                            depth--;
                        tok = tok->next;
                    }
                }
            apply_semantic_attr(ty, attr, attr_tok, unused, deprecated, false,
                                message);
                continue;
            }

            // Handle format attribute: __attribute__((format(printf, fmt_idx, first_arg)))
            if (is_attr_name(tok, "format")) {
                tok = tok->next;
                if (equal(tok, "(")) {
                    tok = tok->next;
                    int style = 0;
                    if (equal(tok, "printf"))
                        style = 1;
                    else if (equal(tok, "scanf"))
                        style = 2;
                    else
                        error_tok(vm, tok,
                                  "expected 'printf' or 'scanf' in format attribute");
                    tok = tok->next;
                    tok = skip(vm, tok, ",");
                    int fmt_idx = const_expr(vm, &tok, tok);
                    tok = skip(vm, tok, ",");
                    int first_arg = const_expr(vm, &tok, tok);
                    tok = skip(vm, tok, ")");
                    if (ty && ty->kind == TY_FUNC) {
                        ty->format_style = style;
                        ty->format_string_index = fmt_idx;
                        ty->format_fmt_first_arg = first_arg;
                    }
                    if (attr) {
                        attr->format_style = style;
                        attr->format_string_index = fmt_idx;
                        attr->format_fmt_first_arg = first_arg;
                    }
                }
                continue;
            }

            // Handle noreturn attribute
            if (is_attr_name(tok, "noreturn")) {
                tok = tok->next;
                if (ty) ty->is_noreturn = true;
                if (attr) attr->is_noreturn = true;
                continue;
            }

            if (is_attr_name(tok, "pure")) {
                tok = tok->next;
                if (ty) ty->is_pure = true;
                if (attr) attr->is_pure = true;
                continue;
            }

            if (is_attr_name(tok, "const")) {
                tok = tok->next;
                if (ty) ty->is_func_const = true;
                if (attr) attr->is_func_const = true;
                continue;
            }

            // Handle optimize attribute: __attribute__((optimize("O2"))) or optimize(2)
            if (is_attr_name(tok, "optimize")) {
                tok = tok->next;
                tok = parse_optimize_attr(vm, tok, ty, attr);
                continue;
            }

            if (find_attribute_macro(vm, tok)) {
                Token *name_tok = tok;
                tok = tok->next;
                Node *args = NULL;
                int arg_count = 0;
                tok = parse_custom_attr_args(vm, tok, &args, &arg_count);
                if (!vm->compiler.in_type_lookahead) {
                    if (attr)
                        append_custom_attr(vm, &attr->custom_attrs, name_tok,
                                           args, arg_count);
                    else if (ty)
                        append_custom_attr(vm, &ty->custom_attrs, name_tok,
                                           args, arg_count);
                    else
                        error_tok(vm, name_tok,
                                  "custom attribute '%.*s' is not valid here",
                                  name_tok->len, name_tok->loc);
                }
                continue;
            }

            // Handle all other attributes - just consume and ignore them
            if (tok->kind == TK_IDENT) {
                Token *name_tok = tok;
                tok = tok->next;
                warn_tok(vm, name_tok, CCCC_WARN_ATTRIBUTES,
                         "unknown attribute '%.*s' ignored",
                         name_tok->len, name_tok->loc);

                // Handle attributes with parameters: attr(args...)
                if (equal(tok, "(")) {
                    int depth = 1;
                    tok = tok->next;
                    // Skip all tokens until matching closing paren
                    while (depth > 0) {
                        if (equal(tok, "("))
                            depth++;
                        else if (equal(tok, ")"))
                            depth--;
                        tok = tok->next;
                    }
                }
                continue;
            }

            // If we hit something unexpected, just skip it
            tok = tok->next;
        }

        tok = skip(vm, tok, ")");
    }

    return tok;
}

// c23-attribute = ("[[" attribute-list "]]")*
static Token *c23_attribute_list(VirtualMachine *vm, Token *tok, Type *ty,
                                 VarAttr *attr) {
    while (equal(tok, "[") && equal(tok->next, "[")) {
        if (vm->compiler.c_std < CCCC_STD_C23 &&
            !vm->compiler.in_type_lookahead)
            warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                     "'[[...]]' attributes are a C23 extension");
        tok = tok->next->next; // Skip [[

        bool first = true;

        while (!equal(tok, "]")) {
            if (!first)
                tok = skip(vm, tok, ",");
            first = false;

            // Parse attribute name
            if (tok->kind != TK_IDENT)
                error_tok(vm, tok, "expected attribute name");

            Token *attr_tok = tok;
            Token *name_tok = tok;
            bool cccc_scoped = false;
            bool gnu_scoped = false;
            if (tok->next && equal(tok->next, ":") &&
                tok->next->next && equal(tok->next->next, ":") &&
                tok->next->next->next &&
                (tok->next->next->next->kind == TK_IDENT ||
                 tok->next->next->next->kind == TK_KEYWORD)) {
                if (equal(tok, "cccc")) {
                    cccc_scoped = true;
                } else if (equal(tok, "gnu")) {
                    gnu_scoped = true;
                }
                if (cccc_scoped || gnu_scoped) {
                    name_tok = tok->next->next->next;
                    tok = name_tok;
                }
            }

            bool unused = equal(name_tok, "maybe_unused");
            bool deprecated = equal(name_tok, "deprecated");
            bool is_noreturn_attr = equal(name_tok, "noreturn");
            bool is_nodiscard_attr = equal(name_tok, "nodiscard");
            bool is_fallthrough_attr = equal(name_tok, "fallthrough");
            bool is_no_unique_address_attr = equal(name_tok, "no_unique_address");
            bool is_pure_attr = equal(name_tok, "pure");
            bool is_func_const_attr = equal(name_tok, "const");
            bool is_optimize_attr = equal(name_tok, "optimize");
            tok = tok->next;

            // Optimize attribute has mandatory args: [[cccc::optimize(2)]] or ("O2")
            if (is_optimize_attr) {
                if (!equal(tok, "("))
                    error_tok(vm, attr_tok,
                              "optimize attribute requires a level argument, "
                              "e.g. [[cccc::optimize(2)]] or [[cccc::optimize(\"O2\")]]");
                tok = parse_optimize_attr(vm, tok, ty, attr);
                continue;
            }

            char *message = NULL;
            if (equal(tok, "(")) {
                if (find_attribute_macro(vm, name_tok)) {
                    Node *args = NULL;
                    int arg_count = 0;
                    tok = parse_custom_attr_args(vm, tok, &args, &arg_count);
                    if (!vm->compiler.in_type_lookahead) {
                        if (attr)
                            append_custom_attr(vm, &attr->custom_attrs,
                                               name_tok, args, arg_count);
                        else if (ty)
                            append_custom_attr(vm, &ty->custom_attrs,
                                               name_tok, args, arg_count);
                        else
                            error_tok(vm, name_tok,
                                      "custom attribute '%.*s' is not valid here",
                                      name_tok->len, name_tok->loc);
                    }
                    continue;
                } else {
                    int depth = 1;
                    tok = tok->next;
                    if ((deprecated || is_nodiscard_attr) && tok->kind == TK_STR)
                        message = tok->str;
                    while (depth > 0) {
                        if (equal(tok, "("))
                            depth++;
                        else if (equal(tok, ")"))
                            depth--;
                        tok = tok->next;
                    }
                }
            }
            if (find_attribute_macro(vm, name_tok)) {
                if (!vm->compiler.in_type_lookahead) {
                    if (attr)
                        append_custom_attr(vm, &attr->custom_attrs, name_tok,
                                           NULL, 0);
                    else if (ty)
                        append_custom_attr(vm, &ty->custom_attrs, name_tok,
                                           NULL, 0);
                    else
                        error_tok(vm, name_tok,
                                  "custom attribute '%.*s' is not valid here",
                                  name_tok->len, name_tok->loc);
                }
                continue;
            }
            if (is_noreturn_attr) {
                if (ty) ty->is_noreturn = true;
                if (attr) attr->is_noreturn = true;
            } else if (is_nodiscard_attr) {
                if (ty) ty->is_nodiscard = true;
                if (attr) {
                    attr->is_nodiscard = true;
                    if (message)
                        attr->nodiscard_msg = message;
                    attr->attribute_tok = attr_tok;
                }
            } else if (is_fallthrough_attr) {
                if (attr)
                    attr->is_fallthrough = true;
            } else if (is_no_unique_address_attr) {
                // Parsed but VM optimisations deferred to a future ticket
            } else if (is_pure_attr) {
                if (ty) ty->is_pure = true;
                if (attr) attr->is_pure = true;
            } else if (is_func_const_attr) {
                if (ty) ty->is_func_const = true;
                if (attr) attr->is_func_const = true;
            } else if (!unused && !deprecated) {
                warn_tok(vm, attr_tok, CCCC_WARN_ATTRIBUTES,
                         "unknown attribute '%.*s' ignored",
                         (cccc_scoped || gnu_scoped) ? name_tok->len : attr_tok->len,
                         (cccc_scoped || gnu_scoped) ? name_tok->loc : attr_tok->loc);
                apply_semantic_attr(ty, attr, attr_tok, unused, deprecated,
                                    false, NULL);
            } else {
                apply_semantic_attr(ty, attr, attr_tok, unused, deprecated,
                                    false, message);
            }
        }

        tok = skip(vm, tok, "]");
        tok = skip(vm, tok, "]");
    }

    return tok;
}

// struct-union-decl = attribute? ident? ("{" struct-members)?
static Type *struct_union_decl(VirtualMachine *vm, Token **rest, Token *tok,
                               TypeKind kind) {
    Type *ty = struct_type(vm);
    ty->kind = kind;
    tok = attribute_list(vm, tok, ty, NULL);
    tok = c23_attribute_list(vm, tok, ty, NULL);

    // Read a tag.
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        ty->name = tag;
        ty->name_pos = tag;
        tok = tok->next;
    }

    if (tag && !equal(tok, "{")) {
        *rest = tok;

        Type *ty2 = find_tag(vm, tag);
        if (ty2) {
            if (ty2->kind != kind)
                error_tok(vm, tag, "tag redeclared as different kind");
            if (ty2->is_deprecated)
                warn_deprecated_use(vm, tag, get_ident(vm, tag),
                                    ty2->deprecated_msg);
            return ty2->is_deprecated ? type_after_deprecated_use(vm, ty2)
                                      : ty2;
        }

        ty->size = -1;
        push_tag_scope(vm, tag, ty);
        return ty;
    }

    tok = skip(vm, tok, "{");

    // Construct a struct object.
    struct_members(vm, &tok, tok, ty);
    tok = attribute_list(vm, tok, ty, NULL);
    *rest = c23_attribute_list(vm, tok, ty, NULL);
    return ty;
}

// struct-decl = struct-union-decl
static Type *struct_decl(VirtualMachine *vm, Token **rest, Token *tok) {
    Type *ty = struct_union_decl(vm, rest, tok, TY_STRUCT);

    if (ty->size < 0)
        return ty;

    // Assign offsets within the struct to members.
    int bits = 0;

    for (Member *mem = ty->members; mem; mem = mem->next) {
        if (mem->is_bitfield && mem->bit_width == 0) {
            // Zero-width anonymous bitfield has a special meaning.
            // It affects only alignment.
            bits = align_to(bits, mem->ty->size * 8);
        } else if (mem->is_bitfield) {
            int sz = mem->ty->size;
            if (bits / (sz * 8) != (bits + mem->bit_width - 1) / (sz * 8))
                bits = align_to(bits, sz * 8);

            mem->offset = align_down(bits / 8, sz);
            mem->bit_offset = bits % (sz * 8);
            bits += mem->bit_width;
        } else {
            // Flexible array members (array with size 0) should not add padding
            // before them, but they DO affect struct alignment (for final size
            // calculation)
            bool is_flexible_array =
                (mem->ty->kind == TY_ARRAY && mem->ty->array_len == 0);
            if (!ty->is_packed && !is_flexible_array)
                bits = align_to(bits, mem->align * 8);
            mem->offset = bits / 8;
            bits += mem->ty->size * 8;

            // Update struct alignment (including for flexible arrays, for final
            // size padding)
            if (!ty->is_packed && ty->align < mem->align)
                ty->align = mem->align;
        }
    }

    ty->size = align_to(bits, ty->align * 8) / 8;
    return install_tag_definition(vm, ty->name, ty, "struct");
}

// union-decl = struct-union-decl
static Type *union_decl(VirtualMachine *vm, Token **rest, Token *tok) {
    Type *ty = struct_union_decl(vm, rest, tok, TY_UNION);

    if (ty->size < 0)
        return ty;

    // If union, we don't have to assign offsets because they
    // are already initialized to zero. We need to compute the
    // alignment and the size though.
    for (Member *mem = ty->members; mem; mem = mem->next) {
        if (ty->align < mem->align)
            ty->align = mem->align;
        if (ty->size < mem->ty->size)
            ty->size = mem->ty->size;
    }
    ty->size = align_to(ty->size, ty->align);
    return install_tag_definition(vm, ty->name, ty, "union");
}

// Find a struct member by name.
static Member *get_struct_member(Type *ty, Token *tok) {
    for (Member *mem = ty->members; mem; mem = mem->next) {
        // Anonymous struct member
        if ((mem->ty->kind == TY_STRUCT || mem->ty->kind == TY_UNION) &&
            !mem->name) {
            if (get_struct_member(mem->ty, tok))
                return mem;
            continue;
        }

        // Regular struct member
        if (mem->name->len == tok->len &&
            !strncmp(mem->name->loc, tok->loc, tok->len))
            return mem;
    }
    return NULL;
}

// Create a node representing a struct member access, such as foo.bar
// where foo is a struct and bar is a member name.
//
// C has a feature called "anonymous struct" which allows a struct to
// have another unnamed struct as a member like this:
//
//   struct { struct { int a; }; int b; } x;
//
// The members of an anonymous struct belong to the outer struct's
// member namespace. Therefore, in the above example, you can access
// member "a" of the anonymous struct as "x.a".
//
// This function takes care of anonymous structs.
static Node *struct_ref(VirtualMachine *vm, Node *node, Token *tok) {
    add_type(vm, node);

    // If the base expression has error type, propagate it
    if (node->ty && is_error_type(node->ty)) {
        Node *err_node = new_node(vm, ND_MEMBER, tok);
        err_node->ty = ty_error;
        return err_node;
    }

    if (node->ty->kind != TY_STRUCT && node->ty->kind != TY_UNION) {
        if (vm->collect_errors &&
            error_tok_recover(vm, node->tok, "not a struct nor a union")) {
            Node *err_node = new_node(vm, ND_MEMBER, tok);
            err_node->ty = ty_error;
            return err_node;
        }
        error_tok(vm, node->tok, "not a struct nor a union");
    }

    Type *ty = node->ty;

    for (;;) {
        Member *mem = get_struct_member(ty, tok);
        if (!mem) {
            if (vm->collect_errors &&
                error_tok_recover(vm, tok, "no such member '%.*s'", tok->len,
                                  tok->loc)) {
                Node *err_node = new_node(vm, ND_MEMBER, tok);
                err_node->ty = ty_error;
                return err_node;
            }
            error_tok(vm, tok, "no such member '%.*s'", tok->len,
                      tok->loc);
        }
        node = new_unary(vm, ND_MEMBER, node, tok);
        node->member = mem;
        if (mem->name)
            break;
        ty = mem->ty;
    }
    return node;
}

// Convert A++ to `(typeof A)((A += 1) - 1)`
static Node *new_inc_dec(VirtualMachine *vm, Node *node, Token *tok, int addend) {
    add_type(vm, node);
    return new_cast(
        vm,
        new_add(vm,
                to_assign(vm, new_add(vm, node, new_num(vm, addend, tok), tok)),
                new_num(vm, -addend, tok), tok),
        node->ty);
}

static Type *compound_literal_type(VirtualMachine *vm, Token **rest, Token *tok,
                                   VarAttr *attr) {
    bool saw_register = false;
    bool saw_auto = false;
    for (Token *p = tok; !equal(p, ")") && p->kind != TK_EOF; p = p->next) {
        DeclKw dk = declspec_kw(p);
        if (dk == DK_REGISTER)
            saw_register = true;
        else if (dk == DK_AUTO)
            saw_auto = true;
    }

    Type *ty = declspec(vm, &tok, tok, attr);

    if (saw_auto || attr->is_typedef || attr->is_extern || attr->is_inline ||
        attr->is_block_var)
        error_tok(vm, tok, "invalid storage class in compound literal");
    if ((saw_register || attr->is_static || attr->is_tls ||
         attr->is_constexpr) &&
        vm->compiler.c_std < CCCC_STD_C23)
        error_tok(vm, tok,
                  "compound literal storage classes are only available in C23");

    ty = abstract_declarator(vm, rest, tok, ty);
    if (attr->is_constexpr) {
        ty = copy_type(vm, ty);
        ty->is_const = true;
    }
    return ty;
}

static bool is_compound_literal_head(VirtualMachine *vm, Token *tok) {
    if (!equal(tok, "(") || !is_typename(vm, tok->next))
        return false;

    int depth = 1;
    for (Token *p = tok->next; p && p->kind != TK_EOF; p = p->next) {
        if (equal(p, "("))
            depth++;
        else if (equal(p, ")")) {
            depth--;
            if (depth == 0)
                return equal(p->next, "{");
        }
    }
    return false;
}

// postfix = "(" type-name ")" "{" initializer-list "}"
//         = ident "(" func-args ")" postfix-tail*
//         | primary postfix-tail*
//
// postfix-tail = "[" expr "]"
//              | "(" func-args ")"
//              | "." ident
//              | "->" ident
//              | "++"
//              | "--"
static Node *postfix(VirtualMachine *vm, Token **rest, Token *tok) {
    if (is_compound_literal_head(vm, tok)) {
        // Compound literal
        Token *start = tok;
        if (vm->compiler.c_std < CCCC_STD_C99)
            warn_tok(vm, start, CCCC_WARN_PEDANTIC,
                     "compound literals are a C99 extension");
        VarAttr attr = {};
        Type *ty = compound_literal_type(vm, &tok, tok->next, &attr);
        tok = skip(vm, tok, ")");

        if (vm->compiler.scope->next == NULL || attr.is_static ||
            attr.is_constexpr || attr.is_tls) {
            Obj *var = new_anon_gvar(vm, ty);
            var->is_constexpr = attr.is_constexpr;
            var->is_static = attr.is_static || attr.is_constexpr;
            var->is_tls = attr.is_tls;
            var->is_local_symbol = vm->compiler.scope->next != NULL;
            gvar_initializer(vm, rest, tok, var);
            return new_var_node(vm, var, start);
        }

        Obj *var = new_lvar(vm, "", 0, ty);
        Node *lhs = lvar_initializer(vm, rest, tok, var);
        Node *rhs = new_var_node(vm, var, tok);
        return new_binary(vm, ND_COMMA, lhs, rhs, start);
    }

    Node *node = primary(vm, &tok, tok);

    for (;;) {
        if (equal(tok, "(")) {
            // Check if this is a block invocation
            add_type(vm, node);
            if (node->ty && node->ty->kind == TY_BLOCK) {
                // Block invocation: create ND_BLOCK_CALL
                Token *start = tok;
                tok = tok->next; // Skip '('

                // Parse arguments
                Node head = {};
                Node *cur = &head;
                while (!equal(tok, ")")) {
                    if (cur != &head)
                        tok = skip(vm, tok, ",");
                    Node *arg = assign(vm, &tok, tok);
                    cur = cur->next = arg;
                }
                tok = tok->next; // Skip ')'

                Node *call = new_node(vm, ND_BLOCK_CALL, start);
                call->lhs = node;
                call->args = head.next;
                call->ty = node->ty->return_ty ? node->ty->return_ty : ty_void;
                node = call;
            } else {
                node = funcall(vm, &tok, tok->next, node);
            }
            continue;
        }

        if (equal(tok, "[")) {
            // x[y] is short for *(x+y)
            Token *start = tok;
            Node *idx = expr(vm, &tok, tok->next);

            // Try to recover if ']' is missing
            if (!equal(tok, "]")) {
                if (vm->collect_errors &&
                    error_tok_recover(vm, tok, "expected ']'")) {
                    // Use index 0 as placeholder and continue
                    idx = new_num(vm, 0, tok);
                } else {
                    tok = skip(vm, tok, "]");
                }
            } else {
                tok = tok->next;
            }

            node =
                new_unary(vm, ND_DEREF, new_add(vm, node, idx, start), start);
            continue;
        }

        if (equal(tok, ".")) {
            node = struct_ref(vm, node, tok->next);
            tok = tok->next->next;
            continue;
        }

        if (equal(tok, "->")) {
            // x->y is short for (*x).y
            node = new_unary(vm, ND_DEREF, node, tok);
            node = struct_ref(vm, node, tok->next);
            tok = tok->next->next;
            continue;
        }

        if (equal(tok, "++")) {
            node = new_inc_dec(vm, node, tok, 1);
            tok = tok->next;
            continue;
        }

        if (equal(tok, "--")) {
            node = new_inc_dec(vm, node, tok, -1);
            tok = tok->next;
            continue;
        }

        *rest = tok;
        return node;
    }
}

// Expected argument types for format string validation
enum {
    FMT_EXPECT_INT,
    FMT_EXPECT_UINT,
    FMT_EXPECT_DOUBLE,
    FMT_EXPECT_STRING,       // char*
    FMT_EXPECT_POINTER,      // void*
    FMT_EXPECT_INT_PTR,      // int* (for %n, scanf %d)
    FMT_EXPECT_UINT_PTR,     // unsigned int* (scanf %u, %x)
    FMT_EXPECT_FLOAT_PTR,    // float* (scanf %f)
    // length-modifier-aware printf variants
    FMT_EXPECT_LONG,         // %ld, %lld, %jd, %td
    FMT_EXPECT_ULONG,        // %lu, %llu, %zu, %ju
    FMT_EXPECT_LDOUBLE,      // %Lf, %Le, %Lg, %La
    // length-modifier-aware scanf pointer variants
    FMT_EXPECT_LONG_PTR,     // scanf %ld → long *
    FMT_EXPECT_ULONG_PTR,    // scanf %lu, %zu → unsigned long *
    FMT_EXPECT_SHORT_PTR,    // scanf %hd → short *
    FMT_EXPECT_SCHAR_PTR,    // scanf %hhd → char *
    FMT_EXPECT_LDOUBLE_PTR,  // scanf %Lf → long double *
};

#define MAX_FMT_ARGS 64

static const char *fmt_type_names[] = {
    "int", "unsigned int", "double", "char *", "void *",
    "int *", "unsigned int *", "float *",
    "long", "unsigned long", "long double",
    "long *", "unsigned long *", "short *", "char *", "long double *"
};

// Validate format string arguments for __attribute__((format(...)))
static void validate_format_call(VirtualMachine *vm, Token *tok, Type *func_ty,
                                  Node *args) {
    if (!func_ty->format_style)
        return;

    // Walk to the format string argument (0-based index)
    Node *fmt_arg = args;
    int idx = 0;
    while (fmt_arg && idx < func_ty->format_string_index - 1) {
        fmt_arg = fmt_arg->next;
        idx++;
    }

    if (!fmt_arg)
        return;

    // String literals may be wrapped in ND_CAST for array-to-pointer decay
    if (fmt_arg->kind == ND_CAST)
        fmt_arg = fmt_arg->lhs;

    if (fmt_arg->kind != ND_VAR || !fmt_arg->var || !fmt_arg->var->init_data)
        return;

    const char *fmt = fmt_arg->var->init_data;
    int style = func_ty->format_style;

    // Count variadic args
    Node *vararg = args;
    int vararg_idx = 0;
    int num_varargs = 0;
    while (vararg) {
        vararg_idx++;
        if (vararg_idx >= func_ty->format_fmt_first_arg)
            num_varargs++;
        vararg = vararg->next;
    }

    // Parse format string and collect expected types
    int fmt_count = 0;
    int expected[MAX_FMT_ARGS];
    int num_expected = 0;

    const char *p = fmt;
    while (*p && num_expected < MAX_FMT_ARGS) {
        if (*p == '%') {
            p++;
            if (*p == '%') { p++; continue; }

            if (*p == '*') {
                expected[num_expected++] = FMT_EXPECT_INT;
                fmt_count++;
                p++;
            }
            while (*p >= '0' && *p <= '9') p++;
            if (*p == '.') {
                p++;
                if (*p == '*') {
                    expected[num_expected++] = FMT_EXPECT_INT;
                    fmt_count++;
                    p++;
                } else {
                    while (*p >= '0' && *p <= '9') p++;
                }
            }
            // Capture length modifier (h, hh, l, ll, L, z, j, t)
            const char *mod_start = p;
            while (*p == 'h' || *p == 'l' || *p == 'L' ||
                   *p == 'z' || *p == 'j' || *p == 't')
                p++;
            int mod_len = (int)(p - mod_start);
            char mod0 = mod_len > 0 ? mod_start[0] : 0;
            char mod1 = mod_len > 1 ? mod_start[1] : 0;
            // mod: 0=none,1=hh,2=h,3=l,4=ll,5=L,6=z,7=j,8=t
            int mod = 0;
            if (mod_len == 0)                      mod = 0;
            else if (mod0 == 'h' && mod1 == 'h')   mod = 1;
            else if (mod0 == 'h')                   mod = 2;
            else if (mod0 == 'l' && mod1 == 'l')   mod = 4;
            else if (mod0 == 'l')                   mod = 3;
            else if (mod0 == 'L')                   mod = 5;
            else if (mod0 == 'z')                   mod = 6;
            else if (mod0 == 'j')                   mod = 7;
            else if (mod0 == 't')                   mod = 8;

            if (*p) {
                char c = *p;
                if (style == 1) {
                    switch (c) {
                        case 'd': case 'i': case 'c':
                            // l/ll/j/t → long; h/hh/none → int (promoted)
                            if (mod == 3 || mod == 4 || mod == 7 || mod == 8)
                                expected[num_expected++] = FMT_EXPECT_LONG;
                            else
                                expected[num_expected++] = FMT_EXPECT_INT;
                            break;
                        case 'u': case 'o': case 'x': case 'X':
                            // l/ll/z/j → unsigned long; h/hh/none → unsigned int (promoted)
                            if (mod == 3 || mod == 4 || mod == 6 || mod == 7)
                                expected[num_expected++] = FMT_EXPECT_ULONG;
                            else
                                expected[num_expected++] = FMT_EXPECT_UINT;
                            break;
                        case 'f': case 'F': case 'e': case 'E':
                        case 'g': case 'G': case 'a': case 'A':
                            // L → long double; none → double (float promoted)
                            if (mod == 5)
                                expected[num_expected++] = FMT_EXPECT_LDOUBLE;
                            else
                                expected[num_expected++] = FMT_EXPECT_DOUBLE;
                            break;
                        case 's':
                            expected[num_expected++] = FMT_EXPECT_STRING; break;
                        case 'p':
                            expected[num_expected++] = FMT_EXPECT_POINTER; break;
                        case 'n':
                            expected[num_expected++] = FMT_EXPECT_INT_PTR; break;
                        default:
                            expected[num_expected++] = FMT_EXPECT_INT; break;
                    }
                } else if (style == 2) {
                    switch (c) {
                        case 'd': case 'i':
                            if (mod == 3 || mod == 4 || mod == 7 || mod == 8)
                                expected[num_expected++] = FMT_EXPECT_LONG_PTR;
                            else if (mod == 2)
                                expected[num_expected++] = FMT_EXPECT_SHORT_PTR;
                            else if (mod == 1)
                                expected[num_expected++] = FMT_EXPECT_SCHAR_PTR;
                            else
                                expected[num_expected++] = FMT_EXPECT_INT_PTR;
                            break;
                        case 'u': case 'o': case 'x': case 'X':
                            if (mod == 3 || mod == 4 || mod == 6 || mod == 7)
                                expected[num_expected++] = FMT_EXPECT_ULONG_PTR;
                            else
                                expected[num_expected++] = FMT_EXPECT_UINT_PTR;
                            break;
                        case 'f': case 'F': case 'e': case 'E':
                        case 'g': case 'G': case 'a': case 'A':
                            if (mod == 5)
                                expected[num_expected++] = FMT_EXPECT_LDOUBLE_PTR;
                            else
                                expected[num_expected++] = FMT_EXPECT_FLOAT_PTR;
                            break;
                        case 's': case 'c':
                            expected[num_expected++] = FMT_EXPECT_STRING; break;
                        case 'p':
                            expected[num_expected++] = FMT_EXPECT_POINTER; break;
                        case 'n':
                            expected[num_expected++] = FMT_EXPECT_INT_PTR; break;
                        default:
                            expected[num_expected++] = FMT_EXPECT_INT_PTR; break;
                    }
                }
                fmt_count++;
                p++;
            }
        } else {
            p++;
        }
    }

    // Count remaining specifiers beyond MAX_FMT_ARGS
    while (*p) {
        if (*p == '%') {
            p++;
            if (*p == '%') { p++; continue; }
            if (*p == '*') { fmt_count++; p++; }
            while (*p >= '0' && *p <= '9') p++;
            if (*p == '.') {
                p++;
                if (*p == '*') { fmt_count++; p++; }
                else while (*p >= '0' && *p <= '9') p++;
            }
            while (*p == 'h' || *p == 'l' || *p == 'L' ||
                   *p == 'z' || *p == 'j' || *p == 't')
                p++;
            if (*p) { fmt_count++; p++; }
        } else {
            p++;
        }
    }

    // Check arg count
    if (fmt_count != num_varargs) {
        if (fmt_count < num_varargs)
            warn_tok(vm, tok, CCCC_WARN_FORMAT,
                     "too many arguments for format string "
                     "(format expects %d, call provides %d)",
                     fmt_count, num_varargs);
        else
            warn_tok(vm, tok, CCCC_WARN_FORMAT,
                     "too few arguments for format string "
                     "(format expects %d, call provides %d)",
                     fmt_count, num_varargs);
        return;
    }

    // Type-check each variadic argument
    int check_idx = 0;
    vararg = args;
    vararg_idx = 0;
    while (vararg && check_idx < num_expected) {
        vararg_idx++;
        if (vararg_idx >= func_ty->format_fmt_first_arg) {
            int exp = expected[check_idx];
            Type *arg_ty = vararg->ty;
            bool ok = true;
            switch (exp) {
                case FMT_EXPECT_INT:
                    ok = (arg_ty->kind == TY_INT || arg_ty->kind == TY_CHAR ||
                          arg_ty->kind == TY_SHORT || arg_ty->kind == TY_LONG);
                    break;
                case FMT_EXPECT_UINT:
                    ok = arg_ty->is_unsigned &&
                         (arg_ty->kind == TY_INT || arg_ty->kind == TY_CHAR ||
                          arg_ty->kind == TY_SHORT || arg_ty->kind == TY_LONG);
                    break;
                case FMT_EXPECT_DOUBLE:
                    ok = (arg_ty->kind == TY_DOUBLE || arg_ty->kind == TY_FLOAT ||
                          arg_ty->kind == TY_LDOUBLE);
                    break;
                case FMT_EXPECT_STRING:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_CHAR);
                    break;
                case FMT_EXPECT_POINTER:
                    ok = (arg_ty->kind == TY_PTR);
                    break;
                case FMT_EXPECT_INT_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          (arg_ty->base->kind == TY_INT ||
                           arg_ty->base->kind == TY_CHAR ||
                           arg_ty->base->kind == TY_SHORT ||
                           arg_ty->base->kind == TY_LONG));
                    break;
                case FMT_EXPECT_UINT_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->is_unsigned &&
                          (arg_ty->base->kind == TY_INT ||
                           arg_ty->base->kind == TY_CHAR ||
                           arg_ty->base->kind == TY_SHORT ||
                           arg_ty->base->kind == TY_LONG));
                    break;
                case FMT_EXPECT_FLOAT_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          (arg_ty->base->kind == TY_FLOAT ||
                           arg_ty->base->kind == TY_DOUBLE ||
                           arg_ty->base->kind == TY_LDOUBLE));
                    break;
                case FMT_EXPECT_LONG:
                    ok = (arg_ty->kind == TY_LONG);
                    break;
                case FMT_EXPECT_ULONG:
                    ok = (arg_ty->kind == TY_LONG && arg_ty->is_unsigned);
                    break;
                case FMT_EXPECT_LDOUBLE:
                    ok = (arg_ty->kind == TY_LDOUBLE);
                    break;
                case FMT_EXPECT_LONG_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_LONG && !arg_ty->base->is_unsigned);
                    break;
                case FMT_EXPECT_ULONG_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_LONG && arg_ty->base->is_unsigned);
                    break;
                case FMT_EXPECT_SHORT_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_SHORT);
                    break;
                case FMT_EXPECT_SCHAR_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_CHAR);
                    break;
                case FMT_EXPECT_LDOUBLE_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_LDOUBLE);
                    break;
            }
            if (!ok)
                warn_tok(vm, tok, CCCC_WARN_FORMAT,
                         "format argument %d expected type '%s' "
                         "but argument has incompatible type",
                         check_idx + 1, fmt_type_names[exp]);
            check_idx++;
        }
        vararg = vararg->next;
    }
}

#undef MAX_FMT_ARGS

// funcall = (assign ("," assign)*)? ")"
static Node *funcall(VirtualMachine *vm, Token **rest, Token *tok, Node *fn) {
    add_type(vm, fn);

    if (fn->ty->kind != TY_FUNC &&
        (fn->ty->kind != TY_PTR || fn->ty->base->kind != TY_FUNC))
        error_tok(vm, fn->tok, "not a function");

    Type *ty = (fn->ty->kind == TY_FUNC) ? fn->ty : fn->ty->base;
    Type *param_ty = ty->params;

    Node head = {};
    Node *cur = &head;
    bool deferred_splice = false;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(vm, tok, ",");

        Node *arg = assign(vm, &tok, tok);
        add_type(vm, arg);

        // Detect $@k splice placeholder: defer all arity/cast checks from here on.
        if (!deferred_splice &&
            arg->kind == ND_VAR && arg->var && arg->var->is_splice_placeholder)
            deferred_splice = true;

        if (!deferred_splice) {
            if (!param_ty && !ty->is_variadic) {
                if (vm->collect_errors &&
                    error_tok_recover(vm, tok, "too many arguments")) {
                    // Continue parsing to find more errors, but don't add this arg
                    continue;
                }
                error_tok(vm, tok, "too many arguments");
            }

            if (param_ty) {
                // [static N] minimum-size check: only enforceable when the argument
                // is a compile-time-sized array; bare pointers are best-effort.
                if (param_ty->static_min > 0 &&
                    arg->ty->kind == TY_ARRAY &&
                    arg->ty->array_len >= 0 &&
                    arg->ty->array_len < param_ty->static_min) {
                    warn_tok(vm, tok, CCCC_WARN_STATIC_ARRAY_SIZE,
                             "array argument has %d element%s but parameter requires at least %d",
                             arg->ty->array_len,
                             arg->ty->array_len == 1 ? "" : "s",
                             param_ty->static_min);
                }

                if (param_ty->kind != TY_STRUCT && param_ty->kind != TY_UNION) {
                    warn_implicit_conversion(vm, arg, param_ty, tok);
                    arg = new_cast(vm, arg, param_ty);
                }
                param_ty = param_ty->next;
            } else if (arg->ty->kind == TY_FLOAT) {
                // If parameter type is omitted (e.g. in "..."), float
                // arguments are promoted to double.
                arg = new_cast(vm, arg, ty_double);
            }
        }

        cur = cur->next = arg;
    }

    if (!deferred_splice && param_ty) {
        if (vm->collect_errors &&
            error_tok_recover(vm, tok, "too few arguments")) {
            // Create placeholder arguments for missing parameters
            while (param_ty) {
                Node *placeholder = new_node(vm, ND_NUM, tok);
                placeholder->ty = param_ty;
                placeholder->val = 0;
                cur = cur->next = placeholder;
                param_ty = param_ty->next;
            }
        } else {
            error_tok(vm, tok, "too few arguments");
        }
    }

    // Validate format string arguments when -F is active
    if (!deferred_splice && (vm->flags & CCCC_FORMAT_STR_CHECKS))
        validate_format_call(vm, tok, ty, head.next);

    if ((vm->compiler.warnings & CCCC_WARN_SIZEOF_POINTER_MEMACCESS) &&
        !deferred_splice &&
        fn->kind == ND_VAR && fn->var && fn->var->name) {
        const char *fname = fn->var->name;
        if (strcmp(fname, "memset") == 0 || strcmp(fname, "memcpy") == 0 ||
            strcmp(fname, "memmove") == 0 || strcmp(fname, "memcmp") == 0) {
            Node *a = head.next;
            for (int i = 0; a && i < 2; i++) a = a->next;
            // strip any implicit cast to the parameter type to reach the sizeof node
            Node *inner = a;
            while (inner && inner->kind == ND_CAST) inner = inner->lhs;
            if (inner && inner->is_sizeof_ptr_expr)
                warn_tok(vm, fn->tok, CCCC_WARN_SIZEOF_POINTER_MEMACCESS,
                         "argument to '%s' is the size of a pointer; "
                         "use sizeof(*ptr) or sizeof(pointed-to type) instead", fname);
        }
    }

    *rest = skip(vm, tok, ")");

    Node *node = new_unary(vm, ND_FUNCALL, fn, tok);
    node->func_ty = ty;
    node->ty = ty->return_ty;
    node->args = head.next;
    node->has_splice_arg = deferred_splice;

    // If a function returns a struct, it is caller's responsibility
    // to allocate a space for the return value.
    if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION)
        node->ret_buffer = new_lvar(vm, "", 0, node->ty);
    return node;
}

// generic-selection = "(" assign "," generic-assoc ("," generic-assoc)* ")"
//
// generic-assoc = type-name ":" assign
//               | "default" ":" assign
static Node *generic_selection(VirtualMachine *vm, Token **rest, Token *tok) {
    Token *start = tok;
    tok = skip(vm, tok, "(");

    Node *ctrl = assign(vm, &tok, tok);
    add_type(vm, ctrl);

    Type *t1 = ctrl->ty;
    if (t1->kind == TY_FUNC)
        t1 = pointer_to(vm, t1);
    else if (t1->kind == TY_ARRAY)
        t1 = pointer_to(vm, t1->base);
    t1 = copy_type(vm, t1);
    t1->is_const = false;
    t1->is_volatile = false;
    t1->origin = NULL;

    Node *match = NULL;
    Node *default_node = NULL;

    while (!consume(vm, rest, tok, ")")) {
        tok = skip(vm, tok, ",");

        if (equal(tok, "default")) {
            tok = skip(vm, tok->next, ":");
            Node *node = assign(vm, &tok, tok);
            if (!default_node)
                default_node = node;
            continue;
        }

        Type *t2 = typename(vm, &tok, tok);
        tok = skip(vm, tok, ":");
        Node *node = assign(vm, &tok, tok);
        if (!match && is_compatible(t1, t2))
            match = node;
    }

    Node *ret = match ? match : default_node;
    if (!ret)
        error_tok(vm, start,
                  "controlling expression type not compatible with"
                  " any generic association type");
    return ret;
}

// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" "(" type-name ")"
//         | "sizeof" unary
//         | "_Alignof" "(" type-name ")"
//         | "_Alignof" unary
//         | "_Generic" generic-selection
//         | "__builtin_types_compatible_p" "(" type-name, type-name, ")"
//         | "__builtin_reg_class" "(" type-name ")"
//         | ident
//         | str
//         | num
static Node *primary(VirtualMachine *vm, Token **rest, Token *tok) {
    Token *start = tok;

    // C23 true/false/nullptr - only when actually classified as keywords
    // (pre-C23 these are downgraded to TK_IDENT and may be used as
    // ordinary identifiers).
    if (tok->kind == TK_KEYWORD && equal(tok, "true")) {
        *rest = tok->next;
        Node *node = new_num(vm, 1, start);
        node->ty = ty_bool;
        return node;
    }

    if (tok->kind == TK_KEYWORD && equal(tok, "false")) {
        *rest = tok->next;
        Node *node = new_num(vm, 0, start);
        node->ty = ty_bool;
        return node;
    }

    if (tok->kind == TK_KEYWORD && equal(tok, "nullptr")) {
        *rest = tok->next;
        Node *node = new_num(vm, 0, start);
        node->ty = ty_nullptr_t;
        return node;
    }

    if (equal(tok, "(") && equal(tok->next, "{")) {
        // This is a GNU statement expresssion.
        Node *node = new_node(vm, ND_STMT_EXPR, tok);
        node->body = compound_stmt(vm, &tok, tok->next->next, NULL)->body;
        *rest = skip(vm, tok, ")");
        return node;
    }

    if (equal(tok, "(")) {
        Node *node = expr(vm, &tok, tok->next);
        *rest = skip(vm, tok, ")");
        return node;
    }

    if (equal(tok, "sizeof") && equal(tok->next, "(") &&
        is_typename(vm, tok->next->next)) {
        Type *ty = typename(vm, &tok, tok->next->next);
        *rest = skip(vm, tok, ")");

        if (ty->kind == TY_VLA) {
            if (ty->vla_size)
                return new_var_node(vm, ty->vla_size, tok);

            Node *lhs = compute_vla_size(vm, ty, tok);
            Node *rhs = new_var_node(vm, ty->vla_size, tok);
            return new_binary(vm, ND_COMMA, lhs, rhs, tok);
        }

        Node *sn = new_ulong(vm, ty->size, start);
        sn->is_sizeof_ptr_expr = (ty->kind == TY_PTR);
        return sn;
    }

    if (equal(tok, "sizeof")) {
        Node *node = unary(vm, rest, tok->next);
        add_type(vm, node);
        if (node->ty->kind == TY_VLA)
            return new_var_node(vm, node->ty->vla_size, tok);
        Node *sn = new_ulong(vm, node->ty->size, tok);
        sn->is_sizeof_ptr_expr = (node->ty->kind == TY_PTR);
        return sn;
    }

    if (equal(tok, "_Alignof") && equal(tok->next, "(") &&
        is_typename(vm, tok->next->next)) {
        Type *ty = typename(vm, &tok, tok->next->next);
        *rest = skip(vm, tok, ")");
        return new_ulong(vm, ty->align, tok);
    }

    if (equal(tok, "_Alignof")) {
        Node *node = unary(vm, rest, tok->next);
        add_type(vm, node);
        return new_ulong(vm, node->ty->align, tok);
    }

    if (equal(tok, "_Generic")) {
        if (vm->compiler.c_std < CCCC_STD_C11)
            warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                     "'_Generic' is a C11 extension");
        return generic_selection(vm, rest, tok->next);
    }

    if (equal(tok, "__builtin_types_compatible_p")) {
        tok = skip(vm, tok->next, "(");
        Type *t1 = typename(vm, &tok, tok);
        tok = skip(vm, tok, ",");
        Type *t2 = typename(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        return new_num(vm, is_compatible(t1, t2), start);
    }

    if (equal(tok, "__builtin_reg_class")) {
        tok = skip(vm, tok->next, "(");
        Type *ty = typename(vm, &tok, tok);
        *rest = skip(vm, tok, ")");

        if (is_integer(ty) || ty->kind == TY_PTR)
            return new_num(vm, 0, start);
        if (is_flonum(ty))
            return new_num(vm, 1, start);
        return new_num(vm, 2, start);
    }

    if (equal(tok, "__builtin_compare_and_swap")) {
        Node *node = new_node(vm, ND_CAS, tok);
        tok = skip(vm, tok->next, "(");
        node->cas_addr = assign(vm, &tok, tok);
        tok = skip(vm, tok, ",");
        node->cas_old = assign(vm, &tok, tok);
        tok = skip(vm, tok, ",");
        node->cas_new = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        return node;
    }

    if (equal(tok, "__builtin_atomic_exchange")) {
        Node *node = new_node(vm, ND_EXCH, tok);
        tok = skip(vm, tok->next, "(");
        node->lhs = assign(vm, &tok, tok);
        tok = skip(vm, tok, ",");
        node->rhs = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        return node;
    }

    // __builtin_atomic_load(addr) — atomic tagged load; emits ALDR opcode
    if (equal(tok, "__builtin_atomic_load")) {
        Node *node = new_node(vm, ND_ALOAD, tok);
        tok = skip(vm, tok->next, "(");
        node->lhs = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        return node;
    }

    // __builtin_atomic_store(addr, val) — atomic tagged store; emits ASTR opcode
    if (equal(tok, "__builtin_atomic_store")) {
        Node *node = new_node(vm, ND_ASTORE, tok);
        tok = skip(vm, tok->next, "(");
        node->lhs = assign(vm, &tok, tok);
        tok = skip(vm, tok, ",");
        node->rhs = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        return node;
    }

    // __builtin_frame_address(0) - returns the current frame's base pointer
    if (equal(tok, "__builtin_frame_address")) {
        tok = skip(vm, tok->next, "(");
        // Only level 0 is supported (current frame)
        long long level = const_expr(vm, &tok, tok);
        if (level != 0)
            error_tok(vm, tok, "__builtin_frame_address only supports level 0");
        *rest = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_FRAME_ADDR, start);
        node->ty = pointer_to(vm, ty_void);
        return node;
    }

    // __builtin_huge_val() -> double infinity
    if (equal(tok, "__builtin_huge_val")) {
        tok = skip(vm, tok->next, "(");
        *rest = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_NUM, start);
        node->fval = HUGE_VAL;
        node->ty = ty_double;
        return node;
    }

    // __builtin_huge_valf() -> float infinity
    if (equal(tok, "__builtin_huge_valf")) {
        tok = skip(vm, tok->next, "(");
        *rest = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_NUM, start);
        node->fval = (float)HUGE_VAL;
        node->ty = ty_float;
        return node;
    }

    // __builtin_inf() / __builtin_inff() -> infinity
    if (equal(tok, "__builtin_inf") || equal(tok, "__builtin_inff")) {
        bool is_f = equal(tok, "__builtin_inff");
        tok = skip(vm, tok->next, "(");
        *rest = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_NUM, start);
        node->fval = INFINITY;
        node->ty = is_f ? ty_float : ty_double;
        return node;
    }

    // __builtin_nan("tag") / __builtin_nanf("tag") -> NaN
    if (equal(tok, "__builtin_nan") || equal(tok, "__builtin_nanf")) {
        bool is_f = equal(tok, "__builtin_nanf");
        tok = skip(vm, tok->next, "(");
        // Parse and discard the string tag argument
        Node *tag = assign(vm, &tok, tok);
        (void)tag;
        *rest = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_NUM, start);
        node->fval = NAN;
        node->ty = is_f ? ty_float : ty_double;
        return node;
    }

    // __builtin_isnan(x) -> x != x
    if (equal(tok, "__builtin_isnan")) {
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        // PLACEHOLDER: arg is evaluated twice; should use a temp for side-effecting exprs
        Node *node = new_binary(vm, ND_NE, arg, arg, start);
        return node;
    }

    // __builtin_isinf(x) -> x == HUGE_VAL || x == -HUGE_VAL
    if (equal(tok, "__builtin_isinf")) {
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        Node *huge = new_node(vm, ND_NUM, start);
        huge->fval = HUGE_VAL;
        huge->ty = ty_double;
        Node *neg_huge = new_unary(vm, ND_NEG, huge, start);
        // PLACEHOLDER: arg is evaluated twice; should use a temp for side-effecting exprs
        Node *eq_pos = new_binary(vm, ND_EQ, arg, huge, start);
        Node *eq_neg = new_binary(vm, ND_EQ, arg, neg_huge, start);
        Node *node = new_binary(vm, ND_LOGOR, eq_pos, eq_neg, start);
        return node;
    }

    // __builtin_isfinite(x) -> !(x != x || x == HUGE_VAL || x == -HUGE_VAL)
    if (equal(tok, "__builtin_isfinite")) {
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        Node *huge = new_node(vm, ND_NUM, start);
        huge->fval = HUGE_VAL;
        huge->ty = ty_double;
        Node *neg_huge = new_unary(vm, ND_NEG, huge, start);
        // PLACEHOLDER: arg is evaluated multiple times; should use a temp
        Node *nan_check = new_binary(vm, ND_NE, arg, arg, start);
        Node *inf_pos = new_binary(vm, ND_EQ, arg, huge, start);
        Node *inf_neg = new_binary(vm, ND_EQ, arg, neg_huge, start);
        Node *inf_check = new_binary(vm, ND_LOGOR, inf_pos, inf_neg, start);
        Node *any = new_binary(vm, ND_LOGOR, nan_check, inf_check, start);
        Node *node = new_unary(vm, ND_NOT, any, start);
        return node;
    }

    // __builtin_signbit(x) -> x < 0
    if (equal(tok, "__builtin_signbit")) {
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        Node *zero = new_node(vm, ND_NUM, start);
        zero->fval = 0.0;
        zero->ty = ty_double;
        Node *node = new_binary(vm, ND_LT, arg, zero, start);
        return node;
    }

    // __builtin_expect(exp, c) -> exp (branch prediction hint, ignored for now)
    if (equal(tok, "__builtin_expect")) {
        tok = skip(vm, tok->next, "(");
        Node *exp = assign(vm, &tok, tok);
        tok = skip(vm, tok, ",");
        Node *c = assign(vm, &tok, tok);
        (void)c;
        *rest = skip(vm, tok, ")");
        return exp;
    }

    // __builtin_constant_p(expr) -> 1 if compile-time constant, 0 otherwise
    if (equal(tok, "__builtin_constant_p")) {
        tok = skip(vm, tok->next, "(");
        Node *expr = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        add_type(vm, expr);
        int is_const = is_const_expr(vm, expr);
        return new_num(vm, is_const ? 1 : 0, start);
    }

    // __builtin_alloca(size) -> dynamic stack allocation
    if (equal(tok, "__builtin_alloca")) {
        tok = skip(vm, tok->next, "(");
        Node *sz = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        Node *node = new_unary(vm, ND_FUNCALL,
            new_var_node(vm, vm->compiler.builtin_alloca, sz->tok), sz->tok);
        node->func_ty = vm->compiler.builtin_alloca->ty;
        node->ty = vm->compiler.builtin_alloca->ty->return_ty;
        node->args = sz;
        add_type(vm, sz);
        return node;
    }

    // __builtin_unreachable() / __builtin_trap() / __builtin_debugtrap() -> BTRAP
    if (equal(tok, "__builtin_unreachable") ||
        equal(tok, "__builtin_trap") ||
        equal(tok, "__builtin_debugtrap")) {
        tok = skip(vm, tok->next, "(");
        *rest = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_UNREACHABLE, start);
        node->ty = ty_void;
        return node;
    }

    // ND_BITOP: integer bit-manipulation builtins (#212)
    // val encoding: (op_selector << 8) | bit_width
    //   op: 0=CLZ 1=CTZ 2=POPCOUNT 3=PARITY 4=FFS 5=BSWAP
    if (equal(tok, "__builtin_clz") || equal(tok, "__builtin_clzll")) {
        int width = equal(tok, "__builtin_clzll") ? 64 : 32;
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        add_type(vm, arg);
        Node *node = new_unary(vm, ND_BITOP, arg, start);
        node->val = (0 << 8) | width;
        node->ty = ty_int;
        return node;
    }

    if (equal(tok, "__builtin_ctz") || equal(tok, "__builtin_ctzll")) {
        int width = equal(tok, "__builtin_ctzll") ? 64 : 32;
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        add_type(vm, arg);
        Node *node = new_unary(vm, ND_BITOP, arg, start);
        node->val = (1 << 8) | width;
        node->ty = ty_int;
        return node;
    }

    if (equal(tok, "__builtin_popcount") || equal(tok, "__builtin_popcountll")) {
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        add_type(vm, arg);
        Node *node = new_unary(vm, ND_BITOP, arg, start);
        node->val = (2 << 8) | 0;
        node->ty = ty_int;
        return node;
    }

    if (equal(tok, "__builtin_parity") || equal(tok, "__builtin_parityll")) {
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        add_type(vm, arg);
        Node *node = new_unary(vm, ND_BITOP, arg, start);
        node->val = (3 << 8) | 0;
        node->ty = ty_int;
        return node;
    }

    if (equal(tok, "__builtin_ffs") || equal(tok, "__builtin_ffsll")) {
        int width = equal(tok, "__builtin_ffsll") ? 64 : 32;
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        add_type(vm, arg);
        Node *node = new_unary(vm, ND_BITOP, arg, start);
        node->val = (4 << 8) | width;
        node->ty = ty_int;
        return node;
    }

    if (equal(tok, "__builtin_bswap16") || equal(tok, "__builtin_bswap32") ||
        equal(tok, "__builtin_bswap64")) {
        int bytes = equal(tok, "__builtin_bswap16") ? 2 :
                    equal(tok, "__builtin_bswap32") ? 4 : 8;
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        add_type(vm, arg);
        Node *node = new_unary(vm, ND_BITOP, arg, start);
        node->val = (5 << 8) | bytes;
        node->ty = (bytes == 2) ? ty_ushort : (bytes == 4) ? ty_uint : ty_ulong;
        return node;
    }

    // ND_OVERFLOW_ARITH: checked arithmetic builtins (#213)
    // val: 0=add 1=sub 2=mul; lhs=a, rhs=b, cas_addr=result_ptr
    if (equal(tok, "__builtin_add_overflow") || equal(tok, "__builtin_sub_overflow") ||
        equal(tok, "__builtin_mul_overflow")) {
        int op = equal(tok, "__builtin_add_overflow") ? 0 :
                 equal(tok, "__builtin_sub_overflow") ? 1 : 2;
        tok = skip(vm, tok->next, "(");
        Node *a = assign(vm, &tok, tok);
        tok = skip(vm, tok, ",");
        Node *b = assign(vm, &tok, tok);
        tok = skip(vm, tok, ",");
        Node *ptr = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        add_type(vm, a);
        add_type(vm, b);
        add_type(vm, ptr);
        if (ptr->ty->kind != TY_PTR)
            error_tok(vm, ptr->tok, "__builtin_*_overflow: third argument must be a pointer");
        Node *node = new_node(vm, ND_OVERFLOW_ARITH, start);
        node->lhs = a;
        node->rhs = b;
        node->cas_addr = ptr;
        node->val = op;
        node->ty = ty_int;
        return node;
    }

    if (equal(tok, "__cccc_cmplx") || equal(tok, "__cccc_cmplxf") ||
        equal(tok, "__cccc_cmplxl")) {
        Type *ty = equal(tok, "__cccc_cmplxf") ? ty_fcomplex :
                   equal(tok, "__cccc_cmplxl") ? ty_ldcomplex : ty_dcomplex;
        tok = skip(vm, tok->next, "(");
        Node *real = assign(vm, &tok, tok);
        tok = skip(vm, tok, ",");
        Node *imag = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        return new_complex_node(vm, real, imag, ty, start);
    }

    if (equal(tok, "__cccc_creal") || equal(tok, "__cccc_crealf") ||
        equal(tok, "__cccc_creall") || equal(tok, "__cccc_cimag") ||
        equal(tok, "__cccc_cimagf") || equal(tok, "__cccc_cimagl")) {
        bool imag_part = equal(tok, "__cccc_cimag") || equal(tok, "__cccc_cimagf") ||
                         equal(tok, "__cccc_cimagl");
        Type *ret_ty = (equal(tok, "__cccc_crealf") || equal(tok, "__cccc_cimagf")) ? ty_float :
                       (equal(tok, "__cccc_creall") || equal(tok, "__cccc_cimagl")) ? ty_ldouble :
                       ty_double;
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        Node *node = new_unary(vm, ND_COMPLEX, arg, start);
        node->val = imag_part ? 2 : 1;
        node->ty = ret_ty;
        return node;
    }

    if (equal(tok, "__cccc_conj") || equal(tok, "__cccc_conjf") ||
        equal(tok, "__cccc_conjl")) {
        Type *ty = equal(tok, "__cccc_conjf") ? ty_fcomplex :
                   equal(tok, "__cccc_conjl") ? ty_ldcomplex : ty_dcomplex;
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        Node *node = new_unary(vm, ND_COMPLEX, arg, start);
        node->val = 3;
        node->ty = ty;
        return node;
    }

    // Block_copy(block) - Apple Blocks extension
    // Copies the descriptor to the heap so the block can safely outlive its
    // declaring stack frame. Calls __cccc_block_copy_impl(desc) which reads
    // desc[1] (the descriptor byte-size) and returns a malloc'd copy.
    if (equal(tok, "Block_copy")) {
        tok = skip(vm, tok->next, "(");
        Node *block_expr = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        add_type(vm, block_expr);

        // The __cccc_block_copy_impl prototype is declared as a builtin (its
        // host implementation is registered in the FFI table by the stdlib).
        Obj *copy_fn = vm->compiler.builtin_block_copy;
        if (!copy_fn) {
            // No stdlib loaded; fall back to returning the block as-is
            return block_expr;
        }

        Node *fn_node = new_var_node(vm, copy_fn, start);
        Node *call = new_unary(vm, ND_FUNCALL, fn_node, start);
        call->func_ty = copy_fn->ty;
        call->ty = copy_fn->ty->return_ty;
        call->args = block_expr;
        return call;
    }

    // Block_release(block) - Apple Blocks extension
    // Frees a heap-allocated block descriptor previously obtained via Block_copy.
    // Only call on blocks returned by Block_copy; calling on a stack block is UB.
    if (equal(tok, "Block_release")) {
        tok = skip(vm, tok->next, "(");
        Node *block_expr = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        add_type(vm, block_expr);

        // Prefer a user-declared free() prototype (e.g. from <stdlib.h>);
        // fall back to the builtin_free prototype so Block_release always
        // works even when <stdlib.h> is not included (#458).
        Obj *free_fn = NULL;
        for (Obj *g = vm->compiler.globals; g; g = g->next)
            if (g->name && strcmp(g->name, "free") == 0) { free_fn = g; break; }
        if (!free_fn)
            free_fn = vm->compiler.builtin_free;

        if (!free_fn) {
            Node *node = new_node(vm, ND_NULL_EXPR, start);
            node->ty = ty_void;
            return node;
        }

        Node *fn_node = new_var_node(vm, free_fn, start);
        Node *call = new_unary(vm, ND_FUNCALL, fn_node, start);
        call->func_ty = free_fn->ty;
        call->ty = ty_void;
        call->args = block_expr;
        return call;
    }

    if (tok->kind == TK_IDENT) {
        // Variable or enum constant
        VarScope *sc = find_var(vm, tok);
        *rest = tok->next;

        // For "static inline" function
        if (sc && sc->var && sc->var->is_function) {
            if (vm->compiler.current_fn)
                arena_strarray_push(vm, &vm->compiler.current_fn->refs,
                                    sc->var->name);
            else
                sc->var->is_root = true;
        }

        if (sc) {
            if (sc->var) {
                sc->var->is_used = true;
                if (sc->var->is_deprecated)
                    warn_deprecated_use(vm, tok, obj_display_name(sc->var),
                                        sc->var->deprecated_msg);
                return new_var_node(vm, sc->var, tok);
            }
            if (sc->enum_ty) {
                if (sc->is_deprecated)
                    warn_deprecated_use(
                        vm, tok, arena_strndup(vm, tok->loc, tok->len),
                        sc->deprecated_msg);
                Node *num = new_num(vm, sc->enum_val, tok);
                // Use the enum's own type so size/signedness are correct for
                // enums with a C23 underlying type (e.g. unsigned long).
                num->ty = sc->enum_ty;
                return num;
            }
        }

        // Check if this is a macro call. When parsing macro bytecode itself,
        // keep calls as ordinary C function calls so macros can call each
        // other directly.
        if (!vm->compiler.in_macro_mode && equal(tok->next, "(")) {
            MacroFn *pm = find_macro_fn(vm, tok);
            if (pm) {
                // Inline macros expand at the call site; non-inline macros
                // are global-generation only and cannot be used in expression
                // position.
                if (!pm->is_inline) {
                    error_tok(vm, tok,
                              "macro '%.*s' is not marked inline; use "
                              "[[cccc::comptime(inline)]] for expression-position "
                              "calls",
                              tok->len, tok->loc);
                }

                // Create ND_MACRO_CALL node
                Token *macro_tok = tok;
                tok = tok->next->next; // Skip identifier and '('

                // Parse arguments
                Node head = {};
                Node *cur = &head;
                int arg_count = 0;

                while (!equal(tok, ")")) {
                    if (cur != &head)
                        tok = skip(vm, tok, ",");
                    Node *arg = assign(vm, &tok, tok);
                    cur = cur->next = arg;
                    arg_count++;
                }
                *rest = tok->next; // Skip ')'

                Node *node = new_node(vm, ND_MACRO_CALL, macro_tok);
                node->macro_name = pm->name;
                node->args = head.next;
                node->macro_arg_count = arg_count;
                node->macro_scope = vm->compiler.scope;
                // Type will be determined after macro expansion
                node->ty =
                    ty_long; // Placeholder - macros return Node* (pointer)
                return node;
            }
        }

        if (equal(tok->next, "(")) {
            warn_tok(vm, tok, CCCC_WARN_IMPLICIT_FUNCTION_DECLARATION,
                     "implicit declaration of function '%.*s'", tok->len,
                     tok->loc);

            Obj *fn = new_implicit_function(vm, tok);
            if (vm->compiler.current_fn)
                arena_strarray_push(vm, &vm->compiler.current_fn->refs,
                                    fn->name);
            else
                fn->is_root = true;
            return new_var_node(vm, fn, tok);
        }

        // Try error recovery if enabled
        if (vm->collect_errors &&
            error_tok_recover(vm, tok, "undefined variable '%.*s'", tok->len,
                              tok->loc)) {
            // Return error placeholder node instead of aborting
            Node *node = new_var_node(vm, error_var, tok);
            node->ty = ty_error;
            return node;
        }

        error_tok(vm, tok, "undefined variable '%.*s'", tok->len,
                  tok->loc);
    }

    if (tok->kind == TK_STR) {
        Obj *var = new_string_literal(vm, tok->str, tok->ty);
        *rest = tok->next;
        return new_var_node(vm, var, tok);
    }

    if (tok->kind == TK_NUM) {
        Node *node;
        if (vm->debug_vm)
            printf("  primary: TK_NUM tok->ty kind=%d, is_flonum=%d\n",
                   tok->ty ? tok->ty->kind : -1, is_flonum(tok->ty));

        if (is_flonum(tok->ty)) {
            node = new_node(vm, ND_NUM, tok);
            node->fval = tok->fval;
            if (vm->debug_vm)
                printf("  primary: created flonum node, fval=%Lf\n",
                       node->fval);
        } else {
            node = new_num(vm, tok->val, tok);
            if (vm->debug_vm)
                printf("  primary: created int node, val=%lld\n", node->val);
        }

        node->ty = tok->ty;
        node->wide_digits = tok->wide_digits;
        node->wide_base = tok->wide_base;
        if (vm->debug_vm)
            printf(" primary: set node->ty to tok->ty, kind=%d\n",
                   node->ty ? node->ty->kind : -1);

        *rest = tok->next;
        return node;
    }

    // Try error recovery if enabled
    if (vm->collect_errors &&
        error_tok_recover(vm, tok, "expected an expression, found '%.*s'",
                          tok->len, tok->loc)) {
        // Skip the invalid token and return error placeholder
        *rest = tok->next;
        Node *node = new_node(vm, ND_NUM, tok);
        node->ty = ty_int;
        node->val = 0;
        return node;
    }

    error_tok(vm, tok, "expected an expression, found '%.*s'", tok->len,
              tok->loc);
    return NULL;
}

static Token *parse_typedef(VirtualMachine *vm, Token *tok, Type *basety, VarAttr *attr) {
    bool first = true;

    while (!consume(vm, &tok, tok, ";")) {
        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        Type *ty = declarator(vm, &tok, tok, basety);
        if (!ty->name)
            error_tok(vm, ty->name_pos, "typedef name omitted");
        char *name = get_ident(vm, ty->name);
        VarScope *sc = push_scope(vm, name, ty->name->len);
        sc->type_def = ty;
        sc->is_deprecated = ty->is_deprecated;
        sc->deprecated_msg = ty->deprecated_msg;
        record_type_name(vm, ty, name, ty->name->len, false);
        run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_TYPEDEF, name, ty,
                              NULL, ty->name);
    }
    return tok;
}

static void create_param_lvars(VirtualMachine *vm, Type *param) {
    if (param) {
        create_param_lvars(vm, param->next);
        if (!param->name)
            error_tok(vm, param->name_pos, "parameter name omitted");
        Obj *var =
            new_lvar(vm, get_ident(vm, param->name), param->name->len, param);
        var->is_param = true;
    }
}

// This function matches gotos or labels-as-values with labels.
//
// We cannot resolve gotos as we parse a function because gotos
// can refer a label that appears later in the function.
// So, we need to do this after we parse the entire function.
static void resolve_goto_labels(VirtualMachine *vm) {
    for (Node *x = vm->compiler.gotos; x; x = x->goto_next) {
        for (Node *y = vm->compiler.labels; y; y = y->goto_next) {
            if (strlen(x->label) == strlen(y->label) &&
                strncmp(x->label, y->label, strlen(y->label)) == 0) {
                x->unique_label = y->unique_label;
                y->label_used = true;
                break;
            }
        }

        if (x->unique_label == NULL)
            error_tok(vm, x->tok->next, "use of undeclared label");
    }

    for (Node *label = vm->compiler.labels; label; label = label->goto_next)
        if (!label->label_used && !label->label_maybe_unused)
            warn_tok(vm, label->tok, CCCC_WARN_UNUSED, "unused label '%s'",
                     label->label);

    vm->compiler.gotos = vm->compiler.labels = NULL;
}

static Obj *find_func(VirtualMachine *vm, char *name, int name_len) {
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        for (VarScopeNode *node = sc->vars; node; node = node->next) {
            if (node->name_len == name_len &&
                strncmp(node->name, name, name_len) == 0) {
                VarScope *sc2 = (VarScope *)node;
                if (sc2->var && sc2->var->is_function)
                    return sc2->var;
                return NULL;
            }
        }
    }
    return NULL;
}

static Obj *find_func_in_current_scope(VirtualMachine *vm, char *name, int name_len) {
    Scope *sc = vm->compiler.scope;
    if (!sc)
        return NULL;

    for (VarScopeNode *node = sc->vars; node; node = node->next) {
        if (node->name_len == name_len &&
            strncmp(node->name, name, name_len) == 0) {
            VarScope *sc2 = (VarScope *)node;
            if (sc2->var && sc2->var->is_function)
                return sc2->var;
            return NULL;
        }
    }
    return NULL;
}

static Obj *declare_function_prototype(VirtualMachine *vm, Type *ty, VarAttr *attr,
                                       Token *tok) {
    if (!ty->name)
        error_tok(vm, ty->name_pos, "function name omitted");

    char *name_str = get_ident(vm, ty->name);

    if (attr->is_noreturn && ty->kind == TY_FUNC)
        ty->is_noreturn = true;

    Obj *parent_fn = vm->compiler.current_fn;
    bool is_nested = (parent_fn != NULL);

    Obj *fn = attr->is_static
                  ? find_func_in_current_scope(vm, name_str, ty->name->len)
                  : find_func(vm, name_str, ty->name->len);
    if (fn) {
        if (!fn->is_function)
            error_tok(vm, tok, "redeclared as a different kind of symbol");
        if (fn->is_implicit) {
            fn->ty = ty;
            fn->is_implicit = false;
        }
        if (!fn->is_static && attr->is_static)
            error_tok(vm, tok,
                      "static declaration follows a non-static declaration");
        fn->is_maybe_unused |= ty->is_maybe_unused;
        fn->is_deprecated |= ty->is_deprecated;
        fn->is_noreturn |= ty->is_noreturn;
        fn->is_pure |= ty->is_pure;
        fn->is_func_const |= ty->is_func_const;
        if (ty->asm_label)
            fn->asm_label = ty->asm_label;
        if (ty->deprecated_msg)
            fn->deprecated_msg = ty->deprecated_msg;
        if (ty->format_style && fn->ty) {
            fn->ty->format_style = ty->format_style;
            fn->ty->format_string_index = ty->format_string_index;
            fn->ty->format_fmt_first_arg = ty->format_fmt_first_arg;
        }
    } else {
        fn = new_gvar(vm, name_str, ty->name->len, ty);
        fn->is_function = true;
        fn->is_definition = false;
        fn->is_static =
            attr->is_static || (attr->is_inline && !attr->is_extern);
        fn->is_inline = attr->is_inline;
        fn->asm_label = ty->asm_label;
    }

    if (is_nested) {
        fn->parent_fn = parent_fn;
        fn->is_nested = true;
        fn->nesting_depth = vm->compiler.fn_nesting_depth + 1;
        fn->is_static = true;
    } else {
        fn->parent_fn = NULL;
        fn->is_nested = false;
        fn->nesting_depth = 0;
    }

    fn->is_root = !(fn->is_static && fn->is_inline);
    run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_FUNCTION, fn->name,
                          fn->ty, fn, fn->tok);
    return fn;
}

static Token *function_declaration_list(VirtualMachine *vm, Token *tok,
                                        Type *basety, VarAttr *attr) {
    bool first = true;

    while (!consume(vm, &tok, tok, ";")) {
        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        Type *ty = declarator(vm, &tok, tok, basety);
        ty = apply_var_attrs_to_type(vm, ty, attr);
        if (ty->kind != TY_FUNC)
            error_tok(vm, ty->name ? ty->name : tok,
                      "expected function declaration");
        declare_function_prototype(vm, ty, attr, tok);
    }

    return tok;
}

static void mark_live(VirtualMachine *vm, Obj *var) {
    if (!var->is_function || var->is_live)
        return;
    var->is_live = true;

    for (int i = 0; i < var->refs.len; i++) {
        Obj *fn = find_func(vm, var->refs.data[i], strlen(var->refs.data[i]));
        if (fn)
            mark_live(vm, fn);
    }
}

static bool statement_terminates(Node *node) {
    if (!node)
        return false;

    switch (node->kind) {
    case ND_RETURN:
        return true;
    case ND_BLOCK: {
        Node *last = node->body;
        if (!last)
            return false;
        while (last->next)
            last = last->next;
        return statement_terminates(last);
    }
    case ND_IF:
        return node->els && statement_terminates(node->then) &&
               statement_terminates(node->els);
    default:
        return false;
    }
}

static void append_implicit_return(VirtualMachine *vm, Obj *fn, Token *tok) {
    Type *ty = fn->ty->return_ty;

    // Noreturn functions must not fall off the end
    if (fn->is_noreturn) {
        if (ty->kind != TY_VOID)
            warn_tok(vm, tok, CCCC_WARN_RETURN_TYPE,
                     "noreturn function should not return a value");
        return;
    }

    if (ty->kind == TY_VOID || strcmp(fn->name, "main") == 0 ||
        statement_terminates(fn->body))
        return;

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
        error_tok(vm, tok, "control reaches end of non-void aggregate function");

    warn_tok(vm, tok, CCCC_WARN_RETURN_TYPE,
             "control reaches end of non-void function");

    Node *ret = new_node(vm, ND_RETURN, tok);
    ret->lhs = new_cast(vm, new_num(vm, 0, tok), ty);

    Node **cur = &fn->body->body;
    while (*cur)
        cur = &(*cur)->next;
    *cur = ret;
}

static bool is_plain_signed_int(Type *ty) {
    return ty && ty->kind == TY_INT && !ty->base && !ty->is_unsigned;
}

static bool is_char_ptr_ptr(Type *ty) {
    return ty && ty->kind == TY_PTR &&
           ty->base && ty->base->kind == TY_PTR &&
           ty->base->base && ty->base->base->size == 1;
}

static void validate_main_signature(VirtualMachine *vm, Obj *fn) {
    if (!fn || !fn->name || strcmp(fn->name, "main") != 0)
        return;

    bool warn = vm->compiler.warnings & CCCC_WARN_MAIN;

    // Check return type.
    if (warn && fn->ty && fn->ty->return_ty &&
        fn->ty->return_ty->kind != TY_INT)
        warn_tok(vm, fn->tok, CCCC_WARN_MAIN,
                 "return type of 'main' is not 'int'");

    // Count parameters and check types.
    int nparams = 0;
    for (Type *p = fn->ty ? fn->ty->params : NULL; p; p = p->next)
        nparams++;

    if (nparams == 0)
        return; // int main(void) — always fine

    if (warn && nparams != 2 && nparams != 3)
        warn_tok(vm, fn->tok, CCCC_WARN_MAIN,
                 "suspicious number of parameters for 'main': expected 0 or 2");

    Type *first = fn->ty->params;
    if (warn && first && !is_plain_signed_int(first))
        warn_tok(vm, first->name_pos ? first->name_pos : fn->tok,
                 CCCC_WARN_MAIN, "first parameter of 'main' should be 'int'");

    Type *second = first ? first->next : NULL;
    if (warn && second && !is_char_ptr_ptr(second))
        warn_tok(vm, second->name_pos ? second->name_pos : fn->tok,
                 CCCC_WARN_MAIN,
                 "second parameter of 'main' should be 'char **'");
}

static Token *function(VirtualMachine *vm, Token *tok, Type *basety, VarAttr *attr) {
    if (attr->is_constexpr)
        error_tok(vm, tok, "constexpr is only supported for object definitions");

    Type *ty = declarator(vm, &tok, tok, basety);
    ty = apply_var_attrs_to_type(vm, ty, attr);
    if (!ty->name)
        error_tok(vm, ty->name_pos, "function name omitted");
    char *name_str = get_ident(vm, ty->name);

    // Propagate noreturn from attribute to function type
    if (attr->is_noreturn && ty->kind == TY_FUNC)
        ty->is_noreturn = true;

    // Check if this is a nested function (defined inside another function)
    Obj *parent_fn = vm->compiler.current_fn;
    bool is_nested = (parent_fn != NULL);
    Obj *saved_locals = NULL;
    int saved_nesting_depth = 0;

    if (is_nested) {
        // Save parent's locals - we're about to start a new locals chain
        saved_locals = vm->compiler.locals;
        saved_nesting_depth = vm->compiler.fn_nesting_depth;
    }

    Obj *fn = attr->is_static
                  ? find_func_in_current_scope(vm, name_str, ty->name->len)
                  : find_func(vm, name_str, ty->name->len);
    if (fn) {
        // Redeclaration
        if (!fn->is_function)
            error_tok(vm, tok, "redeclared as a different kind of symbol");
        if (fn->is_implicit) {
            fn->ty = ty;
            fn->is_implicit = false;
        }
        if (fn->is_definition && equal(tok, "{"))
            error_tok(vm, tok, "redefinition of %s", name_str);
        if (!fn->is_static && attr->is_static)
            error_tok(vm, tok,
                      "static declaration follows a non-static declaration");
        fn->is_definition = fn->is_definition || equal(tok, "{");
        fn->is_maybe_unused |= ty->is_maybe_unused;
        fn->is_deprecated |= ty->is_deprecated;
        fn->is_noreturn |= ty->is_noreturn;
        fn->is_pure |= ty->is_pure;
        fn->is_func_const |= ty->is_func_const;
        if (ty->asm_label)
            fn->asm_label = ty->asm_label;
        if (ty->deprecated_msg)
            fn->deprecated_msg = ty->deprecated_msg;
        if (ty->format_style && fn->ty) {
            fn->ty->format_style = ty->format_style;
            fn->ty->format_string_index = ty->format_string_index;
            fn->ty->format_fmt_first_arg = ty->format_fmt_first_arg;
        }
    } else {
        fn = new_gvar(vm, name_str, ty->name->len, ty);
        fn->is_function = true;
        fn->is_definition = equal(tok, "{");
        fn->is_static =
            attr->is_static || (attr->is_inline && !attr->is_extern);
        fn->is_inline = attr->is_inline;
        fn->asm_label = ty->asm_label;
    }

    // Set up nested function tracking
    if (is_nested) {
        fn->parent_fn = parent_fn;
        fn->is_nested = true;
        fn->nesting_depth = vm->compiler.fn_nesting_depth + 1;
        // Nested functions are implicitly static (not visible outside)
        fn->is_static = true;
    } else {
        fn->parent_fn = NULL;
        fn->is_nested = false;
        fn->nesting_depth = 0;
    }

    fn->is_root = !(fn->is_static && fn->is_inline);

    if (consume(vm, &tok, tok, ";")) {
        run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_FUNCTION, fn->name,
                              fn->ty, fn, fn->tok);
        return tok;
    }

    vm->compiler.current_fn = fn;
    vm->compiler.locals = NULL;
    if (is_nested)
        vm->compiler.fn_nesting_depth++;

    enter_scope(vm);

    // K&R declaration-list: type declarations between ')' and '{' that give
    // explicit types to the parameter names.  Update ty->params *before*
    // create_param_lvars so that stack-slot sizes are derived from the correct
    // types (e.g. a double param must get an 8-byte slot, not a 4-byte int slot).
    if ((vm->compiler.warnings & CCCC_WARN_OLD_STYLE_DEFINITION) &&
        !equal(tok, "{") && tok->kind != TK_EOF && is_typename(vm, tok))
        warn_tok(vm, fn->tok, CCCC_WARN_OLD_STYLE_DEFINITION,
                 "old-style (K&R) function definition");
    while (!equal(tok, "{") && tok->kind != TK_EOF && is_typename(vm, tok)) {
        VarAttr knr_attr = {};
        Type *basety = declspec(vm, &tok, tok, &knr_attr);
        bool first = true;
        for (;;) {
            if (!first)
                tok = skip(vm, tok, ",");
            first = false;
            Type *decl_ty = declarator(vm, &tok, tok, basety);
            if (decl_ty->name) {
                char *pname = decl_ty->name->loc;
                int   plen  = decl_ty->name->len;
                for (Type *p = ty->params; p; p = p->next) {
                    if (p->name && p->name->len == plen &&
                        !memcmp(p->name->loc, pname, plen)) {
                        Token *saved_name = p->name;
                        Type  *saved_next = p->next;
                        *p = *decl_ty;
                        p->name = saved_name;
                        p->next = saved_next;
                        break;
                    }
                }
            }
            if (!consume(vm, &tok, tok, ","))
                break;
        }
        tok = skip(vm, tok, ";");
    }

    validate_main_signature(vm, fn);
    create_param_lvars(vm, ty->params);

    // For nested functions, create a hidden __static_link parameter
    // This holds a pointer to the parent function's stack frame (bp)
    // IMPORTANT: This must be added AFTER create_param_lvars because new_lvar
    // prepends to the locals list. We want __static_link to be at the HEAD of
    // the list (offset -1) to match where ENT3 spills REG_A0.
    if (is_nested) {
        new_lvar(vm, "__static_link", 13, pointer_to(vm, ty_void));
    }

    // Note: Struct/union returns are handled via return_buffer in codegen.c
    // The hidden parameter approach was incomplete (caller never provided it),
    // so it has been removed to fix variadic functions with struct returns.
    // Type *rty = ty->return_ty;
    // if ((rty->kind == TY_STRUCT || rty->kind == TY_UNION) && rty->size > 16)
    //     new_lvar(vm, "", pointer_to(rty));

    fn->params = vm->compiler.locals;

    if (ty->is_variadic)
        fn->va_area =
            new_lvar(vm, "__va_area__", 11, array_of(vm, ty_char, 136));
    fn->alloca_bottom =
        new_lvar(vm, "__alloca_size__", 15, pointer_to(vm, ty_char));

    tok = skip(vm, tok, "{");

    // [https://www.sigbus.info/n1570#6.4.2.2p1] "__func__" is automatically
    // defined as a local variable containing the current function name.
    // Not available before C99 — omitting it causes a natural undeclared-
    // identifier error if used in C89 mode.
    if (vm->compiler.c_std >= CCCC_STD_C99) {
        push_scope(vm, "__func__", 8)->var = new_string_literal(
            vm, fn->name, array_of(vm, ty_char, strlen(fn->name) + 1));

        // [GNU] __FUNCTION__ is yet another name of __func__.
        push_scope(vm, "__FUNCTION__", 12)->var = new_string_literal(
            vm, fn->name, array_of(vm, ty_char, strlen(fn->name) + 1));
    }

    Token *close_brace = NULL;

    // Negative test: body is expected to fail compilation with a specific error.
    // Compile in error-collection mode and absorb all errors regardless of match.
    TestFnRecord *neg_rec = find_neg_test_record(vm, fn->name);
    if (neg_rec) {
        bool old_collect       = vm->collect_errors;
        int  pre_count         = vm->error_count;
        CompileError *pre_tail = vm->errors_tail;
        vm->collect_errors     = true;

        fn->body = compound_stmt(vm, &tok, tok, &close_brace);
        append_implicit_return(vm, fn, close_brace ? close_brace : ty->name);
        fn->locals = vm->compiler.locals;
        leave_scope(vm);
        resolve_goto_labels(vm);

        CompileError *new_errors = pre_tail ? pre_tail->next : vm->errors;
        int err_count = vm->error_count - pre_count;
        if (err_count == 0) {
            neg_rec->neg_passed = 0;
            strncpy(neg_rec->neg_actual, "no error produced",
                    sizeof(neg_rec->neg_actual) - 1);
        } else {
            neg_rec->neg_passed = -1;
            if (new_errors && new_errors->message)
                strncpy(neg_rec->neg_actual, new_errors->message,
                        sizeof(neg_rec->neg_actual) - 1);
            // First check: if error_count operator is set, apply it
            if (neg_rec->error_count_op != CMP_NONE &&
                !apply_cmp_op_i64(neg_rec->error_count_op, err_count, neg_rec->expect_errors)) {
                snprintf(neg_rec->neg_actual, sizeof(neg_rec->neg_actual),
                         "expected error_count %s %d, got %d",
                         cmp_op_str(neg_rec->error_count_op),
                         neg_rec->expect_errors, err_count);
            } else {
                // Second check: error message pattern
                if (neg_rec->error_pat_negate) {
                    // error != "pat": passes if NO error contains the pattern
                    bool matched = false;
                    for (CompileError *e = new_errors; e; e = e->next) {
                        if (e->message && strstr(e->message, neg_rec->error_pat)) {
                            matched = true;
                            snprintf(neg_rec->neg_actual, sizeof(neg_rec->neg_actual),
                                     "error unexpectedly matched \"%s\": %s",
                                     neg_rec->error_pat, e->message);
                            break;
                        }
                    }
                    if (!matched)
                        neg_rec->neg_passed = 1;
                } else {
                    for (CompileError *e = new_errors; e; e = e->next) {
                        if (e->message && strstr(e->message, neg_rec->error_pat)) {
                            neg_rec->neg_passed = 1;
                            strncpy(neg_rec->neg_actual, e->message,
                                    sizeof(neg_rec->neg_actual) - 1);
                            break;
                        }
                    }
                }
            }
        }

        if (pre_tail) pre_tail->next = NULL;
        else          vm->errors = NULL;
        vm->errors_tail  = pre_tail;
        vm->error_count  = pre_count;
        vm->collect_errors = old_collect;

        fn->body = NULL; // suppress codegen — test result is precomputed
    } else {
        fn->body = compound_stmt(vm, &tok, tok, &close_brace);
        append_implicit_return(vm, fn, close_brace ? close_brace : ty->name);
        fn->locals = vm->compiler.locals;
        leave_scope(vm);
        resolve_goto_labels(vm);
    }

    // Restore parent function context if this was a nested function
    if (is_nested) {
        vm->compiler.current_fn = parent_fn;
        vm->compiler.locals = saved_locals;
        vm->compiler.fn_nesting_depth = saved_nesting_depth;
    } else {
        // CRITICAL: Reset current_fn to NULL for top-level functions!
        // Otherwise the next top-level function will incorrectly think it's
        // nested.
        vm->compiler.current_fn = NULL;
    }

    run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_FUNCTION, fn->name,
                          fn->ty, fn, fn->tok);

    return tok;
}

static Token *global_variable(VirtualMachine *vm, Token *tok, Type *basety,
                              VarAttr *attr) {
    bool first = true;

    while (!consume(vm, &tok, tok, ";")) {
        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        Type *ty = declarator(vm, &tok, tok, basety);
        if (!ty->name)
            error_tok(vm, ty->name_pos, "variable name omitted");

        char *var_name     = get_ident(vm, ty->name);
        int   var_name_len = (int)ty->name->len;

        // C23 auto type inference for global variables
        if (attr->is_auto) {
            if (attr->is_extern)
                error_tok(vm, ty->name, "cannot use 'auto' with 'extern'");
            int decl_depth = count_auto_ptr_depth(ty);
            if (decl_depth < 0)
                error_tok(vm, ty->name,
                          "cannot use 'auto' with array or function declarator");
            if (!equal(tok, "="))
                error_tok(vm, ty->name,
                          "declaration of variable '%.*s' with deduced type 'auto' requires an initializer",
                          (int)ty->name->len, ty->name->loc);
            if (equal(tok->next, "{"))
                error_tok(vm, tok->next, "cannot use 'auto' with array in C");

            // Probe: parse the initializer expression to infer the type
            Token *eq_tok = tok;
            Token *probe_tok = tok->next;
            Node *probe = assign(vm, &probe_tok, probe_tok);
            add_type(vm, probe);
            Type *deduced = auto_deduced_type(vm, probe->ty);

            if (count_ptr_depth(deduced) != decl_depth) {
                char stars[16] = "";
                for (int i = 0; i < decl_depth && i < 15; i++)
                    stars[i] = '*';
                error_tok(vm, ty->name,
                          "variable '%.*s' with type 'auto%s%s' has incompatible initializer",
                          (int)ty->name->len, ty->name->loc,
                          decl_depth > 0 ? " " : "", stars);
            }

            Obj *var = new_gvar(vm, var_name, var_name_len, deduced);
            var->is_definition = true;
            var->is_static = attr->is_static;
            if (attr->align)
                var->align = attr->align;

            // Re-parse from eq_tok to write init_data correctly
            gvar_initializer(vm, &tok, eq_tok->next, var);

            run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_GLOBAL, var->name,
                                  var->ty, var, var->tok);
            continue;
        }

        // For extern declarations, check whether a macro-generated global
        // already exists in macro_globals.  If so, push the macro Obj into
        // scope directly so code referencing this name uses the same Obj that
        // will have init_data and the correct data-segment offset in codegen.
        // (Global variable references are offset-based, unlike function calls
        // which are patched by name, so both the scope and codegen must agree
        // on the same Obj.)
        if (attr->is_extern && vm->compiler.macro_globals) {
            Obj *mg = NULL;
            for (Obj *o = vm->compiler.macro_globals; o; o = o->next) {
                if (!o->is_function &&
                    (int)strlen(o->name) == var_name_len &&
                    strncmp(o->name, var_name, var_name_len) == 0) {
                    mg = o;
                    break;
                }
            }
            if (mg) {
                push_scope(vm, var_name, var_name_len)->var = mg;
                if (equal(tok, "="))
                    gvar_initializer(vm, &tok, tok->next, mg);
                run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_GLOBAL,
                                      mg->name, mg->ty, mg, mg->tok);
                continue;
            }
        }

        VarScope *previous = find_var(vm, ty->name);
        Obj *var = new_gvar(vm, var_name, var_name_len, ty);
        if (previous && previous->var && !previous->var->is_function) {
            var->is_maybe_unused |= previous->var->is_maybe_unused;
            var->is_deprecated |= previous->var->is_deprecated;
            if (!var->deprecated_msg)
                var->deprecated_msg = previous->var->deprecated_msg;
            previous->var->is_maybe_unused |= var->is_maybe_unused;
            previous->var->is_deprecated |= var->is_deprecated;
            if (!previous->var->deprecated_msg)
                previous->var->deprecated_msg = var->deprecated_msg;
        }
        var->is_definition = !attr->is_extern;
        var->is_static = attr->is_static;
        var->is_tls = attr->is_tls;
        var->is_constexpr = attr->is_constexpr;
        if (attr->align)
            var->align = attr->align;

        if (var->is_constexpr) {
            if (attr->is_extern || attr->is_tls)
                error_tok(vm, ty->name,
                          "constexpr object must be a definition with internal storage");
            var->is_static = true;
        }

        if (equal(tok, "="))
            gvar_initializer(vm, &tok, tok->next, var);
        else if (var->is_constexpr)
            error_tok(vm, ty->name, "constexpr object requires an initializer");
        else if (!attr->is_extern && !attr->is_tls)
            var->is_tentative = true;

        run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_GLOBAL, var->name,
                              var->ty, var, var->tok);
    }
    return tok;
}

// Lookahead tokens and returns true if a given token is a start
// of a function definition or declaration.
static bool is_function(VirtualMachine *vm, Token *tok, Type *basety) {
    if (equal(tok, ";"))
        return false;

    Type dummy = {};
    bool saved_lookahead = vm->compiler.in_type_lookahead;
    vm->compiler.in_type_lookahead = true;
    Type *ty = declarator(vm, &tok, tok, basety ? copy_type(vm, basety) : &dummy);
    vm->compiler.in_type_lookahead = saved_lookahead;
    return ty->kind == TY_FUNC;
}

static bool is_function_decl_list(VirtualMachine *vm, Token *tok, Type *basety) {
    Type dummy = {};
    bool saved_lookahead = vm->compiler.in_type_lookahead;
    vm->compiler.in_type_lookahead = true;
    Type *ty = declarator(vm, &tok, tok, basety ? copy_type(vm, basety) : &dummy);
    vm->compiler.in_type_lookahead = saved_lookahead;
    return ty->kind == TY_FUNC && equal(tok, ",");
}

// Remove redundant tentative definitions.
static void scan_globals(VirtualMachine *vm) {
    Obj head;
    Obj *cur = &head;

    for (Obj *var = vm->compiler.globals; var; var = var->next) {
        if (!var->is_tentative) {
            cur = cur->next = var;
            continue;
        }

        // Find another definition of the same identifier.
        Obj *var2 = vm->compiler.globals;
        for (; var2; var2 = var2->next)
            if (var != var2 && var2->is_definition &&
                strlen(var->name) == strlen(var2->name) &&
                strncmp(var->name, var2->name, strlen(var2->name)) == 0)
                break;

        // If there's another definition, the tentative definition
        // is redundant
        if (!var2)
            cur = cur->next = var;
    }

    cur->next = NULL;
    vm->compiler.globals = head.next;
}

static void warn_unused_globals(VirtualMachine *vm) {
    for (Obj *var = vm->compiler.globals; var; var = var->next) {
        if (!var->is_static || !var->is_definition || var->is_local_symbol ||
            var->is_macro_generated || !var->tok || var->is_used ||
            var->is_maybe_unused)
            continue;

        bool already_checked = false;
        bool redeclaration_used = false;
        for (Obj *other = vm->compiler.globals; other; other = other->next) {
            if (other == var)
                already_checked = true;
            if (other == var || other->is_function != var->is_function ||
                strcmp(other->name, var->name) != 0)
                continue;
            if (!already_checked)
                redeclaration_used = true;
            if (other->is_used || other->is_maybe_unused)
                redeclaration_used = true;
        }
        if (redeclaration_used)
            continue;

        warn_tok(vm, var->tok, CCCC_WARN_UNUSED, "unused %s '%s'",
                 var->is_function ? "function" : "variable",
                 obj_display_name(var));
    }
}

static void declare_builtin_functions(VirtualMachine *vm) {
    // alloca(size) -> void*
    Type *ty = func_type(vm, pointer_to(vm, ty_void));
    ty->params = copy_type(vm, ty_int);
    vm->compiler.builtin_alloca = new_gvar(vm, "alloca", 6, ty);
    vm->compiler.builtin_alloca->is_definition = false;

    // setjmp(jmp_buf) -> int
    // jmp_buf is an array type, but we'll treat it as a pointer for now
    Type *setjmp_ty = func_type(vm, ty_int);
    setjmp_ty->params = pointer_to(vm, ty_long); // jmp_buf is long long[5]
    vm->compiler.builtin_setjmp = new_gvar(vm, "setjmp", 6, setjmp_ty);
    vm->compiler.builtin_setjmp->is_definition = false;

    // longjmp(jmp_buf, int) -> void (noreturn)
    Type *longjmp_ty = func_type(vm, ty_void);
    longjmp_ty->params = pointer_to(vm, ty_long);
    longjmp_ty->params->next = copy_type(vm, ty_int);
    vm->compiler.builtin_longjmp = new_gvar(vm, "longjmp", 7, longjmp_ty);
    vm->compiler.builtin_longjmp->is_definition = false;

    Type *dlopen_ty = func_type(vm, pointer_to(vm, ty_void));
    dlopen_ty->params = pointer_to(vm, ty_char);
    dlopen_ty->params->next = copy_type(vm, ty_int);
    vm->compiler.builtin_dlopen = new_gvar(vm, "dlopen", 6, dlopen_ty);
    vm->compiler.builtin_dlopen->is_definition = false;

    Type *dlsym_ty = func_type(vm, pointer_to(vm, ty_void));
    dlsym_ty->params = pointer_to(vm, ty_void);
    dlsym_ty->params->next = pointer_to(vm, ty_char);
    vm->compiler.builtin_dlsym = new_gvar(vm, "dlsym", 5, dlsym_ty);
    vm->compiler.builtin_dlsym->is_definition = false;

    Type *dlclose_ty = func_type(vm, ty_int);
    dlclose_ty->params = pointer_to(vm, ty_void);
    vm->compiler.builtin_dlclose = new_gvar(vm, "dlclose", 7, dlclose_ty);
    vm->compiler.builtin_dlclose->is_definition = false;

    Type *dlerror_ty = func_type(vm, pointer_to(vm, ty_char));
    vm->compiler.builtin_dlerror = new_gvar(vm, "dlerror", 7, dlerror_ty);
    vm->compiler.builtin_dlerror->is_definition = false;

    // signal(int sig, void (*func)(int)) -> void (*)(int)
    Type *signal_handler_ty = func_type(vm, ty_void);
    signal_handler_ty->params = copy_type(vm, ty_int);
    Type *signal_ty = func_type(vm, pointer_to(vm, signal_handler_ty));
    signal_ty->params = copy_type(vm, ty_int);
    signal_ty->params->next = pointer_to(vm, signal_handler_ty);
    vm->compiler.builtin_signal = new_gvar(vm, "signal", 6, signal_ty);
    vm->compiler.builtin_signal->is_definition = false;

    // raise(int sig) -> int
    Type *raise_ty = func_type(vm, ty_int);
    raise_ty->params = copy_type(vm, ty_int);
    vm->compiler.builtin_raise = new_gvar(vm, "raise", 5, raise_ty);
    vm->compiler.builtin_raise->is_definition = false;

    // __cccc_block_copy_impl(void *desc) -> void*
    // Internal helper backing the Block_copy() extension; resolved to the
    // host cfunc registered in the FFI table by register_stdlib_functions.
    // Declared as a global prototype so Block_copy's parser lookup finds it.
    Type *block_copy_ty = func_type(vm, pointer_to(vm, ty_void));
    block_copy_ty->params = pointer_to(vm, ty_void);
    vm->compiler.builtin_block_copy =
        new_gvar(vm, "__cccc_block_copy_impl", 22, block_copy_ty);
    vm->compiler.builtin_block_copy->is_function = true;
    vm->compiler.builtin_block_copy->is_definition = false;

    // free(void*) -> void
    // Ensures Block_release always resolves even when <stdlib.h> is not
    // included (#458).  Named "free" so codegen's is_extern_func_name("free")
    // check routes it to MFRE (CCCC_VM_HEAP) or the host free() via FFI.
    // If the TU later declares its own free prototype the parser will find the
    // user declaration in globals first (it's prepended), shadowing this one.
    Type *free_ty = func_type(vm, ty_void);
    free_ty->params = pointer_to(vm, ty_void);
    vm->compiler.builtin_free = new_gvar(vm, "free", 4, free_ty);
    vm->compiler.builtin_free->is_function = true;
    vm->compiler.builtin_free->is_definition = false;
}

// program = (typedef | function-definition | global-variable)*
Obj *parse(VirtualMachine *vm, Token *tok) {
    // Initialize error recovery placeholder
    error_var->ty = ty_error;

    // Initialize global scope
    enter_scope(vm);

    declare_builtin_functions(vm);
    vm->compiler.globals = NULL;

    while (tok->kind != TK_EOF) {
        // _Static_assert or static_assert (C23) - check before declspec
        if (equal(tok, "_Static_assert") || equal(tok, "static_assert")) {
            tok = static_assert_decl(vm, tok);
            continue;
        }

        // File-scope macro calls to non-inline macros are executed pre-parse
        // by cc_execute_inline_macros and their tokens are removed. If the
        // parser still sees one, it had arguments or was missed; skip it.
        // Inline macros cannot be used at file scope.
        if (!vm->compiler.in_macro_mode && tok->kind == TK_IDENT &&
            equal(tok->next, "(")) {
            MacroFn *pm = find_macro_fn(vm, tok);
            if (pm) {
                if (pm->is_inline) {
                    error_tok(vm, tok,
                              "inline macro '%.*s' cannot be used at file "
                              "scope; use it in an expression",
                              tok->len, tok->loc);
                }

                // Skip the call tokens (was executed pre-parse).
                // Walk to matching ')' respecting nesting.
                tok = tok->next->next; // after '('
                int depth = 1;
                while (tok && tok->kind != TK_EOF && depth > 0) {
                    if (equal(tok, "(")) depth++;
                    else if (equal(tok, ")")) {
                        depth--;
                        if (depth == 0) break;
                    }
                    tok = tok->next;
                }
                tok = skip(vm, tok, ")");
                tok = skip(vm, tok, ";");
                continue;
            }
        }

        VarAttr attr = {};
        Type *basety = declspec(vm, &tok, tok, &attr);

        // Typedef
        if (attr.is_typedef) {
            tok = parse_typedef(vm, tok, basety, &attr);
            continue;
        }

        // Function
        if (is_function(vm, tok, basety)) {
            tok = is_function_decl_list(vm, tok, basety)
                      ? function_declaration_list(vm, tok, basety, &attr)
                      : function(vm, tok, basety, &attr);
            continue;
        }

        // File-scope type declaration such as @serialize struct Point { ... };
        if (equal(tok, ";")) {
            if (has_custom_attrs(basety, &attr)) {
                char *name = NULL;
                if (basety->name)
                    name = arena_strndup(vm, basety->name->loc, basety->name->len);
                run_decl_custom_attrs(vm, basety, &attr, ATTR_TARGET_TYPE, name,
                                      basety, NULL, basety->name);
            }
            tok = tok->next;
            continue;
        }

        // Global variable
        tok = global_variable(vm, tok, basety, &attr);
    }

    for (Obj *var = vm->compiler.globals; var; var = var->next)
        if (var->is_root)
            mark_live(vm, var);

    // Remove redundant tentative definitions.
    scan_globals(vm);
    warn_unused_globals(vm);
    return vm->compiler.globals;
}

// Exposed parsing functions for K's ast_parse API
Node *cc_parse_expr(VirtualMachine *vm, Token **rest, Token *tok) {
    return expr(vm, rest, tok);
}

Node *cc_parse_assign(VirtualMachine *vm, Token **rest, Token *tok) {
    return assign(vm, rest, tok);
}

Node *cc_parse_stmt(VirtualMachine *vm, Token **rest, Token *tok) {
    return stmt(vm, rest, tok);
}

Node *cc_parse_compound_stmt(VirtualMachine *vm, Token **rest, Token *tok) {
    return compound_stmt(vm, rest, tok, NULL);
}

int64_t cc_eval(VirtualMachine *vm, Node *node) { return eval(vm, node); }
double  cc_eval_double(VirtualMachine *vm, Node *node) { return eval_double(vm, node); }

void cc_init_parser(VirtualMachine *vm) { error_var->ty = ty_error; }
