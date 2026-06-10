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

// AST to source code serialization
// Converts AST nodes back to C source text for -M pragma macro expansion output

#include "./internal.h"
#ifndef _WIN32
#include <fnmatch.h>
#endif

// Operator precedence (higher = binds tighter)
static int get_precedence(NodeKind kind) {
    switch (kind) {
    case ND_COMMA:
        return 1;
    case ND_ASSIGN:
        return 2;
    case ND_COND:
        return 3;
    case ND_LOGOR:
        return 4;
    case ND_LOGAND:
        return 5;
    case ND_BITOR:
        return 6;
    case ND_BITXOR:
        return 7;
    case ND_BITAND:
        return 8;
    case ND_EQ:
    case ND_NE:
        return 9;
    case ND_LT:
    case ND_LE:
        return 10;
    case ND_SHL:
    case ND_SHR:
        return 11;
    case ND_ADD:
    case ND_SUB:
        return 12;
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
        return 13;
    case ND_NEG:
    case ND_NOT:
    case ND_BITNOT:
    case ND_ADDR:
    case ND_DEREF:
    case ND_CAST:
        return 14;
    case ND_FUNCALL:
    case ND_MEMBER:
        return 15;
    default:
        return 16;
    }
}

// Get operator string for binary operations
static const char *get_binary_op_str(NodeKind kind) {
    switch (kind) {
    case ND_ADD:
        return "+";
    case ND_SUB:
        return "-";
    case ND_MUL:
        return "*";
    case ND_DIV:
        return "/";
    case ND_MOD:
        return "%";
    case ND_BITAND:
        return "&";
    case ND_BITOR:
        return "|";
    case ND_BITXOR:
        return "^";
    case ND_SHL:
        return "<<";
    case ND_SHR:
        return ">>";
    case ND_EQ:
        return "==";
    case ND_NE:
        return "!=";
    case ND_LT:
        return "<";
    case ND_LE:
        return "<=";
    case ND_LOGAND:
        return "&&";
    case ND_LOGOR:
        return "||";
    case ND_ASSIGN:
        return "=";
    case ND_COMMA:
        return ",";
    default:
        return "?";
    }
}

// Get operator string for unary operations
static const char *get_unary_op_str(NodeKind kind) {
    switch (kind) {
    case ND_NEG:
        return "-";
    case ND_NOT:
        return "!";
    case ND_BITNOT:
        return "~";
    case ND_ADDR:
        return "&";
    case ND_DEREF:
        return "*";
    default:
        return "?";
    }
}

typedef struct {
    Type **data;
    int len;
    int cap;
} TypeVec;

typedef struct {
    Type *ty;
    char *name;
    int name_len;
    Obj *owner_fn;
} TypeName;

typedef struct {
    TypeVec seen;
    TypeVec defs;
    TypeName *tags;
    int tags_len;
    int tags_cap;
    TypeName *typedefs;
    int typedefs_len;
    int typedefs_cap;
    Obj *current_fn;
    bool generated_only; // skip header typedefs; output is consumed alongside normal headers
    int anon_local_counter; // names compiler-synthesized temps (e.g. ++/-- desugaring)
} SerializeContext;

// Forward declaration
static void serialize_expr(FILE *f, CCCC *vm, SerializeContext *ctx, Node *node,
                           int parent_prec);
static void serialize_stmt(FILE *f, CCCC *vm, SerializeContext *ctx, Node *node,
                           int indent);

// Returns true if the node produces no output (effectively a no-op expression).
static bool is_noop_expr(Node *node) {
    if (!node) return true;
    if (node->kind == ND_NULL_EXPR) return true;
    if (node->kind == ND_COMMA)
        return is_noop_expr(node->lhs) && is_noop_expr(node->rhs);
    return false;
}
static void serialize_type_defs_for_owner(FILE *f, SerializeContext *ctx,
                                          Obj *owner_fn);
static bool type_has_tag_for_owner(SerializeContext *ctx, Type *ty,
                                   Obj *owner_fn);

static bool same_type_or_origin(Type *a, Type *b) {
    for (Type *pa = a; pa; pa = pa->origin)
        for (Type *pb = b; pb; pb = pb->origin)
            if (pa == pb)
                return true;

    if (a && b && a->kind == b->kind &&
        (a->kind == TY_STRUCT || a->kind == TY_UNION)) {
        Member *ma = a->members;
        Member *mb = b->members;
        for (; ma && mb; ma = ma->next, mb = mb->next) {
            if ((ma->name == NULL) != (mb->name == NULL))
                return false;
            if (ma->name && (ma->name->len != mb->name->len ||
                             strncmp(ma->name->loc, mb->name->loc,
                                     ma->name->len) != 0))
                return false;
            if (!same_type_or_origin(ma->ty, mb->ty))
                return false;
        }
        return ma == NULL && mb == NULL;
    }

    return false;
}

static bool type_vec_contains(TypeVec *vec, Type *ty) {
    for (int i = 0; i < vec->len; i++)
        if (same_type_or_origin(vec->data[i], ty))
            return true;
    return false;
}

static void type_vec_push(TypeVec *vec, Type *ty) {
    if (!ty || type_vec_contains(vec, ty))
        return;

    if (vec->len >= vec->cap) {
        vec->cap = vec->cap ? vec->cap * 2 : 16;
        vec->data = realloc(vec->data, sizeof(Type *) * vec->cap);
    }
    vec->data[vec->len++] = ty;
}

static void type_name_push(TypeName **items, int *len, int *cap, Type *ty,
                           char *name, int name_len, Obj *owner_fn) {
    if (!ty || !name || name_len <= 0)
        return;

    if (*len >= *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *items = realloc(*items, sizeof(TypeName) * *cap);
    }

    (*items)[*len].ty = ty;
    (*items)[*len].name = name;
    (*items)[*len].name_len = name_len;
    (*items)[*len].owner_fn = owner_fn;
    (*len)++;
}

static void collect_scope_names(SerializeContext *ctx, CCCC *vm) {
    for (TypeNameRecord *rec = vm->compiler.type_names; rec; rec = rec->next) {
        if (rec->is_tag)
            type_name_push(&ctx->tags, &ctx->tags_len, &ctx->tags_cap, rec->ty,
                           rec->name, rec->name_len, rec->owner_fn);
        else
            type_name_push(&ctx->typedefs, &ctx->typedefs_len,
                           &ctx->typedefs_cap, rec->ty, rec->name,
                           rec->name_len, rec->owner_fn);
    }
}

static bool name_visible(TypeName *name, Obj *fn) {
    return name->owner_fn == NULL || name->owner_fn == fn;
}

static TypeName *find_tag_name(SerializeContext *ctx, Type *ty) {
    if (!ctx || !ty)
        return NULL;

    for (int i = 0; i < ctx->tags_len; i++)
        if (name_visible(&ctx->tags[i], ctx->current_fn) &&
            same_type_or_origin(ctx->tags[i].ty, ty))
            return &ctx->tags[i];
    return NULL;
}

static TypeName *find_typedef_name(SerializeContext *ctx, Type *ty) {
    if (!ctx || !ty)
        return NULL;

    for (int i = 0; i < ctx->typedefs_len; i++)
        if (name_visible(&ctx->typedefs[i], ctx->current_fn) &&
            same_type_or_origin(ctx->typedefs[i].ty, ty))
            return &ctx->typedefs[i];
    return NULL;
}

static TypeName *find_anonymous_typedef_name(SerializeContext *ctx, Type *ty) {
    if (!ctx || !ty)
        return NULL;
    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION && ty->kind != TY_ENUM)
        return NULL;

    for (int i = 0; i < ctx->typedefs_len; i++) {
        TypeName *name = &ctx->typedefs[i];
        if (!name_visible(name, ctx->current_fn) || !name->ty ||
            name->ty->kind != ty->kind)
            continue;
        if (!type_has_tag_for_owner(ctx, name->ty, name->owner_fn))
            return name;
    }
    return NULL;
}

static Obj *type_decl_owner(SerializeContext *ctx, Type *ty) {
    for (int i = 0; i < ctx->tags_len; i++)
        if (same_type_or_origin(ctx->tags[i].ty, ty))
            return ctx->tags[i].owner_fn;
    for (int i = 0; i < ctx->typedefs_len; i++)
        if (same_type_or_origin(ctx->typedefs[i].ty, ty))
            return ctx->typedefs[i].owner_fn;
    return NULL;
}

static void collect_type(SerializeContext *ctx, Type *ty);

static void collect_node_types(SerializeContext *ctx, Node *node) {
    if (!node)
        return;

    collect_type(ctx, node->ty);
    if (node->var)
        collect_type(ctx, node->var->ty);
    if (node->member)
        collect_type(ctx, node->member->ty);
    if (node->func_ty)
        collect_type(ctx, node->func_ty);

    if (node->kind == ND_SWITCH) {
        collect_node_types(ctx, node->cond);
        for (Node *c = node->case_next; c; c = c->case_next)
            collect_node_types(ctx, c->lhs);
        if (node->default_case)
            collect_node_types(ctx, node->default_case->lhs);
        collect_node_types(ctx, node->next);
        return;
    }

    if (node->kind == ND_CASE) {
        collect_node_types(ctx, node->lhs);
        collect_node_types(ctx, node->next);
        return;
    }

    collect_node_types(ctx, node->lhs);
    collect_node_types(ctx, node->rhs);
    collect_node_types(ctx, node->cond);
    collect_node_types(ctx, node->then);
    collect_node_types(ctx, node->els);
    collect_node_types(ctx, node->init);
    collect_node_types(ctx, node->inc);
    collect_node_types(ctx, node->body);
    collect_node_types(ctx, node->args);

    collect_node_types(ctx, node->next);
}

static void collect_type(SerializeContext *ctx, Type *ty) {
    if (!ty) {
        return;
    }

    if (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_VLA) {
        collect_type(ctx, ty->base);
        return;
    }

    if (ty->kind == TY_FUNC) {
        collect_type(ctx, ty->return_ty);
        for (Type *p = ty->params; p; p = p->next)
            collect_type(ctx, p);
        return;
    }

    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION && ty->kind != TY_ENUM)
        return;

    if (type_vec_contains(&ctx->seen, ty))
        return;
    type_vec_push(&ctx->seen, ty);

    for (Member *m = ty->members; m; m = m->next)
        collect_type(ctx, m->ty);

    type_vec_push(&ctx->defs, ty);
}

static void collect_obj_types(SerializeContext *ctx, Obj *obj) {
    collect_type(ctx, obj->ty);
    collect_node_types(ctx, obj->init_expr);

    for (Obj *param = obj->params; param; param = param->next)
        collect_type(ctx, param->ty);
    for (Obj *local = obj->locals; local; local = local->next)
        collect_type(ctx, local->ty);
    collect_node_types(ctx, obj->body);
}

static void serialize_type(FILE *f, SerializeContext *ctx, Type *ty);

static void serialize_type_decl(FILE *f, SerializeContext *ctx, Type *ty,
                                const char *name) {
    if (!ty) {
        fprintf(f, "void");
        if (name && *name)
            fprintf(f, " %s", name);
        return;
    }

    if (ty->kind == TY_ARRAY) {
        char buf[1024];
        if (ty->array_len < 0)
            snprintf(buf, sizeof(buf), "%s[]", name ? name : "");
        else
            snprintf(buf, sizeof(buf), "%s[%d]", name ? name : "",
                     ty->array_len);
        serialize_type_decl(f, ctx, ty->base, buf);
        return;
    }

    if (ty->kind == TY_PTR) {
        char buf[1024];
        if (ty->base &&
            (ty->base->kind == TY_ARRAY || ty->base->kind == TY_FUNC))
            snprintf(buf, sizeof(buf), "(*%s)", name ? name : "");
        else
            snprintf(buf, sizeof(buf), "*%s", name ? name : "");
        serialize_type_decl(f, ctx, ty->base, buf);
        return;
    }

    if (ty->kind == TY_FUNC) {
        serialize_type(f, ctx, ty->return_ty);
        if (name && *name)
            fprintf(f, " %s", name);
        fprintf(f, "(");
        bool first = true;
        for (Type *p = ty->params; p; p = p->next) {
            if (!first)
                fprintf(f, ", ");
            first = false;
            serialize_type(f, ctx, p);
        }
        if (ty->is_variadic) {
            if (!first)
                fprintf(f, ", ");
            fprintf(f, "...");
        } else if (first) {
            fprintf(f, "void");
        }
        fprintf(f, ")");
        return;
    }

    serialize_type(f, ctx, ty);
    if (name && *name)
        fprintf(f, " %s", name);
}

// Serialize the body of a struct/union with no tag and no typedef alias
// (e.g. `struct { int x; int y; } pt;`) inline at its point of use, since
// there is no name to refer back to it by elsewhere.
static void serialize_anon_aggregate(FILE *f, SerializeContext *ctx, Type *ty) {
    fprintf(f, "%s {\n", ty->kind == TY_UNION ? "union" : "struct");
    for (Member *m = ty->members; m; m = m->next) {
        fprintf(f, "    ");
        char name[256] = "";
        if (m->name) {
            int len = m->name->len;
            if (len >= (int)sizeof(name))
                len = sizeof(name) - 1;
            memcpy(name, m->name->loc, len);
            name[len] = '\0';
        }
        serialize_type_decl(f, ctx, m->ty, name);
        if (m->is_bitfield)
            fprintf(f, " : %d", m->bit_width);
        fprintf(f, ";\n");
    }
    fprintf(f, "}");
}

// Serialize type to string
static void serialize_type(FILE *f, SerializeContext *ctx, Type *ty) {
    if (!ty) {
        fprintf(f, "void");
        return;
    }

    if (ty->is_const)
        fprintf(f, "const ");

    switch (ty->kind) {
    case TY_VOID:
        fprintf(f, "void");
        break;
    case TY_BOOL:
        fprintf(f, "_Bool");
        break;
    case TY_CHAR:
        fprintf(f, "%schar", ty->is_unsigned ? "unsigned " : "");
        break;
    case TY_SHORT:
        fprintf(f, "%sshort", ty->is_unsigned ? "unsigned " : "");
        break;
    case TY_INT:
        fprintf(f, "%sint", ty->is_unsigned ? "unsigned " : "");
        break;
    case TY_LONG:
        fprintf(f, "%slong", ty->is_unsigned ? "unsigned " : "");
        break;
    case TY_FLOAT:
        fprintf(f, "float");
        break;
    case TY_DOUBLE:
        fprintf(f, "double");
        break;
    case TY_LDOUBLE:
        fprintf(f, "long double");
        break;
    case TY_PTR:
        serialize_type_decl(f, ctx, ty, "");
        break;
    case TY_ARRAY:
        serialize_type_decl(f, ctx, ty, "");
        break;
    case TY_STRUCT: {
        TypeName *tag = find_tag_name(ctx, ty);
        TypeName *alias = find_typedef_name(ctx, ty);
        if (tag)
            fprintf(f, "struct %.*s", tag->name_len, tag->name);
        else if (alias)
            fprintf(f, "%.*s", alias->name_len, alias->name);
        else if ((alias = find_anonymous_typedef_name(ctx, ty)))
            fprintf(f, "%.*s", alias->name_len, alias->name);
        else
            serialize_anon_aggregate(f, ctx, ty);
        break;
    }
    case TY_UNION: {
        TypeName *tag = find_tag_name(ctx, ty);
        TypeName *alias = find_typedef_name(ctx, ty);
        if (tag)
            fprintf(f, "union %.*s", tag->name_len, tag->name);
        else if (alias)
            fprintf(f, "%.*s", alias->name_len, alias->name);
        else if ((alias = find_anonymous_typedef_name(ctx, ty)))
            fprintf(f, "%.*s", alias->name_len, alias->name);
        else
            serialize_anon_aggregate(f, ctx, ty);
        break;
    }
    case TY_ENUM: {
        TypeName *tag = find_tag_name(ctx, ty);
        TypeName *alias = find_typedef_name(ctx, ty);
        if (tag)
            fprintf(f, "enum %.*s", tag->name_len, tag->name);
        else if (alias)
            fprintf(f, "%.*s", alias->name_len, alias->name);
        else if ((alias = find_anonymous_typedef_name(ctx, ty)))
            fprintf(f, "%.*s", alias->name_len, alias->name);
        else
            fprintf(f, "int");
        break;
    }
    case TY_FUNC:
        serialize_type_decl(f, ctx, ty, "");
        break;
    default:
        fprintf(f, "/* unknown type */");
        break;
    }
}

// Print escaped string literal
static void serialize_string(FILE *f, const char *str) {
    fprintf(f, "\"");
    for (const char *p = str; *p; p++) {
        switch (*p) {
        case '\n':
            fprintf(f, "\\n");
            break;
        case '\r':
            fprintf(f, "\\r");
            break;
        case '\t':
            fprintf(f, "\\t");
            break;
        case '\\':
            fprintf(f, "\\\\");
            break;
        case '"':
            fprintf(f, "\\\"");
            break;
        case '\0':
            fprintf(f, "\\0");
            break;
        default:
            if (*p >= 32 && *p < 127)
                fputc(*p, f);
            else
                fprintf(f, "\\x%02x", (unsigned char)*p);
            break;
        }
    }
    fprintf(f, "\"");
}

// Print indentation
static void print_indent_level(FILE *f, int indent) {
    for (int i = 0; i < indent; i++)
        fprintf(f, "    ");
}

// Serialize an expression
static void serialize_expr(FILE *f, CCCC *vm, SerializeContext *ctx, Node *node,
                           int parent_prec) {
    (void)vm; // May be used later

    if (!node) {
        fprintf(f, "/* NULL */");
        return;
    }

    int node_prec = get_precedence(node->kind);
    bool need_parens = (node_prec < parent_prec);

    if (need_parens)
        fprintf(f, "(");

    switch (node->kind) {
    case ND_NUM:
        if (node->ty && is_flonum(node->ty))
            fprintf(f, "%Lg", node->fval);
        else
            fprintf(f, "%lld", (long long)node->val);
        break;

    case ND_VAR:
        if (node->var) {
            // Check if this is a string literal (anonymous global with
            // init_data)
            if (node->var->init_data && node->var->name[0] == '.') {
                serialize_string(f, node->var->init_data);
            } else {
                fprintf(f, "%s", node->var->name);
            }
        } else {
            fprintf(f, "/* unknown_var */");
        }
        break;

    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
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
    case ND_ASSIGN:
        serialize_expr(f, vm, ctx, node->lhs, node_prec);
        fprintf(f, " %s ", get_binary_op_str(node->kind));
        serialize_expr(f, vm, ctx, node->rhs, node_prec + 1);
        break;

    case ND_COMMA:
        // Skip null sides — ND_NULL_EXPR , X is not valid C.
        if (is_noop_expr(node->lhs) && is_noop_expr(node->rhs))
            break;
        if (is_noop_expr(node->lhs)) {
            serialize_expr(f, vm, ctx, node->rhs, node_prec + 1);
        } else if (is_noop_expr(node->rhs)) {
            serialize_expr(f, vm, ctx, node->lhs, node_prec);
        } else {
            serialize_expr(f, vm, ctx, node->lhs, node_prec);
            fprintf(f, " , ");
            serialize_expr(f, vm, ctx, node->rhs, node_prec + 1);
        }
        break;

    case ND_NEG:
    case ND_NOT:
    case ND_BITNOT:
    case ND_ADDR:
    case ND_DEREF:
        fprintf(f, "%s", get_unary_op_str(node->kind));
        serialize_expr(f, vm, ctx, node->lhs, node_prec);
        break;

    case ND_CAST: {
        // Suppress widening integer casts — these are always implicit in C.
        // Only emit a cast if it crosses a type category or narrows/changes signedness.
        Type *dst = node->ty;
        Type *src = node->lhs ? node->lhs->ty : NULL;
        bool dst_int = dst && (dst->kind == TY_BOOL || dst->kind == TY_CHAR ||
                               dst->kind == TY_SHORT || dst->kind == TY_INT ||
                               dst->kind == TY_LONG);
        bool src_int = src && (src->kind == TY_BOOL || src->kind == TY_CHAR ||
                               src->kind == TY_SHORT || src->kind == TY_INT ||
                               src->kind == TY_LONG);
        static const int int_rank[] = {
            [TY_BOOL]=0, [TY_CHAR]=1, [TY_SHORT]=2, [TY_INT]=3, [TY_LONG]=4
        };
        bool widening = dst_int && src_int &&
                        dst->is_unsigned == src->is_unsigned &&
                        int_rank[dst->kind] >= int_rank[src->kind];
        if (widening) {
            serialize_expr(f, vm, ctx, node->lhs, parent_prec);
        } else {
            fprintf(f, "(");
            serialize_type(f, ctx, node->ty);
            fprintf(f, ")");
            serialize_expr(f, vm, ctx, node->lhs, node_prec);
        }
        break;
    }

    case ND_COND:
        serialize_expr(f, vm, ctx, node->cond, 0);
        fprintf(f, " ? ");
        serialize_expr(f, vm, ctx, node->then, 0);
        fprintf(f, " : ");
        serialize_expr(f, vm, ctx, node->els, 0);
        break;

    case ND_FUNCALL:
        serialize_expr(f, vm, ctx, node->lhs, node_prec);
        fprintf(f, "(");
        for (Node *arg = node->args; arg; arg = arg->next) {
            serialize_expr(f, vm, ctx, arg, 0);
            if (arg->next)
                fprintf(f, ", ");
        }
        fprintf(f, ")");
        break;

    case ND_MEMBER:
        serialize_expr(f, vm, ctx, node->lhs, node_prec);
        if (node->member && node->member->name)
            fprintf(f, ".%.*s", node->member->name->len,
                    node->member->name->loc);
        else
            fprintf(f, "./* unknown */");
        break;

    case ND_STMT_EXPR:
        fprintf(f, "({\n");
        for (Node *s = node->body; s; s = s->next) {
            serialize_stmt(f, vm, ctx, s, 1);
        }
        fprintf(f, "})");
        break;

    case ND_MEMZERO:
        if (node->var)
            fprintf(f, "__builtin_memset(&%s, 0, sizeof(%s))",
                    node->var->name, node->var->name);
        else
            fprintf(f, "/* memzero */");
        break;

    case ND_NULL_EXPR:
        // Empty expression
        break;

    default:
        fprintf(f, "/* unsupported expr kind %d */", node->kind);
        break;
    }

    if (need_parens)
        fprintf(f, ")");
}

// Serialize a statement
static void serialize_stmt(FILE *f, CCCC *vm, SerializeContext *ctx, Node *node,
                           int indent) {
    if (!node)
        return;

    switch (node->kind) {
    case ND_RETURN:
        print_indent_level(f, indent);
        fprintf(f, "return");
        if (node->lhs) {
            fprintf(f, " ");
            serialize_expr(f, vm, ctx, node->lhs, 0);
        }
        fprintf(f, ";\n");
        break;

    case ND_EXPR_STMT:
        if (is_noop_expr(node->lhs)) break;
        print_indent_level(f, indent);
        serialize_expr(f, vm, ctx, node->lhs, 0);
        fprintf(f, ";\n");
        break;

    case ND_BLOCK:
        print_indent_level(f, indent);
        fprintf(f, "{\n");
        for (Node *s = node->body; s; s = s->next) {
            serialize_stmt(f, vm, ctx, s, indent + 1);
        }
        print_indent_level(f, indent);
        fprintf(f, "}\n");
        break;

    case ND_IF:
        print_indent_level(f, indent);
        fprintf(f, "if (");
        serialize_expr(f, vm, ctx, node->cond, 0);
        fprintf(f, ")\n");
        serialize_stmt(f, vm, ctx, node->then, indent + 1);
        if (node->els) {
            print_indent_level(f, indent);
            fprintf(f, "else\n");
            serialize_stmt(f, vm, ctx, node->els, indent + 1);
        }
        break;

    case ND_FOR:
        print_indent_level(f, indent);
        fprintf(f, "for (");
        if (node->init)
            serialize_expr(f, vm, ctx, node->init, 0);
        fprintf(f, "; ");
        if (node->cond)
            serialize_expr(f, vm, ctx, node->cond, 0);
        fprintf(f, "; ");
        if (node->inc)
            serialize_expr(f, vm, ctx, node->inc, 0);
        fprintf(f, ")\n");
        serialize_stmt(f, vm, ctx, node->then, indent + 1);
        break;

    case ND_DO:
        print_indent_level(f, indent);
        fprintf(f, "do\n");
        serialize_stmt(f, vm, ctx, node->then, indent + 1);
        print_indent_level(f, indent);
        fprintf(f, "while (");
        serialize_expr(f, vm, ctx, node->cond, 0);
        fprintf(f, ");\n");
        break;

    case ND_SWITCH:
        print_indent_level(f, indent);
        fprintf(f, "switch (");
        serialize_expr(f, vm, ctx, node->cond, 0);
        fprintf(f, ") {\n");
        for (Node *c = node->case_next; c; c = c->case_next) {
            print_indent_level(f, indent);
            fprintf(f, "case %ld:\n", c->begin);
            serialize_stmt(f, vm, ctx, c->lhs, indent + 1);
        }
        if (node->default_case) {
            print_indent_level(f, indent);
            fprintf(f, "default:\n");
            serialize_stmt(f, vm, ctx, node->default_case->lhs, indent + 1);
        }
        print_indent_level(f, indent);
        fprintf(f, "}\n");
        break;

    case ND_GOTO:
        print_indent_level(f, indent);
        fprintf(f, "goto %s;\n", node->label);
        break;

    case ND_LABEL:
        fprintf(f, "%s:\n", node->label);
        serialize_stmt(f, vm, ctx, node->lhs, indent);
        break;

    case ND_CASE:
        // Handled as part of switch
        break;

    default:
        // Treat as expression statement
        print_indent_level(f, indent);
        serialize_expr(f, vm, ctx, node, 0);
        fprintf(f, ";\n");
        break;
    }
}

static void serialize_function_signature(FILE *f, SerializeContext *ctx,
                                         Obj *fn) {
    if (fn->is_static)
        fprintf(f, "static ");

    char *rt = NULL;
    size_t rtsz = 0;
    FILE *rtf = open_memstream(&rt, &rtsz);
    if (fn->ty && fn->ty->return_ty)
        serialize_type(rtf, ctx, fn->ty->return_ty);
    else
        fprintf(rtf, "int");
    fclose(rtf);
    if (rtsz > 0 && rt[rtsz - 1] == '*')
        fprintf(f, "%s%s(", rt, fn->name);
    else
        fprintf(f, "%s %s(", rt, fn->name);
    free(rt);

    bool first = true;
    for (Obj *param = fn->params; param; param = param->next) {
        if (!first)
            fprintf(f, ", ");
        first = false;
        serialize_type_decl(f, ctx, param->ty, param->name);
    }

    if (fn->ty && fn->ty->is_variadic && !first) {
        fprintf(f, ", ...");
    } else if (first) {
        fprintf(f, "void");
    }

    fprintf(f, ")");
}

// Serialize a function
static void serialize_function(FILE *f, CCCC *vm, SerializeContext *ctx,
                               Obj *fn) {
    if (!fn->is_function)
        return;

    // Skip pragma macro functions (they were consumed)
    // Skip non-definitions
    if (!fn->is_definition && !fn->body)
        return;

    serialize_function_signature(f, ctx, fn);

    if (fn->body) {
        fprintf(f, " {\n");
        Obj *saved_fn = ctx->current_fn;
        ctx->current_fn = fn;

        // Function-local typedefs/tags are emitted at the top of the function,
        // matching the serializer's existing local declaration hoisting.
        serialize_type_defs_for_owner(f, ctx, fn);

        // Local variable declarations
        for (Obj *var = fn->locals; var; var = var->next) {
            if (var->is_param)
                continue;
            // Compiler-synthesized temporaries (e.g. from ++/--/op=
            // desugaring) have an empty name; give them one so they can
            // be declared and referenced as valid C identifiers.
            if (var->name[0] == '\0')
                var->name = arena_format(vm, "__cccc_tmp%d",
                                          ctx->anon_local_counter++);
            print_indent_level(f, 1);
            serialize_type_decl(f, ctx, var->ty, var->name);
            fprintf(f, ";\n");
        }

        // Function body — unpack a single ND_BLOCK to avoid double-brace wrapping.
        // Both the parser and $function_set_body store the body as an ND_BLOCK node.
        Node *body_stmts;
        if (fn->body && fn->body->kind == ND_BLOCK && !fn->body->next)
            body_stmts = fn->body->body;
        else
            body_stmts = fn->body;
        for (Node *s = body_stmts; s; s = s->next) {
            serialize_stmt(f, vm, ctx, s, 1);
        }

        fprintf(f, "}\n\n");
        ctx->current_fn = saved_fn;
    } else {
        fprintf(f, ";\n\n");
    }
}

// Serialize global variable
static void serialize_global_var(FILE *f, CCCC *vm, SerializeContext *ctx,
                                 Obj *var) {
    (void)vm;

    if (var->is_function)
        return;

    // Skip string literals (anonymous)
    if (var->name[0] == '.')
        return;

    if (var->is_static)
        fprintf(f, "static ");

    serialize_type_decl(f, ctx, var->ty, var->name);

    if (var->init_data) {
        fprintf(f, " = ");
        if (var->ty->kind == TY_ARRAY && var->ty->base->kind == TY_CHAR) {
            serialize_string(f, var->init_data);
        } else if (var->ty->kind == TY_FLOAT) {
            float fv; memcpy(&fv, var->init_data, 4);
            fprintf(f, "%.9gf", (double)fv);
        } else if (var->ty->kind == TY_DOUBLE || var->ty->kind == TY_LDOUBLE) {
            double dv; memcpy(&dv, var->init_data, 8);
            fprintf(f, "%.17g", dv);
        } else if (var->ty->kind == TY_BOOL || var->ty->kind == TY_CHAR ||
                   var->ty->kind == TY_SHORT || var->ty->kind == TY_INT ||
                   var->ty->kind == TY_LONG || var->ty->kind == TY_ENUM ||
                   var->ty->kind == TY_PTR) {
            int64_t iv = 0;
            int sz = var->ty->size < 8 ? var->ty->size : 8;
            memcpy(&iv, var->init_data, sz);
            if (sz < 8 && (iv >> (sz * 8 - 1)) & 1)
                iv |= (-1LL << (sz * 8));
            fprintf(f, "%lld", (long long)iv);
        } else {
            fprintf(f, "/* init data */");
        }
    }

    fprintf(f, ";\n");
}

// Serialize struct/union type definition
static const char *aggregate_keyword(Type *ty) {
    return ty->kind == TY_UNION ? "union" : "struct";
}

static void serialize_struct_def(FILE *f, SerializeContext *ctx, Type *ty) {
    if (!ty)
        return;

    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
        return;

    TypeName *tag = find_tag_name(ctx, ty);
    TypeName *alias = find_typedef_name(ctx, ty);

    if (!tag && alias)
        fprintf(f, "typedef %s", aggregate_keyword(ty));
    else
        fprintf(f, "%s", aggregate_keyword(ty));

    if (tag)
        fprintf(f, " %.*s", tag->name_len, tag->name);

    if (!ty->members) {
        if (tag)
            fprintf(f, ";\n\n");
        return;
    }

    fprintf(f, " {\n");
    for (Member *m = ty->members; m; m = m->next) {
        fprintf(f, "    ");
        char name[256] = "";
        if (m->name) {
            int len = m->name->len;
            if (len >= (int)sizeof(name))
                len = sizeof(name) - 1;
            memcpy(name, m->name->loc, len);
            name[len] = '\0';
        }
        serialize_type_decl(f, ctx, m->ty, name);
        if (m->is_bitfield)
            fprintf(f, " : %d", m->bit_width);
        fprintf(f, ";\n");
    }
    fprintf(f, "}");

    if (!tag && alias)
        fprintf(f, " %.*s", alias->name_len, alias->name);
    fprintf(f, ";\n\n");
}

// Serialize enum type definition
static void serialize_enum_def(FILE *f, SerializeContext *ctx, Type *ty) {
    if (!ty || ty->kind != TY_ENUM)
        return;

    TypeName *tag = find_tag_name(ctx, ty);
    TypeName *alias = find_typedef_name(ctx, ty);

    if (!tag && alias)
        fprintf(f, "typedef enum");
    else
        fprintf(f, "enum");

    if (tag)
        fprintf(f, " %.*s", tag->name_len, tag->name);

    if (!ty->enum_constants) {
        if (tag)
            fprintf(f, ";\n\n");
        return;
    }

    fprintf(f, " {\n");
    for (EnumConstant *ec = ty->enum_constants; ec; ec = ec->next) {
        fprintf(f, "    %s = %d", ec->name, ec->value);
        if (ec->next)
            fprintf(f, ",");
        fprintf(f, "\n");
    }
    fprintf(f, "}");

    if (!tag && alias)
        fprintf(f, " %.*s", alias->name_len, alias->name);
    fprintf(f, ";\n\n");
}

static bool type_has_tag_for_owner(SerializeContext *ctx, Type *ty,
                                   Obj *owner_fn) {
    for (int i = 0; i < ctx->tags_len; i++)
        if (ctx->tags[i].owner_fn == owner_fn &&
            same_type_or_origin(ctx->tags[i].ty, ty))
            return true;
    return false;
}

static bool aggregate_typedef_is_definition(SerializeContext *ctx,
                                            TypeName *alias) {
    if (!alias->ty)
        return false;
    if (alias->ty->kind != TY_STRUCT && alias->ty->kind != TY_UNION &&
        alias->ty->kind != TY_ENUM)
        return false;
    return !type_has_tag_for_owner(ctx, alias->ty, alias->owner_fn);
}

static void serialize_typedef_alias(FILE *f, SerializeContext *ctx,
                                    TypeName *alias) {
    if (!alias || aggregate_typedef_is_definition(ctx, alias))
        return;

    char name[256];
    int len = alias->name_len;
    if (len >= (int)sizeof(name))
        len = sizeof(name) - 1;
    memcpy(name, alias->name, len);
    name[len] = '\0';

    fprintf(f, "typedef ");
    serialize_type_decl(f, ctx, alias->ty, name);
    fprintf(f, ";\n\n");
}

static void serialize_type_defs_for_owner(FILE *f, SerializeContext *ctx,
                                          Obj *owner_fn) {
    Obj *saved_fn = ctx->current_fn;
    ctx->current_fn = owner_fn;

    for (int i = 0; i < ctx->defs.len; i++) {
        Type *ty = ctx->defs.data[i];
        if (type_decl_owner(ctx, ty) != owner_fn)
            continue;
        // Types with no tag and no typedef alias have nothing to refer back
        // to them by, so they're serialized inline at their point of use
        // (e.g. `struct { int x; } pt;`) instead of as a standalone def.
        if (!find_tag_name(ctx, ty) && !find_typedef_name(ctx, ty) &&
            !find_anonymous_typedef_name(ctx, ty))
            continue;
        if (ty->kind == TY_ENUM)
            serialize_enum_def(f, ctx, ty);
        else
            serialize_struct_def(f, ctx, ty);
    }

    // In generated_only mode the output is consumed alongside normal headers,
    // so typedefs are already defined by the consumer's includes.
    if (!ctx->generated_only) {
        for (int i = ctx->typedefs_len - 1; i >= 0; i--) {
            if (ctx->typedefs[i].owner_fn == owner_fn)
                serialize_typedef_alias(f, ctx, &ctx->typedefs[i]);
        }
    }

    ctx->current_fn = saved_fn;
}

// Public API: Serialize entire program to C source
void cc_serialize_program(FILE *f, CCCC *vm, Obj *prog, bool generated_only) {
    if (!f || !prog)
        return;

    SerializeContext ctx = {.generated_only = generated_only};
    collect_scope_names(&ctx, vm);
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (generated_only && !obj->is_macro_generated)
            continue;
        if (obj->is_function && !obj->is_definition && !obj->body)
            continue;
        if (!obj->is_function && obj->name[0] == '.')
            continue;
        collect_obj_types(&ctx, obj);
    }

    // Header comment
    fprintf(f, "/* Generated by CCCC pragma macro expansion */\n\n");

    // Prepend forward includes declared by macros via __cccc_forward_include (#276)
    for (int i = 0; i < vm->compiler.forward_includes.len; i++)
        fprintf(f, "#include %s\n", vm->compiler.forward_includes.data[i]);
    if (vm->compiler.forward_includes.len > 0)
        fprintf(f, "\n");

    // Serialize file-scope type definitions before declarations that reference them.
    serialize_type_defs_for_owner(f, &ctx, NULL);

    // Serialize global variables
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (generated_only && !obj->is_macro_generated)
            continue;
        if (!obj->is_function)
            serialize_global_var(f, vm, &ctx, obj);
    }

    // Serialize function prototypes before bodies so generated C is valid when
    // a function is called before its definition appears in the Obj list.
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (generated_only && !obj->is_macro_generated)
            continue;
        if (!obj->is_function || (!obj->is_definition && !obj->body))
            continue;
        serialize_function_signature(f, &ctx, obj);
        fprintf(f, ";\n\n");
    }

    // Serialize functions
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (generated_only && !obj->is_macro_generated)
            continue;
        if (obj->is_function)
            serialize_function(f, vm, &ctx, obj);
    }

    free(ctx.seen.data);
    free(ctx.defs.data);
    free(ctx.tags);
    free(ctx.typedefs);
}

// Serialize a single node to a string (for debugging)
char *serialize_node_to_source(CCCC *vm, Node *node) {
    if (!node)
        return strdup("");

    // Create a memory stream
    char *buffer = NULL;
    size_t size = 0;
    FILE *f = open_memstream(&buffer, &size);
    if (!f)
        return strdup("/* serialization error */");

    SerializeContext ctx = {};
    collect_scope_names(&ctx, vm);
    collect_node_types(&ctx, node);

    serialize_expr(f, vm, &ctx, node, 0);
    fclose(f);

    free(ctx.seen.data);
    free(ctx.defs.data);
    free(ctx.tags);
    free(ctx.typedefs);

    return buffer;
}

// ============================================================================

// ============================================================================
// Native test harness serialization (--testing -c=native)
// ============================================================================


// Runtime C source emitted verbatim at the top of the generated harness file.
// Implements all __cccc_assert_* functions using native C signatures.
static const char s_native_runtime[] = {
#embed "native_tests_harness.inc" suffix(, 0)
};

// $assert* macros — embed the canonical header directly so the macro
// definitions never drift out of sync with include/cccc/tests.h.
static const char s_native_macros[] = {
#embed "../include/cccc/tests.h" suffix(, 0)
};

// Emit a C string literal with proper escaping into f.
static void emit_cstr(FILE *f, const char *s) {
    if (!s) { fputs("NULL", f); return; }
    fputc('"', f);
    for (const char *p = s; *p; p++) {
        if      (*p == '"')  fputs("\\\"", f);
        else if (*p == '\\') fputs("\\\\", f);
        else if (*p == '\n') fputs("\\n",  f);
        else if (*p == '\r') fputs("\\r",  f);
        else if (*p == '\t') fputs("\\t",  f);
        else                 fputc(*p, f);
    }
    fputc('"', f);
}

// Count positive (non-error_pat) native tests matching the filter options.
static int count_matching_native(TestFnRecord *list, const CcTestOptions *opts) {
    int n = 0;
    for (TestFnRecord *r = list; r; r = r->next) {
        if (r->error_pat) continue;
        const char *disp = r->display_name ? r->display_name : r->name;
        if (opts->test_glob && fnmatch(opts->test_glob, disp, 0) != 0)
            continue;
        if (opts->suite_filter && (!r->suite || strcmp(r->suite, opts->suite_filter) != 0))
            continue;
        n++;
    }
    return n;
}

// Emit the return-value check for one test into the generated child code.
// On entry: __res.passed == 1, _ri/_rf/_rs hold the captured return value.
static void emit_child_ret_check(FILE *f, const TestFnRecord *r) {
    if (r->ret_kind == RET_NONE) return;

    double eps = r->ret_epsilon > 0.0 ? r->ret_epsilon : 1e-9;
    // CmpOp values: NONE=0 EQ=1 NE=2 LT=3 LE=4 GT=5 GE=6
    int op = (int)r->ret_op;

    if (r->ret_kind == RET_INT) {
        fprintf(f,
            "                    {\n"
            "                    int64_t _got=_ri,_exp=%lldLL;\n"
            "                    int _ok;\n"
            "                    switch(%d){\n"
            "                    case 1:_ok=(_got==_exp);break;\n"
            "                    case 2:_ok=(_got!=_exp);break;\n"
            "                    case 3:_ok=(_got< _exp);break;\n"
            "                    case 4:_ok=(_got<=_exp);break;\n"
            "                    case 5:_ok=(_got> _exp);break;\n"
            "                    case 6:_ok=(_got>=_exp);break;\n"
            "                    default:_ok=1;break;}\n"
            "                    if(!_ok){__res.passed=0;"
            "snprintf(__res.fail_msg,sizeof(__res.fail_msg),"
            "\"expected return %s %%lld, got %%lld\",_exp,_got);}\n"
            "                    }\n",
            (long long)r->ret_expect.ret_int, op, cmp_op_str(r->ret_op));
    } else if (r->ret_kind == RET_FLOAT) {
        fprintf(f,
            "                    {\n"
            "                    double _got=_rf,_exp=%.17g,_eps=%.17g;\n"
            "                    int _ok;\n"
            "                    switch(%d){\n"
            "                    case 1:{double _d=_got-_exp;if(_d<0)_d=-_d;_ok=(_d<_eps);break;}\n"
            "                    case 2:{double _d=_got-_exp;if(_d<0)_d=-_d;_ok=!(_d<_eps);break;}\n"
            "                    case 3:_ok=(_got< _exp);break;\n"
            "                    case 4:_ok=(_got<=_exp);break;\n"
            "                    case 5:_ok=(_got> _exp);break;\n"
            "                    case 6:_ok=(_got>=_exp);break;\n"
            "                    default:_ok=1;break;}\n"
            "                    if(!_ok){__res.passed=0;"
            "snprintf(__res.fail_msg,sizeof(__res.fail_msg),"
            "\"expected return %s %%g, got %%g\",_exp,_got);}\n"
            "                    }\n",
            r->ret_expect.ret_float, eps, op, cmp_op_str(r->ret_op));
    } else if (r->ret_kind == RET_STR) {
        const char *exp_str = r->ret_expect.ret_str ? r->ret_expect.ret_str : "";
        fprintf(f,
            "                    {\n"
            "                    const char *_got=_rs,*_exp=");
        emit_cstr(f, exp_str);
        fprintf(f,
            ";\n"
            "                    int _cmp=(_got&&_exp)?strcmp(_got,_exp):(_got!=_exp);\n"
            "                    int _ok;\n"
            "                    switch(%d){\n"
            "                    case 1:_ok=(_cmp==0);break;\n"
            "                    case 2:_ok=(_cmp!=0);break;\n"
            "                    case 3:_ok=(_cmp< 0);break;\n"
            "                    case 4:_ok=(_cmp<=0);break;\n"
            "                    case 5:_ok=(_cmp> 0);break;\n"
            "                    case 6:_ok=(_cmp>=0);break;\n"
            "                    default:_ok=1;break;}\n"
            "                    if(!_ok){__res.passed=0;"
            "snprintf(__res.fail_msg,sizeof(__res.fail_msg),"
            "\"expected return %s \\\"%%s\\\", got \\\"%%s\\\"\","
            "_exp?_exp:\"(null)\",_got?_got:\"(null)\");}\n"
            "                    }\n",
            op, cmp_op_str(r->ret_op));
    }
}

// Returns true if per-test (non-once) hook `s` should run for the given test
// (suite/name_pat filters mirror run_hooks() in tests.c). `disp` is the raw
// display name (no "[native]" suffix), used for fnmatch matching.
static bool hook_matches_test(const TestSetupRecord *s, bool is_teardown,
                              const char *suite, const char *disp) {
    if (s->is_teardown != is_teardown) return false;
    if (s->once) return false;
    if (s->suite && (!suite || strcmp(s->suite, suite) != 0)) return false;
    if (s->name_pat && (!disp || fnmatch(s->name_pat, disp, 0) != 0)) return false;
    return true;
}

// Emit a guarded call to a "once" hook function in the parent/main() context.
// On failure, prints a TAP/PLAIN diagnostic similar to run_once_hooks() in
// tests.c (e.g. "# once-setup for suite \"db\" failed").
static void emit_guarded_hook(FILE *f, const char *fn_name,
                              const char *kind, const char *ctx_label) {
    fprintf(f,
        "    {\n"
        "        __cccc_run_t __honce={0};\n"
        "        __s_run=&__honce;\n"
        "        if(setjmp(__honce.jmp)==0){ %s(); }\n"
        "        else {\n",
        fn_name);
    fprintf(f, "            if(_fmt==0) printf(\"# %s for %%s failed\\n\",", kind);
    emit_cstr(f, ctx_label);
    fprintf(f, ");\n");
    fprintf(f, "            else if(_fmt==1) printf(\"  ! %s for %%s failed\\n\",", kind);
    emit_cstr(f, ctx_label);
    fprintf(f,
        ");\n"
        "        }\n"
        "        __s_run=NULL;\n"
        "    }\n");
}

void cc_serialize_test_harness(FILE *f, CCCC *vm, Obj *prog,
                               TestFnRecord *list,
                               TestSetupRecord *setups,
                               const CcTestOptions *opts,
                               int start_at) {
    // ── Native runtime + assert macros ───────────────────────────────────────
    fputs(s_native_runtime, f);
    fputs(s_native_macros,  f);

    // ── User code ─────────────────────────────────────────────────────────────
    cc_serialize_program(f, vm, prog, false);

    // ── Typed wrappers (avoids UB with void-fn pointers) ─────────────────────
    fprintf(f, "\n/* --- test wrappers --- */\n");
    for (TestFnRecord *r = list; r; r = r->next) {
        if (r->error_pat) continue;
        fprintf(f,
            "static void __wrap_%s(int64_t *_ri,double *_rf,const char **_rs){\n",
            r->name);
        switch (r->ret_kind) {
        case RET_INT:
            fprintf(f, "    *_ri=(int64_t)%s();\n", r->name); break;
        case RET_FLOAT:
            fprintf(f, "    *_rf=(double)%s();\n",  r->name); break;
        case RET_STR:
            fprintf(f, "    *_rs=(const char*)%s();\n", r->name); break;
        default:
            fprintf(f, "    (void)_ri;(void)_rf;(void)_rs;%s();\n", r->name); break;
        }
        fprintf(f, "}\n");
    }

    // ── main() ───────────────────────────────────────────────────────────────
    int total          = count_matching_native(list, opts);
    int global_timeout = opts->test_timeout; // seconds

    fprintf(f,
        "\nint main(void){\n"
        "    int _fmt=%d,_fail_fast=%d,_gtimeout=%d;\n"
        "    int _tn=%d,_passed=0,_failed=0,_timeouts=0;\n"
        "    const char *_suite_cur=NULL;\n",
        (int)opts->format,
        opts->fail_fast ? 1 : 0,
        global_timeout,
        start_at);

    // Header (only for pure-native mode where start_at==1)
    if (start_at == 1) {
        fprintf(f,
            "    if(_fmt==0) printf(\"TAP version 13\\n1..%d\\n\");\n"
            "    else if(_fmt==1) printf(\"Running %d test(s)...\\n\");\n"
            "    else printf(\"[\\n\");\n",
            total, total);
    }

    const char *prev_suite = NULL;

    for (TestFnRecord *r = list; r; r = r->next) {
        if (r->error_pat) continue;

        const char *raw = r->display_name ? r->display_name : r->name;
        // display name gets "[native]" suffix
        char disp[640];
        snprintf(disp, sizeof(disp), "%s [native]", raw);

        const char *cur_suite = r->suite;
        bool suite_changed = (cur_suite != prev_suite) &&
                             (cur_suite == NULL || prev_suite == NULL ||
                              strcmp(cur_suite, prev_suite) != 0);

        long eff_ms = r->timeout_ms > 0 ? r->timeout_ms
                    : (global_timeout > 0 ? (long)global_timeout * 1000L : 0L);

        fprintf(f, "    /* test: %s */\n    {\n", r->name);

        // Filter: test_glob (baked at serialization time — no runtime filtering needed
        // since we already counted matching tests). But we still need it if the user
        // wants to re-run with --test flag at binary level. For v1, filters are baked.

        if (suite_changed) {
            // Suite-once teardown for the suite we're leaving.
            if (prev_suite) {
                char label[320];
                snprintf(label, sizeof(label), "suite \"%s\"", prev_suite);
                for (TestSetupRecord *s = setups; s; s = s->next) {
                    if (s->is_teardown && s->once && s->suite && !s->name_pat &&
                        strcmp(s->suite, prev_suite) == 0)
                        emit_guarded_hook(f, s->fn_name, "once-teardown", label);
                }
            }

            // TAP suite header.
            if (cur_suite) {
                fprintf(f, "    if(_fmt==0) printf(\"# Suite: %%s\\n\",");
                emit_cstr(f, cur_suite);
                fprintf(f, ");\n");
            } else {
                fprintf(f, "    if(_fmt==0) printf(\"# Suite: (none)\\n\");\n");
            }

            // Suite header for PLAIN output (unchanged).
            if (cur_suite) {
                fprintf(f, "    if(_fmt==1&&(_suite_cur==NULL||strcmp(_suite_cur,");
                emit_cstr(f, cur_suite);
                fprintf(f, ")!=0)){printf(\"\\n-- %%s --\\n\",");
                emit_cstr(f, cur_suite);
                fprintf(f, ");_suite_cur=");
                emit_cstr(f, cur_suite);
                fprintf(f, ";}\n");
            }

            // Suite-once setup for the suite we're entering.
            if (cur_suite) {
                char label[320];
                snprintf(label, sizeof(label), "suite \"%s\"", cur_suite);
                for (TestSetupRecord *s = setups; s; s = s->next) {
                    if (!s->is_teardown && s->once && s->suite && !s->name_pat &&
                        strcmp(s->suite, cur_suite) == 0)
                        emit_guarded_hook(f, s->fn_name, "once-setup", label);
                }
            }

            prev_suite = cur_suite;
        }

        // Name-pattern / global once-setup hooks: fire on first match (or
        // unconditionally before the first test for global once hooks with
        // no suite/name_pat), in the parent so COW propagates the effect to
        // this and all subsequent children.
        for (TestSetupRecord *s = setups; s; s = s->next) {
            if (s->is_teardown || !s->once || s->suite || s->once_fired) continue;
            bool fires = s->name_pat ? (fnmatch(s->name_pat, raw, 0) == 0) : true;
            if (!fires) continue;
            char label[320];
            if (s->name_pat)
                snprintf(label, sizeof(label), "pattern \"%s\"", s->name_pat);
            else
                snprintf(label, sizeof(label), "%s", "global setup");
            emit_guarded_hook(f, s->fn_name, "once-setup", label);
            s->once_fired = true;
        }

        // Determine whether this test has any matching per-test teardown hooks.
        bool has_teardown = false;
        for (TestSetupRecord *s = setups; s; s = s->next) {
            if (hook_matches_test(s, true, cur_suite, raw)) { has_teardown = true; break; }
        }

        // Fork
        fprintf(f,
            "    {\n"
            "        int __pfd[2];\n"
            "        if(pipe(__pfd)!=0){perror(\"pipe\");return 1;}\n"
            "        fflush(stdout);\n"
            "        pid_t __pid=fork();\n"
            "        if(__pid<0){perror(\"fork\");return 1;}\n"
            "        if(__pid==0){\n"
            "            close(__pfd[0]);\n"
            "            __cccc_run_t __run={0};\n"
            "            __s_run=&__run;\n"
            "            __s_alarm_fired=0;\n"
            "            signal(SIGALRM,__cccc_alarm_hdl);\n");

        if (eff_ms > 0) {
            fprintf(f,
                "            {struct itimerval __tv={{0,0},{%ldL,%ldL}};"
                "setitimer(ITIMER_REAL,&__tv,NULL);}\n",
                eff_ms / 1000L, (eff_ms % 1000L) * 1000L);
        }

        fprintf(f,
            "            __cccc_result __res={0};\n"
            "            int64_t _ri=0;double _rf=0.0;const char *_rs=NULL;\n"
            "            int __jv=setjmp(__run.jmp);\n"
            "            if(__jv==0){\n");

        // Per-test setup hooks, in declaration order.
        for (TestSetupRecord *s = setups; s; s = s->next) {
            if (hook_matches_test(s, false, cur_suite, raw))
                fprintf(f, "                %s();\n", s->fn_name);
        }

        fprintf(f, "                __wrap_%s(&_ri,&_rf,&_rs);\n", r->name);

        // Capture return value
        if (r->ret_kind == RET_INT)   fprintf(f, "                /* ret captured in _ri */\n");
        if (r->ret_kind == RET_FLOAT) fprintf(f, "                /* ret captured in _rf */\n");
        if (r->ret_kind == RET_STR) {
            fprintf(f,
                "                if(_rs)strncpy(__res.ret_str,_rs,sizeof(__res.ret_str)-1);\n");
        }

        fprintf(f, "                __res.passed=1;\n");

        // Return-value assertion (while still in passed branch, before the jmp escapes)
        if (r->ret_kind != RET_NONE) {
            emit_child_ret_check(f, r);
        }

        fprintf(f,
            "            } else if(__jv==2){\n"
            "                __res.timed_out=1;\n"
            "            } else {\n"
            "                snprintf(__res.fail_msg,sizeof(__res.fail_msg),\"%%s\",__run.fail_msg);\n"
            "            }\n");

        // Per-test teardown hooks, in declaration order. Run even if the test
        // failed (but not if it timed out). A teardown failure overrides a
        // passing result; if the test had already failed, its message wins.
        if (has_teardown) {
            fprintf(f,
                "            if(!__res.timed_out){\n"
                "                int __jvt=setjmp(__run.jmp);\n"
                "                if(__jvt==0){\n");
            for (TestSetupRecord *s = setups; s; s = s->next) {
                if (hook_matches_test(s, true, cur_suite, raw))
                    fprintf(f, "                    %s();\n", s->fn_name);
            }
            fprintf(f,
                "                } else if(__jvt==2){\n"
                "                    __res.timed_out=1;\n"
                "                } else {\n"
                "                    if(__res.passed){\n"
                "                        __res.passed=0;\n"
                "                        snprintf(__res.fail_msg,sizeof(__res.fail_msg),\"%%s\",__run.fail_msg);\n"
                "                    }\n"
                "                }\n"
                "            }\n");
        }

        if (eff_ms > 0) {
            fprintf(f,
                "            {struct itimerval __z={{0,0},{0,0}};setitimer(ITIMER_REAL,&__z,NULL);}\n");
        }

        fprintf(f,
            "            write(__pfd[1],&__res,sizeof(__res));\n"
            "            _exit(0);\n"
            "        }\n"
            "        /* parent */\n"
            "        close(__pfd[1]);\n"
            "        __cccc_result __res={0};\n"
            "        ssize_t __nr=read(__pfd[0],&__res,sizeof(__res));\n"
            "        close(__pfd[0]);\n"
            "        int __st=0;waitpid(__pid,&__st,0);\n"
            "        if(__nr<(ssize_t)sizeof(__res)){\n"
            "            __res.passed=0;\n"
            "            if(__nr==0&&WIFSIGNALED(__st))\n"
            "                snprintf(__res.fail_msg,sizeof(__res.fail_msg),\n"
            "                    \"child killed by signal %%d\",WTERMSIG(__st));\n"
            "            else\n"
            "                snprintf(__res.fail_msg,sizeof(__res.fail_msg),\"child exited unexpectedly\");\n"
            "        }\n"
            "        __cccc_report(_tn++,");
        emit_cstr(f, disp);
        fprintf(f,
            ",_fmt,&__res,&_passed,&_failed,&_timeouts);\n"
            "        if(_fail_fast&&(_failed+_timeouts)>0) return 1;\n"
            "    }\n"
            "    }\n\n");
    }

    // Final suite-once teardown for the last suite, and any name-pattern /
    // global once-teardown hooks that haven't fired yet.
    if (prev_suite) {
        char label[320];
        snprintf(label, sizeof(label), "suite \"%s\"", prev_suite);
        for (TestSetupRecord *s = setups; s; s = s->next) {
            if (s->is_teardown && s->once && s->suite && !s->name_pat &&
                strcmp(s->suite, prev_suite) == 0)
                emit_guarded_hook(f, s->fn_name, "once-teardown", label);
        }
    }
    for (TestSetupRecord *s = setups; s; s = s->next) {
        if (!s->is_teardown || !s->once || s->suite || s->once_fired) continue;
        char label[320];
        if (s->name_pat)
            snprintf(label, sizeof(label), "pattern \"%s\"", s->name_pat);
        else
            snprintf(label, sizeof(label), "%s", "global teardown");
        emit_guarded_hook(f, s->fn_name, "once-teardown", label);
        s->once_fired = true;
    }

    // Footer / summary (only for pure-native; mixed mode parent owns the summary)
    if (start_at == 1) {
        fprintf(f,
            "    if(_fmt==1){\n"
            "        printf(\"\\n\");\n"
            "        printf(\"Total:   %%8d\\n\",_passed+_failed+_timeouts);\n"
            "        printf(\"Passed:  %%8d\\n\",_passed);\n"
            "        printf(\"Failed:  %%8d\\n\",_failed+_timeouts);\n"
            "    } else if(_fmt==2) printf(\"]\\n\");\n");
    } else {
        // Mixed mode: just close JSON array if needed (parent opened it)
        fprintf(f,
            "    /* mixed mode: parent owns header/footer */\n"
            "    (void)_passed;(void)_failed;(void)_timeouts;\n");
    }

    fprintf(f, "    return (_failed+_timeouts)>0?1:0;\n}\n");
}
