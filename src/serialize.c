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
} SerializeContext;

// Forward declaration
static void serialize_expr(FILE *f, JCC *vm, SerializeContext *ctx, Node *node,
                           int parent_prec);
static void serialize_stmt(FILE *f, JCC *vm, SerializeContext *ctx, Node *node,
                           int indent);

static bool same_type_or_origin(Type *a, Type *b) {
    for (Type *pa = a; pa; pa = pa->origin)
        for (Type *pb = b; pb; pb = pb->origin)
            if (pa == pb)
                return true;
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
                           char *name, int name_len) {
    if (!ty || !name || name_len <= 0)
        return;

    if (*len >= *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *items = realloc(*items, sizeof(TypeName) * *cap);
    }

    (*items)[*len].ty = ty;
    (*items)[*len].name = name;
    (*items)[*len].name_len = name_len;
    (*len)++;
}

static void collect_scope_names(SerializeContext *ctx, JCC *vm) {
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        for (TagScopeNode *tag = sc->tags; tag; tag = tag->next)
            type_name_push(&ctx->tags, &ctx->tags_len, &ctx->tags_cap, tag->ty,
                           tag->name, tag->name_len);

        for (VarScopeNode *var = sc->vars; var; var = var->next)
            if (var->type_def)
                type_name_push(&ctx->typedefs, &ctx->typedefs_len,
                               &ctx->typedefs_cap, var->type_def, var->name,
                               var->name_len);
    }
}

static TypeName *find_tag_name(SerializeContext *ctx, Type *ty) {
    if (!ctx || !ty)
        return NULL;

    for (int i = 0; i < ctx->tags_len; i++)
        if (same_type_or_origin(ctx->tags[i].ty, ty))
            return &ctx->tags[i];
    return NULL;
}

static TypeName *find_typedef_name(SerializeContext *ctx, Type *ty) {
    if (!ctx || !ty)
        return NULL;

    for (int i = 0; i < ctx->typedefs_len; i++)
        if (same_type_or_origin(ctx->typedefs[i].ty, ty))
            return &ctx->typedefs[i];
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

    collect_node_types(ctx, node->lhs);
    collect_node_types(ctx, node->rhs);
    collect_node_types(ctx, node->cond);
    collect_node_types(ctx, node->then);
    collect_node_types(ctx, node->els);
    collect_node_types(ctx, node->init);
    collect_node_types(ctx, node->inc);
    collect_node_types(ctx, node->body);
    collect_node_types(ctx, node->args);

    for (Node *c = node->case_next; c; c = c->case_next)
        collect_node_types(ctx, c);
    collect_node_types(ctx, node->default_case);

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
        else
            fprintf(f, "struct /* anonymous */");
        break;
    }
    case TY_UNION: {
        TypeName *tag = find_tag_name(ctx, ty);
        TypeName *alias = find_typedef_name(ctx, ty);
        if (tag)
            fprintf(f, "union %.*s", tag->name_len, tag->name);
        else if (alias)
            fprintf(f, "%.*s", alias->name_len, alias->name);
        else
            fprintf(f, "union /* anonymous */");
        break;
    }
    case TY_ENUM: {
        TypeName *tag = find_tag_name(ctx, ty);
        TypeName *alias = find_typedef_name(ctx, ty);
        if (tag)
            fprintf(f, "enum %.*s", tag->name_len, tag->name);
        else if (alias)
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
static void serialize_expr(FILE *f, JCC *vm, SerializeContext *ctx, Node *node,
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
    case ND_COMMA:
        serialize_expr(f, vm, ctx, node->lhs, node_prec);
        fprintf(f, " %s ", get_binary_op_str(node->kind));
        serialize_expr(f, vm, ctx, node->rhs, node_prec + 1);
        break;

    case ND_NEG:
    case ND_NOT:
    case ND_BITNOT:
    case ND_ADDR:
    case ND_DEREF:
        fprintf(f, "%s", get_unary_op_str(node->kind));
        serialize_expr(f, vm, ctx, node->lhs, node_prec);
        break;

    case ND_CAST:
        fprintf(f, "(");
        serialize_type(f, ctx, node->ty);
        fprintf(f, ")");
        serialize_expr(f, vm, ctx, node->lhs, node_prec);
        break;

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
static void serialize_stmt(FILE *f, JCC *vm, SerializeContext *ctx, Node *node,
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
            serialize_stmt(f, vm, ctx, c->body, indent + 1);
        }
        if (node->default_case) {
            print_indent_level(f, indent);
            fprintf(f, "default:\n");
            serialize_stmt(f, vm, ctx, node->default_case->body, indent + 1);
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

// Serialize a function
static void serialize_function(FILE *f, JCC *vm, SerializeContext *ctx,
                               Obj *fn) {
    if (!fn->is_function)
        return;

    // Skip pragma macro functions (they were consumed)
    // Skip non-definitions
    if (!fn->is_definition && !fn->body)
        return;

    // Static keyword
    if (fn->is_static)
        fprintf(f, "static ");

    // Return type
    if (fn->ty && fn->ty->return_ty)
        serialize_type(f, ctx, fn->ty->return_ty);
    else
        fprintf(f, "int");

    fprintf(f, " %s(", fn->name);

    // Parameters
    bool first = true;
    for (Obj *param = fn->params; param; param = param->next) {
        if (!first)
            fprintf(f, ", ");
        first = false;
        serialize_type_decl(f, ctx, param->ty, param->name);
    }

    if (fn->ty && fn->ty->is_variadic) {
        if (!first)
            fprintf(f, ", ");
        fprintf(f, "...");
    }

    fprintf(f, ")");

    if (fn->body) {
        fprintf(f, " {\n");

        // Local variable declarations
        for (Obj *var = fn->locals; var; var = var->next) {
            if (var->is_param)
                continue;
            print_indent_level(f, 1);
            serialize_type_decl(f, ctx, var->ty, var->name);
            fprintf(f, ";\n");
        }

        // Function body
        for (Node *s = fn->body; s; s = s->next) {
            serialize_stmt(f, vm, ctx, s, 1);
        }

        fprintf(f, "}\n\n");
    } else {
        fprintf(f, ";\n\n");
    }
}

// Serialize global variable
static void serialize_global_var(FILE *f, JCC *vm, SerializeContext *ctx,
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

// Public API: Serialize entire program to C source
void cc_serialize_program(FILE *f, JCC *vm, Obj *prog) {
    if (!f || !prog)
        return;

    SerializeContext ctx = {};
    collect_scope_names(&ctx, vm);
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_function && !obj->is_definition && !obj->body)
            continue;
        if (!obj->is_function && obj->name[0] == '.')
            continue;
        collect_obj_types(&ctx, obj);
    }

    // Header comment
    fprintf(f, "/* Generated by JCC pragma macro expansion */\n\n");

    // Serialize type definitions before declarations that reference them.
    for (int i = 0; i < ctx.defs.len; i++) {
        Type *ty = ctx.defs.data[i];
        if (ty->kind == TY_ENUM)
            serialize_enum_def(f, &ctx, ty);
        else
            serialize_struct_def(f, &ctx, ty);
    }

    // Serialize global variables
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function)
            serialize_global_var(f, vm, &ctx, obj);
    }

    // Serialize functions
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_function)
            serialize_function(f, vm, &ctx, obj);
    }

    free(ctx.seen.data);
    free(ctx.defs.data);
    free(ctx.tags);
    free(ctx.typedefs);
}

// Serialize a single node to a string (for debugging)
char *serialize_node_to_source(JCC *vm, Node *node) {
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
