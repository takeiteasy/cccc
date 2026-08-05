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
    // #891: mirrors TypeNameRecord.from_include/always_emit (cccc.h) -- used
    // in !generated_only mode (-c=native, -M without -G) to avoid re-emitting
    // a definition the consumer's own #include already provides.
    bool from_include;
    bool always_emit;
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
    // #891: --emit-only suppresses auto-capture (preprocess.c), so under it
    // the primary file's own #include directives are NOT re-emitted -- a
    // header-sourced typedef/tag has no re-emitted #include to collide with
    // and must still be serialized. Only skip has_include gates when this
    // is false.
    bool emit_strict;
    int anon_local_counter; // names compiler-synthesized temps (e.g. ++/-- desugaring)
} SerializeContext;

// Forward declaration
static void serialize_expr(FILE *f, VirtualMachine *vm, SerializeContext *ctx, Node *node,
                           int parent_prec);
static void serialize_stmt(FILE *f, VirtualMachine *vm, SerializeContext *ctx, Node *node,
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
        // #892: distinguish tagged aggregates by tag name before falling
        // back to structural (member-wise) comparison below. Without this,
        // two unrelated opaque (incomplete) structs -- which have no
        // members for the loop below to compare -- collapsed into "the
        // same type" (the loop body never runs for either side, so the
        // function fell through to `ma == NULL && mb == NULL` == true).
        // That corrupted find_tag_name()'s linear scan into returning
        // whichever opaque tag happens to appear first in scope (e.g.
        // reflection.h's `AttrTarget`) for every opaque handle typedef'd
        // in a comptime-using file. A tag mismatch is conclusive; a
        // tagged-vs-anonymous pairing falls through to the structural
        // comparison unchanged (an anonymous *incomplete* aggregate can't
        // exist in valid C, so this only affects complete types, e.g.
        // `typedef struct { int x; } Foo;`).
        if (a->struct_tag && b->struct_tag) {
            bool tag_match = a->struct_tag->len == b->struct_tag->len &&
                             strncmp(a->struct_tag->loc, b->struct_tag->loc,
                                     a->struct_tag->len) == 0;
            if (!tag_match)
                return false;
        }

        // Incomplete aggregates have no members to compare -- tag identity
        // above is the only signal available (mutual anonymity can't occur
        // for an incomplete type).
        if (a->size < 0 || b->size < 0)
            return a->struct_tag != NULL && b->struct_tag != NULL;

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
                           char *name, int name_len, Obj *owner_fn,
                           bool from_include, bool always_emit) {
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
    (*items)[*len].from_include = from_include;
    (*items)[*len].always_emit = always_emit;
    (*len)++;
}

static void collect_scope_names(SerializeContext *ctx, VirtualMachine *vm) {
    for (TypeNameRecord *rec = vm->compiler.type_names; rec; rec = rec->next) {
        if (rec->is_tag)
            type_name_push(&ctx->tags, &ctx->tags_len, &ctx->tags_cap, rec->ty,
                           rec->name, rec->name_len, rec->owner_fn,
                           rec->from_include, rec->always_emit);
        else
            type_name_push(&ctx->typedefs, &ctx->typedefs_len,
                           &ctx->typedefs_cap, rec->ty, rec->name,
                           rec->name_len, rec->owner_fn,
                           rec->from_include, rec->always_emit);
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
    case TY_DECIMAL32:
        fprintf(f, "_Decimal32");
        break;
    case TY_DECIMAL64:
        fprintf(f, "_Decimal64");
        break;
    case TY_DECIMAL128:
        fprintf(f, "_Decimal128");
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
    case TY_NULLPTR_T:
        // nullptr_t has the same size/representation as a pointer; emit a
        // type that is valid without requiring <stddef.h> in the output.
        fprintf(f, "void *");
        break;
    case TY_BITINT:
        // Emit as the underlying container integer type
        if (ty->size == 1) fprintf(f, ty->is_unsigned ? "unsigned char" : "signed char");
        else if (ty->size == 2) fprintf(f, ty->is_unsigned ? "unsigned short" : "short");
        else if (ty->size == 4) fprintf(f, ty->is_unsigned ? "unsigned int" : "int");
        else fprintf(f, ty->is_unsigned ? "unsigned long" : "long");
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
                fprintf(f, "\\%03o", (unsigned char)*p);
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
static void serialize_expr(FILE *f, VirtualMachine *vm, SerializeContext *ctx, Node *node,
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
        if (node->ty && is_decimal(node->ty)) {
            // #402: node->fval/val are never populated for a decimal literal
            // (see tokenize.c) -- dec_digits plus the width-appropriate
            // suffix is the only way to round-trip it back to valid C source.
            const char *suffix = dec_width_code(node->ty) == 0 ? "df"
                                : dec_width_code(node->ty) == 1 ? "dd"
                                                                 : "dl";
            fprintf(f, "%s%s", node->dec_digits ? node->dec_digits : "0", suffix);
        } else if (node->ty && is_flonum(node->ty))
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
static void serialize_stmt(FILE *f, VirtualMachine *vm, SerializeContext *ctx, Node *node,
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

// KNOWN ISSUE (#897): a struct/union-by-value parameter's type is
// mis-serialized here as "struct <param-name>" instead of its real tag --
// e.g. `int helper(struct Point q)` emits "struct q" in the generated
// native C, which clang then rejects as an incomplete/undeclared type.
// Found incidentally while fixing #896; confirmed unrelated to #896's
// #include/@comptime handling (reproduces in a single file with no
// #include at all). Not fixed here -- see #897 for the repro and a
// (unverified) hypothesis about the root cause.
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
    if (fn->params) {
        for (Obj *param = fn->params; param; param = param->next) {
            if (!first)
                fprintf(f, ", ");
            first = false;
            serialize_type_decl(f, ctx, param->ty, param->name);
        }
    } else if (fn->ty) {
        // #901: a bodiless declaration (e.g. `int abs(int x);`) never runs
        // the body-parsing path that populates fn->params (the Obj-based
        // parameter list created for stack-slot allocation) -- only
        // fn->ty->params (the Type-based prototype list) exists. Fall back
        // to it so such a declaration serializes its real parameter types
        // instead of degrading to "()"/"(void)".
        int anon = 0;
        for (Type *param = fn->ty->params; param; param = param->next) {
            if (!first)
                fprintf(f, ", ");
            first = false;
            char buf[64];
            if (param->name) {
                int len = param->name->len;
                if (len > (int)sizeof(buf) - 1)
                    len = (int)sizeof(buf) - 1;
                memcpy(buf, param->name->loc, len);
                buf[len] = '\0';
            } else {
                snprintf(buf, sizeof buf, "__a%d", anon++);
            }
            serialize_type_decl(f, ctx, param, buf);
        }
    }

    if (fn->ty && fn->ty->is_variadic && !first) {
        fprintf(f, ", ...");
    } else if (first) {
        fprintf(f, "void");
    }

    fprintf(f, ")");
}

// Serialize a function
static void serialize_function(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
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
        // Both the parser and FunctionSetBody store the body as an ND_BLOCK node.
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
static void serialize_global_var(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                                 Obj *var) {
    (void)vm;

    if (var->is_function)
        return;

    // Skip string literals (anonymous)
    if (var->name[0] == '.')
        return;

    if (var->is_static)
        fprintf(f, "static ");
    else if (!var->is_definition)
        // #901: a global written `extern int g;` (no initializer, no
        // tentative-definition fallback -- parse.c sets is_definition =
        // !attr->is_extern) is a declaration, not a definition. Emitting
        // it as a bare `int g;` produces a tentative definition that
        // collides with the real symbol at link time.
        fprintf(f, "extern ");

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
        } else if (is_decimal(var->ty)) {
            // #402: raw BID bytes in init_data -> C source text. Requires
            // CCCC_HAS_DECIMAL=1 (the same build that could have produced
            // these bytes in the first place); cccc_dec_format returns -1
            // in the off build, which can't happen here.
            char buf[80];
            int w = dec_width_code(var->ty);
            const char *suffix = w == 0 ? "df" : w == 1 ? "dd" : "dl";
            if (cccc_dec_format(buf, sizeof buf, var->init_data, w) >= 0)
                fprintf(f, "%s%s", buf, suffix);
            else
                fprintf(f, "0%s", suffix);
        } else if (var->ty->kind == TY_BOOL || var->ty->kind == TY_CHAR ||
                   var->ty->kind == TY_SHORT || var->ty->kind == TY_INT ||
                   var->ty->kind == TY_LONG || var->ty->kind == TY_ENUM ||
                   var->ty->kind == TY_PTR || var->ty->kind == TY_NULLPTR_T) {
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

    // C23 underlying type
    if (ty->enum_base_type) {
        fprintf(f, " : ");
        serialize_type(f, ctx, ty->enum_base_type);
    }

    if (!ty->enum_constants) {
        if (tag)
            fprintf(f, ";\n\n");
        return;
    }

    fprintf(f, " {\n");
    for (EnumConstant *ec = ty->enum_constants; ec; ec = ec->next) {
        fprintf(f, "    %s = %lld", ec->name, (long long)ec->value);
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
    // #891: in !generated_only mode (-c=native, -M without -G), a
    // header-sourced typedef would collide with the consumer's own
    // #include of the same header (auto-capture re-emits that #include
    // verbatim) -- e.g. `typedef void FILE;` from CCCC's own stdio.h
    // polyfill alongside a real `#include <stdio.h>`. Comptime/reflection-
    // synthesized aliases (always_emit) are exempt: they have no header of
    // their own to collide with, and dropping them would silently delete
    // macro-generated typedefs from the output.
    if (!ctx->generated_only && !ctx->emit_strict && alias->from_include &&
        !alias->always_emit)
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
        TypeName *tag = find_tag_name(ctx, ty);
        TypeName *alias = find_typedef_name(ctx, ty);
        if (!tag && !alias && !find_anonymous_typedef_name(ctx, ty))
            continue;
        // #891: same reasoning as serialize_typedef_alias -- in
        // !generated_only mode, a header-sourced struct/enum tag (e.g.
        // `struct tm` from `#include <time.h>`) would collide with the
        // consumer's own #include of that header, whether it's named by a
        // tag (`struct tm`) or only by a typedef alias to an anonymous
        // struct/union/enum. Usage sites still refer to it by name
        // (find_tag_name/find_typedef_name above are unaffected); only the
        // standalone definition is suppressed.
        TypeName *provenance_source = tag ? tag : alias;
        if (!ctx->generated_only && !ctx->emit_strict && provenance_source &&
            provenance_source->from_include && !provenance_source->always_emit)
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
// #904: CCCC's own polyfill headers (stdio.h/errno.h/getopt.h in src/std.c)
// define stdout/stderr/stdin/errno/optarg/optind/opterr/optopt as macros
// that expand to a call into an internal accessor shim (__cccc_stdout(),
// etc -- see src/stdlib/stdio.c and src/stdlib/posix.c) so they reflect
// the real host state instead of being inert, always-zero/NULL guest
// globals (#736). That macro expansion happens during preprocessing,
// before this backend ever runs, so the AST already contains a call to
// e.g. __cccc_stdout() with no record that it started life as `stdout`.
// Under -c=native the #901 fix correctly declines to serialize a
// prototype for these (they're declared in CCCC's own header, not the
// primary file, so #901's from_include check excludes them) -- but with
// no prototype AND no definition, the generated call is entirely
// undeclared and the downstream compiler rejects it outright. Define each
// shim actually used in terms of the real symbol instead: the auto-capture
// mechanism (this same function, just above) has already re-emitted the
// real #include that provides it, since that's the only way the macro
// which expands to this shim call could have been reached in the first
// place.
static const struct {
    const char *name;
    const char *def;
} native_accessor_shims[] = {
    {"__cccc_stdin",      "static FILE *__cccc_stdin(void) { return stdin; }\n"},
    {"__cccc_stdout",     "static FILE *__cccc_stdout(void) { return stdout; }\n"},
    {"__cccc_stderr",     "static FILE *__cccc_stderr(void) { return stderr; }\n"},
    {"__cccc_errno_ptr",  "static int *__cccc_errno_ptr(void) { return &errno; }\n"},
    {"__cccc_optarg_ptr", "static char **__cccc_optarg_ptr(void) { return &optarg; }\n"},
    {"__cccc_optind_ptr", "static int *__cccc_optind_ptr(void) { return &optind; }\n"},
    {"__cccc_opterr_ptr", "static int *__cccc_opterr_ptr(void) { return &opterr; }\n"},
    {"__cccc_optopt_ptr", "static int *__cccc_optopt_ptr(void) { return &optopt; }\n"},
};

static void serialize_native_accessor_shims(FILE *f, Obj *prog) {
    bool any = false;
    for (size_t i = 0; i < sizeof(native_accessor_shims) / sizeof(native_accessor_shims[0]); i++) {
        for (Obj *obj = prog; obj; obj = obj->next) {
            if (!obj->is_function || !obj->is_used)
                continue;
            if (strcmp(obj->name, native_accessor_shims[i].name) != 0)
                continue;
            fprintf(f, "%s", native_accessor_shims[i].def);
            any = true;
            break;
        }
    }
    if (any)
        fprintf(f, "\n");
}

void cc_serialize_program(FILE *f, VirtualMachine *vm, Obj *prog, bool generated_only) {
    if (!f || !prog)
        return;

    SerializeContext ctx = {.generated_only = generated_only,
                           .emit_strict = vm->compiler.emit_strict != 0};
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

    // Re-emit libraries queued via #pragma cccc link() as the portable
    // comment(lib, ...) form so downstream compilers can honour them.
    for (int i = 0; i < vm->compiler.pragma_link_libs.len; i++)
        fprintf(f, "#pragma comment(lib, \"%s\")\n",
                vm->compiler.pragma_link_libs.data[i]);
    if (vm->compiler.pragma_link_libs.len > 0)
        fprintf(f, "\n");

    if (generated_only && vm->compiler.emit_events_head) {
        serialize_type_defs_for_owner(f, &ctx, NULL);
        for (EmitEvent *ev = vm->compiler.emit_events_head; ev; ev = ev->next) {
            if (ev->kind == CCCC_EMIT_SOURCE) {
                fprintf(f, "%s\n", ev->source);
                continue;
            }
            Obj *obj = ev->obj;
            if (!obj || !obj->is_macro_generated)
                continue;
            if (obj->is_function) {
                if (!obj->is_definition && !obj->body)
                    continue;
                serialize_function_signature(f, &ctx, obj);
                fprintf(f, ";\n\n");
                if (obj->body)
                    serialize_function(f, vm, &ctx, obj);
            } else if (obj->name[0] != '.') {
                serialize_global_var(f, vm, &ctx, obj);
            }
        }
        free(ctx.seen.data);
        free(ctx.defs.data);
        free(ctx.tags);
        free(ctx.typedefs);
        return;
    }

    // Prepend preprocessor directives routed to generated output.
    // #896: an auto-captured #include line whose resolved file (directly, or
    // transitively through its own plain #includes) contains cccc-only
    // routing syntax (@comptime/@shared/@emit/@build/@test, or the
    // [[cccc::...]] spellings) is never re-emitted here -- a downstream
    // system compiler opening that file directly would choke on syntax it
    // doesn't understand (see run_native_backend, main.c). serialize_typedef_alias
    // / serialize_type_defs_for_owner compensate by no longer treating that
    // file's types as from_include, so their definitions are still emitted
    // below instead of being silently dropped.
    for (int i = 0; i < vm->compiler.emit_directives.len; i++) {
        char *line = vm->compiler.emit_directives.data[i];
        char *resolved = hashmap_get(&vm->compiler.emit_include_paths, line);
        if (resolved && cc_file_is_cccc_only(vm, resolved))
            continue;
        fprintf(f, "%s\n", line);
    }
    if (vm->compiler.emit_directives.len > 0)
        fprintf(f, "\n");

    // #904: real symbols for internal host-accessor shims (stdout/errno/
    // etc) -- only meaningful once the real headers above are visible, and
    // only outside generated_only (-G), matching the from_include filter's
    // gating for the same reason (see the comment on this function).
    if (!generated_only)
        serialize_native_accessor_shims(f, prog);

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
        if (!obj->is_function)
            continue;
        if (!obj->is_definition && !obj->body) {
            // #901: a bare declaration with no body (e.g. `int abs(int
            // x);`) used to be dropped entirely here. The VM path needs
            // no native declaration -- it resolves the call as an FFI
            // symbol with a known signature -- but the downstream system
            // compiler does, so silently omitting it produced an
            // undeclared-function error in the generated C. Emit it when
            // it was written in the primary file (or in a cccc-only-
            // routed include, whose own #include is never re-emitted --
            // #896); a header-sourced declaration is left out, since the
            // auto-captured #include (see TypeNameRecord.from_include)
            // already supplies it to the native compiler. An implicit
            // declaration's guessed signature is skipped outright -- it
            // could conflict with the real one from a re-emitted header.
            if (obj->is_implicit)
                continue;
            Token *t = obj->tok;
            bool from_primary = t && t->file &&
                (t->file == vm->compiler.primary_file ||
                 cc_file_is_cccc_only(vm, t->file->name));
            if (!from_primary)
                continue;
        }
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

