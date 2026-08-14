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
    // in !generated_only mode (-c=native, -m without -c=generated) to avoid
    // re-emitting a definition the consumer's own #include already provides.
    bool from_include;
    bool always_emit;
    // #953: mirrors TypeNameRecord.file_path -- used in generated_only mode
    // to tell whether this type's declaring header was actually captured
    // into the output.
    char *file_path;
} TypeName;

// #965: pairs a lifted block function (Obj.is_block) with the name of the
// environment struct serialize_block_preamble() emitted for it, so
// serialize_expr's ND_BLOCK_LITERAL/ND_BLOCK_CALL cases and ND_VAR's
// captured-variable lookup can find it again without re-deriving it. Built
// once in serialize_block_preamble(), read-only afterward.
typedef struct {
    Obj *block_fn;
    char *env_struct_name; // includes the leading "struct " keyword
} BlockEnvEntry;

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
    bool emit_cccc; // --emit-cccc: serialize checked-pointer qualifiers instead of dropping them
    int anon_local_counter; // names compiler-synthesized temps (e.g. ++/-- desugaring)
    int anon_global_counter; // names non-string-literal `.L..N` globals (#925)
    // #953: resolved paths of headers actually auto-captured into
    // generated_only (-c=generated) output -- built once in
    // cc_serialize_program from vm->compiler.emit_include_paths. Only
    // consulted in generated_only mode; see serialize_type_defs_for_owner.
    // A VLA's length is an expression node, so serializing its declarator
    // (serialize_type_decl, which has no vm parameter) needs the vm the
    // expression serializer takes. Set once in cc_serialize_program.
    VirtualMachine *vm;
    char **captured_paths;
    int captured_paths_len;
    // #965: block-literal env structs -- see BlockEnvEntry and
    // serialize_block_preamble().
    BlockEnvEntry *block_envs;
    int block_envs_len;
    int block_envs_cap;
    // #989: types promoted from function-local to file scope (a block
    // capture's own struct/union/enum type declared inside a function,
    // needed because its lifted environment struct is emitted at file
    // scope). Doubles as the post-order seen-set during promotion and as
    // the skip-set serialize_type_defs_for_owner uses to avoid re-emitting
    // a definition the preamble already wrote out.
    TypeVec hoisted;
    int hoisted_type_counter; // names renamed/synthesized hoisted tags, parallel to anon_global_counter
} SerializeContext;

// Forward declaration
static void serialize_expr(FILE *f, VirtualMachine *vm, SerializeContext *ctx, Node *node,
                           int parent_prec);
static void serialize_stmt(FILE *f, VirtualMachine *vm, SerializeContext *ctx, Node *node,
                           int indent);
// #964: mutually recursive with serialize_stmt -- see the comment on its
// definition, near ND_BLOCK below.
static void serialize_stmt_list_item(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                                     Node *node, int indent);

// #918: usual_arith_conv() (src/type.c) casts BOTH operands of a pointer
// +/- integer expression to the same pointer type before new_add()/
// new_sub() (src/parse.c) return -- so by the time serialize_expr() sees an
// ND_ADD/ND_SUB node, node->lhs->ty and node->rhs->ty are indistinguishable
// (both TY_PTR). The scaled byte offset new_add()/new_sub() baked in
// (`rhs *= sizeof(*ptr)`) is only recoverable by peeling back to the
// *pre-cast* operand -- these three helpers do that peeling and classify
// what's underneath. Do not rely on operand position (new_add() canonicalizes
// pointer-to-lhs, but set_checked_deref_bounds() in parse.c builds ND_ADD via
// new_binary() directly, which does not canonicalize).
static Node *strip_casts(Node *n) {
    while (n && n->kind == ND_CAST && n->lhs)
        n = n->lhs;
    return n;
}

static bool node_is_pointerish(Node *n) {
    // #964: TY_VLA decays the same as TY_ARRAY in pointer arithmetic (`v + 1`
    // on a VLA `v`) -- without it here, the ND_ADD/ND_SUB case below falls
    // through to plain binary arithmetic and adds two pointers together.
    return n && n->ty && (n->ty->kind == TY_PTR || n->ty->kind == TY_ARRAY ||
                          n->ty->kind == TY_VLA);
}

static bool node_is_integerish(Node *n) {
    return n && n->ty && is_integer(n->ty);
}

// node_is_vla_ptr_assign / node_is_deferred_vla_ptr_init / block_defines_vla
// moved to internal.h (#981) so codegen.c can reuse the identical
// "does this block declare a VLA" check for HMRK/HREL emission -- see
// their comments there.

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
                           bool from_include, bool always_emit,
                           char *file_path) {
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
    (*items)[*len].file_path = file_path;
    (*len)++;
}

static void collect_scope_names(SerializeContext *ctx, VirtualMachine *vm) {
    for (TypeNameRecord *rec = vm->compiler.type_names; rec; rec = rec->next) {
        if (rec->is_tag)
            type_name_push(&ctx->tags, &ctx->tags_len, &ctx->tags_cap, rec->ty,
                           rec->name, rec->name_len, rec->owner_fn,
                           rec->from_include, rec->always_emit, rec->file_path);
        else
            type_name_push(&ctx->typedefs, &ctx->typedefs_len,
                           &ctx->typedefs_cap, rec->ty, rec->name,
                           rec->name_len, rec->owner_fn,
                           rec->from_include, rec->always_emit, rec->file_path);
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

// #999: pointer-identity counterpart to find_typedef_name's structural
// same_type_or_origin match, used for a non-aggregate (scalar/pointer)
// typedef -- struct/union/enum already spell by name via find_typedef_name
// (structural matching there is required: a typedef and its tag are
// different Type objects for the same aggregate). A scalar typedef has no
// tag of its own to distinguish it from the bare builtin type it aliases,
// so matching structurally here would rename every plain use of that
// builtin too (e.g. every `unsigned long` in the program, once one
// `typedef unsigned long DyValue;` exists) -- parse_typedef() now
// copy_type()s a scalar typedef's Type specifically so this exact check
// can tell "this node's type really is the DyValue typedef" apart from
// "this node's type merely has the same underlying representation".
static TypeName *find_typedef_name_exact(SerializeContext *ctx, Type *ty) {
    if (!ctx || !ty)
        return NULL;
    // Walk the ->origin chain copy_type() builds, not just `ty` itself: a
    // parameter's Type is itself a copy_type() of whatever declarator()
    // produced (func_params(), src/parse.c, always makes one more copy per
    // parameter slot regardless of where the parameter's type came from),
    // so a DyValue-typed parameter's own Type is one hop past the Type
    // parse_typedef() actually recorded, not identical to it. Bounded to a
    // handful of hops -- copy_type() chains built while parsing one
    // declarator are short; this is a safety margin against an unforeseen
    // cycle, not a realistic depth.
    for (int hop = 0; ty && hop < 8; ty = ty->origin, hop++)
        for (int i = 0; i < ctx->typedefs_len; i++)
            if (ctx->typedefs[i].ty == ty &&
                name_visible(&ctx->typedefs[i], ctx->current_fn))
                return &ctx->typedefs[i];
    return NULL;
}

// #952: matches a typedef that actually names `ty` itself, not merely a
// same-kind tagless typedef -- e.g. `typedef struct { char *reg_ptr; ...; }
// va_list;` (include/stdarg.h) used to win this lookup for *every* anonymous
// struct in scope, since the loop below only compared ty->kind before
// checking type_has_tag_for_owner. The same_type_or_origin() check makes
// this the "does an alias exist for this exact type" query its caller
// (serialize_type) already assumes it is; unrelated tagless typedefs now
// correctly fall through to serialize_anon_aggregate() instead.
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
        if (!same_type_or_origin(name->ty, ty))
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

// #956: -c=generated support -- tracks which macro-generated functions
// already have a prototype in the output (either from a preceding
// forward-declare or their own definition), so a function body that
// references another generated function whose own emit event hasn't been
// reached yet can have that callee's prototype inserted just ahead of it.
typedef struct {
    Obj **data;
    int len;
    int cap;
} ObjVec;

static bool obj_vec_contains(ObjVec *vec, Obj *obj) {
    for (int i = 0; i < vec->len; i++)
        if (vec->data[i] == obj)
            return true;
    return false;
}

static void obj_vec_push(ObjVec *vec, Obj *obj) {
    if (!obj || obj_vec_contains(vec, obj))
        return;
    if (vec->len == vec->cap) {
        vec->cap = vec->cap ? vec->cap * 2 : 8;
        vec->data = realloc(vec->data, sizeof(Obj *) * vec->cap);
    }
    vec->data[vec->len++] = obj;
}

// Walks a function body for any reference (ND_VAR) to another
// macro-generated function -- both a direct call (ND_FUNCALL through a
// plain ND_VAR callee) and a bare reference used as a function-pointer
// value (e.g. a closure built from `(void (*)(...))some_generated_fn`)
// need the same forward declaration. Mirrors collect_node_types's
// traversal shape.
static void collect_generated_call_targets(Node *node, ObjVec *out) {
    if (!node)
        return;

    if (node->kind == ND_VAR && node->var && node->var->is_function &&
        node->var->is_macro_generated)
        obj_vec_push(out, node->var);

    // #995: a block literal's descriptor initializer references its lifted
    // function through node->block_fn directly, not through an ND_VAR child
    // -- this pass would otherwise miss it entirely, since block_fn's
    // definition is emitted later in event order (the macro_globals drain
    // in macros.c is newest-first, and the calling function's own
    // PublishNode event precedes the block's) than the caller's body that
    // references it.
    if (node->kind == ND_BLOCK_LITERAL && node->block_fn &&
        node->block_fn->is_macro_generated)
        obj_vec_push(out, node->block_fn);

    if (node->kind == ND_SWITCH) {
        collect_generated_call_targets(node->cond, out);
        for (Node *c = node->case_next; c; c = c->case_next)
            collect_generated_call_targets(c->lhs, out);
        if (node->default_case)
            collect_generated_call_targets(node->default_case->lhs, out);
        collect_generated_call_targets(node->next, out);
        return;
    }

    if (node->kind == ND_CASE) {
        collect_generated_call_targets(node->lhs, out);
        collect_generated_call_targets(node->next, out);
        return;
    }

    collect_generated_call_targets(node->lhs, out);
    collect_generated_call_targets(node->rhs, out);
    collect_generated_call_targets(node->cond, out);
    collect_generated_call_targets(node->then, out);
    collect_generated_call_targets(node->els, out);
    collect_generated_call_targets(node->init, out);
    collect_generated_call_targets(node->inc, out);
    collect_generated_call_targets(node->body, out);
    collect_generated_call_targets(node->args, out);

    collect_generated_call_targets(node->next, out);
}

static void serialize_type(FILE *f, SerializeContext *ctx, Type *ty);

// --emit-cccc: format a checked pointer's [[cccc::single/array/ntarray]]
// (+ count()/byte_count()/bounds() bounds form, if any) qualifier for
// re-emission in the post-'*' declarator position it was originally written
// in. Returns "" for an unchecked pointer.
static void format_checked_ptr_qualifier(char *buf, size_t cap, Type *ty) {
    buf[0] = '\0';
    if (!ty || ty->checked_kind == CHECKED_NONE)
        return;
    const char *kind_name = ty->checked_kind == CHECKED_SINGLE  ? "single"
                           : ty->checked_kind == CHECKED_ARRAY   ? "array"
                                                                  : "ntarray";
    int n = snprintf(buf, cap, " [[cccc::%s]]", kind_name);
    if (n < 0 || (size_t)n >= cap)
        return;
    switch (ty->checked_bounds_form) {
    case CB_COUNT:
    case CB_BYTE_COUNT:
        if (ty->checked_bounds_arg1)
            snprintf(buf + n, cap - (size_t)n, " [[cccc::%s(%.*s)]]",
                     ty->checked_bounds_form == CB_COUNT ? "count" : "byte_count",
                     ty->checked_bounds_arg1->len, ty->checked_bounds_arg1->loc);
        break;
    case CB_RANGE:
        if (ty->checked_bounds_arg1 && ty->checked_bounds_arg2)
            snprintf(buf + n, cap - (size_t)n, " [[cccc::bounds(%.*s, %.*s)]]",
                     ty->checked_bounds_arg1->len, ty->checked_bounds_arg1->loc,
                     ty->checked_bounds_arg2->len, ty->checked_bounds_arg2->loc);
        break;
    case CB_UNKNOWN:
        snprintf(buf + n, cap - (size_t)n, " [[cccc::bounds(unknown)]]");
        break;
    case CB_NONE:
    default:
        break;
    }
}

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

    if (ty->kind == TY_VLA) {
        // `int v[n]` -- the length is an expression, not a constant, so it
        // has to go through serialize_expr rather than be printed straight
        // into the declarator buffer like TY_ARRAY's constant length. #964:
        // this used to emit the base type, then name, then `[len]` directly
        // -- correct for a single-dimension VLA, but a nested VLA-of-VLA (or
        // VLA-of-array) mis-spelled as `int[m] v[n]` instead of the correct
        // `int v[n][m]`, since the outer dimension's base was printed before
        // recursing into it rather than after. Route through the same
        // buffer-recursion shape TY_ARRAY uses above -- capture the length
        // expression into a string via open_memstream (this file's existing
        // idiom, see serialize_function_signature()), fold `name[len]` into
        // one declarator buffer, then recurse on ty->base so nested
        // dimensions accumulate in the right order regardless of whether
        // they are VLA or constant-length TY_ARRAY.
        char *lenbuf = NULL;
        size_t lensz = 0;
        FILE *lf = open_memstream(&lenbuf, &lensz);
        if (ty->vla_len && ctx->vm)
            serialize_expr(lf, ctx->vm, ctx, ty->vla_len, 0);
        fclose(lf);

        char buf[1024];
        snprintf(buf, sizeof(buf), "%s[%s]", name ? name : "",
                 lenbuf ? lenbuf : "");
        free(lenbuf);
        serialize_type_decl(f, ctx, ty->base, buf);
        return;
    }

    if (ty->kind == TY_PTR) {
        char buf[1024];
        char qual[256] = "";
        if (ctx->emit_cccc)
            format_checked_ptr_qualifier(qual, sizeof(qual), ty);
        const char *sep = qual[0] ? " " : "";
        // #971: TY_VLA is an array type for declarator-parenthesization
        // purposes, same as TY_ARRAY -- pointer-to-VLA (the row type of a
        // multi-dimensional VLA, `int (*)[m]`) needs the same `(*name)`
        // grouping a fixed-size array pointer gets, or the `*` binds to the
        // element type and mis-spells it as `int *[m]` (array of pointers).
        if (ty->base &&
            (ty->base->kind == TY_ARRAY || ty->base->kind == TY_VLA ||
             ty->base->kind == TY_FUNC))
            snprintf(buf, sizeof(buf), "(*%s%s%s)", qual, sep, name ? name : "");
        else
            snprintf(buf, sizeof(buf), "*%s%s%s", qual, sep, name ? name : "");
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

    // Deliberately no output for ty->checked_kind (#770/#482-484): a
    // checked pointer's [[cccc::single/array/ntarray]] qualifier is a
    // cccc-internal VM-side check, not a real C construct -- gcc/clang would
    // reject the attribute names outright, and #488 requires -E/-c=generated
    // native output to be unchanged for a checked declaration ("no change to ABI or
    // to unchecked callers"). Falls out for free today since this function
    // only ever emits is_const anyway (is_volatile/is_restrict are likewise
    // never serialized), but noted explicitly so it isn't "fixed" by a
    // future generalization of the qualifier-printing above.

    // #999: a scalar (non-aggregate) typedef -- e.g. `typedef unsigned long
    // DyValue;` -- previously always spelled as its canonical underlying
    // type below, losing the typedef entirely. That's a cosmetic gap on a
    // platform where the typedef and the canonical spelling denote the
    // same real type, but a hard "conflicting types" error from the
    // downstream compiler when they don't -- e.g. `uint64_t` is `unsigned
    // long long` on LP64 Darwin, not `unsigned long`, so re-declaring a
    // `uint64_t`-typed function parameter as `unsigned long` collides with
    // that same function's real prototype in a header the output also
    // includes. TY_STRUCT/TY_UNION/TY_ENUM already have their own alias
    // lookup below (find_typedef_name, structural match -- needed there
    // since a typedef and its tag are different Type objects); TY_FUNC
    // recurses into serialize_type_decl and is left alone. Every other
    // kind gets the same treatment here, via find_typedef_name_exact's
    // pointer-identity match (see its own comment for why identity, not
    // structural, matching is required for a scalar).
    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION && ty->kind != TY_ENUM &&
        ty->kind != TY_FUNC) {
        TypeName *alias = find_typedef_name_exact(ctx, ty);
        if (alias) {
            fprintf(f, "%.*s", alias->name_len, alias->name);
            return;
        }
    }

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
    case TY_VLA:
        serialize_type_decl(f, ctx, ty, "");
        break;
    case TY_COMPLEX:
        // `base` is the element float type (see ty_fcomplex/ty_dcomplex/
        // ty_ldcomplex in type.c), so the spelling falls out of it directly.
        fprintf(f, "_Complex ");
        serialize_type(f, ctx, ty->base);
        break;
    case TY_VECTOR:
        // GNU vector: element type + vector_size in *bytes* (ty->size is the
        // total, which is what vector_size takes -- not vec_len). clang and
        // gcc both accept the attribute in this position.
        serialize_type(f, ctx, ty->base);
        fprintf(f, " __attribute__((vector_size(%d)))", ty->size);
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
    case TY_BLOCK:
        // #965: on the default (non `-fblocks`) lowering path a block value
        // is always a pointer to the common-initial-sequence descriptor
        // struct emitted by serialize_block_preamble() -- see the "Blocks"
        // entry in COVERAGE.md's serialized-output-divergences section.
        // TY_BLOCK never needs a case in serialize_type_decl (unlike
        // TY_PTR/TY_ARRAY/TY_VLA/TY_FUNC): it's already an atomic
        // pointer-sized type here, not a container recursing into a base,
        // so the declarator-building default branch's plain "<type> <name>"
        // handles it correctly.
        fprintf(f, "struct __cccc_block *");
        break;
    case TY_ERROR:
    case TY_AUTO:
        // #963c: both are internal sentinels that must never survive to
        // serialization. TY_ERROR only exists after a compile error has
        // already been recorded (which bails out before this function is
        // ever reached); TY_AUTO (C23 `auto`) is resolved to the inferred
        // concrete type at parse time, before -m/-c=native/-c=generated's
        // serialization pass runs. Reaching either case here means an
        // internal invariant was violated upstream, not that the user wrote
        // something unsupported -- hence a hard error naming the kind
        // rather than a silently emitted comment (see the default: arm
        // below for the general case this guards against).
        error("cccc: internal error: TypeKind '%s' reached the serializer "
              "unresolved (should have been eliminated before serialization)",
              cc_type_kind_name(ty->kind));
        break;
    default:
        // #963c: every TypeKind is expected to have an explicit case above.
        // This used to emit "/* unknown type */" and keep going, producing
        // C the host compiler then rejected at the use site -- a delayed,
        // confusing failure. Fail immediately and name the kind instead, so
        // the next TypeKind added without a case here is caught at
        // implementation/test time rather than silently miscompiling.
        error("cccc: internal error: no serializer case for TypeKind '%s' "
              "(kind %d)", cc_type_kind_name(ty->kind), ty->kind);
        break;
    }
}

// Print an escaped string literal covering exactly `len` bytes of `str` --
// NOT NUL-terminated iteration. #918: a NUL-terminated for-loop (the
// previous implementation) truncates at the first embedded NUL, silently
// dropping any bytes after it (e.g. `char a[4] = {1,0,2,0};`, legal C with
// no string semantics at all). NUL bytes are always escaped as the 3-digit
// octal form `\000` (never the 1-digit `\0`) -- `\0` immediately followed
// by an ASCII digit in the emitted source (e.g. a NUL followed by the
// character '1') would be misparsed by the host compiler as a 2-digit
// octal escape `\01`; `\000` has no such ambiguity.
static void serialize_string_n(FILE *f, const char *str, int len) {
    fprintf(f, "\"");
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        switch (c) {
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
        default:
            if (c >= 32 && c < 127)
                fputc(c, f);
            else
                fprintf(f, "\\%03o", c);
            break;
        }
    }
    fprintf(f, "\"");
}

// #965: index of `var` in `block_fn`'s capture list, or -1. Mirrors
// find_capture_index (codegen.c) exactly -- kept as an independent copy
// here rather than shared, since codegen's version is `static` in a
// different translation unit and this file's dependency surface is
// otherwise limited to internal.h's declarations.
static int block_capture_index(Obj *block_fn, Obj *var) {
    if (!block_fn || !var)
        return -1;
    for (int i = 0; i < block_fn->num_captures; i++)
        if (block_fn->captures[i] == var)
            return i;
    return -1;
}

// #965: the env struct name serialize_block_preamble() paired with
// `block_fn`, or NULL if `block_fn` isn't a block function (or somehow has
// no entry -- defensive only, every Obj.is_block function gets one).
static const char *find_block_env(SerializeContext *ctx, Obj *block_fn) {
    for (int i = 0; i < ctx->block_envs_len; i++)
        if (ctx->block_envs[i].block_fn == block_fn)
            return ctx->block_envs[i].env_struct_name;
    return NULL;
}

// #965: when `var` is captured by the block literal currently being
// serialized (ctx->current_fn), print the descriptor-field access that
// reaches it through __static_link and return true; otherwise print
// nothing and return false so the caller falls back to the variable's
// plain name. Mirrors gen_addr's cap_idx >= 0 branch (codegen.c) exactly:
// a plain capture's descriptor field holds the snapshotted value directly,
// while an is_block_var capture's field holds the shared heap box's
// pointer, so reading the *value* needs one extra dereference -- the `->`
// in `(*((T*)p)->__capK)` already binds tighter than the outer `*`, so this
// is unambiguous without further parenthesization.
static bool serialize_block_capture_ref(FILE *f, SerializeContext *ctx, Obj *var) {
    if (!ctx->current_fn || !ctx->current_fn->is_block)
        return false;
    int idx = block_capture_index(ctx->current_fn, var);
    if (idx < 0)
        return false;
    const char *env = find_block_env(ctx, ctx->current_fn);
    if (!env)
        env = "struct __cccc_block_env_?"; // defensive only, see find_block_env
    if (var->is_block_var)
        fprintf(f, "(*((%s *)__static_link)->__cap%d)", env, idx);
    else
        fprintf(f, "((%s *)__static_link)->__cap%d", env, idx);
    return true;
}

// True when an ND_ALOAD/ND_ASTORE address expression has a pointee the
// __atomic_* builtins accept. Mirrors codegen's ALDR/ASTR guard (1/2/4/8-byte
// non-float pointee); anything else takes codegen's plain load/store fallback,
// so serializing it as a plain dereference matches the VM -- and
// __atomic_load_n would not compile on a float or aggregate pointee anyway.
static bool atomic_serializable_pointee(Node *addr) {
    if (!addr || !addr->ty || !addr->ty->base)
        return false;
    Type *base = addr->ty->base;
    if (is_flonum(base))
        return false;
    return base->size == 1 || base->size == 2 || base->size == 4 || base->size == 8;
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
            // A dotted `.L..N` name means "anonymous global" (new_anon_gvar,
            // parse.c) -- shared by string literals, static locals, and
            // compound literals (#925). Only a genuine string literal
            // inlines as string text here; the other two are renamed to a
            // valid identifier and given a real definition by
            // rename_anon_globals() before this ever runs, so they hit the
            // plain `fprintf(f, "%s", ...)` fallback below like any other
            // named var.
            if (node->var->is_string_literal) {
                // #918: use the global's actual array length, not NUL
                // termination -- an anonymous string-literal global can
                // legitimately contain embedded NULs (wide/multi-part
                // literals, __func__ splicing, etc).
                int len = (node->var->ty && node->var->ty->kind == TY_ARRAY)
                              ? node->var->ty->array_len
                              : (int)strlen(node->var->init_data);
                serialize_string_n(f, node->var->init_data, len);
            } else if (serialize_block_capture_ref(f, ctx, node->var)) {
                // #965: handled -- var is captured by the block literal
                // currently being serialized.
            } else if (node->var->is_block_var) {
                // #965: a __block local's stack slot now holds a heap box
                // pointer (see serialize_function's hoist loop) -- read
                // through it so the expression's spelled type matches
                // node->ty (the variable's declared type, not a pointer to
                // it). Only reached here for a __block var referenced from
                // *outside* any block capturing it (or from the same
                // function that declared it); the captured case above
                // already applies its own dereference.
                fprintf(f, "(*%s)", node->var->name);
            } else {
                fprintf(f, "%s", node->var->name);
            }
        } else {
            fprintf(f, "/* unknown_var */");
        }
        break;

    case ND_ADD:
    case ND_SUB: {
        // #918: pointer arithmetic needs (char *)-based casts, not the
        // naively-printed operand types -- see the strip_casts()/
        // node_is_pointerish()/node_is_integerish() comment above.
        Node *lhs_inner = strip_casts(node->lhs);
        Node *rhs_inner = strip_casts(node->rhs);
        bool lhs_ptr = node_is_pointerish(lhs_inner);
        bool rhs_ptr = node_is_pointerish(rhs_inner);
        bool lhs_int = node_is_integerish(lhs_inner);
        bool rhs_int = node_is_integerish(rhs_inner);

        if (node->kind == ND_SUB && lhs_ptr && rhs_ptr) {
            // ptr - ptr: new_sub() already wraps this whole node in an
            // outer `(hi - lo) / elemsize` ND_DIV (untouched here) -- only
            // the pointer subtraction itself needs (char *) casts, so the
            // host compiler doesn't scale by its own idea of the element
            // size on top of that division.
            fprintf(f, "((char *)");
            serialize_expr(f, vm, ctx, node->lhs, 14);
            fprintf(f, " - (char *)");
            serialize_expr(f, vm, ctx, node->rhs, 14);
            fprintf(f, ")");
            break;
        }

        if (lhs_ptr && rhs_int) {
            // ptr +/- num: rhs is already the byte-scaled offset new_add()/
            // new_sub() computed (rhs *= sizeof(*ptr)) -- casting the
            // pointer to (char *) before applying it, then casting the
            // whole result back to node->ty, keeps the host from scaling
            // the offset a second time. Print rhs_inner (not node->rhs):
            // the outer cast usual_arith_conv() wrapped it in is a bogus
            // (pointer-typed) cast on an integer offset, and printing it
            // would produce the exact `ptr + (int *)offset` error this
            // fix exists to avoid.
            //
            // #928: node->ty can itself be an array type (e.g. a reflection
            // MakeSubscript() on an array-typed anon global) -- serialize_type
            // would print `(int [3])`, and a cast to array type is not valid
            // C. Cast to pointer-to-element instead; the ND_DEREF this node
            // is wrapped in still reads the right value through it. #964:
            // node->ty can also be TY_VLA (`int[n]`), same fix applies.
            fprintf(f, "(");
            if (node->ty && (node->ty->kind == TY_ARRAY || node->ty->kind == TY_VLA)) {
                serialize_type(f, ctx, node->ty->base);
                fprintf(f, " *");
            } else {
                serialize_type(f, ctx, node->ty);
            }
            fprintf(f, ")((char *)");
            serialize_expr(f, vm, ctx, node->lhs, 14);
            fprintf(f, " %s ", get_binary_op_str(node->kind));
            serialize_expr(f, vm, ctx, rhs_inner, node_prec + 1);
            fprintf(f, ")");
            break;
        }

        if (node->kind == ND_ADD && rhs_ptr && lhs_int) {
            // num + ptr: new_add() canonicalizes this to ptr + num, but
            // set_checked_deref_bounds() builds ND_ADD via new_binary()
            // directly and does not canonicalize -- handle it defensively.
            // Print lhs_inner for the same reason as above.
            // #928: same array-cast fix as the ptr+num arm above; #964
            // extends it to TY_VLA (a cast to `int[n]` is equally invalid C).
            fprintf(f, "(");
            if (node->ty && (node->ty->kind == TY_ARRAY || node->ty->kind == TY_VLA)) {
                serialize_type(f, ctx, node->ty->base);
                fprintf(f, " *");
            } else {
                serialize_type(f, ctx, node->ty);
            }
            fprintf(f, ")((char *)");
            serialize_expr(f, vm, ctx, node->rhs, 14);
            fprintf(f, " + ");
            serialize_expr(f, vm, ctx, lhs_inner, node_prec + 1);
            fprintf(f, ")");
            break;
        }

        // Plain arithmetic (int+int, float+float, ...).
        serialize_expr(f, vm, ctx, node->lhs, node_prec);
        fprintf(f, " %s ", get_binary_op_str(node->kind));
        serialize_expr(f, vm, ctx, node->rhs, node_prec + 1);
        break;
    }

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
        // #969: __builtin_pc_function_name / __builtin_pc_source_location
        // lower to a call into a VM-only FFI shim (__cccc_pc_to_name /
        // __cccc_pc_to_source, cc_load_symbolize_runtime, debugger.c) whose
        // argument is a VM bytecode offset. Neither the shim nor the symbol
        // table it reads exists natively, so there is nothing to lower to --
        // reject here rather than emit a call the host compiler rejects by
        // its internal name. Deliberately not rejected at parse time
        // (primary(), parse.c): under -c=generated only *generated* code is
        // serialized, and a __builtin_pc_* call in VM-only code is legal
        // there.
        if (node->lhs && node->lhs->kind == ND_VAR && node->lhs->var) {
            const char *pc_builtin = NULL;
            if (vm->compiler.builtin_pc_to_name &&
                node->lhs->var == vm->compiler.builtin_pc_to_name)
                pc_builtin = "__builtin_pc_function_name";
            else if (vm->compiler.builtin_pc_to_source &&
                     node->lhs->var == vm->compiler.builtin_pc_to_source)
                pc_builtin = "__builtin_pc_source_location";
            if (pc_builtin) {
                if (node->tok)
                    error_tok(vm, node->tok,
                              "%s cannot be serialized to C: it resolves a "
                              "VM bytecode offset via the VM's symbol "
                              "table, which does not exist natively",
                              pc_builtin);
                else
                    error("cccc: %s cannot be serialized to C: it resolves "
                          "a VM bytecode offset via the VM's symbol table, "
                          "which does not exist natively", pc_builtin);
            }
        }
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
        if (node->var && node->var->is_block_var)
            // #965: a __block local's slot holds the heap box *pointer*
            // (see serialize_function's hoist loop), not the storage
            // itself -- &name/sizeof(name) would zero the 8-byte pointer
            // slot instead of the real storage. Mirrors codegen's own
            // is_block_var arm on ND_MEMZERO (codegen.c), the same arm
            // #982's TY_VLA case was modelled on.
            fprintf(f, "__builtin_memset(%s, 0, sizeof(*%s))",
                    node->var->name, node->var->name);
        else if (node->var)
            fprintf(f, "__builtin_memset(&%s, 0, sizeof(%s))",
                    node->var->name, node->var->name);
        else
            fprintf(f, "/* memzero */");
        break;

    case ND_NULL_EXPR:
        // Empty expression
        break;

    case ND_FRAME_ADDR:
        // The parser rejects any level but 0, so there is nothing to carry.
        fprintf(f, "__builtin_frame_address(0)");
        break;

    case ND_RETURN_ADDR:
        // The *value* diverges by design: under the VM this is a bytecode pc
        // cast to void*, natively it is a real host return address. Both are
        // "the return address n frames up" in their own runtime, which is the
        // most faithful mapping available -- see COVERAGE.md.
        fprintf(f, "__builtin_return_address(%lld)", (long long)node->val);
        break;

    case ND_UNREACHABLE:
        // __builtin_unreachable, __builtin_trap and __builtin_debugtrap all
        // lower to the same BTRAP opcode, so the VM traps for all three and
        // the original spelling is not recoverable here. __builtin_trap() is
        // the emission that matches that behaviour; __builtin_unreachable()
        // would be UB natively and the optimizer would delete the path.
        fprintf(f, "__builtin_trap()");
        break;

    case ND_BITOP: {
        // val = (op << 8) | width. popcount/parity encode width 0 (see
        // parse.c), so the `ll` variant has to come from the argument's own
        // type -- emitting __builtin_popcount for a 64-bit argument would
        // compile cleanly and silently truncate.
        int op = (int)(node->val >> 8);
        int width = (int)(node->val & 0xff);
        bool wide = node->lhs && node->lhs->ty && node->lhs->ty->size == 8;
        const char *name;
        switch (op) {
        case 0: name = (width == 64) ? "__builtin_clzll" : "__builtin_clz"; break;
        case 1: name = (width == 64) ? "__builtin_ctzll" : "__builtin_ctz"; break;
        case 2: name = wide ? "__builtin_popcountll" : "__builtin_popcount"; break;
        case 3: name = wide ? "__builtin_parityll" : "__builtin_parity"; break;
        case 4: name = (width == 64) ? "__builtin_ffsll" : "__builtin_ffs"; break;
        default:
            // bswap: `width` is the byte count, not a bit width.
            name = (width == 2) ? "__builtin_bswap16"
                 : (width == 4) ? "__builtin_bswap32"
                                : "__builtin_bswap64";
            break;
        }
        fprintf(f, "%s(", name);
        serialize_expr(f, vm, ctx, node->lhs, 0);
        fprintf(f, ")");
        break;
    }

    case ND_VLA_PTR:
        // #964: `v` decayed to `v` -- serialize_stmt_list_item()/the
        // ND_EXPR_STMT case above already replace this node's only
        // constructor site (the `v = alloca(...)` assignment, parse.c) with
        // a real declaration, so this is a defensive fallback for any other
        // use of the variable (e.g. `v[0]` decays through ND_ADD, which
        // reaches here via node->lhs). `v` is now a genuine C array/VLA
        // local, so referencing its name is correct in both lvalue and
        // rvalue position.
        fprintf(f, "%s", node->var ? node->var->name : "/* unknown_vla */");
        break;

    case ND_OVERFLOW_ARITH: {
        // #964: val: 0=add 1=sub 2=mul (parse.c); lhs/rhs are the operands,
        // cas_addr the result pointer -- this maps directly onto the same
        // three GCC/clang builtins the parser accepted, both of which
        // support this signature natively.
        static const char *names[] = {
            "__builtin_add_overflow", "__builtin_sub_overflow", "__builtin_mul_overflow",
        };
        const char *name = (node->val >= 0 && node->val <= 2) ? names[node->val]
                                                               : "__builtin_add_overflow";
        fprintf(f, "%s(", name);
        serialize_expr(f, vm, ctx, node->lhs, 0);
        fprintf(f, ", ");
        serialize_expr(f, vm, ctx, node->rhs, 0);
        fprintf(f, ", ");
        serialize_expr(f, vm, ctx, node->cas_addr, 0);
        fprintf(f, ")");
        break;
    }

    case ND_DYNOBJ_SIZE:
        fprintf(f, "__builtin_dynamic_object_size(");
        serialize_expr(f, vm, ctx, node->lhs, 0);
        fprintf(f, ", %lld)", (long long)node->val);
        break;

    case ND_ALOAD:
        // codegen only takes the atomic path for 1/2/4/8-byte non-float
        // pointees and falls back to a plain load otherwise; mirror that,
        // since __atomic_load_n does not accept a float or aggregate pointee
        // and the VM is not being atomic there either.
        if (atomic_serializable_pointee(node->lhs)) {
            fprintf(f, "__atomic_load_n(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ", __ATOMIC_SEQ_CST)");
        } else {
            fprintf(f, "(*(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, "))");
        }
        break;

    case ND_ASTORE:
        // An atomic store in *expression* position has to yield the stored
        // value (codegen gives it C assignment semantics) but
        // __atomic_store_n returns void, so the value is threaded through a
        // statement expression rather than evaluating the operand twice. The
        // common statement-position case is handled in serialize_stmt and
        // emits the plain call.
        if (atomic_serializable_pointee(node->lhs)) {
            fprintf(f, "__extension__ ({ __typeof__(*(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ")) __cccc_astore_v = (");
            serialize_expr(f, vm, ctx, node->rhs, 0);
            fprintf(f, "); __atomic_store_n(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ", __cccc_astore_v, __ATOMIC_SEQ_CST); __cccc_astore_v; })");
        } else {
            fprintf(f, "(*(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ") = ");
            serialize_expr(f, vm, ctx, node->rhs, 0);
            fprintf(f, ")");
        }
        break;

    case ND_EXCH:
        // codegen rejects float/odd-size pointees outright for exchange and
        // compare-and-swap, so these two map 1:1 with no fallback arm.
        fprintf(f, "__atomic_exchange_n(");
        serialize_expr(f, vm, ctx, node->lhs, 0);
        fprintf(f, ", ");
        serialize_expr(f, vm, ctx, node->rhs, 0);
        fprintf(f, ", __ATOMIC_SEQ_CST)");
        break;

    case ND_CAS:
        // (obj, *expected, desired) -> bool, matching codegen's ACAS contract:
        // cas_old is a *pointer* to the expected value, as __atomic_compare_
        // exchange_n also takes. weak = 0.
        fprintf(f, "__atomic_compare_exchange_n(");
        serialize_expr(f, vm, ctx, node->cas_addr, 0);
        fprintf(f, ", ");
        serialize_expr(f, vm, ctx, node->cas_old, 0);
        fprintf(f, ", ");
        serialize_expr(f, vm, ctx, node->cas_new, 0);
        fprintf(f, ", 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)");
        break;

    case ND_LABEL_VAL:
        // [GNU] labels-as-values. node->label is the source identifier, the
        // same one ND_LABEL/ND_GOTO already serialize.
        fprintf(f, "&&%s", node->label ? node->label : "/* unknown label */");
        break;

    case ND_COMPLEX: {
        // val: 0 = construct from (real, imag), 1 = creal, 2 = cimag,
        // 3 = conj. The f/l suffix follows the element float type.
        Type *elem = node->ty;
        if (elem && elem->kind == TY_COMPLEX && elem->base)
            elem = elem->base;
        const char *suffix = !elem                     ? ""
                           : (elem->kind == TY_FLOAT)  ? "f"
                           : (elem->kind == TY_LDOUBLE) ? "l"
                                                        : "";
        if (node->val == 0) {
            // __builtin_complex requires both operands to have the same real
            // floating type, so each is cast to the element type explicitly.
            fprintf(f, "__builtin_complex((");
            serialize_type(f, ctx, elem);
            fprintf(f, ")(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, "), (");
            serialize_type(f, ctx, elem);
            fprintf(f, ")(");
            serialize_expr(f, vm, ctx, node->rhs, 0);
            fprintf(f, "))");
        } else {
            const char *name = (node->val == 1) ? "creal"
                             : (node->val == 2) ? "cimag"
                                                : "conj";
            fprintf(f, "__builtin_%s%s(", name, suffix);
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ")");
        }
        break;
    }

    case ND_CONVERTVECTOR:
        fprintf(f, "__builtin_convertvector(");
        serialize_expr(f, vm, ctx, node->lhs, 0);
        fprintf(f, ", ");
        serialize_type(f, ctx, node->ty);
        fprintf(f, ")");
        break;

    case ND_DECIMAL_TO_CHARS:
        // #402, CCCC_HAS_DECIMAL builds only. Unlike every other builtin
        // here there is no host equivalent to lower to -- clang and gcc have
        // no _Decimal support at all -- so this fails loudly rather than
        // fabricating a call that would not link.
        error("cccc: _Decimal is not supported in native/serialized output "
              "(__builtin_decimal_to_chars has no host equivalent)");
        break;

    case ND_BLOCK_LITERAL: {
        // #965: default lowering -- lift + explicit env struct
        // (serialize_block_preamble, block_capture_index). The env struct
        // instance lives in node->block_desc_var, an existing local on the
        // *enclosing* function's frame (block_literal(), parse.c) --
        // exactly matching the VM's own per-invocation stack descriptor
        // (ND_BLOCK_LITERAL, codegen.c), so lifetime semantics carry over
        // unchanged. Building it as a comma expression writes into that
        // named local (rather than a temporary), which is what keeps the
        // block value's address stable for the rest of the enclosing
        // scope.
        Obj *block_fn = node->block_fn;
        const char *env = block_fn ? find_block_env(ctx, block_fn) : NULL;
        if (!node->block_desc_var || !block_fn || !env) {
            // #963c: this used to fall back to the same
            // "/* unsupported expr kind N */" comment as the generic
            // default: arm below -- a silent drop inside a *handled* case,
            // in the newest code in this file. block_desc_var/block_fn/env
            // are all set unconditionally by block_literal() (parse.c) and
            // serialize_block_preamble()'s registration pass before this
            // function ever runs, so reaching here means one of those
            // invariants was violated upstream; fail loudly instead of
            // emitting a null expression.
            if (node->tok)
                error_tok(vm, node->tok,
                          "internal error: block literal is missing its "
                          "descriptor local, function, or env struct at "
                          "serialization time");
            else
                error("cccc: internal error: block literal is missing its "
                      "descriptor local, function, or env struct at "
                      "serialization time");
            break;
        }
        if (!ctx->current_fn) {
            // A block literal's descriptor is a *local* -- there is no
            // enclosing frame to hold one at file scope.
            if (node->tok)
                error_tok(vm, node->tok,
                          "a block literal cannot be serialized at file "
                          "scope (its descriptor needs an enclosing "
                          "function's frame)");
            else
                error("cccc: a block literal cannot be serialized at file scope");
        }

        const char *desc = node->block_desc_var->name;
        fprintf(f, "(%s.__invoke = (void *)%s, %s.__size = (long)sizeof(%s)",
                desc, block_fn->name, desc, desc);
        for (int i = 0; i < node->num_block_captures; i++) {
            Obj *cap = node->block_captures[i];
            // #994: a by-value capture whose type is an array (accepted by
            // the parser like clang rejects but this compiler doesn't --
            // collect_captures_in_node has no guard) can't use plain `=` --
            // C forbids array assignment. The env struct field is declared
            // with the real array type (serialize_block_preamble), so copy
            // through __builtin_memcpy instead; every other capture kind
            // (scalar, struct/union, block-var pointer) keeps plain `=`,
            // valid C for all of them.
            bool is_array_cap = !cap->is_block_var && cap->ty->kind == TY_ARRAY;
            if (is_array_cap)
                fprintf(f, ", __builtin_memcpy(%s.__cap%d, ", desc, i);
            else
                fprintf(f, ", %s.__cap%d = ", desc, i);

            // Mirrors codegen's ND_BLOCK_LITERAL capture-copy loop
            // (codegen.c) exactly, three sources in the same order:
            int enc_idx = (ctx->current_fn->is_block)
                              ? block_capture_index(ctx->current_fn, cap) : -1;
            const char *enc_env =
                enc_idx >= 0 ? find_block_env(ctx, ctx->current_fn) : NULL;
            if (enc_idx >= 0 && enc_env) {
                // Transitive capture: read from the enclosing block's own
                // descriptor via __static_link. Exactly one dereference
                // either way -- for an is_block_var capture the parent's
                // field already holds the box pointer (copied verbatim
                // below); for a plain capture the parent's field holds the
                // value itself.
                fprintf(f, "((%s *)__static_link)->__cap%d", enc_env, enc_idx);
            } else if (cap->is_block_var) {
                // Direct __block local in the enclosing stack: copy its box
                // pointer verbatim -- the new field is T*, matching it.
                fprintf(f, "%s", cap->name);
            } else {
                // Ordinary local or global: copy its value.
                fprintf(f, "%s", cap->name);
            }
            if (is_array_cap)
                fprintf(f, ", sizeof(%s.__cap%d))", desc, i);
        }
        fprintf(f, ", (struct __cccc_block *)&%s)", desc);
        break;
    }

    case ND_BLOCK_CALL: {
        // #965: GNU statement expression -- the descriptor pointer is
        // needed twice (loaded from ->__invoke, then passed again as the
        // static link), and evaluating node->lhs a second time would be
        // wrong for a non-idempotent expression (e.g. a block-returning
        // function call as the callee). gcc and clang both accept
        // statement expressions, and this file's output is already
        // GNU-flavoured (__builtin_memset/__builtin_memcpy elsewhere,
        // ND_STMT_EXPR itself above).
        fprintf(f, "({ struct __cccc_block *__cccc_blk = (struct __cccc_block *)(");
        serialize_expr(f, vm, ctx, node->lhs, 0);
        fprintf(f, "); ((");
        serialize_type(f, ctx, node->ty);
        fprintf(f, " (*)(void *");
        Type *block_ty = (node->lhs && node->lhs->ty &&
                          node->lhs->ty->kind == TY_BLOCK)
                             ? node->lhs->ty : NULL;
        for (Type *p = block_ty ? block_ty->params : NULL; p; p = p->next) {
            fprintf(f, ", ");
            serialize_type(f, ctx, p);
        }
        fprintf(f, "))__cccc_blk->__invoke)(__cccc_blk");
        for (Node *arg = node->args; arg; arg = arg->next) {
            fprintf(f, ", ");
            serialize_expr(f, vm, ctx, arg, 0);
        }
        fprintf(f, "); })");
        break;
    }

    case ND_MACRO_CALL:
    case ND_INIT_SPLICE:
        // #963c: both are comptime-internal and are consumed before this
        // function ever runs -- ND_MACRO_CALL is compiled away by
        // compile_all_macros/cc_eager_expand_macro_call during
        // cc_expand_macros (main.c), which always runs ahead of the
        // -m/-c=native/-c=generated serialization pass, and the one path
        // that could defer one into a global initializer
        // (has_pending_macro_init, parse.c) is resolved to concrete .data
        // bytes by cc_finalize_macro_gvar_inits, also inside
        // cc_expand_macros, before serialization starts. ND_INIT_SPLICE is
        // likewise expanded away by quote_substitute at comptime. Reaching
        // either case here means a macro/splice escaped expansion, which is
        // an internal invariant violation, not user-writable input -- fail
        // loudly and name the kind rather than emitting a silently-dropped
        // comment.
        if (node->tok)
            error_tok(vm, node->tok,
                      "internal error: %s reached the serializer "
                      "unexpanded (should have been resolved during "
                      "macro/comptime expansion)",
                      cc_node_kind_name(node->kind));
        else
            error("cccc: internal error: %s reached the serializer "
                  "unexpanded (should have been resolved during "
                  "macro/comptime expansion)", cc_node_kind_name(node->kind));
        break;

    default:
        // #963c: every reachable NodeKind is expected to have an explicit
        // case above (see COVERAGE.md's "Serialized-output divergences"
        // section for the constructs that are intentionally dropped with a
        // diagnostic rather than serialized). This used to emit
        // "/* unsupported expr kind N */" and keep going -- in expression
        // position that fails the host build loudly, but in statement
        // position (serialize_stmt's own default: routes here and appends
        // ";") it produced a syntactically valid null statement: the
        // construct silently vanished and the native binary returned a
        // different answer than the VM (#963's whole motivation). Fail
        // immediately and name the kind instead, so the next NodeKind added
        // without a case here is caught at implementation/test time rather
        // than silently miscompiling.
        if (node->tok)
            error_tok(vm, node->tok,
                      "internal error: no serializer case for %s (kind %d)",
                      cc_node_kind_name(node->kind), node->kind);
        else
            error("cccc: internal error: no serializer case for %s (kind %d)",
                  cc_node_kind_name(node->kind), node->kind);
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
        // #964: `v = alloca(tmp)` is declaration()'s lowering of a VLA local
        // -- re-emitting it literally would diverge from VM semantics
        // (alloca in a loop body is not freed per iteration the way a real
        // VLA is, so a loop declaring a VLA would grow the host stack
        // unboundedly) and the assignment target isn't a valid C lvalue once
        // `v` is a genuine array. Emit a real declaration in its place
        // instead; serialize_stmt_list_item() keeps the enclosing block
        // unbraced so it stays visible to later statements.
        if (node_is_vla_ptr_assign(node->lhs)) {
            Obj *var = node->lhs->lhs->var;
            print_indent_level(f, indent);
            serialize_type_decl(f, ctx, var->ty, var->name);
            fprintf(f, ";\n");
            break;
        }
        // #973 follow-up: the initializer of a pointer-to-VLA local (see
        // Obj.deferred_vla_ptr_init, cccc.h) was skipped by the hoist loop
        // above -- this is its recorded in-place declaration site. Emit a
        // real declaration with the initializer attached instead of a bare
        // assignment to an as-yet-undeclared name. Identity (not shape)
        // comparison: node->lhs is a plain ND_ASSIGN like any reassignment
        // of the same variable would produce, so only the exact node
        // recorded at parse time is treated as the declaration.
        if (node_is_deferred_vla_ptr_init(node->lhs)) {
            Obj *var = node->lhs->lhs->var;
            print_indent_level(f, indent);
            serialize_type_decl(f, ctx, var->ty, var->name);
            fprintf(f, " = ");
            serialize_expr(f, vm, ctx, node->lhs->rhs, 0);
            fprintf(f, ";\n");
            break;
        }
        // An atomic store written as its own statement (the usual case)
        // discards its value, so hand it to the ND_ASTORE statement case and
        // emit the plain void-returning call rather than the value-producing
        // statement expression.
        if (node->lhs && node->lhs->kind == ND_ASTORE) {
            serialize_stmt(f, vm, ctx, node->lhs, indent);
            break;
        }
        print_indent_level(f, indent);
        serialize_expr(f, vm, ctx, node->lhs, 0);
        fprintf(f, ";\n");
        break;

    case ND_BLOCK:
        print_indent_level(f, indent);
        fprintf(f, "{\n");
        for (Node *s = node->body; s; s = s->next) {
            serialize_stmt_list_item(f, vm, ctx, s, indent + 1);
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
        // #927: a declaration-form init (`for (int i = 0; ...)`) parses as
        // an ND_BLOCK whose body is one ND_EXPR_STMT per declarator
        // (declaration(), parse.c) -- not an expression, so handing it to
        // serialize_expr fell through to its default case and silently
        // dropped the initialization (loop variable left uninitialized;
        // #963c has since turned that default case into a hard error, so
        // this exact failure mode can no longer reach the host compiler
        // silently -- this ND_FOR handling avoids it in the first place by
        // never calling serialize_expr on the ND_BLOCK at all). The
        // declarations themselves
        // are already hoisted to the top of the function by
        // serialize_function(); only the initializing assignment(s) belong
        // in the init clause, comma-joined for a multi-declarator init
        // (`for (int i = 0, j = 1; ...)`). A no-initializer declaration
        // (`for (int i; ...)`) has an empty body -- emit nothing, matching
        // a bare `for (;;)`-style empty init clause. A non-declaration init
        // (`for (i = 0; ...)`) is a bare ND_EXPR_STMT (expr_stmt(),
        // parse.c) and serializes the same way.
        if (node->init) {
            // #964: a VLA declared in a for-loop initializer (`for (int i =
            // 0, v[n]; ...)`) parses and runs in the VM, but this init
            // clause is serialized as comma-joined *assignments* below --
            // C forbids mixing a declaration with expressions there, and
            // hoisting the declaration out ahead of the loop would change
            // its scope/lifetime (and can read a variable the init clause
            // itself assigns). Rejected with a diagnostic rather than
            // emitted as broken C; doing this properly is tracked as a
            // follow-up.
            if (node->init->kind == ND_BLOCK && block_defines_vla(node->init))
                error_tok(vm, node->tok,
                         "a variable-length array declared in a for-loop "
                         "initializer cannot be serialized to C");
            if (node->init->kind == ND_BLOCK) {
                bool first_init = true;
                for (Node *s = node->init->body; s; s = s->next) {
                    if (s->kind != ND_EXPR_STMT || is_noop_expr(s->lhs))
                        continue;
                    if (!first_init)
                        fprintf(f, ", ");
                    first_init = false;
                    serialize_expr(f, vm, ctx, s->lhs, 0);
                }
            } else if (node->init->kind == ND_EXPR_STMT) {
                if (!is_noop_expr(node->init->lhs))
                    serialize_expr(f, vm, ctx, node->init->lhs, 0);
            } else {
                serialize_expr(f, vm, ctx, node->init, 0);
            }
        }
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

    case ND_GOTO_EXPR:
        // [GNU] `goto *ptr`. Parsed by stmt() and consuming its own `;`, so
        // it is a statement here even though the audit files it with the
        // expression kinds.
        print_indent_level(f, indent);
        fprintf(f, "goto *(");
        serialize_expr(f, vm, ctx, node->lhs, 0);
        fprintf(f, ");\n");
        break;

    case ND_ASTORE:
        // Statement position discards the result, so the plain void-returning
        // call is enough -- no statement expression needed. See the
        // ND_ASTORE case in serialize_expr for the value-producing form.
        print_indent_level(f, indent);
        if (atomic_serializable_pointee(node->lhs)) {
            fprintf(f, "__atomic_store_n(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ", ");
            serialize_expr(f, vm, ctx, node->rhs, 0);
            fprintf(f, ", __ATOMIC_SEQ_CST);\n");
        } else {
            fprintf(f, "*(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ") = ");
            serialize_expr(f, vm, ctx, node->rhs, 0);
            fprintf(f, ";\n");
        }
        break;

    case ND_ASM:
        // asm is the one construct deliberately emitted verbatim even though
        // the VM does not execute it by default (--asm-passthru opts into VM
        // execution): there is no way to evaluate host assembly in the VM, so
        // native output hands it to the host compiler. See COVERAGE.md.
        print_indent_level(f, indent);
        fprintf(f, "asm(");
        if (node->asm_str)
            serialize_string_n(f, node->asm_str, (int)strlen(node->asm_str));
        else
            fprintf(f, "\"\"");
        fprintf(f, ");\n");
        break;

    default:
        // Treat as expression statement. #963c deliberately leaves this
        // default: arm alone: it is the legitimate route for every
        // expression-kind NodeKind reaching statement position (there is no
        // per-kind list to maintain here), not a fallback for an unhandled
        // kind. It now inherits serialize_expr's own hard error for any
        // kind that function doesn't recognize, so an unhandled kind still
        // fails loudly here -- it just fails one call deeper than it used
        // to, instead of this arm silently emitting a "comment + ;" null
        // statement (#963's original silent-miscompile symptom).
        print_indent_level(f, indent);
        serialize_expr(f, vm, ctx, node, 0);
        fprintf(f, ";\n");
        break;
    }
}

// #964: serialize one statement in a *list* context (a function body or an
// ND_BLOCK's own body) -- the one place a VLA-defining ND_BLOCK is safe to
// unbrace. declaration()'s ND_BLOCK wrapping (used to bundle a single `type
// v1, v2;` statement's per-declarator initializers) is not a real C block
// scope; the plain ND_BLOCK case in serialize_stmt() braces it like any
// other compound statement, which is harmless for an ordinary declaration
// (its variable is already hoisted, only initializer assignments remain
// inside) but would end a VLA's C-level scope right where it's declared.
// Only called from statement-list positions -- never the direct body of an
// if/else/loop/switch, where a declaration can't legally sit anyway (cccc
// itself already rejects e.g. `if (n) int v[n];`).
static void serialize_stmt_list_item(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                                     Node *node, int indent) {
    if (node && node->kind == ND_BLOCK && block_defines_vla(node)) {
        for (Node *s = node->body; s; s = s->next)
            serialize_stmt(f, vm, ctx, s, indent);
        return;
    }
    serialize_stmt(f, vm, ctx, node, indent);
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
        //
        // Hoisting every local to one flat top-of-function list assumes C
        // block scoping never needs to distinguish two locals with the same
        // name -- false when a name is reused in sibling (or nested) blocks,
        // e.g. two `for (int i = ...)` loops in the same function each
        // declaring their own `int i`. Renaming on collision (#926) below
        // avoids two declarations of the same identifier in the same
        // (flattened) scope; params occupy an identifier too (they are on
        // fn->locals with is_param set, just not declared here) so they
        // seed the collision check.
        for (Obj *var = fn->locals; var; var = var->next) {
            // Params are never renamed here -- serialize_function_signature
            // already printed the function's signature (with each param's
            // current name) before this loop runs, so renaming a param's
            // Obj this late would desync the signature from the body. C's
            // own rules already guarantee distinct params never collide
            // with each other; only a non-param can be renamed to resolve
            // a collision against a param or another non-param.
            if (var->is_param)
                continue;

            // #965: __static_link (block_literal(), parse.c) is spliced
            // into fn->params but never marked is_param -- that flag is
            // only ever set by assign_lvar_offsets (codegen.c), which
            // -m/-c=native never run. Match by list membership instead of
            // trusting the flag, so it isn't re-declared here as an
            // ordinary local (it's already a parameter, printed by
            // serialize_function_signature). codegen.c's own
            // assign_lvar_offsets (:8622-8633) does this exact membership
            // scan for the same reason.
            bool is_actual_param = false;
            for (Obj *p = fn->params; p; p = p->next)
                if (p == var) { is_actual_param = true; break; }
            if (is_actual_param)
                continue;

            if (var->name[0] == '\0')
                // Compiler-synthesized temporaries (e.g. from ++/--/op=
                // desugaring) have an empty name; give them one so they can
                // be declared and referenced as valid C identifiers.
                var->name = arena_format(vm, "__cccc_tmp%d",
                                          ctx->anon_local_counter++);

            // #926: rename on collision against every *other* local/param
            // in the function -- not just those before it in the raw list,
            // since fn->locals is in reverse declaration order and a param
            // can sit after the body local shadowing it. Comparing against
            // the whole list (not only already-finalized entries) is still
            // sound: a later non-param entry that shares var's pre-rename
            // name simply detects the collision itself, against var's new
            // name, when its own turn comes. Linear scan per local (O(n^2)
            // in locals), matching this file's existing style; move to a
            // hashmap if a function with enough locals to matter shows up.
            bool renamed_again;
            do {
                renamed_again = false;
                for (Obj *other = fn->locals; other; other = other->next) {
                    if (other == var || strcmp(other->name, var->name) != 0)
                        continue;
                    var->name = arena_format(vm, "%s__cccc_%d", var->name,
                                             ctx->anon_local_counter++);
                    renamed_again = true;
                    break;
                }
            } while (renamed_again);

            // #964: a VLA's declaration can't be hoisted here -- its length
            // expression reads a variable (`int n=4; int v[n];`) that must
            // already be in scope at the point of the flattened declaration,
            // and the hoist loop runs before any of the function body has
            // been emitted. It keeps its slot in the collision-renaming
            // above (so a same-named non-VLA local elsewhere still detects
            // the collision), but the declaration itself is emitted in
            // place by the ND_EXPR_STMT case in serialize_stmt() that
            // recognizes its `ND_VLA_PTR = alloca(...)` initializer.
            if (var->ty->kind == TY_VLA)
                continue;

            // #973 follow-up: same reasoning, extended to a pointer-to-VLA
            // local (`int (*p)[n] = &v;`) -- its declarator also reads a
            // runtime variable. Only skip when we know there's an
            // initializer to anchor the in-place declaration to (see the
            // ND_EXPR_STMT case below, and Obj.deferred_vla_ptr_init in
            // cccc.h); a pointer-to-VLA local declared with no initializer
            // falls through to the normal hoist below, which re-emits a
            // declarator referencing a not-yet-declared variable and fails
            // to compile -- a pre-existing gap this fix doesn't widen,
            // tracked separately rather than fixed here.
            if (var->deferred_vla_ptr_init)
                continue;

            // #965: a block literal's descriptor local (Node.block_desc_var)
            // is typed `long[N]` at parse time only so it gets frame space --
            // its real C type is the paired block function's env struct
            // (serialize_block_preamble), which doesn't exist as a Type* and
            // so can't go through serialize_type_decl. Emit its declaration
            // directly instead.
            if (var->block_desc_of) {
                const char *env = find_block_env(ctx, var->block_desc_of);
                print_indent_level(f, 1);
                fprintf(f, "%s %s;\n", env ? env : "struct __cccc_block_env_?",
                        var->name);
                continue;
            }

            // #965: a __block local's stack slot holds a heap box pointer at
            // runtime (codegen.c's ALCB prologue) -- declare it as a pointer
            // and malloc it here, matching that prologue's per-function
            // allocation. Every ordinary read/write of it is rewritten to
            // `(*name)` by serialize_expr's ND_VAR case (and ND_MEMZERO's own
            // is_block_var arm). Never freed, matching the VM's own
            // never-reclaimed ALLOC_KIND_BLOCK_BOX.
            if (var->is_block_var) {
                print_indent_level(f, 1);
                serialize_type_decl(f, ctx, pointer_to(vm, var->ty), var->name);
                fprintf(f, ";\n");
                print_indent_level(f, 1);
                fprintf(f, "%s = __builtin_malloc(sizeof(*%s));\n", var->name,
                        var->name);
                continue;
            }

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
            serialize_stmt_list_item(f, vm, ctx, s, 1);
        }

        fprintf(f, "}\n\n");
        ctx->current_fn = saved_fn;
    } else {
        fprintf(f, ";\n\n");
    }
}

// #918: resolve a Relocation's target symbol name to its Obj. Mirrors
// codegen.c's find_global_obj (static there, not visible from this file) --
// vm->compiler.globals is the full accumulated global+function list
// (bytecode.c sets it once parsing completes), and rel->label points at the
// target Obj's ->name field directly (see eval2()/eval_rval() in parse.c),
// so a plain name match is exact. &&label targets (a computed-goto label
// address stored in a static/global initializer) live in codegen.c's
// text-segment label map instead of as an Obj and are not resolved here --
// vanishingly rare in an initializer and not worth threading codegen state
// into the serializer for; falls through to the "unresolved relocation"
// hard error below.
static Obj *serialize_find_global(VirtualMachine *vm, const char *name) {
    for (Obj *g = vm->compiler.globals; g; g = g->next)
        if (strcmp(g->name, name) == 0)
            return g;
    return NULL;
}

// Find the Relocation (if any) covering byte `offset` within `var`'s
// init_data -- a pointer-sized initializer slot that names a symbol (`&x`,
// a string literal, a function pointer, ...) has its raw bytes zeroed by
// write_gvar_data() (parse.c) and the real target recorded here instead.
static Relocation *serialize_find_reloc(Obj *var, int offset) {
    for (Relocation *r = var->rel; r; r = r->next)
        if (r->offset == offset)
            return r;
    return NULL;
}

static void serialize_init_bytes(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                                 Obj *var, Type *ty, int offset);

// A pointer-typed initializer slot backed by a Relocation (#918 defect C):
// previously the zeroed init_data bytes were printed verbatim as a null
// pointer -- silent miscompilation, valid C that runs wrong. `rel->label`
// names the target Obj by its ->name field.
static void serialize_reloc_init(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                                 Obj *var, Type *ty, Relocation *rel) {
    if (!rel->label || !*rel->label)
        error("cccc: invalid relocation in initializer for global '%s'", var->name);

    const char *target_name = *rel->label;
    Obj *target = serialize_find_global(vm, target_name);
    if (!target)
        error("cccc: cannot serialize initializer for global '%s' in native "
              "mode: unresolved relocation target '%s'",
              var->name, target_name);

    // Anonymous string-literal global -- serialize_global_var() never
    // emits these on their own (see is_string_literal skip below), so
    // inline the literal here instead of naming a symbol that doesn't
    // exist in the output.
    if (target->is_string_literal && target->init_data) {
        int len = (target->ty && target->ty->kind == TY_ARRAY)
                      ? target->ty->array_len
                      : (int)strlen(target->init_data);
        bool plain_char_ptr = ty->kind == TY_PTR && ty->base &&
                              ty->base->kind == TY_CHAR && rel->addend == 0;
        if (!plain_char_ptr) {
            fprintf(f, "(");
            serialize_type(f, ctx, ty);
            fprintf(f, ")((char *)");
        }
        serialize_string_n(f, target->init_data, len);
        if (!plain_char_ptr)
            fprintf(f, " + %lld)", (long long)rel->addend);
        return;
    }

    // #925: any other anonymous (`.L..N`) global -- a compound literal or
    // static local -- is renamed to a valid identifier and given a real
    // definition by rename_anon_globals() before serialization proceeds.
    // If one still has a dotted name here, it was reachable through this
    // Relocation but never renamed (not on the `prog` list the pre-pass
    // walks) -- fail loudly rather than emit a reference to a symbol that
    // was never defined (#918's fail-loudly policy).
    if (target->name[0] == '.')
        error("cccc: cannot serialize initializer for global '%s' in native "
              "mode: relocation target '%s' was never assigned a valid name",
              var->name, target_name);

    fprintf(f, "(");
    serialize_type(f, ctx, ty);
    fprintf(f, ")((char *)&%s + %lld)", target->name, (long long)rel->addend);
}

// Reconstruct a global variable's initializer from its raw `init_data`
// bytes (plus any Relocations) as C source text, recursing through
// arrays/vectors/structs/unions. Replaces the old scalar-only dispatch that
// fell back to the placeholder comment `/* init data */` for every
// aggregate shape -- text a host compiler rejects outright (#918 defect B).
static void serialize_init_bytes(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                                 Obj *var, Type *ty, int offset) {
    if (!ty)
        error("cccc: cannot serialize initializer for global '%s' in native "
              "mode: unknown type", var->name);

    if (ty->kind == TY_PTR || ty->kind == TY_NULLPTR_T) {
        Relocation *rel = serialize_find_reloc(var, offset);
        if (rel) {
            serialize_reloc_init(f, vm, ctx, var, ty, rel);
            return;
        }
    }

    switch (ty->kind) {
    case TY_ARRAY:
        if (ty->base->kind == TY_CHAR && !serialize_find_reloc(var, offset)) {
            serialize_string_n(f, var->init_data + offset, ty->array_len);
            return;
        }
        fprintf(f, "{ ");
        for (int i = 0; i < ty->array_len; i++) {
            if (i > 0)
                fprintf(f, ", ");
            serialize_init_bytes(f, vm, ctx, var, ty->base,
                                 offset + i * ty->base->size);
        }
        fprintf(f, " }");
        return;

    case TY_VECTOR:
        fprintf(f, "{ ");
        for (int i = 0; i < ty->vec_len; i++) {
            if (i > 0)
                fprintf(f, ", ");
            serialize_init_bytes(f, vm, ctx, var, ty->base,
                                 offset + i * ty->base->size);
        }
        fprintf(f, " }");
        return;

    case TY_STRUCT: {
        fprintf(f, "{ ");
        bool first = true;
        for (Member *m = ty->members; m; m = m->next) {
            if (m->is_bitfield && !m->name)
                continue; // anonymous bitfield: padding, nothing to designate
            if (!first)
                fprintf(f, ", ");
            first = false;
            if (m->name)
                fprintf(f, ".%.*s = ", m->name->len, m->name->loc);
            if (m->is_bitfield) {
                int64_t container = 0;
                int sz = m->ty->size < 8 ? m->ty->size : 8;
                memcpy(&container, var->init_data + offset + m->offset, sz);
                uint64_t mask = (m->bit_width >= 64)
                                    ? ~0ULL
                                    : ((1ULL << m->bit_width) - 1);
                uint64_t bits = ((uint64_t)container >> m->bit_offset) & mask;
                fprintf(f, "%lluu", (unsigned long long)bits);
            } else {
                serialize_init_bytes(f, vm, ctx, var, m->ty, offset + m->offset);
            }
        }
        fprintf(f, " }");
        return;
    }

    case TY_UNION: {
        // Reconstruct via the largest member (first on a tie) -- byte-exact
        // whenever some member spans the whole object, which is the normal
        // case; a union with no full-size member falls through to the
        // "cannot serialize" error below via the recursive call, since no
        // member type here can losslessly represent the other members'
        // bytes either.
        Member *largest = NULL;
        for (Member *m = ty->members; m; m = m->next)
            if (!largest || m->ty->size > largest->ty->size)
                largest = m;
        if (!largest || largest->ty->size < ty->size)
            error("cccc: cannot serialize initializer for global '%s' in "
                  "native mode: union has no member spanning the full "
                  "%d-byte object", var->name, ty->size);
        fprintf(f, "{ .%.*s = ", largest->name->len, largest->name->loc);
        serialize_init_bytes(f, vm, ctx, var, largest->ty, offset);
        fprintf(f, " }");
        return;
    }

    case TY_FLOAT: {
        float fv; memcpy(&fv, var->init_data + offset, 4);
        fprintf(f, "%.9gf", (double)fv);
        return;
    }

    case TY_DOUBLE:
    case TY_LDOUBLE: {
        // TY_LDOUBLE shares TY_DOUBLE's 8-byte read and unsuffixed %g here,
        // matching this function's pre-#918 behavior exactly -- a latent
        // long-double-precision/suffix gap, but pre-existing and unrelated
        // to #918's scope.
        double dv; memcpy(&dv, var->init_data + offset, 8);
        fprintf(f, "%.17g", dv);
        return;
    }

    default:
        break;
    }

    if (is_decimal(ty)) {
        // #402: raw BID bytes in init_data -> C source text. Requires
        // CCCC_HAS_DECIMAL=1 (the same build that could have produced
        // these bytes in the first place); cccc_dec_format returns -1
        // in the off build, which can't happen here.
        char buf[80];
        int w = dec_width_code(ty);
        const char *suffix = w == 0 ? "df" : w == 1 ? "dd" : "dl";
        if (cccc_dec_format(buf, sizeof buf, var->init_data + offset, w) >= 0)
            fprintf(f, "%s%s", buf, suffix);
        else
            fprintf(f, "0%s", suffix);
        return;
    }

    if (ty->kind == TY_BOOL || ty->kind == TY_CHAR || ty->kind == TY_SHORT ||
        ty->kind == TY_INT || ty->kind == TY_LONG || ty->kind == TY_ENUM ||
        ty->kind == TY_PTR || ty->kind == TY_NULLPTR_T || ty->kind == TY_BLOCK) {
        // #965: TY_BLOCK is 8 bytes, pointer-shaped (see block_type(),
        // type.c) -- a block value can only be a compile-time-constant
        // global initializer as a null pointer anyway (the VM's own
        // is_const_expr rejects a real block literal there before this is
        // ever reached), so it reads exactly like TY_PTR.
        int64_t iv = 0;
        int sz = ty->size < 8 ? ty->size : 8;
        memcpy(&iv, var->init_data + offset, sz);
        if (sz < 8 && ty->kind != TY_PTR && ty->kind != TY_NULLPTR_T &&
            (iv >> (sz * 8 - 1)) & 1)
            iv |= (-1LL << (sz * 8));
        fprintf(f, "%lld", (long long)iv);
        return;
    }

    // TY_COMPLEX and anything else with no verified byte layout here: fail
    // loudly rather than guess (#918's whole point -- emitting a plausible-
    // but-wrong initializer is the bug class being fixed, not a shape to
    // reproduce for cases this function doesn't yet handle).
    error("cccc: cannot serialize initializer for global '%s' in native "
          "mode: unsupported initializer type (kind %d)", var->name, ty->kind);
}

// Serialize global variable
static void serialize_global_var(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                                 Obj *var) {
    if (var->is_function)
        return;

    // String literals are inlined at their point of use (ND_VAR /
    // serialize_reloc_init) instead of getting their own definition. Every
    // other `.L..N`-named global (compound literal, static local) is
    // renamed to a valid identifier by rename_anon_globals() before this
    // runs, so it falls through and is serialized like any other global
    // (#925).
    if (var->is_string_literal)
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
        serialize_init_bytes(f, vm, ctx, var, var->ty, 0);
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

// #989: true when `ty` already has a record (tag or typedef) with
// owner_fn == NULL -- i.e. it's an ordinary file-scope type, not merely
// unowned because it was never named at all (a tagless local aggregate has
// no record either way, so type_decl_owner() alone can't tell the two
// apart -- see hoist_local_type_to_file_scope()).
static bool type_has_file_scope_name(SerializeContext *ctx, Type *ty) {
    for (int i = 0; i < ctx->tags_len; i++)
        if (ctx->tags[i].owner_fn == NULL && same_type_or_origin(ctx->tags[i].ty, ty))
            return true;
    for (int i = 0; i < ctx->typedefs_len; i++)
        if (ctx->typedefs[i].owner_fn == NULL && same_type_or_origin(ctx->typedefs[i].ty, ty))
            return true;
    return false;
}

// #989: promotes a function-local struct/union/enum type (or one reachable
// through a pointer/array/VLA/function-type chain) to file scope, so a block
// literal's environment struct -- itself emitted at file scope, ahead of
// the function that would otherwise bring the type's tag into scope -- can
// spell a capture's type. Mirrors collect_type()'s traversal shape so
// dependencies are promoted (and defined) before dependents.
//
// A tagless local aggregate (`struct { int x; } p`) has no TypeName record
// at all -- previously serialize_type fell through to
// serialize_anon_aggregate() and inlined a *fresh* anonymous body at every
// use site, producing two structurally-identical but nominally distinct
// types and a hard clang error at the env-struct assignment. This
// synthesizes a tag for that case too, not just hoisting an already-named
// one.
static void hoist_local_type_to_file_scope(FILE *f, VirtualMachine *vm,
                                           SerializeContext *ctx, Type *ty) {
    if (!ty)
        return;

    if (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_VLA) {
        hoist_local_type_to_file_scope(f, vm, ctx, ty->base);
        return;
    }

    if (ty->kind == TY_FUNC) {
        hoist_local_type_to_file_scope(f, vm, ctx, ty->return_ty);
        for (Type *p = ty->params; p; p = p->next)
            hoist_local_type_to_file_scope(f, vm, ctx, p);
        return;
    }

    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION && ty->kind != TY_ENUM)
        return;

    if (type_vec_contains(&ctx->hoisted, ty))
        return;

    Obj *owner = type_decl_owner(ctx, ty);
    if (owner == NULL && type_has_file_scope_name(ctx, ty))
        return; // already an ordinary file-scope type -- nothing to hoist

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
        for (Member *m = ty->members; m; m = m->next)
            hoist_local_type_to_file_scope(f, vm, ctx, m->ty);

    type_vec_push(&ctx->hoisted, ty);

    // Find the first existing tag record (if any) so a collision-free tag
    // keeps its original spelling -- this leaves existing -m output and
    // existing tests untouched in the common (no-collision) case.
    TypeName *existing_tag = NULL;
    for (int i = 0; i < ctx->tags_len; i++)
        if (same_type_or_origin(ctx->tags[i].ty, ty)) {
            existing_tag = &ctx->tags[i];
            break;
        }

    char *chosen_name;
    int chosen_len;
    if (existing_tag) {
        bool collides = false;
        for (int i = 0; i < ctx->tags_len && !collides; i++) {
            if (&ctx->tags[i] == existing_tag)
                continue;
            if (ctx->tags[i].owner_fn == NULL &&
                ctx->tags[i].name_len == existing_tag->name_len &&
                strncmp(ctx->tags[i].name, existing_tag->name,
                        existing_tag->name_len) == 0 &&
                !same_type_or_origin(ctx->tags[i].ty, ty))
                collides = true;
        }
        if (!collides) {
            chosen_name = existing_tag->name;
            chosen_len = existing_tag->name_len;
        } else {
            chosen_name = arena_format(vm, "__cccc_local_%.*s_%d",
                                       existing_tag->name_len, existing_tag->name,
                                       ctx->hoisted_type_counter++);
            chosen_len = (int)strlen(chosen_name);
        }
    } else {
        chosen_name = arena_format(vm, "__cccc_local_anon_%d",
                                   ctx->hoisted_type_counter++);
        chosen_len = (int)strlen(chosen_name);
    }

    // #989: two different functions each declaring an identical `struct P`
    // compare equal under same_type_or_origin's structural fallback, so
    // hoisting one makes the other resolve to this same file-scope name too
    // -- harmless (identical layout, one definition, consistent spelling)
    // but non-obvious, hence this comment. Mutate every matching record, not
    // just the first: type_decl_owner() above only inspected the first hit,
    // but find_tag_name()/find_typedef_name() may later return a different
    // one depending on ctx->current_fn.
    for (int i = 0; i < ctx->tags_len; i++)
        if (same_type_or_origin(ctx->tags[i].ty, ty)) {
            ctx->tags[i].owner_fn = NULL;
            ctx->tags[i].name = chosen_name;
            ctx->tags[i].name_len = chosen_len;
        }
    for (int i = 0; i < ctx->typedefs_len; i++)
        if (same_type_or_origin(ctx->typedefs[i].ty, ty))
            ctx->typedefs[i].owner_fn = NULL;

    if (!existing_tag)
        // No tag record existed at all (a tagless local aggregate) --
        // synthesize one so serialize_type prefers `struct <tag>` at every
        // site, including inside the declaring function, which is exactly
        // what gives the one-definition property.
        type_name_push(&ctx->tags, &ctx->tags_len, &ctx->tags_cap, ty,
                       chosen_name, chosen_len, NULL, false, true, NULL);

    Obj *saved_fn = ctx->current_fn;
    ctx->current_fn = NULL;
    if (ty->kind == TY_ENUM)
        serialize_enum_def(f, ctx, ty);
    else
        serialize_struct_def(f, ctx, ty);
    ctx->current_fn = saved_fn;
}

// #953: true when `path` (a type's declaring file, TypeName.file_path) is
// one of the resolved include paths auto-capture actually re-emitted into
// generated_only (-c=generated) output -- see the ctx->captured_paths
// population in cc_serialize_program. A NULL path (no declaring token) or a
// path not in the set means nothing else supplies this definition, so the
// caller must still serialize it despite from_include being true.
// #1003: true when `path`'s final path component is exactly `name` --
// `path` may be a real filesystem path, a bare filename (no directory
// found on disk), or a synthetic "<embedded>/name" key (embedded_header_key,
// preprocess.c), so a suffix match on '/' is used rather than assuming any
// particular shape.
static bool path_basename_is(const char *path, const char *name) {
    if (!path)
        return false;
    size_t plen = strlen(path), nlen = strlen(name);
    if (plen == nlen)
        return strcmp(path, name) == 0;
    return plen > nlen && path[plen - nlen - 1] == '/' &&
           strcmp(path + plen - nlen, name) == 0;
}

static bool path_is_captured(SerializeContext *ctx, const char *path) {
    if (!path)
        return false;
    for (int i = 0; i < ctx->captured_paths_len; i++)
        if (ctx->captured_paths[i] && strcmp(ctx->captured_paths[i], path) == 0)
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
    // #891: in !generated_only mode (-c=native, -m without -c=generated), a
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

    // #999: printing a typedef's own RHS must not resolve back to the
    // typedef itself. find_typedef_name_exact() (used by serialize_type
    // for a non-aggregate kind -- struct/union/enum avoid this because
    // find_tag_name takes priority over find_typedef_name for them) would
    // otherwise match `alias->ty` here against this exact `alias` record,
    // since they're the same Type pointer -- e.g. `typedef int (^IntBlock)
    // (int);` printed as `typedef IntBlock IntBlock;`. Temporarily hide
    // this one record from that lookup for the duration of the call: the
    // real Type is still passed through explicitly (serialize_type_decl's
    // `ty` parameter), only the *lookup table entry* is blanked, so a
    // different typedef further down the same origin chain (a real,
    // distinct alias-of-an-alias) still resolves normally.
    Type *real_ty = alias->ty;
    alias->ty = NULL;
    fprintf(f, "typedef ");
    serialize_type_decl(f, ctx, real_ty, name);
    fprintf(f, ";\n\n");
    alias->ty = real_ty;
}

static void serialize_type_defs_for_owner(FILE *f, SerializeContext *ctx,
                                          Obj *owner_fn) {
    Obj *saved_fn = ctx->current_fn;
    ctx->current_fn = owner_fn;

    for (int i = 0; i < ctx->defs.len; i++) {
        Type *ty = ctx->defs.data[i];
        if (type_decl_owner(ctx, ty) != owner_fn)
            continue;
        // #989: hoist_local_type_to_file_scope() rewrites a hoisted type's
        // tag/typedef record(s) to owner_fn = NULL, so on the file-scope
        // pass (owner_fn == NULL here) the check above no longer excludes
        // it -- without this, serialize_block_preamble's already-emitted
        // definition would be re-derived here too, a hard "redefinition"
        // error.
        if (type_vec_contains(&ctx->hoisted, ty))
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
        //
        // #953: generated_only (-c=generated) output can ALSO already
        // contain this definition via an auto-captured `#include` -- the
        // capture (preprocess.c) records source text into emit_events_head
        // regardless of generated_only, and cc_serialize_program's
        // generated_only branch replays it verbatim -- so re-deriving the
        // same struct/enum here produces a hard "redefinition" error. That
        // only holds when the include was actually captured, though: a type
        // reached solely via `#include @comptime "x.h"` (never captured --
        // its whole point is to stay invisible to the runtime TU) has
        // nothing else to supply the definition, so it must still be
        // re-derived. path_is_captured() distinguishes the two by checking
        // whether provenance_source's declaring file is one of the
        // resolved paths auto-capture actually emitted for this program.
        TypeName *provenance_source = tag ? tag : alias;
        if (!ctx->emit_strict && provenance_source &&
            provenance_source->from_include && !provenance_source->always_emit &&
            (!ctx->generated_only ||
             path_is_captured(ctx, provenance_source->file_path)))
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

// #925/#928: new_anon_gvar() (parse.c) and reflect_new_anon_gvar()
// (reflection.c) both hand out the same `.L..N` name to string literals,
// static locals, and compound literals alike -- a dot isn't a valid C
// identifier character, so every non-string-literal use needs a real name
// before anything below references it. Runs once, before any
// collection/emission pass, so every later `is_string_literal`/dotted-name
// check sees the final state. Also runs under generated_only (-c=generated):
// #928 found that reflection API compound-literal/init-struct globals built
// while running under -c=generated (e.g. a comptime macro calling CompoundLiteral()/
// InitArray()/InitStruct() at file scope) hit this exact gap when renaming
// was skipped here -- the emit-event walk's own dotted-name skip (see
// `obj->name[0] != '.'` further down) only prevented emitting a bogus
// reference, it never gave the global a real name or definition.
static void rename_anon_globals(VirtualMachine *vm, Obj *prog, SerializeContext *ctx) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        // #965: a lifted block literal function (block_literal(), parse.c)
        // is named ".L..N" from the same new_unique_name() counter as a
        // string literal or compound literal, but it's a *function* -- the
        // generic branch below is skipped for those (`obj->is_function`
        // continues past it) since a real function normally already has a
        // legal name. Rename it to a C-legal identifier here too, sharing
        // the same counter; serialize_block_preamble() reuses this same
        // numeric suffix for the function's paired env struct name, so the
        // two stay paired without extra state.
        if (obj->is_function) {
            if (obj->is_block && obj->name[0] == '.')
                obj->name = arena_format(vm, "__cccc_block_%d",
                                         ctx->anon_global_counter++);
            continue;
        }
        if (obj->name[0] != '.' || obj->is_string_literal)
            continue;
        // new_gvar() (parse.c) defaults display_name to the same dotted
        // name it was created with; only a static local overrides it (to
        // the real source identifier) after the fact. A still-dotted
        // display_name means no such override happened (a compound
        // literal) -- fall back to a plain "anon" tag rather than
        // splicing the dot into the new name too.
        const char *tag = (obj->display_name && obj->display_name[0] != '.')
                              ? obj->display_name : "anon";
        obj->name = arena_format(vm, "__cccc_%s_%d", tag,
                                 ctx->anon_global_counter++);
        // An anonymous global (compound literal or static local) can never
        // be referenced from another translation unit -- internal linkage
        // makes the #918 forward-declaration pass ahead of global
        // definitions emit a valid `static T name;` + `static T name = ...;`
        // tentative-definition pair instead of `extern` plus an external
        // definition.
        obj->is_static = true;
    }
}

// #953: hashmap_foreach callback collecting emit_include_paths' values
// (resolved paths of auto-captured #include directives) into
// ctx->captured_paths for path_is_captured() to scan.
static int collect_captured_path(char *key, int keylen, void *val, void *user_data) {
    (void)key;
    (void)keylen;
    SerializeContext *ctx = user_data;
    ctx->captured_paths = realloc(ctx->captured_paths,
                                  sizeof(char *) * (ctx->captured_paths_len + 1));
    ctx->captured_paths[ctx->captured_paths_len++] = val;
    return 0;
}

// #965: does `node` (or anything reachable from it) directly call `target`
// -- matched by identity against the callee's own ND_VAR, the shape
// Block_copy(block) lowers to (parse.c). Mirrors collect_node_types's
// traversal shape. Used only to decide whether serialize_block_preamble
// needs to emit the native __cccc_block_copy_impl replacement.
static bool node_calls_obj(Node *node, Obj *target) {
    if (!node)
        return false;
    if (node->kind == ND_FUNCALL && node->lhs && node->lhs->kind == ND_VAR &&
        node->lhs->var == target)
        return true;
    return node_calls_obj(node->lhs, target) ||
           node_calls_obj(node->rhs, target) ||
           node_calls_obj(node->cond, target) ||
           node_calls_obj(node->then, target) ||
           node_calls_obj(node->els, target) ||
           node_calls_obj(node->init, target) ||
           node_calls_obj(node->inc, target) ||
           node_calls_obj(node->body, target) ||
           node_calls_obj(node->args, target) ||
           node_calls_obj(node->next, target);
}

// #990/#993: does `ty` (or anything reachable from it) mention TY_BLOCK --
// used to decide whether `struct __cccc_block` itself needs a definition
// even when the TU declares no block *literal* (e.g. a function that only
// takes a block parameter and calls Block_copy/Block_release/the block
// itself). Mirrors collect_type()'s PTR/ARRAY/VLA/FUNC traversal shape, but
// deliberately does NOT recurse into struct/union members: a block-typed
// member is stored as a pointer, and any *use* of it (a read, a call)
// necessarily produces an expression whose own ->ty is TY_BLOCK, which
// node_mentions_block below already catches -- recursing into members here
// would need a seen-set to be cycle-safe for no additional coverage.
static bool type_mentions_block(Type *ty) {
    if (!ty)
        return false;
    if (ty->kind == TY_BLOCK)
        return true;
    if (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_VLA)
        return type_mentions_block(ty->base);
    if (ty->kind == TY_FUNC) {
        if (type_mentions_block(ty->return_ty))
            return true;
        for (Type *p = ty->params; p; p = p->next)
            if (type_mentions_block(p))
                return true;
    }
    return false;
}

// #990/#993: mirrors collect_node_types()'s traversal shape (including its
// ND_SWITCH/ND_CASE special cases) to find any node whose type -- or a
// var/member/func_ty attached to it -- mentions TY_BLOCK.
static bool node_mentions_block(Node *node) {
    if (!node)
        return false;
    if (type_mentions_block(node->ty))
        return true;
    if (node->var && type_mentions_block(node->var->ty))
        return true;
    if (node->member && type_mentions_block(node->member->ty))
        return true;
    if (node->func_ty && type_mentions_block(node->func_ty))
        return true;

    if (node->kind == ND_SWITCH) {
        if (node_mentions_block(node->cond))
            return true;
        for (Node *c = node->case_next; c; c = c->case_next)
            if (node_mentions_block(c->lhs))
                return true;
        if (node->default_case && node_mentions_block(node->default_case->lhs))
            return true;
        return node_mentions_block(node->next);
    }
    if (node->kind == ND_CASE)
        return node_mentions_block(node->lhs) || node_mentions_block(node->next);

    return node_mentions_block(node->lhs) || node_mentions_block(node->rhs) ||
           node_mentions_block(node->cond) || node_mentions_block(node->then) ||
           node_mentions_block(node->els) || node_mentions_block(node->init) ||
           node_mentions_block(node->inc) || node_mentions_block(node->body) ||
           node_mentions_block(node->args) || node_mentions_block(node->next);
}

// #990/#993: mirrors collect_obj_types()'s traversal shape.
static bool obj_uses_block_type(Obj *obj) {
    if (type_mentions_block(obj->ty))
        return true;
    if (node_mentions_block(obj->init_expr))
        return true;
    for (Obj *param = obj->params; param; param = param->next)
        if (type_mentions_block(param->ty))
            return true;
    for (Obj *local = obj->locals; local; local = local->next)
        if (type_mentions_block(local->ty))
            return true;
    return node_mentions_block(obj->body);
}

// #965: emits, once, everything a lowered block literal needs at file
// scope: the common-initial-sequence `struct __cccc_block` every env
// struct shares (so a block value's pointer type is well-defined
// regardless of which block literal produced it), one
// `struct __cccc_block_env_N` per block function (its captures, in the
// exact order codegen's descriptor layout uses -- ND_BLOCK_LITERAL,
// codegen.c), and -- only if Block_copy/Block_release is actually
// reachable -- a native replacement for the VM-only __cccc_block_copy_impl
// FFI shim (its real implementation, src/stdlib/stdlib.c, exists only
// inside the VM's host runtime and would otherwise leave a call to an
// undeclared symbol in the generated C) / an `extern void free(void *);`
// declaration (#990: vm->compiler.builtin_free has no obj->tok, so the
// prototype pass's from_primary filter always drops it). Runs after
// rename_anon_globals() (block functions already have their final
// __cccc_block_N names) and before type/prototype collection, so both the
// generated_only and normal cc_serialize_program branches share it -- a
// macro-generated block literal (via Quote(), unlikely but not excluded)
// gets the same treatment as an ordinary one.
//
// #990/#993: `struct __cccc_block` itself, and the copy-impl/free
// declarations, are needed even in a TU with no block *literal* at all --
// e.g. a function that only takes a block parameter and calls
// Block_copy/Block_release/the block itself. Gated on `any_block ||
// uses_block_type || copy_used || release_used` rather than `any_block`
// alone; the env-struct loop (and its #989 hoist pass) still only makes
// sense when there's an actual block literal to describe, so those stay
// gated on `any_block`.
static void serialize_block_preamble(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                                     Obj *prog) {
    bool any_block = false;
    bool uses_block_type = false;
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_function && obj->is_block)
            any_block = true;
        if (obj_uses_block_type(obj))
            uses_block_type = true;
        if (any_block && uses_block_type)
            break;
    }

    // #990: gated on the same `reachable` condition the #989 hoist loop
    // below uses -- under generated_only (-c=generated), a call inside an
    // ordinary (non-macro-generated) function never reaches the output, so
    // scanning it here would emit a copy-impl/free declaration nothing
    // actually calls.
    bool copy_used = false;
    bool release_used = false;
    for (Obj *obj = prog; obj && (!copy_used || !release_used); obj = obj->next) {
        if (!obj->is_function || !obj->body)
            continue;
        bool reachable = !ctx->generated_only || obj->is_macro_generated;
        if (!reachable)
            continue;
        if (!copy_used && vm->compiler.builtin_block_copy)
            copy_used = node_calls_obj(obj->body, vm->compiler.builtin_block_copy);
        if (!release_used && vm->compiler.builtin_free)
            release_used = node_calls_obj(obj->body, vm->compiler.builtin_free);
    }

    if (!any_block && !uses_block_type && !copy_used && !release_used)
        return;

    fprintf(f, "struct __cccc_block { void *__invoke; long __size; };\n\n");

    if (!any_block)
        goto emit_copy_and_free;

    // #989: hoist every capture's own struct/union/enum type to file scope
    // -- if it was declared inside a function, this env struct (below) is
    // emitted ahead of the function that would otherwise bring its tag into
    // scope, and serialize_type/serialize_anon_aggregate would otherwise
    // silently inline a fresh, nominally-distinct anonymous copy of the
    // body at each use site (confirmed via a real clang "assigning to ...
    // from incompatible type" error before this fix landed). Must run
    // before the env-struct loop below so the definitions are already in
    // ctx->hoisted (and already emitted) by the time serialize_type_decl
    // needs to spell a capture's field. Gated on the same `reachable`
    // condition the emit-event loop further down uses to decide what
    // actually reaches the output (#969's precedent: hoist only what is
    // actually serialized, not what merely exists in `prog`) -- under
    // generated_only (-c=generated), an ordinary (non-macro-generated)
    // block's code is never emitted at all, so hoisting its capture's type
    // here would push a real file-scope tag into output that could collide
    // with the consumer's own.
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->is_block)
            continue;
        bool reachable = !ctx->generated_only || obj->is_macro_generated;
        if (!reachable)
            continue;
        for (int i = 0; i < obj->num_captures; i++)
            hoist_local_type_to_file_scope(f, vm, ctx, obj->captures[i]->ty);
    }

    static const char *BLOCK_FN_PREFIX = "__cccc_block_";
    size_t prefix_len = strlen(BLOCK_FN_PREFIX);

    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->is_block)
            continue;

        // obj->name was rewritten to "__cccc_block_<N>" by
        // rename_anon_globals() just above -- reuse its numeric suffix so
        // the env struct name pairs with it without extra state.
        const char *suffix = (strncmp(obj->name, BLOCK_FN_PREFIX, prefix_len) == 0)
                                 ? obj->name + prefix_len : obj->name;
        char *env_name = arena_format(vm, "struct __cccc_block_env_%s", suffix);

        fprintf(f, "%s {\n    void *__invoke;\n    long __size;\n", env_name);
        for (int i = 0; i < obj->num_captures; i++) {
            Obj *cap = obj->captures[i];

            // #989: a capture whose own struct/union/enum type was declared
            // inside a function is already hoisted to file scope (with
            // renaming on collision) by the loop above, before this one
            // runs -- see hoist_local_type_to_file_scope(). Previously
            // (#965) this was a hard error; the fix landed here.
            Type *field_ty = cap->is_block_var ? pointer_to(vm, cap->ty) : cap->ty;
            char field_name[32];
            snprintf(field_name, sizeof(field_name), "__cap%d", i);
            fprintf(f, "    ");
            serialize_type_decl(f, ctx, field_ty, field_name);
            fprintf(f, ";\n");
        }
        fprintf(f, "};\n\n");

        if (ctx->block_envs_len == ctx->block_envs_cap) {
            ctx->block_envs_cap = ctx->block_envs_cap ? ctx->block_envs_cap * 2 : 8;
            ctx->block_envs = realloc(ctx->block_envs,
                                      sizeof(BlockEnvEntry) * ctx->block_envs_cap);
        }
        ctx->block_envs[ctx->block_envs_len].block_fn = obj;
        ctx->block_envs[ctx->block_envs_len].env_struct_name = env_name;
        ctx->block_envs_len++;
    }

emit_copy_and_free:
    if (copy_used) {
        fprintf(f,
            "static void *__cccc_block_copy_impl(void *__d) {\n"
            "    long __n = ((struct __cccc_block *)__d)->__size;\n"
            "    void *__c = __builtin_malloc((unsigned long)__n);\n"
            "    if (__c) __builtin_memcpy(__c, __d, (unsigned long)__n);\n"
            "    return __c;\n"
            "}\n\n");
    }
    // #990: vm->compiler.builtin_free is a synthesized `free` prototype
    // with no obj->tok (parse.c's Block_release path falls back to it when
    // no user-visible `free` is in scope, #458) -- the prototype pass's
    // from_primary filter always drops a tok-less Obj, so without this the
    // generated C called an undeclared `free`. A redundant declaration
    // here is always compatible with a real <stdlib.h> one if both end up
    // in the output (builtin_free is only ever used when parse.c found no
    // user `free`, so there is nothing for this to conflict with in
    // practice either way).
    if (release_used)
        fprintf(f, "extern void free(void *);\n\n");
}

// #999: a `static` function with a body, declared in a plain #include'd
// header (not a command-line input file, not a cccc-only-routed one -- #896)
// rather than synthesized/macro-generated, is already supplied to the output by
// that header's own auto-captured #include text. Emitting it again from
// `prog` -- which holds one Obj *per TU* that included the header, since
// `static` internal-linkage functions are deliberately left uncanonicalized
// across translation units by cc_link_progs (#957) -- produces a
// "redefinition" error the moment more than one input file shares that
// header (dandy's `internal.h`, `static inline` NaN-boxing accessors,
// #999). Mirrors the from_primary check the prototype pass already uses
// for a *bodyless* declaration just below, generalized to also cover a
// function that has one. In generated_only mode (-c=generated), the same
// header text is only in scope if it was actually auto-captured -- see
// #953's identical reasoning for a struct/enum tag definition just above
// this function -- so path_is_captured() gates it there; a plain -m/
// -c=native always replays every captured #include verbatim, so
// from_primary alone is sufficient.
// #1002 (investigation): true when `name` is the exact path of one of the
// files the user listed on the command line, as opposed to a header any of
// them #included. Replaces a plain `== vm->compiler.primary_file` token-file
// comparison, which only ever names input_files[0] (cc_preprocess/linker.c
// pin primary_file to the *first* input file forever) -- so a static
// function or bodyless declaration written in input_files[1..N] used to be
// misidentified as "supplied by a replayed header" and silently dropped
// from -c=native/-m output (found investigating #1002; not what that ticket
// itself reported, but blocks it -- see CLAUDE.md). Keyed by File.name,
// which new_file() (tokenize.c) sets to the exact string main.c passed to
// cc_preprocess(), so a straight lookup is sufficient -- no path
// canonicalization is attempted here, matching every other filename
// comparison in this file (e.g. cc_file_is_cccc_only).
static bool file_is_command_line_input(VirtualMachine *vm, const char *name) {
    return name && hashmap_get(&vm->compiler.command_line_inputs, name) != NULL;
}

static bool function_is_header_supplied(VirtualMachine *vm, SerializeContext *ctx,
                                        Obj *obj) {
    if (!obj->is_static || !obj->body || obj->is_macro_generated)
        return false;
    Token *t = obj->tok;
    if (!t || !t->file)
        return false;
    if (file_is_command_line_input(vm, t->file->name) ||
        cc_file_is_cccc_only(vm, t->file->name))
        return false;
    return !ctx->generated_only || path_is_captured(ctx, t->file->name);
}

void cc_serialize_program(FILE *f, VirtualMachine *vm, Obj *prog, bool generated_only) {
    if (!f || !prog)
        return;

    SerializeContext ctx = {.generated_only = generated_only,
                           .emit_strict = vm->compiler.emit_strict != 0,
                           .emit_cccc = vm->compiler.emit_cccc,
                           .vm = vm};
    if (generated_only)
        hashmap_foreach(&vm->compiler.emit_include_paths, collect_captured_path, &ctx);
    collect_scope_names(&ctx, vm);
    rename_anon_globals(vm, prog, &ctx);
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

    // #965/#993: block env structs (see serialize_block_preamble) are
    // emitted once both mechanisms that can bring a *capture's* type into
    // scope have already run: serialize_type_defs_for_owner(f, &ctx, NULL)
    // (file-scope struct/union/enum definitions -- including a
    // cccc-only-routed include's type, which #896 deliberately re-derives
    // here rather than re-emitting its #include) and, in the
    // !generated_only branch, the #include replay further down (a plain
    // captured header like <time.h>). Originally this call sat ahead of
    // everything (see history) -- a by-value capture of a *header-declared*
    // type (e.g. `struct tm`) was serialized while that type wasn't
    // complete yet, the mirror image of the function-local-type problem
    // #989 fixed (there the env struct was ahead of the declaring function;
    // here it needed to be *behind* whichever mechanism brings the header
    // type into scope). Moving the include replay alone is not sufficient:
    // a cccc-only-routed include's type reaches the output via
    // serialize_type_defs_for_owner, not the replay (#896), so both must
    // precede this call. Placed identically in both branches below, right
    // after each one's own serialize_type_defs_for_owner call.
    //
    // This flips the #989 hoist (inside serialize_block_preamble) relative
    // to serialize_type_defs_for_owner: a function-local capture type still
    // has owner_fn != NULL when the file-scope pass above runs, so it's
    // skipped there (not yet hoisted), then hoisted/emitted here -- verified
    // no double-emission against the #989 regression case.
    //
    // Residual, not fixed here: in the generated_only branch below, a
    // captured #include is replayed via CCCC_EMIT_SOURCE events interleaved
    // with generated functions (pinned there by #953), so a header-type
    // capture in *generated* code can still precede its #include -- filed
    // as #995.
    if (generated_only && vm->compiler.emit_events_head) {
        serialize_type_defs_for_owner(f, &ctx, NULL);
        serialize_block_preamble(f, vm, &ctx, prog);
        // #928: forward-declare every macro-generated global before any
        // definition, mirroring the #918 pass below (serialize_global_var's
        // sibling loop, further down this function) and for the same
        // reason -- the drain that populates these emit events
        // (macros.c:2775-2783) walks vm->compiler.globals newest-first, so
        // objects created earlier in one macro invocation are recorded
        // *later*. A file-scope CompoundLiteral()/InitStruct() call (whose
        // anon gvar is created before the function that references it)
        // would otherwise emit that function body ahead of the global's own
        // definition -- a forward reference with nothing in scope yet.
        for (EmitEvent *ev = vm->compiler.emit_events_head; ev; ev = ev->next) {
            if (ev->kind != CCCC_EMIT_OBJECT)
                continue;
            Obj *obj = ev->obj;
            if (!obj || !obj->is_macro_generated || obj->is_function || obj->name[0] == '.')
                continue;
            fprintf(f, obj->is_static ? "static " : "extern ");
            serialize_type_decl(f, &ctx, obj->ty, obj->name);
            fprintf(f, ";\n");
        }
        // #956: forward-declare a macro-generated function's callees the
        // moment its own event is reached, rather than hoisting every
        // prototype up front -- emission order here follows
        // PublishNode/MakeFunction event order, which has no relation to
        // the call graph, so a function's body can reference another
        // generated function whose own event appears later. Hoisting
        // every prototype unconditionally (tried first) broke two other
        // guarantees: a prototype placed ahead of the #include that
        // defines one of its struct-tag types gets function-prototype
        // scope for that tag, conflicting with the type's real,
        // later-in-scope definition (#953); and a function generated
        // inside a preprocessor-routed `#ifdef` block (test_emit_ordered_
        // ifdef.c) needs its prototype to stay inside that block, not
        // float above it. Doing this on demand, scanning each function's
        // body for calls to not-yet-declared generated functions right
        // before emitting it, keeps unrelated functions and #ifdef-guarded
        // ones exactly where they were and only forward-declares what a
        // caller actually needs.
        ObjVec declared = {0};
        for (EmitEvent *ev = vm->compiler.emit_events_head; ev; ev = ev->next) {
            if (ev->kind == CCCC_EMIT_SOURCE) {
                fprintf(f, "%s\n", ev->source);
                continue;
            }
            Obj *obj = ev->obj;
            if (!obj || !obj->is_macro_generated)
                continue;
            if (obj->is_function) {
                if (obj->body) {
                    ObjVec needed = {0};
                    collect_generated_call_targets(obj->body, &needed);
                    for (int i = 0; i < needed.len; i++) {
                        Obj *callee = needed.data[i];
                        if (obj_vec_contains(&declared, callee))
                            continue;
                        serialize_function_signature(f, &ctx, callee);
                        fprintf(f, ";\n");
                        obj_vec_push(&declared, callee);
                    }
                    free(needed.data);
                }
                // A FunctionPrototype()+PublishNode() that never gets a
                // body still needs to reach the output -- previously
                // dropped entirely by this loop's `!is_definition &&
                // !body` skip.
                if (!obj_vec_contains(&declared, obj)) {
                    serialize_function_signature(f, &ctx, obj);
                    fprintf(f, ";\n\n");
                    obj_vec_push(&declared, obj);
                }
                if (obj->body)
                    serialize_function(f, vm, &ctx, obj);
            } else if (obj->name[0] != '.') {
                serialize_global_var(f, vm, &ctx, obj);
            }
        }
        free(declared.data);
        free(ctx.seen.data);
        free(ctx.defs.data);
        free(ctx.tags);
        free(ctx.typedefs);
        free(ctx.captured_paths);
        free(ctx.block_envs);
        free(ctx.hoisted.data);
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
        // --emit-cccc: re-emit cccc-only includes too -- the caller has
        // opted into dialect-fidelity output, so a downstream reader is
        // expected to understand the routing syntax those files carry.
        if (!vm->compiler.emit_cccc && resolved && cc_file_is_cccc_only(vm, resolved)) {
            // #1003: <decimal_math.h>'s static inline wrappers all bottom
            // out in `extern __cccc_dec_*` symbols that exist only inside
            // the VM's FFI runtime (src/stdlib/decimal_math.c) -- unlike
            // every other header this loop suppresses (whose content the
            // type/function-definition passes below can genuinely
            // re-derive as real, linkable C), there is no host definition
            // to link against here. Re-deriving would only trade "file not
            // found" for "undefined symbol"; hard error instead, matching
            // the existing _Decimal serialization refusal
            // (__builtin_decimal_to_chars, above in this file).
            if (path_basename_is(resolved, "decimal_math.h"))
                error("cccc: <decimal_math.h> is not supported in "
                      "native/serialized output (__cccc_dec_* helpers have "
                      "no host definition)");
            continue;
        }
        fprintf(f, "%s\n", line);
    }
    if (vm->compiler.emit_directives.len > 0)
        fprintf(f, "\n");

    // #904: real symbols for internal host-accessor shims (stdout/errno/
    // etc) -- only meaningful once the real headers above are visible, and
    // only outside generated_only (-c=generated), matching the from_include
    // filter's gating for the same reason (see the comment on this function).
    if (!generated_only)
        serialize_native_accessor_shims(f, prog);

    // Serialize file-scope type definitions before declarations that reference them.
    serialize_type_defs_for_owner(f, &ctx, NULL);

    // #965/#993: see the comment on the generated_only branch's own call
    // above -- must run after both the #include replay and the file-scope
    // type-def pass just above, so a capture's type (however it reaches the
    // output) is already visible.
    serialize_block_preamble(f, vm, &ctx, prog);

    // #918: forward-declare every global before any definition, mirroring
    // the function-prototype pass below and for the same reason -- a
    // global's initializer can take the address of another global that
    // appears later in `prog` (e.g. `int *p = &g;` parsed/emitted before
    // `g`'s own definition), which used to compile "successfully" only
    // because that address was silently serialized as a null pointer
    // (defect C) rather than the real `&g` reference. Once the real
    // reference is emitted, the forward case needs a declaration in scope.
    // Redundant for the (common) non-forward-referencing case, but a
    // duplicate `extern`/tentative-`static` declaration is always valid C.
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (generated_only && !obj->is_macro_generated)
            continue;
        if (obj->is_function || obj->name[0] == '.')
            continue;
        fprintf(f, obj->is_static ? "static " : "extern ");
        serialize_type_decl(f, &ctx, obj->ty, obj->name);
        fprintf(f, ";\n");
    }

    // #999: forward-declare any function a global's initializer references
    // by address (`var->rel`, resolved the same way serialize_reloc_init
    // resolves it further down) -- e.g. `static const VT k = { .open =
    // none_open };` where `none_open` is a `static` function defined later
    // in `prog`. The #918 loop just above only forward-declares *globals*;
    // the function-prototype pass below (which would otherwise supply
    // `none_open`'s own declaration) doesn't run until after every global
    // definition has already been emitted, so a forward reference like this
    // one reached the output with nothing in scope yet ("use of undeclared
    // identifier"). Resolved on demand here rather than by moving the whole
    // prototype pass above the global-definitions pass: #953 records that
    // hoisting every prototype unconditionally can put a struct-tag
    // parameter type in function-prototype scope ahead of the #include that
    // actually defines it, conflicting with the tag's real, later
    // definition -- the same reasoning #956 used for generated-function
    // forward declarations. Deduped so a vtable naming the same function
    // twice (or two vtables sharing one) doesn't declare it twice.
    {
        ObjVec reloc_fns = {0};
        for (Obj *obj = prog; obj; obj = obj->next) {
            if (generated_only && !obj->is_macro_generated)
                continue;
            if (obj->is_function || obj->name[0] == '.')
                continue;
            for (Relocation *rel = obj->rel; rel; rel = rel->next) {
                if (!rel->label || !*rel->label)
                    continue;
                Obj *target = serialize_find_global(vm, *rel->label);
                if (!target || !target->is_function ||
                    target == vm->compiler.builtin_block_copy ||
                    obj_vec_contains(&reloc_fns, target))
                    continue;
                obj_vec_push(&reloc_fns, target);
                serialize_function_signature(f, &ctx, target);
                fprintf(f, ";\n");
            }
        }
        free(reloc_fns.data);
    }

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
        // #965: __cccc_block_copy_impl is a VM-only FFI shim (its real
        // implementation is host-side, src/stdlib/stdlib.c) -- it has no
        // obj->tok (ty->name was never set for this builtin prototype, see
        // its registration in parse.c), so the from_primary check just
        // below already leaves it unemitted here in practice. Skip it
        // explicitly regardless, so a native replacement is only ever
        // supplied by serialize_block_preamble's own static definition
        // (emitted when Block_copy is actually reachable) and this loop
        // can never introduce a second, conflicting extern declaration.
        if (obj == vm->compiler.builtin_block_copy)
            continue;
        // #999: a header-sourced `static` definition is already supplied
        // by that header's own replayed #include text -- see
        // function_is_header_supplied()'s comment. This is the "has a
        // body" counterpart to the from_primary check the bodyless branch
        // just below already applies.
        if (function_is_header_supplied(vm, &ctx, obj))
            continue;
        if (!obj->is_definition && !obj->body) {
            // #901: a bare declaration with no body (e.g. `int abs(int
            // x);`) used to be dropped entirely here. The VM path needs
            // no native declaration -- it resolves the call as an FFI
            // symbol with a known signature -- but the downstream system
            // compiler does, so silently omitting it produced an
            // undeclared-function error in the generated C. Emit it when
            // it was written in a command-line input file (or in a cccc-only-
            // routed include, whose own #include is never re-emitted --
            // #896); a header-sourced declaration is left out, since the
            // auto-captured #include (see TypeNameRecord.from_include)
            // already supplies it to the native compiler. An implicit
            // declaration's guessed signature is skipped outright -- it
            // could conflict with the real one from a re-emitted header.
            if (obj->is_implicit)
                continue;
            // #956: a FunctionPrototype()+PublishNode() generated function
            // has no obj->tok (it was synthesized, not parsed from any
            // file), so the from_primary check below would always drop
            // it -- treat every macro-generated prototype as eligible
            // regardless of origin, matching the emit-event path's
            // unconditional hoist above.
            if (!obj->is_macro_generated) {
                Token *t = obj->tok;
                // #1002 (investigation): file_is_command_line_input(), not a
                // primary_file-only comparison -- see that function's
                // comment. Variable renamed from from_primary to
                // from_input to match.
                bool from_input = t && t->file &&
                    (file_is_command_line_input(vm, t->file->name) ||
                     cc_file_is_cccc_only(vm, t->file->name));
                if (!from_input)
                    continue;
            }
        }
        serialize_function_signature(f, &ctx, obj);
        fprintf(f, ";\n\n");
    }

    // Serialize functions
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (generated_only && !obj->is_macro_generated)
            continue;
        if (obj->is_function && !function_is_header_supplied(vm, &ctx, obj))
            serialize_function(f, vm, &ctx, obj);
    }

    free(ctx.seen.data);
    free(ctx.defs.data);
    free(ctx.tags);
    free(ctx.typedefs);
    free(ctx.captured_paths);
    free(ctx.block_envs);
    free(ctx.hoisted.data);
}

