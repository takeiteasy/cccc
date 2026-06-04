/*
 JCC: JIT C Compiler

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
#include "jcc.h"
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
    int enum_val;
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
    bool is_maybe_unused;
    bool is_deprecated;
    char *deprecated_msg;
    Token *attribute_tok;
    int align;
} VarAttr;

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

static bool is_typename(JCC *vm, Token *tok);
static Type *declspec(JCC *vm, Token **rest, Token *tok, VarAttr *attr);
static Type *typename(JCC *vm, Token **rest, Token *tok);
static Type *enum_specifier(JCC *vm, Token **rest, Token *tok);
static Type *typeof_specifier(JCC *vm, Token **rest, Token *tok);
static Type *typeof_unqual_specifier(JCC *vm, Token **rest, Token *tok);
static Type *type_suffix(JCC *vm, Token **rest, Token *tok, Type *ty);
static Type *declarator(JCC *vm, Token **rest, Token *tok, Type *ty);
static Token *attribute_list(JCC *vm, Token *tok, Type *ty, VarAttr *attr);
static Token *c23_attribute_list(JCC *vm, Token *tok, Type *ty, VarAttr *attr);
static void inherit_semantic_attrs(Type *dst, Type *src);
static Type *apply_var_attrs_to_type(JCC *vm, Type *ty, VarAttr *attr);
static Node *declaration(JCC *vm, Token **rest, Token *tok, Type *basety,
                         VarAttr *attr);
static void array_initializer2(JCC *vm, Token **rest, Token *tok,
                               Initializer *init, int i);
static void struct_initializer2(JCC *vm, Token **rest, Token *tok,
                                Initializer *init, Member *mem);
static void initializer2(JCC *vm, Token **rest, Token *tok, Initializer *init);
static Initializer *initializer(JCC *vm, Token **rest, Token *tok, Type *ty,
                                Type **new_ty);
static Node *lvar_initializer(JCC *vm, Token **rest, Token *tok, Obj *var);
static void gvar_initializer(JCC *vm, Token **rest, Token *tok, Obj *var);
static Node *create_vla_init(JCC *vm, Initializer *init, Type *ty, Obj *var,
                             Token *tok);
static Node *compound_stmt(JCC *vm, Token **rest, Token *tok);
static Node *stmt(JCC *vm, Token **rest, Token *tok);
static Node *expr_stmt(JCC *vm, Token **rest, Token *tok);
static Node *expr(JCC *vm, Token **rest, Token *tok);
static int64_t eval(JCC *vm, Node *node);
static int64_t eval2(JCC *vm, Node *node, char ***label);
static int64_t eval_rval(JCC *vm, Node *node, char ***label);
static bool is_const_expr(JCC *vm, Node *node);
static Node *assign(JCC *vm, Token **rest, Token *tok);
static Node *logor(JCC *vm, Token **rest, Token *tok);
static double eval_double(JCC *vm, Node *node);
static Node *conditional(JCC *vm, Token **rest, Token *tok);
static Node *logand(JCC *vm, Token **rest, Token *tok);
static Node *bitor(JCC *vm, Token **rest, Token *tok);
static Node *bitxor(JCC *vm, Token **rest, Token *tok);
static Node *bitand(JCC *vm, Token **rest, Token *tok);
static Node *equality(JCC *vm, Token **rest, Token *tok);
static Node *relational(JCC *vm, Token **rest, Token *tok);
static Node *shift(JCC *vm, Token **rest, Token *tok);
static Node *add(JCC *vm, Token **rest, Token *tok);
static Node *new_add(JCC *vm, Node *lhs, Node *rhs, Token *tok);
static Node *new_sub(JCC *vm, Node *lhs, Node *rhs, Token *tok);
static Node *mul(JCC *vm, Token **rest, Token *tok);
static Node *cast(JCC *vm, Token **rest, Token *tok);
static Member *get_struct_member(Type *ty, Token *tok);
static Type *struct_decl(JCC *vm, Token **rest, Token *tok);
static Type *union_decl(JCC *vm, Token **rest, Token *tok);
static Node *postfix(JCC *vm, Token **rest, Token *tok);
static Node *funcall(JCC *vm, Token **rest, Token *tok, Node *node);
static Node *unary(JCC *vm, Token **rest, Token *tok);
static Node *primary(JCC *vm, Token **rest, Token *tok);
static Token *parse_typedef(JCC *vm, Token *tok, Type *basety);
static bool is_function(JCC *vm, Token *tok);
static Token *function(JCC *vm, Token *tok, Type *basety, VarAttr *attr);
static Token *global_variable(JCC *vm, Token *tok, Type *basety, VarAttr *attr);

static int align_to(int n, int align) {
    return (int)(((long long)n + align - 1) / align * align);
}

static int align_down(int n, int align) {
    return align_to(n - align + 1, align);
}

static void enter_scope(JCC *vm) {
    Scope *sc = arena_alloc(&vm->compiler.parser_arena, sizeof(Scope));
    memset(sc, 0, sizeof(Scope));
    sc->next = vm->compiler.scope;
    vm->compiler.scope = sc;
}

static char *obj_display_name(Obj *var) {
    return var->display_name ? var->display_name : var->name;
}

static void warn_deprecated_use(JCC *vm, Token *tok, char *name,
                                char *message) {
    if (message)
        warn_tok(vm, tok, JCC_WARN_DEPRECATED, "'%s' is deprecated: %s", name,
                 message);
    else
        warn_tok(vm, tok, JCC_WARN_DEPRECATED, "'%s' is deprecated", name);
}

static Type *type_after_deprecated_use(JCC *vm, Type *ty) {
    Type *copy = copy_type(vm, ty);
    copy->is_deprecated = false;
    copy->deprecated_msg = NULL;
    return copy;
}

static void warn_unused_scope(JCC *vm, Scope *sc) {
    for (VarScopeNode *node = sc->vars; node; node = node->next) {
        Obj *var = node->var;
        if (!var || !var->is_local_symbol || !var->tok || var->is_used ||
            var->is_maybe_unused)
            continue;
        warn_tok(vm, var->tok, JCC_WARN_UNUSED, "unused %s '%s'",
                 var->is_param ? "parameter" : "variable",
                 obj_display_name(var));
    }
}

static void leave_scope(JCC *vm) {
    warn_unused_scope(vm, vm->compiler.scope);
    hashmap_deinit_borrowed(&vm->compiler.scope->var_map);
    hashmap_deinit_borrowed(&vm->compiler.scope->tag_map);
    vm->compiler.scope = vm->compiler.scope->next;
}

// Find a variable by name.
static VarScope *find_var(JCC *vm, Token *tok) {
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

static void warn_if_shadowing(JCC *vm, Token *tok) {
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
            warn_tok(vm, tok, JCC_WARN_SHADOW,
                     "declaration of '%.*s' shadows an outer variable",
                     tok->len, tok->loc);
        return;
    }
}

// Find a macro function by name
static MacroFn *find_macro_fn(JCC *vm, Token *tok) {
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next) {
        if (pm->is_macro_entry && strlen(pm->name) == tok->len &&
            strncmp(pm->name, tok->loc, tok->len) == 0) {
            return pm;
        }
    }
    return NULL;
}

static Type *find_tag(JCC *vm, Token *tok) {
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

static Node *new_node(JCC *vm, NodeKind kind, Token *tok) {
    Node *node = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    memset(node, 0, sizeof(Node));
    node->kind = kind;
    node->tok = tok;
    return node;
}

static Node *new_binary(JCC *vm, NodeKind kind, Node *lhs, Node *rhs,
                        Token *tok) {
    Node *node = new_node(vm, kind, tok);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

static Node *new_unary(JCC *vm, NodeKind kind, Node *expr, Token *tok) {
    Node *node = new_node(vm, kind, tok);
    node->lhs = expr;
    return node;
}

static Node *new_num(JCC *vm, int64_t val, Token *tok) {
    Node *node = new_node(vm, ND_NUM, tok);
    node->val = val;
    node->ty = ty_int;
    return node;
}

static Node *new_long(JCC *vm, int64_t val, Token *tok) {
    Node *node = new_node(vm, ND_NUM, tok);
    node->val = val;
    node->ty = ty_long;
    return node;
}

static Node *new_ulong(JCC *vm, long val, Token *tok) {
    Node *node = new_node(vm, ND_NUM, tok);
    node->val = val;
    node->ty = ty_ulong;
    return node;
}

static Node *new_complex_node(JCC *vm, Node *real, Node *imag, Type *ty,
                              Token *tok) {
    Node *node = new_node(vm, ND_COMPLEX, tok);
    node->lhs = real;
    node->rhs = imag;
    node->ty = ty;
    return node;
}

static Node *new_var_node(JCC *vm, Obj *var, Token *tok) {
    Node *node = new_node(vm, ND_VAR, tok);
    node->var = var;
    return node;
}

static Node *new_vla_ptr(JCC *vm, Obj *var, Token *tok) {
    Node *node = new_node(vm, ND_VLA_PTR, tok);
    node->var = var;
    return node;
}

Node *new_cast(JCC *vm, Node *expr, Type *ty) {
    add_type(vm, expr);
    Node *node = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    memset(node, 0, sizeof(Node));
    node->kind = ND_CAST;
    node->tok = expr->tok;
    node->lhs = expr;
    node->ty = copy_type(vm, ty);
    return node;
}

static VarScope *push_scope(JCC *vm, char *name, int name_len) {
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

static void record_type_name(JCC *vm, Type *ty, char *name, int name_len,
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

static Initializer *new_initializer(JCC *vm, Type *ty, bool is_flexible) {
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

static Obj *new_var(JCC *vm, char *name, int name_len, Type *ty) {
    Obj *var = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(var, 0, sizeof(Obj));
    var->name = name;
    var->ty = ty;
    var->align = ty->align;
    var->tok = ty->name;
    var->display_name =
        ty->name ? arena_strndup(vm, ty->name->loc, ty->name->len) : name;
    var->is_maybe_unused = ty->is_maybe_unused;
    var->is_deprecated = ty->is_deprecated;
    var->deprecated_msg = ty->deprecated_msg;
    push_scope(vm, name, name_len)->var = var;
    return var;
}

static Obj *new_lvar(JCC *vm, char *name, int name_len, Type *ty) {
    warn_if_shadowing(vm, ty->name);
    Obj *var = new_var(vm, name, name_len, ty);
    var->is_local = true;
    var->is_local_symbol = ty->name && name_len > 0;
    var->next = vm->compiler.locals;
    vm->compiler.locals = var;
    return var;
}

static Obj *new_gvar(JCC *vm, char *name, int name_len, Type *ty) {
    Obj *var = new_var(vm, name, name_len, ty);
    var->next = vm->compiler.globals;
    var->is_static = true;
    var->is_definition = true;
    vm->compiler.globals = var;
    return var;
}

static Obj *new_implicit_function(JCC *vm, Token *tok) {
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

static char *new_unique_name(JCC *vm) {
    return arena_format(vm, ".L..%d", vm->compiler.unique_name_counter++);
}

static Obj *new_anon_gvar(JCC *vm, Type *ty) {
    char *name = new_unique_name(vm);
    return new_gvar(vm, name, strlen(name), ty);
}

static Obj *new_string_literal(JCC *vm, char *p, Type *ty) {
    Obj *var = new_anon_gvar(vm, ty);
    var->init_data = p;
    return var;
}

static char *get_ident(JCC *vm, Token *tok) {
    if (tok->kind != TK_IDENT)
        error_tok(vm, tok, "expected an identifier, found '%.*s'", tok->len,
                  tok->loc);
    char *s = arena_alloc(&vm->compiler.parser_arena, tok->len + 1);
    memcpy(s, tok->loc, tok->len);
    s[tok->len] = '\0';
    return s;
}

// Error recovery helper: Skip to end of statement (semicolon or closing brace)
static Token *skip_to_stmt_end(JCC *vm, Token *tok) {
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
static Token *skip_to_decl_boundary(JCC *vm, Token *tok) {
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

static Type *find_typedef(JCC *vm, Token *tok) {
    if (tok->kind == TK_IDENT) {
        VarScope *sc = find_var(vm, tok);
        if (sc)
            return sc->type_def;
    }
    return NULL;
}

static void push_tag_scope(JCC *vm, Token *tok, Type *ty) {
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
} DeclKw;

static DeclKw declspec_kw(Token *tok) {
    const char *s = tok->loc;
    switch (tok->len) {
    case 3:  // int
        if (s[0]=='i' && s[1]=='n' && s[2]=='t') return DK_INT;
        break;
    case 4:  // auto, char, enum, long, void
        switch (s[0]) {
        case 'a': if (s[1]=='u' && s[2]=='t' && s[3]=='o') return DK_AUTO;  break;
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
    case 7:  // _Atomic, __block, typedef
        if (s[0] == '_') {
            if (s[1]=='A' && memcmp(s+2,"tomic",5)==0) return DK_ATOMIC;
            if (s[1]=='_' && memcmp(s+2,"block",5)==0) return DK_BLOCK_VAR;
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
    case 10:  // __restrict, _Imaginary
        if (s[0]=='_') {
            if (s[1]=='_' && memcmp(s+2,"restrict",8)==0) return DK_RESTRICT;
            if (s[1]=='I' && memcmp(s+2,"maginary",8)==0) return DK_IMAGINARY;
        }
        break;
    case 12:  // __restrict__
        if (memcmp(s,"__restrict__",12)==0) return DK_RESTRICT;
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
static Type *declspec(JCC *vm, Token **rest, Token *tok, VarAttr *attr) {
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
    bool is_atomic = false;
    bool is_const = false;
    bool is_volatile = false;

    while (is_typename(vm, tok)) {
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
                if (vm->compiler.c_std < JCC_STD_C99)
                    error_tok(vm, tok, "'inline' is not available before C99");
                attr->is_inline = true;
            }
            else if (dk == DK_CONSTEXPR) attr->is_constexpr = true;
            else if (dk == DK_BLOCK_VAR) attr->is_block_var = true;
            else                         attr->is_tls       = true;
            if (attr->is_typedef && attr->is_static + attr->is_extern +
                                        attr->is_inline + attr->is_tls > 1)
                error_tok(vm, tok,
                          "typedef may not be used together with static,"
                          " extern, inline, __thread or _Thread_local");
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
        case DK_AUTO: case DK_REGISTER: case DK_RESTRICT: case DK_NORETURN:
            tok = tok->next;
            continue;
        case DK_ATOMIC:
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
        case DK_STRUCT: case DK_UNION: case DK_ENUM:
        case DK_TYPEOF: case DK_TYPEOF_UNQUAL: case DK_NONE: {
            Type *ty2 = (dk == DK_NONE) ? find_typedef(vm, tok) : NULL;
            if (counter) goto declspec_done;
            if      (dk == DK_STRUCT)        ty = struct_decl(vm, &tok, tok->next);
            else if (dk == DK_UNION)         ty = union_decl(vm, &tok, tok->next);
            else if (dk == DK_ENUM)          ty = enum_specifier(vm, &tok, tok->next);
            else if (dk == DK_TYPEOF)        ty = typeof_specifier(vm, &tok, tok->next);
            else if (dk == DK_TYPEOF_UNQUAL) ty = typeof_unqual_specifier(vm, &tok, tok->next);
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
            if ((counter & LONG) && vm->compiler.c_std < JCC_STD_C99 &&
                !vm->compiler.in_type_lookahead)
                warn_tok(vm, tok, JCC_WARN_PEDANTIC,
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
        warn_tok(vm, start, JCC_WARN_IMPLICIT_INT,
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

    *rest = tok;
    return ty;
}

// func-params = ("void" | param ("," param)* ("," "...")?)? ")"
// param       = declspec declarator
static Type *func_params(JCC *vm, Token **rest, Token *tok, Type *ty) {
    if (equal(tok, "void") && equal(tok->next, ")")) {
        *rest = tok->next->next;
        return func_type(vm, ty);
    }

    Type head = {};
    Type *cur = &head;
    bool is_variadic = false;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(vm, tok, ",");

        if (equal(tok, "...")) {
            is_variadic = true;
            tok = tok->next;
            skip(vm, tok, ")");
            break;
        }

        Type *ty2 = declspec(vm, &tok, tok, NULL);
        ty2 = declarator(vm, &tok, tok, ty2);

        Token *name = ty2->name;

        if (ty2->kind == TY_ARRAY) {
            // "array of T" is converted to "pointer to T" only in the parameter
            // context. For example, *argv[] is converted to **argv by this.
            ty2 = pointer_to(vm, ty2->base);
            ty2->name = name;
        } else if (ty2->kind == TY_FUNC) {
            // Likewise, a function is converted to a pointer to a function
            // only in the parameter context.
            ty2 = pointer_to(vm, ty2);
            ty2->name = name;
        }

        cur = cur->next = copy_type(vm, ty2);
    }

    if (cur == &head)
        is_variadic = true;

    ty = func_type(vm, ty);
    ty->params = head.next;
    ty->is_variadic = is_variadic;
    *rest = tok->next;
    return ty;
}

// array-dimensions = ("static" | "restrict")* const-expr? "]" type-suffix
static Type *array_dimensions(JCC *vm, Token **rest, Token *tok, Type *ty) {
    while (equal(tok, "static") || equal(tok, "restrict"))
        tok = tok->next;

    if (equal(tok, "]")) {
        ty = type_suffix(vm, rest, tok->next, ty);
        return array_of(vm, ty, -1);
    }

    Token *expr_tok = tok;
    Node *expr = conditional(vm, &tok, tok);
    tok = skip(vm, tok, "]");
    ty = type_suffix(vm, rest, tok, ty);

    if (ty->kind == TY_VLA || !is_const_expr(vm, expr)) {
        if (vm->compiler.c_std < JCC_STD_C99)
            error_tok(vm, expr_tok, "variable-length arrays are not available before C99");
        return vla_of(vm, ty, expr);
    }
    return array_of(vm, ty, eval(vm, expr));
}

// type-suffix = "(" func-params
//             | "[" array-dimensions
//             | ε
static Type *type_suffix(JCC *vm, Token **rest, Token *tok, Type *ty) {
    if (equal(tok, "("))
        return func_params(vm, rest, tok->next, ty);

    if (equal(tok, "[") && !equal(tok->next, "["))
        return array_dimensions(vm, rest, tok->next, ty);

    *rest = tok;
    return ty;
}

// pointers = ("*" ("const" | "volatile" | "restrict")*)*
static Type *pointers(JCC *vm, Token **rest, Token *tok, Type *ty) {
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
            }
            tok = tok->next;
        }
    }
    *rest = tok;
    return ty;
}

// declarator = attribute? pointers ("(" ident ")" | "(" declarator ")" | ident)
// type-suffix attribute?
static Type *declarator(JCC *vm, Token **rest, Token *tok, Type *ty) {
    // Handle __attribute__ before declarator
    VarAttr prefix_attr = {};
    tok = attribute_list(vm, tok, NULL, &prefix_attr);
    tok = c23_attribute_list(vm, tok, NULL, &prefix_attr);
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

    // Handle __attribute__ after declarator
    VarAttr suffix_attr = {};
    tok = attribute_list(vm, *rest, NULL, &suffix_attr);
    tok = c23_attribute_list(vm, tok, NULL, &suffix_attr);
    ty = apply_var_attrs_to_type(vm, ty, &suffix_attr);

    ty->name = name;
    ty->name_pos = name_pos;
    *rest = tok;
    return ty;
}

// abstract-declarator = attribute? pointers ("(" abstract-declarator ")")?
// type-suffix attribute?
static Type *abstract_declarator(JCC *vm, Token **rest, Token *tok, Type *ty) {
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
static Type *typename(JCC *vm, Token **rest, Token *tok) {
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

// enum-specifier = ident? "{" enum-list? "}"
//                | ident ("{" enum-list? "}")?
//
// enum-list      = ident ("=" num)? ("," ident ("=" num)?)* ","?
static Type *enum_specifier(JCC *vm, Token **rest, Token *tok) {
    Type *ty = enum_type(vm);

    // Read a struct tag.
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        tok = tok->next;
    }

    if (tag && !equal(tok, "{")) {
        Type *ty = find_tag(vm, tag);
        if (!ty)
            error_tok(vm, tag, "unknown enum type");
        if (ty->kind != TY_ENUM)
            error_tok(vm, tag, "not an enum tag");
        if (ty->is_deprecated)
            warn_deprecated_use(vm, tag, get_ident(vm, tag),
                                ty->deprecated_msg);
        *rest = tok;
        return ty->is_deprecated ? type_after_deprecated_use(vm, ty) : ty;
    }

    tok = skip(vm, tok, "{");

    // Read an enum-list.
    int i = 0;
    int val = 0;
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

        VarScope *sc = push_scope(vm, name, name_len);
        sc->enum_ty = ty;
        sc->enum_val = val;
        sc->is_deprecated = enum_attr.is_deprecated;
        sc->deprecated_msg = enum_attr.deprecated_msg;

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

    if (tag)
        push_tag_scope(vm, tag, ty);
    return ty;
}

// typeof-specifier = "(" (expr | typename) ")"
static Type *typeof_specifier(JCC *vm, Token **rest, Token *tok) {
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
static Type *typeof_unqual_specifier(JCC *vm, Token **rest, Token *tok) {
    Type *ty = typeof_specifier(vm, rest, tok);
    // Copy the type to avoid mutating the original
    ty = copy_type(vm, ty);
    // Remove all qualifiers
    ty->is_const = false;
    ty->is_volatile = false;
    return ty;
}

// Get size for a type (no adjustment needed - types are already correct)
static int get_vm_size(Type *ty) { return ty->size; }

// Generate code for computing a VLA size.
static Node *compute_vla_size(JCC *vm, Type *ty, Token *tok) {
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

static Node *new_alloca(JCC *vm, Node *sz) {
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
static Node *declaration(JCC *vm, Token **rest, Token *tok, Type *basety,
                         VarAttr *attr) {
    Node head = {};
    Node *cur = &head;
    int i = 0;

    while (!equal(tok, ";")) {
        if (i++ > 0)
            tok = skip(vm, tok, ",");

        Type *ty = declarator(vm, &tok, tok, basety);

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

        if (attr && attr->is_static) {
            // static local variable
            warn_if_shadowing(vm, ty->name);
            Obj *var = new_anon_gvar(vm, ty);
            var->tok = ty->name;
            var->display_name = get_ident(vm, ty->name);
            var->is_local_symbol = true;
            var->is_maybe_unused = ty->is_maybe_unused;
            var->is_deprecated = ty->is_deprecated;
            var->deprecated_msg = ty->deprecated_msg;
            push_scope(vm, get_ident(vm, ty->name), ty->name->len)->var = var;
            if (equal(tok, "="))
                gvar_initializer(vm, &tok, tok->next, var);
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

static Token *skip_excess_element(JCC *vm, Token *tok) {
    if (equal(tok, "{")) {
        tok = skip_excess_element(vm, tok->next);
        return skip(vm, tok, "}");
    }

    assign(vm, &tok, tok);
    return tok;
}

// string-initializer = string-literal
static void string_initializer(JCC *vm, Token **rest, Token *tok,
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
static void array_designator(JCC *vm, Token **rest, Token *tok, Type *ty,
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
static Member *struct_designator(JCC *vm, Token **rest, Token *tok, Type *ty) {
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
static void designation(JCC *vm, Token **rest, Token *tok, Initializer *init) {
    if (equal(tok, "[")) {
        if (vm->compiler.c_std < JCC_STD_C99)
            error_tok(vm, tok, "designated initializers are not available before C99");
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
        if (vm->compiler.c_std < JCC_STD_C99)
            error_tok(vm, tok, "designated initializers are not available before C99");
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
        if (vm->compiler.c_std < JCC_STD_C99)
            error_tok(vm, tok, "designated initializers are not available before C99");
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
static int count_array_init_elements(JCC *vm, Token *tok, Type *ty) {
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
static void array_initializer1(JCC *vm, Token **rest, Token *tok,
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
            if (vm->compiler.c_std < JCC_STD_C99)
                error_tok(vm, tok, "designated initializers are not available before C99");
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
static void array_initializer2(JCC *vm, Token **rest, Token *tok,
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
static void struct_initializer1(JCC *vm, Token **rest, Token *tok,
                                Initializer *init) {
    tok = skip(vm, tok, "{");

    Member *mem = init->ty->members;
    bool first = true;

    while (!consume_end(rest, tok)) {
        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        if (equal(tok, ".")) {
            if (vm->compiler.c_std < JCC_STD_C99)
                error_tok(vm, tok, "designated initializers are not available before C99");
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
static void struct_initializer2(JCC *vm, Token **rest, Token *tok,
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

static void union_initializer(JCC *vm, Token **rest, Token *tok,
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
static void initializer2(JCC *vm, Token **rest, Token *tok, Initializer *init) {
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
        if (expr->ty->kind == TY_STRUCT) {
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

static Type *copy_struct_type(JCC *vm, Type *ty) {
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

static Initializer *initializer(JCC *vm, Token **rest, Token *tok, Type *ty,
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

static Node *init_desg_expr(JCC *vm, InitDesg *desg, Token *tok) {
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

static Node *create_lvar_init(JCC *vm, Initializer *init, Type *ty,
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
        InitDesg desg2 = {desg, 0, mem};
        return create_lvar_init(vm, init->children[mem->idx], mem->ty, &desg2,
                                tok);
    }

    if (!init->expr)
        return new_node(vm, ND_NULL_EXPR, tok);

    Node *lhs = init_desg_expr(vm, desg, tok);
    return new_binary(vm, ND_ASSIGN, lhs, init->expr, tok);
}

// Generate initialization for VLA
// Unlike create_lvar_init which uses ty->array_len, VLAs have
// runtime-determined size We generate assignments based on the number of
// initializer elements (known at parse time)
static Node *create_vla_init(JCC *vm, Initializer *init, Type *ty, Obj *var,
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
static Node *lvar_initializer(JCC *vm, Token **rest, Token *tok, Obj *var) {
    Initializer *init = initializer(vm, rest, tok, var->ty, &var->ty);
    InitDesg desg = {NULL, 0, NULL, var};

    // If a partial initializer list is given, the standard requires
    // that unspecified elements are set to 0. Here, we simply
    // zero-initialize the entire memory region of a variable before
    // initializing it with user-supplied values.
    Node *lhs = new_node(vm, ND_MEMZERO, tok);
    lhs->var = var;

    Node *rhs = create_lvar_init(vm, init, var->ty, &desg, tok);
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

static Relocation *write_gvar_data(JCC *vm, Relocation *cur, Initializer *init,
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
static void gvar_initializer(JCC *vm, Token **rest, Token *tok, Obj *var) {
    Initializer *init = initializer(vm, rest, tok, var->ty, &var->ty);

    // For constexpr variables, save the initializer expression for compile-time
    // evaluation
    if (var->is_constexpr && init && init->expr) {
        var->init_expr = init->expr;
    }

    Relocation head = {};
    char *buf = arena_alloc(&vm->compiler.parser_arena, var->ty->size);
    memset(buf, 0, var->ty->size);
    write_gvar_data(vm, &head, init, var->ty, buf, 0);
    var->init_data = buf;
    var->rel = head.next;
}

// Returns true if a given token represents a type.
static bool is_typename(JCC *vm, Token *tok) {
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
        };

        for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
            hashmap_put_borrowed(&map, kw[i], (void *)1);
    }

    return hashmap_get2(&map, tok->loc, tok->len) || find_typedef(vm, tok);
}

// asm-stmt = "asm" ("volatile" | "inline")* "(" string-literal ")"
static Node *asm_stmt(JCC *vm, Token **rest, Token *tok) {
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
static Node *stmt(JCC *vm, Token **rest, Token *tok) {
    if (equal(tok, "_Static_assert") || equal(tok, "static_assert")) {
        tok = skip(vm, tok->next, "(");
        long long val = const_expr(vm, &tok, tok);
        tok = skip(vm, tok, ",");
        if (tok->kind != TK_STR)
            error_tok(vm, tok, "expected string literal, found '%.*s'",
                      tok->len, tok->loc);
        if (!val)
            error_tok(vm, tok, "%s", tok->str);
        tok = skip(vm, tok->next, ")");
        *rest = skip(vm, tok, ";");
        return new_node(vm, ND_BLOCK, tok);
    }

    if (equal(tok, "return")) {
        Node *node = new_node(vm, ND_RETURN, tok);
        if (consume(vm, rest, tok->next, ";")) {
            if (vm->compiler.current_fn) {
                Type *ty = vm->compiler.current_fn->ty->return_ty;
                if (ty->kind != TY_VOID) {
                    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
                        error_tok(vm, tok,
                                  "non-void aggregate function should return a value");
                    warn_tok(vm, tok, JCC_WARN_RETURN_TYPE,
                             "non-void function should return a value");
                    node->lhs = new_cast(vm, new_num(vm, 0, tok), ty);
                }
            }
            return node;
        }

        Node *exp = expr(vm, &tok, tok->next);
        *rest = skip(vm, tok, ";");

        add_type(vm, exp);
        // current_fn may be NULL when a _QUOTE template is parsed at file scope
        // (e.g. inside a top-level pragma macro call that uses _QUOTE("return x;")).
        // Guard the implicit return-type cast; types will be resolved by add_type
        // later, or by the caller establishing context via _AST_WITH_FN.
        if (vm->compiler.current_fn) {
            Type *ty = vm->compiler.current_fn->ty->return_ty;
            if (ty->kind == TY_VOID) {
                warn_tok(vm, node->tok, JCC_WARN_RETURN_TYPE,
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
        return node;
    }

    if (equal(tok, "switch")) {
        Node *node = new_node(vm, ND_SWITCH, tok);
        tok = skip(vm, tok->next, "(");
        node->cond = expr(vm, &tok, tok);
        tok = skip(vm, tok, ")");

        Node *sw = vm->compiler.current_switch;
        vm->compiler.current_switch = node;

        char *brk = vm->compiler.brk_label;
        vm->compiler.brk_label = node->brk_label = new_unique_name(vm);

        node->then = stmt(vm, rest, tok);

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
        node->lhs = stmt(vm, rest, tok);
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
        node->lhs = stmt(vm, rest, tok);
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

        if (is_typename(vm, tok)) {
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

    if (tok->kind == TK_IDENT && equal(tok->next, ":")) {
        Node *node = new_node(vm, ND_LABEL, tok);
        node->label = arena_strndup(vm, tok->loc, tok->len);
        node->unique_label = new_unique_name(vm);
        Token *body_tok = tok->next->next;
        body_tok = attribute_list(vm, body_tok, NULL, &label_attr);
        body_tok = c23_attribute_list(vm, body_tok, NULL, &label_attr);
        node->label_maybe_unused = label_attr.is_maybe_unused;
        node->lhs = stmt(vm, rest, body_tok);
        node->goto_next = vm->compiler.labels;
        vm->compiler.labels = node;
        return node;
    }

    if (equal(tok, "{"))
        return compound_stmt(vm, rest, tok->next);

    return expr_stmt(vm, rest, tok);
}

// compound-stmt = (typedef | declaration | stmt)* "}"
static Node *compound_stmt(JCC *vm, Token **rest, Token *tok) {
    Node *node = new_node(vm, ND_BLOCK, tok);
    Node head = {};
    Node *cur = &head;

    enter_scope(vm);

    bool seen_stmt = false;
    while (!equal(tok, "}")) {
        if (is_typename(vm, tok) && !equal(tok->next, ":")) {
            if (seen_stmt && vm->compiler.c_std < JCC_STD_C99)
                error_tok(vm, tok, "mixing declarations and code is not available before C99");
            VarAttr attr = {};
            Type *basety = declspec(vm, &tok, tok, &attr);

            if (attr.is_typedef) {
                tok = parse_typedef(vm, tok, basety);
                continue;
            }

            if (is_function(vm, tok)) {
                tok = function(vm, tok, basety, &attr);
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
    *rest = tok->next;
    return node;
}

// expr-stmt = expr? ";"
static Node *expr_stmt(JCC *vm, Token **rest, Token *tok) {
    if (equal(tok, ";")) {
        *rest = tok->next;
        return new_node(vm, ND_BLOCK, tok);
    }

    Node *node = new_node(vm, ND_EXPR_STMT, tok);
    node->lhs = expr(vm, &tok, tok);
    *rest = skip(vm, tok, ";");
    return node;
}

// expr = assign ("," expr)?
static Node *expr(JCC *vm, Token **rest, Token *tok) {
    Node *node = assign(vm, &tok, tok);

    if (equal(tok, ","))
        return new_binary(vm, ND_COMMA, node, expr(vm, rest, tok->next), tok);

    *rest = tok;
    return node;
}

static int64_t eval(JCC *vm, Node *node) { return eval2(vm, node, NULL); }

// Evaluate a given node as a constant expression.
//
// A constant expression is either just a number or ptr+n where ptr
// is a pointer to a global variable and n is a postiive/negative
// number. The latter form is accepted only as an initialization
// expression for a global variable.
static int64_t eval2(JCC *vm, Node *node, char ***label) {
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
        if (!label)
            error_tok(vm, node->tok,
                      "not a compile-time constant (member access)");
        if (node->ty->kind != TY_ARRAY)
            error_tok(vm, node->tok,
                      "invalid initializer (member is not an array)");
        return eval_rval(vm, node->lhs, label) + node->member->offset;
    case ND_VAR:
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

static int64_t eval_rval(JCC *vm, Node *node, char ***label) {
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

static bool is_const_expr(JCC *vm, Node *node) {
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
    default:
        return false;
    }
}

int64_t const_expr(JCC *vm, Token **rest, Token *tok) {
    Node *node = conditional(vm, rest, tok);
    return eval(vm, node);
}

// Returns true when `expr` is an integer constant expression whose value fits
// within the range of `to` without truncation.  Used to suppress -Wconversion
// false positives such as `char c = 0;` or `char c = 1 + 1;`.
bool node_int_const_fits(JCC *vm, Node *expr, Type *to) {
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

static double eval_double(JCC *vm, Node *node) {
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
static Node *to_assign(JCC *vm, Node *binary) {
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
static Node *assign(JCC *vm, Token **rest, Token *tok) {
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
static Node *conditional(JCC *vm, Token **rest, Token *tok) {
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
static Node *logor(JCC *vm, Token **rest, Token *tok) {
    Node *node = logand(vm, &tok, tok);
    while (equal(tok, "||")) {
        Token *start = tok;
        node =
            new_binary(vm, ND_LOGOR, node, logand(vm, &tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

// logand = bitor ("&&" bitor)*
static Node *logand(JCC *vm, Token **rest, Token *tok) {
    Node *node = bitor(vm, &tok, tok);
    while (equal(tok, "&&")) {
        Token *start = tok;
        node =
            new_binary(vm, ND_LOGAND, node, bitor(vm, &tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

// bitor = bitxor ("|" bitxor)*
static Node *bitor(JCC *vm, Token **rest, Token *tok) {
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
static Node *bitxor(JCC *vm, Token **rest, Token *tok) {
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
static Node *bitand(JCC *vm, Token **rest, Token *tok) {
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
static Node *equality(JCC *vm, Token **rest, Token *tok) {
    Node *node = relational(vm, &tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "==")) {
            node = new_binary(vm, ND_EQ, node, relational(vm, &tok, tok->next),
                              start);
            continue;
        }

        if (equal(tok, "!=")) {
            node = new_binary(vm, ND_NE, node, relational(vm, &tok, tok->next),
                              start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static Node *relational(JCC *vm, Token **rest, Token *tok) {
    Node *node = shift(vm, &tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "<")) {
            node =
                new_binary(vm, ND_LT, node, shift(vm, &tok, tok->next), start);
            continue;
        }

        if (equal(tok, "<=")) {
            node =
                new_binary(vm, ND_LE, node, shift(vm, &tok, tok->next), start);
            continue;
        }

        if (equal(tok, ">")) {
            node =
                new_binary(vm, ND_LT, shift(vm, &tok, tok->next), node, start);
            continue;
        }

        if (equal(tok, ">=")) {
            node =
                new_binary(vm, ND_LE, shift(vm, &tok, tok->next), node, start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// shift = add ("<<" add | ">>" add)*
static Node *shift(JCC *vm, Token **rest, Token *tok) {
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
static Node *new_add(JCC *vm, Node *lhs, Node *rhs, Token *tok) {
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
        warn_tok(vm, tok, JCC_WARN_POINTER_ARITH,
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
        warn_tok(vm, tok, JCC_WARN_POINTER_ARITH,
                 "pointer to a function used in arithmetic");

    // ptr + num
    rhs = new_binary(vm, ND_MUL, rhs,
                     new_long(vm, get_vm_size(lhs->ty->base), tok), tok);
    return new_binary(vm, ND_ADD, lhs, rhs, tok);
}

// Like `+`, `-` is overloaded for the pointer type.
static Node *new_sub(JCC *vm, Node *lhs, Node *rhs, Token *tok) {
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
            warn_tok(vm, tok, JCC_WARN_POINTER_ARITH,
                     "pointer of type 'void *' used in arithmetic");
        else if (lhs->ty->base->kind == TY_FUNC)
            warn_tok(vm, tok, JCC_WARN_POINTER_ARITH,
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
            warn_tok(vm, tok, JCC_WARN_POINTER_ARITH,
                     "pointer of type 'void *' used in arithmetic");
        else if (lhs->ty->base->kind == TY_FUNC || rhs->ty->base->kind == TY_FUNC)
            warn_tok(vm, tok, JCC_WARN_POINTER_ARITH,
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
static Node *add(JCC *vm, Token **rest, Token *tok) {
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
static Node *mul(JCC *vm, Token **rest, Token *tok) {
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
static Node *cast(JCC *vm, Token **rest, Token *tok) {
    if (equal(tok, "(") && is_typename(vm, tok->next)) {
        Token *start = tok;
        Type *ty = typename(vm, &tok, tok->next);
        tok = skip(vm, tok, ")");

        // compound literal
        if (equal(tok, "{"))
            return unary(vm, rest, start);

        // type cast
        Node *node = new_cast(vm, cast(vm, &tok, tok), ty);
        node->tok = start;
        *rest = tok;
        return node;
    }

    return unary(vm, rest, tok);
}

// ========== Block Literal Support (Apple Blocks Extension) ==========

// Recursively collect variables from outer scopes that are referenced in an
// expression
static void collect_captures_in_node(JCC *vm, Node *node, Obj *outer_locals,
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
}

// Parse a block literal: ^{ ... } or ^(params){ ... } or ^returntype(params){
// ... }
static Node *block_literal(JCC *vm, Token **rest, Token *tok) {
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

                Type *param_ty = declspec(vm, &tok, tok, NULL);
                param_ty = declarator(vm, &tok, tok, param_ty);

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
    block_fn->body = compound_stmt(vm, &tok, tok);
    block_fn->locals = vm->compiler.locals;

    leave_scope(vm);

    // Collect captured variables from the parsed body
    // Use saved_locals (the outer function's locals list at the time we
    // started)
    Obj **captures = NULL;
    int num_captures = 0, cap_capacity = 0;

    if (saved_locals) {
        collect_captures_in_node(vm, block_fn->body, saved_locals, &captures,
                                 &num_captures, &cap_capacity);
    }

    block_fn->captures = captures;
    block_fn->num_captures = num_captures;

    // Store capture offsets - each captured variable will be at a known offset
    // in descriptor Descriptor layout: [0]=invoke_ptr, [8]=cap0, [16]=cap1, ...
    for (int i = 0; i < num_captures; i++) {
        captures[i]->block_capture_offset = (i + 1) * 8; // offset in descriptor
    }

    // Restore outer function context
    vm->compiler.current_fn = outer_fn;
    vm->compiler.locals = saved_locals;

    // Create the block literal node
    Node *node = new_node(vm, ND_BLOCK_LITERAL, start);
    node->block_fn = block_fn;
    node->block_captures = captures;
    node->num_block_captures = num_captures;

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
static Node *unary(JCC *vm, Token **rest, Token *tok) {
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
static void struct_members(JCC *vm, Token **rest, Token *tok, Type *ty) {
    Member head = {};
    Member *cur = &head;
    int idx = 0;

    while (!equal(tok, "}")) {
        VarAttr attr = {};
        Type *basety = declspec(vm, &tok, tok, &attr);
        bool first = true;

        // Anonymous struct member
        Token *anon_tok = tok;
        if ((basety->kind == TY_STRUCT || basety->kind == TY_UNION) &&
            consume(vm, &tok, tok, ";")) {
            if (vm->compiler.c_std < JCC_STD_C11)
                error_tok(vm, anon_tok, "anonymous structs/unions are not available before C11");
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
        if (vm->compiler.c_std < JCC_STD_C99)
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
                                bool unused, bool deprecated, char *message) {
    if (ty) {
        ty->is_maybe_unused |= unused;
        ty->is_deprecated |= deprecated;
        if (message)
            ty->deprecated_msg = message;
    }
    if (attr) {
        attr->is_maybe_unused |= unused;
        attr->is_deprecated |= deprecated;
        if (message)
            attr->deprecated_msg = message;
        if (unused || deprecated)
            attr->attribute_tok = tok;
    }
}

static Type *apply_var_attrs_to_type(JCC *vm, Type *ty, VarAttr *attr) {
    if (!attr || (!attr->is_maybe_unused && !attr->is_deprecated))
        return ty;
    ty = copy_type(vm, ty);
    apply_semantic_attr(ty, NULL, attr->attribute_tok, attr->is_maybe_unused,
                        attr->is_deprecated, attr->deprecated_msg);
    return ty;
}

static void inherit_semantic_attrs(Type *dst, Type *src) {
    if (!dst || !src)
        return;
    dst->is_maybe_unused |= src->is_maybe_unused;
    dst->is_deprecated |= src->is_deprecated;
    if (!dst->deprecated_msg)
        dst->deprecated_msg = src->deprecated_msg;
}

// attribute = ("__attribute__" "(" "(" attribute-list ")" ")")*
static Token *attribute_list(JCC *vm, Token *tok, Type *ty, VarAttr *attr) {
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
                apply_semantic_attr(ty, attr, attr_tok, unused, deprecated,
                                    message);
                continue;
            }

            // Handle all other attributes - just consume and ignore them
            if (tok->kind == TK_IDENT) {
                tok = tok->next;

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
static Token *c23_attribute_list(JCC *vm, Token *tok, Type *ty,
                                 VarAttr *attr) {
    while (equal(tok, "[") && equal(tok->next, "[")) {
        if (vm->compiler.c_std < JCC_STD_C23 &&
            !vm->compiler.in_type_lookahead)
            warn_tok(vm, tok, JCC_WARN_PEDANTIC,
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
            bool unused = equal(tok, "maybe_unused");
            bool deprecated = equal(tok, "deprecated");
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
            apply_semantic_attr(ty, attr, attr_tok, unused, deprecated,
                                message);
        }

        tok = skip(vm, tok, "]");
        tok = skip(vm, tok, "]");
    }

    return tok;
}

// struct-union-decl = attribute? ident? ("{" struct-members)?
static Type *struct_union_decl(JCC *vm, Token **rest, Token *tok) {
    Type *ty = struct_type(vm);
    tok = attribute_list(vm, tok, ty, NULL);
    tok = c23_attribute_list(vm, tok, ty, NULL);

    // Read a tag.
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        tok = tok->next;
    }

    if (tag && !equal(tok, "{")) {
        *rest = tok;

        Type *ty2 = find_tag(vm, tag);
        if (ty2) {
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

    if (tag) {
        // If this is a redefinition, overwrite a previous type.
        // Otherwise, register the struct type.
        // Linear search in current scope only
        Type *ty2 = NULL;
        for (TagScopeNode *node = vm->compiler.scope->tags; node;
             node = node->next) {
            if (node->name_len == tag->len &&
                strncmp(node->name, tag->loc, tag->len) == 0) {
                ty2 = node->ty;
                break;
            }
        }
        if (ty2) {
            *ty2 = *ty;
            return ty2;
        }

        push_tag_scope(vm, tag, ty);
    }

    return ty;
}

// struct-decl = struct-union-decl
static Type *struct_decl(JCC *vm, Token **rest, Token *tok) {
    Type *ty = struct_union_decl(vm, rest, tok);
    ty->kind = TY_STRUCT;

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
    return ty;
}

// union-decl = struct-union-decl
static Type *union_decl(JCC *vm, Token **rest, Token *tok) {
    Type *ty = struct_union_decl(vm, rest, tok);
    ty->kind = TY_UNION;

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
    return ty;
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
static Node *struct_ref(JCC *vm, Node *node, Token *tok) {
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
static Node *new_inc_dec(JCC *vm, Node *node, Token *tok, int addend) {
    add_type(vm, node);
    return new_cast(
        vm,
        new_add(vm,
                to_assign(vm, new_add(vm, node, new_num(vm, addend, tok), tok)),
                new_num(vm, -addend, tok), tok),
        node->ty);
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
static Node *postfix(JCC *vm, Token **rest, Token *tok) {
    if (equal(tok, "(") && is_typename(vm, tok->next)) {
        // Compound literal
        Token *start = tok;
        if (vm->compiler.c_std < JCC_STD_C99)
            error_tok(vm, start, "compound literals are not available before C99");
        Type *ty = typename(vm, &tok, tok->next);
        tok = skip(vm, tok, ")");

        if (vm->compiler.scope->next == NULL) {
            Obj *var = new_anon_gvar(vm, ty);
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

// funcall = (assign ("," assign)*)? ")"
static Node *funcall(JCC *vm, Token **rest, Token *tok, Node *fn) {
    add_type(vm, fn);

    if (fn->ty->kind != TY_FUNC &&
        (fn->ty->kind != TY_PTR || fn->ty->base->kind != TY_FUNC))
        error_tok(vm, fn->tok, "not a function");

    Type *ty = (fn->ty->kind == TY_FUNC) ? fn->ty : fn->ty->base;
    Type *param_ty = ty->params;

    Node head = {};
    Node *cur = &head;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(vm, tok, ",");

        Node *arg = assign(vm, &tok, tok);
        add_type(vm, arg);

        if (!param_ty && !ty->is_variadic) {
            if (vm->collect_errors &&
                error_tok_recover(vm, tok, "too many arguments")) {
                // Continue parsing to find more errors, but don't add this arg
                continue;
            }
            error_tok(vm, tok, "too many arguments");
        }

        if (param_ty) {
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

        cur = cur->next = arg;
    }

    if (param_ty) {
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

    *rest = skip(vm, tok, ")");

    Node *node = new_unary(vm, ND_FUNCALL, fn, tok);
    node->func_ty = ty;
    node->ty = ty->return_ty;
    node->args = head.next;

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
static Node *generic_selection(JCC *vm, Token **rest, Token *tok) {
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
static Node *primary(JCC *vm, Token **rest, Token *tok) {
    Token *start = tok;

    if (equal(tok, "(") && equal(tok->next, "{")) {
        // This is a GNU statement expresssion.
        Node *node = new_node(vm, ND_STMT_EXPR, tok);
        node->body = compound_stmt(vm, &tok, tok->next->next)->body;
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

        return new_ulong(vm, ty->size, start);
    }

    if (equal(tok, "sizeof")) {
        Node *node = unary(vm, rest, tok->next);
        add_type(vm, node);
        if (node->ty->kind == TY_VLA)
            return new_var_node(vm, node->ty->vla_size, tok);
        return new_ulong(vm, node->ty->size, tok);
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
        if (vm->compiler.c_std < JCC_STD_C11)
            error_tok(vm, tok, "'_Generic' is not available before C11");
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

    if (equal(tok, "__jcc_cmplx") || equal(tok, "__jcc_cmplxf") ||
        equal(tok, "__jcc_cmplxl")) {
        Type *ty = equal(tok, "__jcc_cmplxf") ? ty_fcomplex :
                   equal(tok, "__jcc_cmplxl") ? ty_ldcomplex : ty_dcomplex;
        tok = skip(vm, tok->next, "(");
        Node *real = assign(vm, &tok, tok);
        tok = skip(vm, tok, ",");
        Node *imag = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        return new_complex_node(vm, real, imag, ty, start);
    }

    if (equal(tok, "__jcc_creal") || equal(tok, "__jcc_crealf") ||
        equal(tok, "__jcc_creall") || equal(tok, "__jcc_cimag") ||
        equal(tok, "__jcc_cimagf") || equal(tok, "__jcc_cimagl")) {
        bool imag_part = equal(tok, "__jcc_cimag") || equal(tok, "__jcc_cimagf") ||
                         equal(tok, "__jcc_cimagl");
        Type *ret_ty = (equal(tok, "__jcc_crealf") || equal(tok, "__jcc_cimagf")) ? ty_float :
                       (equal(tok, "__jcc_creall") || equal(tok, "__jcc_cimagl")) ? ty_ldouble :
                       ty_double;
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        Node *node = new_unary(vm, ND_COMPLEX, arg, start);
        node->val = imag_part ? 2 : 1;
        node->ty = ret_ty;
        return node;
    }

    if (equal(tok, "__jcc_conj") || equal(tok, "__jcc_conjf") ||
        equal(tok, "__jcc_conjl")) {
        Type *ty = equal(tok, "__jcc_conjf") ? ty_fcomplex :
                   equal(tok, "__jcc_conjl") ? ty_ldcomplex : ty_dcomplex;
        tok = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        Node *node = new_unary(vm, ND_COMPLEX, arg, start);
        node->val = 3;
        node->ty = ty;
        return node;
    }

    // Block_copy(block) - Apple Blocks extension
    // In JCC's simplified model, blocks are already heap-allocated, so this
    // just returns the block
    if (equal(tok, "Block_copy")) {
        tok = skip(vm, tok->next, "(");
        Node *block_expr = assign(vm, &tok, tok);
        *rest = skip(vm, tok, ")");
        // Simply return the block expression as-is (already has TY_BLOCK type
        // or void*)
        add_type(vm, block_expr);
        return block_expr;
    }

    // Block_release(block) - Apple Blocks extension
    // In JCC's simplified model, this is a no-op (VM teardown handles cleanup)
    if (equal(tok, "Block_release")) {
        tok = skip(vm, tok->next, "(");
        Node *block_expr = assign(vm, &tok, tok); // Evaluate but discard
        *rest = skip(vm, tok, ")");
        // Return a null expression (void, no effect)
        add_type(vm, block_expr);
        Node *node = new_node(vm, ND_NULL_EXPR, start);
        node->ty = ty_void;
        return node;
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
                return new_num(vm, sc->enum_val, tok);
            }
        }

        // Check if this is a macro call. When parsing macro bytecode itself,
        // keep calls as ordinary C function calls so macros can call each
        // other directly.
        if (!vm->compiler.in_macro_mode && equal(tok->next, "(")) {
            MacroFn *pm = find_macro_fn(vm, tok);
            if (pm) {
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
            warn_tok(vm, tok, JCC_WARN_IMPLICIT_FUNCTION_DECLARATION,
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

static Token *parse_typedef(JCC *vm, Token *tok, Type *basety) {
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
    }
    return tok;
}

static void create_param_lvars(JCC *vm, Type *param) {
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
static void resolve_goto_labels(JCC *vm) {
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
            warn_tok(vm, label->tok, JCC_WARN_UNUSED, "unused label '%s'",
                     label->label);

    vm->compiler.gotos = vm->compiler.labels = NULL;
}

static Obj *find_func(JCC *vm, char *name, int name_len) {
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

static Obj *find_func_in_current_scope(JCC *vm, char *name, int name_len) {
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

static void mark_live(JCC *vm, Obj *var) {
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

static void append_implicit_return(JCC *vm, Obj *fn, Token *tok) {
    Type *ty = fn->ty->return_ty;
    if (ty->kind == TY_VOID || strcmp(fn->name, "main") == 0 ||
        statement_terminates(fn->body))
        return;

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
        error_tok(vm, tok, "control reaches end of non-void aggregate function");

    warn_tok(vm, tok, JCC_WARN_RETURN_TYPE,
             "control reaches end of non-void function");

    Node *ret = new_node(vm, ND_RETURN, tok);
    ret->lhs = new_cast(vm, new_num(vm, 0, tok), ty);

    Node **cur = &fn->body->body;
    while (*cur)
        cur = &(*cur)->next;
    *cur = ret;
}

static Token *function(JCC *vm, Token *tok, Type *basety, VarAttr *attr) {
    Type *ty = declarator(vm, &tok, tok, basety);
    if (!ty->name)
        error_tok(vm, ty->name_pos, "function name omitted");
    char *name_str = get_ident(vm, ty->name);

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
        if (ty->deprecated_msg)
            fn->deprecated_msg = ty->deprecated_msg;
    } else {
        fn = new_gvar(vm, name_str, ty->name->len, ty);
        fn->is_function = true;
        fn->is_definition = equal(tok, "{");
        fn->is_static =
            attr->is_static || (attr->is_inline && !attr->is_extern);
        fn->is_inline = attr->is_inline;
        fn->is_constexpr = attr->is_constexpr;
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

    if (consume(vm, &tok, tok, ";"))
        return tok;

    vm->compiler.current_fn = fn;
    vm->compiler.locals = NULL;
    if (is_nested)
        vm->compiler.fn_nesting_depth++;

    enter_scope(vm);

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
    if (vm->compiler.c_std >= JCC_STD_C99) {
        push_scope(vm, "__func__", 8)->var = new_string_literal(
            vm, fn->name, array_of(vm, ty_char, strlen(fn->name) + 1));

        // [GNU] __FUNCTION__ is yet another name of __func__.
        push_scope(vm, "__FUNCTION__", 12)->var = new_string_literal(
            vm, fn->name, array_of(vm, ty_char, strlen(fn->name) + 1));
    }

    fn->body = compound_stmt(vm, &tok, tok);
    append_implicit_return(vm, fn, ty->name);
    fn->locals = vm->compiler.locals;
    leave_scope(vm);
    resolve_goto_labels(vm);

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

    return tok;
}

static Token *global_variable(JCC *vm, Token *tok, Type *basety,
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

        if (equal(tok, "="))
            gvar_initializer(vm, &tok, tok->next, var);
        else if (!attr->is_extern && !attr->is_tls)
            var->is_tentative = true;
    }
    return tok;
}

// Lookahead tokens and returns true if a given token is a start
// of a function definition or declaration.
static bool is_function(JCC *vm, Token *tok) {
    if (equal(tok, ";"))
        return false;

    Type dummy = {};
    bool saved_lookahead = vm->compiler.in_type_lookahead;
    vm->compiler.in_type_lookahead = true;
    Type *ty = declarator(vm, &tok, tok, &dummy);
    vm->compiler.in_type_lookahead = saved_lookahead;
    return ty->kind == TY_FUNC;
}

// Remove redundant tentative definitions.
static void scan_globals(JCC *vm) {
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

static void warn_unused_globals(JCC *vm) {
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

        warn_tok(vm, var->tok, JCC_WARN_UNUSED, "unused %s '%s'",
                 var->is_function ? "function" : "variable",
                 obj_display_name(var));
    }
}

static void declare_builtin_functions(JCC *vm) {
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
}

// program = (typedef | function-definition | global-variable)*
Obj *parse(JCC *vm, Token *tok) {
    // Initialize error recovery placeholder
    error_var->ty = ty_error;

    // Initialize global scope
    enter_scope(vm);

    declare_builtin_functions(vm);
    vm->compiler.globals = NULL;

    while (tok->kind != TK_EOF) {
        // _Static_assert or static_assert (C23) - check before declspec
        if (equal(tok, "_Static_assert") || equal(tok, "static_assert")) {
            tok = skip(vm, tok->next, "(");
            long long val = const_expr(vm, &tok, tok);
            tok = skip(vm, tok, ",");
            if (tok->kind != TK_STR)
                error_tok(vm, tok, "expected string literal, found '%.*s'",
                          tok->len, tok->loc);
            if (!val)
                error_tok(vm, tok, "%s", tok->str);
            tok = skip(vm, tok->next, ")");
            tok = skip(vm, tok, ";");
            continue;
        }

        // A known macro function may be called as a file-scope compile-time
        // directive. The returned node is ignored; side effects such as
        // generated declarations/functions are kept in the active parse state.
        if (!vm->compiler.in_macro_mode && tok->kind == TK_IDENT &&
            equal(tok->next, "(")) {
            MacroFn *pm = find_macro_fn(vm, tok);
            if (pm) {
                Token *macro_tok = tok;
                tok = tok->next->next;

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

                tok = skip(vm, tok, ")");
                tok = skip(vm, tok, ";");

                cc_execute_top_level_macro(vm, pm->name, macro_tok,
                                           head.next, arg_count);
                continue;
            }
        }

        VarAttr attr = {};
        Type *basety = declspec(vm, &tok, tok, &attr);

        // Typedef
        if (attr.is_typedef) {
            tok = parse_typedef(vm, tok, basety);
            continue;
        }

        // Function
        if (is_function(vm, tok)) {
            tok = function(vm, tok, basety, &attr);
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
Node *cc_parse_expr(JCC *vm, Token **rest, Token *tok) {
    return expr(vm, rest, tok);
}

Node *cc_parse_assign(JCC *vm, Token **rest, Token *tok) {
    return assign(vm, rest, tok);
}

Node *cc_parse_stmt(JCC *vm, Token **rest, Token *tok) {
    return stmt(vm, rest, tok);
}

Node *cc_parse_compound_stmt(JCC *vm, Token **rest, Token *tok) {
    return compound_stmt(vm, rest, tok);
}

void cc_init_parser(JCC *vm) { error_var->ty = ty_error; }
