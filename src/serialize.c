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
    // #1010: mirrors TypeNameRecord.defines_type -- see that field's comment
    // (cccc.h) and find_tag_name_for_provenance() below.
    bool defines_type;
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

// #1074: the native-serializer mirror of a GNU nested function's static
// link. One entry per function that directly parents at least one
// Obj.is_nested function -- even a function whose own locals are never
// referenced by a descendant still needs an entry (with an empty upvars
// list) purely to carry `__up` for an intervening level of a multi-level
// nest. `upvars` holds `owner_fn`'s own locals/params that some nested
// descendant (at any depth) reads or writes, in first-seen order -- that
// order is also each var's field index (`__uv<index>`). Built once in
// serialize_nested_preamble(), read-only afterward; mirrors BlockEnvEntry/
// serialize_block_preamble()'s shape exactly, just keyed by the parent
// function instead of the block literal.
typedef struct {
    Obj *owner_fn;
    char *env_struct_name; // includes the leading "struct " keyword
    Obj **upvars;
    int upvars_len;
    int upvars_cap;
} NestedEnvEntry;

// #1005: break/continue lower to an ND_GOTO with only `unique_label` set (a
// non-C-identifier ".L..N" string, parse.c's new_unique_name()) -- a source
// `goto` sets `label` instead. Serializing such an ND_GOTO back to a literal
// `break;`/`continue;` needs to know, at the point it's printed, which
// enclosing construct `unique_label` refers to. This mirrors NNJumpTarget/
// nn_find_target (parse.c's null-neighbor analysis) exactly, including its
// pointer-identity matching rule: new_unique_name() hands the identical
// string pointer to both a construct's brk_label/cont_label and to every
// break/continue targeting it, so `==` (not strcmp) is correct and cheap.
typedef struct SerJumpFrame {
    struct SerJumpFrame *parent;
    char *brk_label;   // NULL if this construct isn't a break target
    char *cont_label;  // NULL for switch -- parse.c's switch parsing only
                        // saves/restores brk_label, never cont_label, so a
                        // `continue` inside a switch inside a loop must skip
                        // over the switch frame and resolve to the loop.
} SerJumpFrame;

// #1015: one renamed enumerator, keyed by the enum Type it belongs to (via
// same_type_strong()) plus its original spelling -- see the ctx->
// enum_renames doc comment on SerializeContext for why this is a table
// rather than an in-place EnumConstant.name mutation.
typedef struct {
    Type *rep;
    char *orig;
    char *new_name;
} EnumConstRename;

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
    // #1074: nested-function env structs -- see NestedEnvEntry and
    // serialize_nested_preamble(). Built (and every illegal non-call
    // reference to a nested function rejected) entirely during that one
    // preamble pass, before any function body is actually emitted -- so
    // serialize_expr's own ND_VAR/ND_FUNCALL arms never need to re-derive
    // "is this reference legal", only "what does a known-legal one print
    // as".
    NestedEnvEntry *nested_envs;
    int nested_envs_len;
    int nested_envs_cap;
    // #989: types promoted from function-local to file scope (a block
    // capture's own struct/union/enum type declared inside a function,
    // needed because its lifted environment struct is emitted at file
    // scope). Doubles as the post-order seen-set during promotion and as
    // the skip-set serialize_type_defs_for_owner uses to avoid re-emitting
    // a definition the preamble already wrote out.
    TypeVec hoisted;
    int hoisted_type_counter; // names renamed/synthesized hoisted tags, parallel to anon_global_counter
    // #1046: struct/union/enum bodies actually printed so far, across BOTH
    // emitters that can print one -- the usage-driven ctx->defs loop in
    // serialize_type_defs_for_owner() and the typedef-dependency-driven
    // emit_typedef_and_deps() below (an anonymous aggregate whose only
    // spelling is a typedef alias, e.g. `typedef struct { int a[2]; } P,
    // *Pp;`, is never pushed into ctx->defs at all when P itself is never
    // used by value -- see emit_typedef_and_deps's own comment). Shared
    // dedup set so neither emitter re-prints what the other already did.
    TypeVec emitted_defs;
    // #1005: current break/continue target stack (innermost first) and the
    // innermost enclosing ND_SWITCH (for ND_CASE to test default_case
    // against) -- both NULL outside any loop/switch, pushed/popped around
    // ND_FOR/ND_DO/ND_SWITCH bodies in serialize_stmt.
    SerJumpFrame *jumps;
    Node *cur_switch;
    // #1014: set by rename_colliding_type_tags() iff it actually renamed a
    // colliding struct/union/enum tag -- gates find_tag_name()'s extra
    // completeness-preferring scan so a program with no collision is
    // unaffected (byte-identical output).
    bool tag_renamed;
    // #1015: colliding-enumerator rename table, built by
    // rename_colliding_enum_constants() (right after rename_colliding_
    // type_tags()). Deliberately a print-time lookup table, not a mutation
    // of EnumConstant.name -- same_type_or_origin()'s TY_ENUM arm compares
    // enumerators by strcmp, and every consumer of it (collect_type,
    // type_vec_contains/find, find_tag_name) runs after this pass; mutating
    // one Type's enumerator and not a structurally-identical sibling's
    // would break that equality and re-introduce the collision as a
    // full-definition dup instead. Consulted only by serialize_enum_def via
    // enum_const_spelling().
    EnumConstRename *enum_renames;
    int enum_renames_len;
    int enum_renames_cap;
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

    // #1006: two command-line input files can each independently declare an
    // identical `typedef enum { ... } Thing;` at file scope (record_type_name
    // no longer marks either from_include -- see the parse.c fix above), and
    // unlike TY_STRUCT/TY_UNION just above, this branch previously had no
    // structural fallback for TY_ENUM at all -- two origin-unrelated Type
    // objects with the same tag/spelling always compared unequal, so
    // type_vec_contains() (below) never deduped them and both got pushed
    // into ctx->defs, producing a hard "redefinition of enumerator" error
    // from the host compiler. Mirrors the TY_STRUCT/TY_UNION shape exactly:
    // tag mismatch is conclusive when both are tagged; otherwise (or when
    // tags match) fall back to comparing enumerators by name and value.
    // #1046: an array member's element type (e.g. `char n[32]` inside a
    // tagless struct) had no structural fallback at all -- only the
    // pointer-identity origin-chain check above applied, which two
    // independently-parsed occurrences of the same declaration (comptime's
    // re-parse of a `[[cccc::comptime]] gen()`-visible file-scope
    // declaration, same mechanism #1006's TY_ENUM case above was added for)
    // never share. Without this, a duplicate anonymous struct/union whose
    // difference is buried inside an array member's dimension+base (as
    // opposed to a scalar member, which TY_ARRAY's own recursion into a
    // non-array base already reaches once THIS TY_ARRAY case exists) always
    // compared unequal, so callers relying on this function to dedup two
    // structurally-identical re-parsed declarations (#1046's
    // ctx->emitted_defs) printed the same aggregate body twice -- a hard
    // "typedef redefinition with different types" from the host compiler
    // even though the two types are, by every measure other than pointer
    // identity, identical.
    if (a && b && a->kind == TY_ARRAY && b->kind == TY_ARRAY)
        return a->array_len == b->array_len && same_type_or_origin(a->base, b->base);

    if (a && b && a->kind == TY_ENUM && b->kind == TY_ENUM) {
        if (a->struct_tag && b->struct_tag) {
            bool tag_match = a->struct_tag->len == b->struct_tag->len &&
                             strncmp(a->struct_tag->loc, b->struct_tag->loc,
                                     a->struct_tag->len) == 0;
            if (!tag_match)
                return false;
        }
        EnumConstant *ea = a->enum_constants;
        EnumConstant *eb = b->enum_constants;
        for (; ea && eb; ea = ea->next, eb = eb->next) {
            if ((ea->name == NULL) != (eb->name == NULL))
                return false;
            if (ea->name && strcmp(ea->name, eb->name) != 0)
                return false;
            if (ea->value != eb->value)
                return false;
        }
        return ea == NULL && eb == NULL;
    }

    return false;
}

static bool type_vec_contains(TypeVec *vec, Type *ty) {
    for (int i = 0; i < vec->len; i++)
        if (same_type_or_origin(vec->data[i], ty))
            return true;
    return false;
}

// #1010 defect B: index-returning counterpart to type_vec_contains(), used
// by collect_type()'s incomplete-vs-complete swap below.
static int type_vec_find(TypeVec *vec, Type *ty) {
    for (int i = 0; i < vec->len; i++)
        if (same_type_or_origin(vec->data[i], ty))
            return i;
    return -1;
}

static void type_vec_remove_at(TypeVec *vec, int idx) {
    if (idx < 0 || idx >= vec->len)
        return;
    for (int i = idx; i < vec->len - 1; i++)
        vec->data[i] = vec->data[i + 1];
    vec->len--;
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
                           char *file_path, bool defines_type) {
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
    (*items)[*len].defines_type = defines_type;
    (*len)++;
}

static void collect_scope_names(SerializeContext *ctx, VirtualMachine *vm) {
    for (TypeNameRecord *rec = vm->compiler.type_names; rec; rec = rec->next) {
        if (rec->is_tag)
            type_name_push(&ctx->tags, &ctx->tags_len, &ctx->tags_cap, rec->ty,
                           rec->name, rec->name_len, rec->owner_fn,
                           rec->from_include, rec->always_emit, rec->file_path,
                           rec->defines_type);
        else
            type_name_push(&ctx->typedefs, &ctx->typedefs_len,
                           &ctx->typedefs_cap, rec->ty, rec->name,
                           rec->name_len, rec->owner_fn,
                           rec->from_include, rec->always_emit, rec->file_path,
                           rec->defines_type);
    }
}

static bool name_visible(TypeName *name, Obj *fn) {
    return name->owner_fn == NULL || name->owner_fn == fn;
}

// #1014: true when `ty` is a struct/union/enum with an actual body -- an
// incomplete (forward-declared-only) tagged aggregate has no members/
// enumerators for same_type_or_origin()'s structural fallback to compare, so
// it deliberately compares equal to *any* complete aggregate sharing its tag
// (#892, same_type_or_origin's `a->size < 0 || b->size < 0` branch above).
// That's the right call for provenance/dedup, but fatal for
// same_type_strong() below: without excluding incomplete types, a pure-use
// TU's incomplete record would match -- and vote for -- every differently-
// shaped complete group sharing the tag.
static bool type_is_complete_tagged(Type *ty) {
    if (!ty)
        return false;
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
        return ty->size >= 0;
    if (ty->kind == TY_ENUM)
        return ty->enum_constants != NULL;
    return false;
}

// #1014: same_type_or_origin(), but additionally requires both sides to
// agree on completeness. Used only where distinguishing two differently-
// shaped *complete* aggregates sharing one tag actually matters
// (rename_colliding_type_tags() and find_tag_name()'s post-rename lookup
// below) -- everywhere else same_type_or_origin()'s looser, deliberate
// incomplete-matches-complete behavior is still correct and unchanged.
static bool same_type_strong(Type *a, Type *b) {
    return type_is_complete_tagged(a) == type_is_complete_tagged(b) &&
           same_type_or_origin(a, b);
}

// #1015: forward-declared here since serialize_enum_def() (below) needs it
// but its definition, next to rename_colliding_enum_constants(), comes
// much later in this file.
static const char *enum_const_spelling(SerializeContext *ctx, Type *ty, const char *name);

// #1047: forward-declared here since serialize_global_var() (below) needs
// it but its definition, next to function_is_header_supplied() (the
// function-side counterpart it mirrors), comes much later in this file.
static bool global_is_header_supplied(VirtualMachine *vm, SerializeContext *ctx,
                                      Obj *obj);

static TypeName *find_tag_name(SerializeContext *ctx, Type *ty) {
    if (!ctx || !ty)
        return NULL;

    // #1014: once a collision has actually been renamed, prefer a
    // completeness-matched record first -- this is what routes a pure-use
    // TU's incomplete Type (e.g. a bare `DyGC *` parameter) to the record
    // that still carries the plain, header-exposed name, while each
    // differently-shaped complete definition resolves to its own (possibly
    // renamed) group. Skipped entirely when nothing was renamed, so a
    // program with no tag collision serializes byte-identically to before.
    if (ctx->tag_renamed)
        for (int i = 0; i < ctx->tags_len; i++)
            if (name_visible(&ctx->tags[i], ctx->current_fn) &&
                same_type_strong(ctx->tags[i].ty, ty))
                return &ctx->tags[i];

    for (int i = 0; i < ctx->tags_len; i++)
        if (name_visible(&ctx->tags[i], ctx->current_fn) &&
            same_type_or_origin(ctx->tags[i].ty, ty))
            return &ctx->tags[i];
    return NULL;
}

// #1010: like find_tag_name(), but for deciding *provenance* (whether a
// header supplies this type's definition) rather than spelling. ctx->tags
// is in reverse record-creation order (collect_scope_names() walks
// type_names head-first, and record_type_name() prepends), so
// find_tag_name()'s first-match scan returns whichever record was created
// *last* -- normally fine (a later declaration's provenance is the more
// accurate one), but wrong when that later record is an unrelated forward
// declaration of an already-completed tag: same_type_or_origin() treats a
// tagged incomplete aggregate as equal to the tagged complete one
// (deliberately -- see that function's #892 comment), so a from_include
// forward declaration recorded after the real definition (e.g. a second
// command-line input file re-parsing a shared header under #1001's per-TU
// preprocessor isolation) would otherwise win and wrongly suppress the only
// definition available. Prefers a defines_type record; falls back to the
// first match (today's behavior) when no record actually defines the tag,
// e.g. every command-line input file only ever forward-declares it.
static TypeName *find_tag_name_for_provenance(SerializeContext *ctx, Type *ty) {
    if (!ctx || !ty)
        return NULL;

    TypeName *first_match = NULL;
    for (int i = 0; i < ctx->tags_len; i++) {
        if (!name_visible(&ctx->tags[i], ctx->current_fn) ||
            !same_type_or_origin(ctx->tags[i].ty, ty))
            continue;
        if (ctx->tags[i].defines_type)
            return &ctx->tags[i];
        if (!first_match)
            first_match = &ctx->tags[i];
    }
    return first_match;
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

    // #1005: no ND_SWITCH/ND_CASE special case here (there used to be one,
    // walking node->case_next/->default_case instead of node->then) -- that
    // walked only each case's *first* statement, so a type used later in a
    // case's body (after its first statement) was never collected, and its
    // definition never emitted. ND_CASE nodes sit inline in the switch
    // body's statement list (node->then's ND_BLOCK -> ->body -> ->next
    // chain), so the generic traversal below already reaches every case via
    // ->then, and each ND_CASE's own generic ->lhs/->next below reaches its
    // target statement and the following statement in the block -- no
    // special-casing needed, matching how the ND_SWITCH/ND_CASE serializer
    // arms were rewritten to work.
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

    int seen_idx = type_vec_find(&ctx->seen, ty);
    if (seen_idx >= 0) {
        // #1010 defect B: same_type_or_origin() deliberately treats a
        // tagged *incomplete* struct/union as equal to the tagged
        // *complete* one (#892) -- so a use-site TU that only ever sees a
        // header's forward declaration can get here first (e.g. #1001's
        // per-TU preprocessor isolation re-parsing a shared header from a
        // second command-line input file) and claim ctx->seen/ctx->defs'
        // slot for its member-less Type* before the completing TU's own
        // Type* is ever collected. serialize_struct_def then has no
        // members to print and emits a bare `struct Foo;`. Swap the
        // complete Type* in when this happens: remove-then-recollect
        // (rather than mutating the ctx->defs entry in place) so a member
        // type the incomplete stub never referenced -- e.g. `struct Outer
        // { struct Inner i; }` -- is still collected and, being freshly
        // pushed, still emitted ahead of its own user. TY_ENUM is excluded
        // deliberately: same_type_or_origin()'s enum arm compares
        // enum_constants lists directly with no completeness shortcut, so
        // an incomplete (constant-less) enum never structurally matches a
        // complete one in the first place -- this branch is unreachable
        // for enums.
        Type *prior = ctx->seen.data[seen_idx];
        if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) &&
            prior->size < 0 && ty->size >= 0) {
            ctx->seen.data[seen_idx] = ty;
            type_vec_remove_at(&ctx->defs, type_vec_find(&ctx->defs, ty));
            for (Member *m = ty->members; m; m = m->next)
                collect_type(ctx, m->ty);
            type_vec_push(&ctx->defs, ty);
        }
        return;
    }
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

    // #1005: see the matching comment in collect_node_types() -- no
    // ND_SWITCH/ND_CASE special case needed; the generic traversal below
    // already reaches every case via node->then and each ND_CASE's own
    // ->lhs/->next.
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

// Emit a float initializer value as a C `f`-suffixed floating constant.
// `%.9g` of an integral value like 1.0f prints "1" with no '.'/'e' -- append
// with the "f" suffix directly that reads as the invalid token `1f`
// ("invalid digit 'f' in decimal constant" from the host compiler), not a
// float literal. Force a decimal point when the %g text has none, so the
// suffix always lands on a valid floating-constant. inf/nan text
// ("inf"/"-inf"/"nan") is left alone -- neither is a valid C floating
// constant with an "f" suffix at all; that's a separate, pre-existing gap.
// #1021: neither plain %g/%Lg text ("inf"/"-inf"/"nan") nor
// format_float_literal's own "always end in a valid float suffix" fixup
// (immediately below -- its comment already flagged this exact gap) produce
// a token real C accepts; a bare `nan`/`inf` identifier is undeclared and
// the host build fails with "call to undeclared function"/"use of
// undeclared identifier". __builtin_{inf,nan}[f|l] are portable clang/gcc
// intrinsics that always parse, with no header dependency at all. `suf` is
// "" for TY_DOUBLE, "f" for TY_FLOAT, "l" for TY_LDOUBLE, matching each
// builtin family's own naming (__builtin_inf/inff/infl,
// __builtin_nan/nanf/nanl). Returns true (having already printed) when `v`
// was non-finite; the caller falls through to its normal finite-value
// formatting otherwise.
static bool serialize_flonum_special(FILE *f, long double v, const char *suf) {
    if (isnan(v)) {
        fprintf(f, "__builtin_nan%s(\"\")", suf);
        return true;
    }
    if (isinf(v)) {
        fprintf(f, "%s__builtin_inf%s()", (v < 0) ? "-" : "", suf);
        return true;
    }
    return false;
}

static void format_float_literal(char *buf, size_t cap, double v) {
    int n = snprintf(buf, cap, "%.9g", v);
    if (n > 0 && (size_t)n < cap
        && !strpbrk(buf, ".eEnN")) {
        snprintf(buf + n, cap - (size_t)n, ".0");
    }
}

// #1038: long-double counterpart of format_float_literal, same "force a
// decimal point before the suffix lands" reasoning -- %.21Lg of an
// integral long double like 1.0L prints "1" with no '.'/'e', and "1"
// followed directly by an "L" suffix is a valid (and wrong) *integer*
// literal token, not the intended floating one. 21 significant digits is
// enough to round-trip an 80-bit x86 extended-precision long double (64
// bits of mantissa, ~19.2 decimal digits) with margin; a narrower
// long double (e.g. aarch64, where long double == double) round-trips
// exactly with fewer digits, %.21Lg just prints trailing zeros for it.
static void format_ldouble_literal(char *buf, size_t cap, long double v) {
    int n = snprintf(buf, cap, "%.21Lg", v);
    if (n > 0 && (size_t)n < cap
        && !strpbrk(buf, ".eEnN")) {
        snprintf(buf + n, cap - (size_t)n, ".0");
    }
}

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

// #1023: a global whose type is (or contains, through TY_ARRAY/TY_PTR)
// an untagged, alias-less struct/union -- `static const struct { ... }
// codes[74]` is exactly this -- has no name serialize_type_decl can spell
// in a standalone forward declaration; each attempt instead falls through
// to serialize_anon_aggregate (below) and re-derives the member list
// verbatim, which the host compiler treats as a *structurally distinct*
// anonymous type every time it's written out, even though the text is
// identical (C has no notion of two anonymous struct spellings being "the
// same" type). The #918/#928 forward-declaration pass emitting one before
// the real definition therefore produces two non-identical types under one
// name -- "redefinition of 'codes' with a different type". Such a global
// is fundamentally unrepresentable as a forward declaration in C (there is
// no name to forward-declare), so the caller skips the forward-decl line
// for it entirely rather than emit invalid text; the real definition
// (serialize_global_var) still carries the only copy of the type. Reuses
// exactly the tag/typedef/anonymous-typedef lookup serialize_type's own
// TY_STRUCT/TY_UNION cases use to decide whether to call
// serialize_anon_aggregate in the first place, so this predicate is true
// if and only if that call would be.
static bool type_needs_anon_aggregate(SerializeContext *ctx, Type *ty) {
    while (ty && (ty->kind == TY_ARRAY || ty->kind == TY_PTR || ty->kind == TY_VLA))
        ty = ty->base;
    if (!ty || (ty->kind != TY_STRUCT && ty->kind != TY_UNION))
        return false;
    return !find_tag_name(ctx, ty) && !find_typedef_name(ctx, ty) &&
           !find_anonymous_typedef_name(ctx, ty);
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

    // #1045: a const-qualified *pointer* (`int *const p`, is_const lives on
    // the TY_PTR Type itself, not its base) used to print this leading
    // `const ` unconditionally, then fall through to the switch's TY_PTR
    // case below, which recurses into serialize_type_decl() -- whose own
    // TY_PTR branch has never emitted pointer-level const at all (it only
    // ever prints `*name`, never `*const name`). The leading `const ` above
    // therefore ended up qualifying the *pointee* instead: `const int *`
    // (pointer to const int) rather than `int *const` (const pointer to
    // int) -- a genuinely incompatible type the host compiler rejects
    // ("incompatible function pointer types") wherever a const-pointer
    // value is cast to or declared alongside its non-const-pointer
    // counterpart. Same bug, latent: TY_FUNC's parameter list
    // (serialize_type_decl below) prints each param through this same
    // function, so a `void f(int *const p)` parameter's prototype and
    // definition could disagree the same way. Fixed by normalizing rather
    // than relocating the qualifier: a bare (non-typedef'd) pointer never
    // gets pointer-level const printed here either, matching what
    // declarator position already does -- dropping a top-level qualifier
    // is always type-compatible C, and CCCC's own parser already enforced
    // constness before this point. A *typedef'd* pointer (`const MyPtrT`,
    // where MyPtrT's underlying type is itself a pointer) is a different
    // case and untouched: `const MyPtrT` correctly spells "const-qualify
    // the whole aliased type", i.e. exactly `T *const`, so that alias arm
    // below still needs the leading `const` -- only the un-aliased,
    // structurally-printed TY_PTR fallthrough drops it.
    bool suppress_ptr_const = ty->kind == TY_PTR &&
                              !find_typedef_name_exact(ctx, ty);
    if (ty->is_const && !suppress_ptr_const)
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

// #1074: the env struct name serialize_nested_preamble() paired with
// `owner_fn`, or a defensive placeholder if somehow missing (every function
// that directly parents a nested function gets an entry -- see
// NestedEnvEntry's comment).
static const char *find_nested_env_name(SerializeContext *ctx, Obj *owner_fn) {
    for (int i = 0; i < ctx->nested_envs_len; i++)
        if (ctx->nested_envs[i].owner_fn == owner_fn)
            return ctx->nested_envs[i].env_struct_name;
    return "struct __cccc_nenv_?";
}

// #1074: builds a C expression, of type `<target>'s own env struct> *`,
// giving the address of `target`'s env struct instance as seen from inside
// `from_fn`'s own serialized body -- `from_fn` must be nested and `target`
// must be `from_fn`'s immediate parent or a stricter ancestor of it (never
// `from_fn` itself; callers already special-case that, since it just means
// "my own env", `&__cccc_nenv`, with nothing to chase).
//
// Mirrors codegen_expr.c's calling_nested static-link chase
// (:2938-2960) exactly: `from_fn`'s own `__static_link` parameter already
// *is* a pointer to its immediate parent's env (untyped `void *`, so it
// only needs a cast); reaching a stricter ancestor means walking that
// env's own `__up` field once per intervening level, casting at each hop.
static char *nested_env_ptr_expr(VirtualMachine *vm, SerializeContext *ctx,
                                 Obj *from_fn, Obj *target) {
    Obj *p = from_fn->parent_fn;
    char *expr = arena_format(vm, "((%s *)__static_link)",
                              find_nested_env_name(ctx, p));
    while (p != target) {
        p = p->parent_fn;
        expr = arena_format(vm, "((%s *)%s->__up)",
                            find_nested_env_name(ctx, p), expr);
    }
    return expr;
}

// #1074: when `var` is an upvar of the nested function currently being
// serialized (ctx->current_fn) -- i.e. a local/param owned by one of its
// ancestors, recorded by serialize_nested_preamble()'s analysis pass --
// print the env-chase expression that reaches it and return true;
// otherwise print nothing and return false so the caller falls back to the
// variable's plain name (its own local/param, or a block capture, handled
// separately above). Every legal upvar reference was already validated
// (VLA/deferred-VLA-pointer/__block-storage locals rejected, see
// serialize_nested_preamble()) by the time this ever runs.
static bool serialize_nested_upvar_ref(FILE *f, VirtualMachine *vm,
                                       SerializeContext *ctx, Obj *var) {
    // is_block reuses is_nested for VM codegen purposes (parse_blocks.c) --
    // a block's own outer-local reference is already fully handled by
    // serialize_block_capture_ref() above, a distinct, already-correct
    // mechanism; excluded here so it's never double-handled.
    if (!ctx->current_fn || !ctx->current_fn->is_nested || ctx->current_fn->is_block)
        return false;
    for (Obj *v = ctx->current_fn->locals; v; v = v->next)
        if (v == var)
            return false; // owned by the nested function itself, not an upvar
    Obj *owner = NULL;
    for (Obj *anc = ctx->current_fn->parent_fn; anc; anc = anc->parent_fn) {
        for (Obj *v = anc->locals; v; v = v->next)
            if (v == var) { owner = anc; break; }
        if (owner)
            break;
    }
    if (!owner)
        return false; // defensive only -- see the identical scan below
    NestedEnvEntry *e = NULL;
    for (int i = 0; i < ctx->nested_envs_len; i++)
        if (ctx->nested_envs[i].owner_fn == owner) { e = &ctx->nested_envs[i]; break; }
    if (!e)
        return false; // defensive only -- owner must already have an entry
    int idx = -1;
    for (int i = 0; i < e->upvars_len; i++)
        if (e->upvars[i] == var) { idx = i; break; }
    if (idx < 0)
        return false; // defensive only

    fprintf(f, "(*%s->__uv%d)",
            nested_env_ptr_expr(vm, ctx, ctx->current_fn, owner), idx);
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

// #1018 follow-up: C's default argument promotions (C17 6.5.2.2p6/7,
// applied to a variadic call's own trailing arguments) mean a real
// `va_arg(ap, T)` may never spell a promotable T -- clang/gcc both
// diagnose it explicitly ("second argument to 'va_arg' is of promotable
// type 'float'/'char'/...; this va_arg has undefined behavior because
// arguments will be promoted to 'double'/'int'"). CCCC's own VM-ABI
// va_arg macro reads exactly `type`'s own width from the slot (its
// __builtin_choose_expr's fp arm already special-cases float, but only to
// pick the 8-byte double-read path -- the *printed* type argument still
// came from the user's literal `type` text, unpromoted) and happens to
// work regardless, since the VM's own calling convention always writes a
// full 8-byte slot -- so this had no VM-visible symptom, only a native
// diagnostic (confirmed against real clang: unpromoted `char`/`float`
// both actually round-trip correctly here since clang's promotion and
// CCCC's slot width agree, but printing the promoted type is what a real
// host compiler's own <stdarg.h> expects and is the only way to silence
// the UB warning). Mirrors integer_promotion (type.c, static) for the
// integer half; float is the one additional case that helper doesn't
// cover, since it's scoped to is_integer().
static Type *va_arg_promoted_type(Type *ty) {
    if (!ty)
        return ty;
    if (ty->kind == TY_FLOAT)
        return ty_double;
    if (is_integer(ty) && ty->kind != TY_BITINT && ty->size < 4)
        return ty_int;
    return ty;
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

    // #1018: a va_start/va_arg/va_copy/va_end node (Node.va_form, src/
    // cccc.h) always prints as the real host <stdarg.h> call instead of
    // walking its own VM-ABI impl subtree -- checked before node_prec is
    // computed from node->kind, since that kind (whatever the impl
    // expression's own root operator happens to be -- ND_STMT_EXPR,
    // ND_COND, ND_ASSIGN, ...) has nothing to do with this call's own
    // precedence (highest, like any other function call). No on-demand
    // #include is needed the way #1050/#1057 needed one: unlike a
    // reflection-API builder resolving memcpy/size_t with no source-level
    // #include at all, va_start/va_arg/va_copy/va_end only exist as macros
    // -- reaching this node at all requires the user's own
    // `#include <stdarg.h>`/`"stdarg.h"` to already be in the TU (for the
    // macro expansion to have happened), and that line is auto-captured
    // and replayed like any ordinary header; include/stdarg.h's own
    // `#include_next` hand-off (the #1040 follow-on) already resolves it
    // to the real host header whenever a genuine host compiler (not
    // CCCC's own preprocessor) is the one reading it.
    if (node->va_form != VA_NONE) {
        switch (node->va_form) {
        case VA_START:
            fprintf(f, "va_start(");
            serialize_expr(f, vm, ctx, node->va_ap, 2);
            fprintf(f, ", ");
            serialize_expr(f, vm, ctx, node->va_last, 2);
            fprintf(f, ")");
            return;
        case VA_ARG:
            fprintf(f, "va_arg(");
            serialize_expr(f, vm, ctx, node->va_ap, 2);
            fprintf(f, ", ");
            serialize_type(f, ctx, va_arg_promoted_type(node->va_type));
            fprintf(f, ")");
            return;
        case VA_COPY:
            fprintf(f, "va_copy(");
            serialize_expr(f, vm, ctx, node->va_ap, 2);
            fprintf(f, ", ");
            serialize_expr(f, vm, ctx, node->va_src, 2);
            fprintf(f, ")");
            return;
        case VA_END:
            fprintf(f, "va_end(");
            serialize_expr(f, vm, ctx, node->va_ap, 2);
            fprintf(f, ")");
            return;
        default:
            break;
        }
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
        } else if (node->ty && is_flonum(node->ty)) {
            // #1038: this used to funnel every flonum type -- TY_FLOAT,
            // TY_DOUBLE and TY_LDOUBLE alike -- through a single unsuffixed
            // `%Lg` (default 6 significant digits), losing both precision
            // (e.g. DBL_MAX round-tripped as the text "1.79769e+308") and,
            // for TY_FLOAT/TY_LDOUBLE, the type entirely (a `1.0f` or
            // `1.0L` literal re-emitted as a plain double constant).
            // node->fval is always long double; TY_DOUBLE's %g conversion
            // needs an explicit (double) cast -- passing a long double to a
            // non-L conversion is undefined varargs behavior, harmless on
            // platforms where long double == double (aarch64) but wrong on
            // x86_64. Mirrors serialize_init_bytes's TY_FLOAT/TY_DOUBLE
            // arms (this is the ND_NUM/expression-literal counterpart of
            // that global-initializer path).
            if (node->ty->kind == TY_FLOAT) {
                if (!serialize_flonum_special(f, node->fval, "f")) {
                    char buf[64];
                    format_float_literal(buf, sizeof buf, (double)node->fval);
                    fprintf(f, "%sf", buf);
                }
            } else if (node->ty->kind == TY_LDOUBLE) {
                // Builtin family name suffix ("l") and the literal suffix
                // ("L") are cased differently -- __builtin_infl/__builtin_nanl
                // vs. the `L` token suffix -- don't conflate them.
                if (!serialize_flonum_special(f, node->fval, "l")) {
                    char buf[64];
                    format_ldouble_literal(buf, sizeof buf, node->fval);
                    fprintf(f, "%sL", buf);
                }
            } else { // TY_DOUBLE
                // #1058: same "force a decimal point" fixup
                // format_float_literal/format_ldouble_literal already apply
                // for TY_FLOAT/TY_LDOUBLE -- this arm used to print a bare
                // %.17g with no fixup at all, so an integral value like
                // 55.0 serialized as "55", read back by a real host
                // compiler as an *integer* literal. Harmless under an
                // enclosing (double) cast, a wrong answer wherever the
                // literal's own text is what supplies its type (e.g. a
                // double vararg passed positionally, #1018).
                if (!serialize_flonum_special(f, node->fval, "")) {
                    char buf[64];
                    int n = snprintf(buf, sizeof buf, "%.17g", (double)node->fval);
                    if (n > 0 && (size_t)n < sizeof buf && !strpbrk(buf, ".eEnN"))
                        snprintf(buf + n, sizeof(buf) - (size_t)n, ".0");
                    fprintf(f, "%s", buf);
                }
            }
        } else if (node->ty && is_integer(node->ty)) {
            // #1031: the old unconditional `%lld` of the raw bit pattern
            // had two failures for a folded integer literal. (1) An
            // unsigned 64-bit value >= 2^63 (e.g. 18446744073709551615ULL,
            // ULLONG_MAX) prints its reinterpreted-as-signed text with no
            // `U`/`ULL` suffix, so a real host compiler reads it back as a
            // negative `int` -- a later implicit conversion (e.g. to
            // double) then produces the wrong value. Confirmed via
            // test_unsigned_int_to_float_conversion.c. (2) INT64_MIN
            // itself (-9223372036854775808) isn't a valid signed literal
            // token at all -- the host warns "integer literal is too
            // large to be represented in a signed integer type,
            // interpreting as unsigned" and silently reinterprets it.
            // Emitting a width/sign-accurate suffix always (not only when
            // the value would otherwise misparse) also matters for a
            // partially folded expression: `sizeof(x) - 1` folding to
            // `8ULL` vs. plain `8` changes a later subtraction from
            // unsigned-huge to signed -2.
            //
            // A negative node->val can only reach here via constant
            // folding -- a literal `-1` as written in source parses as
            // ND_NEG(ND_NUM(1)), not a single ND_NUM with val == -1 (see
            // the #1031-adjacent note near line 1677 below).
            if (node->ty->is_unsigned) {
                uint64_t uv = (uint64_t)node->val;
                // Mask to the type's true width first -- a narrow
                // unsigned value may have arrived here sign-extended
                // (serialize_init_bytes does the same narrow-type
                // sign-extension at its own ND_NUM-adjacent site), and
                // 1ULL << 64 is UB so the size==8 case must skip the mask
                // entirely.
                if (node->ty->size < 8)
                    uv &= (1ULL << (node->ty->size * 8)) - 1;
                fprintf(f, "%llu%s", (unsigned long long)uv,
                        node->ty->size == 8 ? "ULL" : "U");
            } else if (node->val == INT64_MIN) {
                // The only spelling a host compiler accepts for the most
                // negative signed 64-bit value -- a bare
                // "-9223372036854775808" token is parsed as unary minus
                // applied to a positive literal one past LLONG_MAX.
                fprintf(f, "(-9223372036854775807LL - 1)");
            } else {
                fprintf(f, "%lld%s", (long long)node->val,
                        node->ty->size == 8 ? "LL" : "");
            }
        } else
            // A folded null-pointer constant (TY_PTR/TY_NULLPTR_T) or a
            // node with no type reaches here -- plain decimal text (e.g.
            // "0") is valid C in either context, no suffix needed.
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
            } else if (serialize_nested_upvar_ref(f, vm, ctx, node->var)) {
                // #1074: handled -- var is an upvar of the nested function
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
            } else if (node->var == vm->compiler.builtin_alloca) {
                // #1024: declare_builtin_functions (parse_decl.c) names this
                // Obj literally "alloca" so the VM's own symbol table
                // resolves it -- but that Obj has no source token and no
                // definition, so the #901 from_include prototype pass never
                // declares a plain `alloca` for the host compiler to see
                // ("call to undeclared library function 'alloca'"). Every
                // native cc that matters (gcc/clang) supplies
                // __builtin_alloca with no header at all; spell the call
                // that way here instead of trying to synthesize a
                // <alloca.h>/<stdlib.h> declaration.
                fprintf(f, "__builtin_alloca");
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
        // #1035: include/fenv.h's FE_DFL_ENV sentinel is spelled
        // `((const fenv_t *)-1)` -- a value src/stdlib/fenv.c's wrap_fe*()
        // functions recognize and substitute the real host FE_DFL_ENV for
        // under the VM, but under -c=native the literal -1 pointer reaches
        // the real host libm directly and gets dereferenced (SIGSEGV). The
        // generated C's own re-included <fenv.h> falls through to the
        // host's real header via #include_next (see that file's own
        // comment), so the bare identifier FE_DFL_ENV resolves in the
        // output -- emit it instead of the sentinel's numeric encoding.
        // The AST for a guest `FE_DFL_ENV` use is a *doubled* cast
        // (`(const fenv_t *)(const fenv_t *)-1`, once from the macro's own
        // cast and once from however the guest expression casts it again,
        // e.g. fesetenv's implicit-const-add), so peel through as many
        // nested casts-to-fenv_t-pointer as present down to the underlying
        // -1 constant, then swallow the whole outer node.
        if (node->ty && node->ty->kind == TY_PTR) {
            Node *inner = node;
            while (inner->kind == ND_CAST && inner->ty &&
                   inner->ty->kind == TY_PTR) {
                TypeName *pointee = find_typedef_name(ctx, inner->ty->base);
                if (!pointee || pointee->name_len != 6 ||
                    strncmp(pointee->name, "fenv_t", 6) != 0)
                    break;
                inner = inner->lhs;
            }
            // A literal -1 parses as ND_NEG(ND_NUM(1)), not a single
            // ND_NUM with val == -1 -- unwrap one more level.
            bool matched = false;
            if (inner != node) {
                Node *lit = inner;
                if (lit->kind == ND_NEG && lit->lhs) {
                    // The literal `1` being negated arrives wrapped in its
                    // own (implicit int-promotion) ND_CAST(s) -- peel
                    // those too before checking for ND_NUM(1).
                    Node *n = lit->lhs;
                    while (n->kind == ND_CAST && n->lhs)
                        n = n->lhs;
                    matched = n->kind == ND_NUM && n->val == 1;
                } else {
                    matched = lit->kind == ND_NUM && lit->val == -1;
                }
            }
            if (matched) {
                fprintf(f, "FE_DFL_ENV");
                break;
            }
        }
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
        // #1019: a scalar operand of a `vector op scalar` binary op gets an
        // implicit ND_CAST(vector_ty, scalar) inserted by usual_arith_conv
        // (type.c) as its internal marker for "broadcast this scalar across
        // the vector's lanes" -- it is not source-level C. GCC/clang perform
        // that broadcast themselves inside the operator and reject the same
        // thing spelled as an explicit cast ("invalid conversion between
        // vector type and integer type of different size"). Emit the bare
        // scalar operand instead and let the host compiler's own vector
        // extension do the broadcast, exactly as real vector_size source
        // would. Only the scalar-source case is suppressed here -- a
        // vector-to-vector cast (same-type no-op, or a genuine bitcast
        // between differently-shaped vectors) still needs to print.
        bool scalar_splat = dst && dst->kind == TY_VECTOR &&
                            src && src->kind != TY_VECTOR;
        if (widening || scalar_splat) {
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
        // #1019: GNU per-lane vector select -- the condition is itself a
        // vector (typically a comparison mask), and each lane independently
        // picks its then/els element (type.c's ND_COND type-checking, which
        // requires the arms to be identically-typed vectors here, and
        // codegen_expr.c's gen_vector_expr both dispatch on this same
        // is_vector(node->cond->ty) check). GCC accepts `cond ? a : b` with
        // a vector cond directly; clang rejects it ("used type '...' where
        // arithmetic or pointer type is required"). Lower to portable mask
        // arithmetic instead of relying on the GCC-only extension: the
        // condition's own vector type is guaranteed (type.c) to have the
        // same lane count/width as the arms, so it doubles as the mask type
        // and casting an arm to/from it is a same-size bitcast. `!= 0`
        // implements GCC's "nonzero", not "all-bits-set", per-lane
        // truthiness rule. Each of cond/then/els is evaluated exactly once.
        //
        // An ordinary C ternary with a *scalar* condition and vector arms
        // (standard C, not this GNU extension) falls through to the plain
        // `?:` below unchanged -- real clang already accepts that form.
        if (is_vector(node->cond->ty)) {
            fprintf(f, "__extension__ ({ ");
            serialize_type(f, ctx, node->cond->ty);
            fprintf(f, " __cccc_vsel_m = (");
            serialize_expr(f, vm, ctx, node->cond, 0);
            fprintf(f, ") != 0; ");
            serialize_type(f, ctx, node->ty);
            fprintf(f, " __cccc_vsel_t = (");
            serialize_expr(f, vm, ctx, node->then, 0);
            fprintf(f, "); ");
            serialize_type(f, ctx, node->ty);
            fprintf(f, " __cccc_vsel_f = (");
            serialize_expr(f, vm, ctx, node->els, 0);
            fprintf(f, "); (");
            serialize_type(f, ctx, node->ty);
            fprintf(f, ")((__cccc_vsel_m & (");
            serialize_type(f, ctx, node->cond->ty);
            fprintf(f, ")__cccc_vsel_t) | (~__cccc_vsel_m & (");
            serialize_type(f, ctx, node->cond->ty);
            fprintf(f, ")__cccc_vsel_f)); })");
            break;
        }
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
        // #1054/#1030: setjmp/longjmp/_setjmp/_longjmp all print as calls to
        // exactly `_setjmp`/`_longjmp` -- real, plain `extern`-declared
        // functions on every supported host (unlike plain `setjmp`, a
        // macro on glibc) -- with the env argument cast to `(void *)`
        // rather than the implicit `long *` these builtins' VM-side
        // parameter type would otherwise print (parse_decl.c). See
        // serialize_synth_setjmp_decls()'s own comment (this file) for why:
        // it declares exactly these two names, and never replays the
        // captured `#include <setjmp.h>` line, so nothing else may spell
        // the callee any other way here.
        if (node->lhs && node->lhs->kind == ND_VAR && node->lhs->var &&
            ((vm->compiler.builtin_setjmp &&
              node->lhs->var == vm->compiler.builtin_setjmp) ||
             (vm->compiler.builtin_longjmp &&
              node->lhs->var == vm->compiler.builtin_longjmp) ||
             (vm->compiler.builtin__setjmp &&
              node->lhs->var == vm->compiler.builtin__setjmp) ||
             (vm->compiler.builtin__longjmp &&
              node->lhs->var == vm->compiler.builtin__longjmp))) {
            bool is_longjmp =
                (vm->compiler.builtin_longjmp &&
                 node->lhs->var == vm->compiler.builtin_longjmp) ||
                (vm->compiler.builtin__longjmp &&
                 node->lhs->var == vm->compiler.builtin__longjmp);
            fprintf(f, is_longjmp ? "_longjmp(" : "_setjmp(");
            Node *arg = node->args;
            if (arg) {
                Node *env = arg;
                while (env->kind == ND_CAST && env->lhs)
                    env = env->lhs;
                fprintf(f, "(void *)");
                serialize_expr(f, vm, ctx, env, node_prec);
                arg = arg->next;
            }
            for (; arg; arg = arg->next) {
                fprintf(f, ", ");
                serialize_expr(f, vm, ctx, arg, 2);
            }
            fprintf(f, ")");
            break;
        }
        serialize_expr(f, vm, ctx, node->lhs, node_prec);
        fprintf(f, "(");
        // #1074: a direct call to a nested function needs its hidden
        // __static_link argument supplied explicitly -- the parser already
        // gave the callee's own signature a leading `void *__static_link`
        // parameter (parse_decl.c), but nothing else ever passed it. Mirrors
        // codegen_expr.c's calling_nested value selection exactly: calling
        // one's own direct child passes that child's own env (declared as
        // `__cccc_nenv` at the top of the function currently being
        // serialized, ctx->current_fn -- see serialize_function); calling a
        // sibling or an ancestor's nested function (only reachable from
        // inside that ancestor's own nest, so ctx->current_fn must itself be
        // nested) chases ->__up via nested_env_ptr_expr(). serialize_nested_
        // preamble() has already rejected, at compile time, every reference
        // to a nested function that ISN'T a direct callee, so `node->lhs`
        // here is guaranteed to be exactly this shape whenever the check
        // below matches.
        if (node->lhs && node->lhs->kind == ND_VAR && node->lhs->var &&
            node->lhs->var->is_function && node->lhs->var->is_nested &&
            !node->lhs->var->is_block) {
            Obj *callee_parent = node->lhs->var->parent_fn;
            Obj *current_fn = ctx->current_fn;
            fprintf(f, "(void *)");
            if (callee_parent == current_fn)
                fprintf(f, "&__cccc_nenv");
            else if (current_fn && current_fn->is_nested && !current_fn->is_block)
                fprintf(f, "%s",
                        nested_env_ptr_expr(vm, ctx, current_fn, callee_parent));
            else
                // Unreachable in valid C (a nested function's name has block
                // scope, only visible inside its own parent's nest) -- mirror
                // codegen_expr.c's identical fallback rather than emit
                // nothing.
                fprintf(f, "&__cccc_nenv");
            if (node->args)
                fprintf(f, ", ");
        }
        for (Node *arg = node->args; arg; arg = arg->next) {
            // #1042(b): a comma-expression argument (e.g. from a macro like
            // ivalue(r) that expands to one) must stay parenthesized here --
            // the comma is the argument separator in this context, so an
            // unparenthesized ND_COMMA silently splits into extra arguments
            // ("too many arguments to function call"). parent_prec 2 is
            // above get_precedence(ND_COMMA)'s 1 but below every other node
            // kind's precedence, so only a bare top-level comma gets wrapped.
            serialize_expr(f, vm, ctx, arg, 2);
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
        else if (!node->member)
            // Genuinely unresolved -- distinct from the anonymous-member
            // case below, which is a normal, valid access.
            fprintf(f, "./* unknown */");
        // else: node->member is an anonymous struct/union member (its
        // C11 6.7.2.1p13 members are promoted into the enclosing
        // aggregate's namespace) -- an intermediate link in a struct_ref()
        // chain like `x.a` through `struct { struct { int a; }; } x`.
        // It has no spelling of its own and is transparent in C, so it
        // must serialize to nothing rather than the placeholder comment
        // that used to sit here -- that produced `s./* unknown */.i`,
        // which the host compiler rejects outright.
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
        // #1042(b): every comma below is a real argument separator to a
        // fixed-arity builtin, same comma-in-arg-position hazard as
        // ND_FUNCALL.
        fprintf(f, "%s(", name);
        serialize_expr(f, vm, ctx, node->lhs, 2);
        fprintf(f, ", ");
        serialize_expr(f, vm, ctx, node->rhs, 2);
        fprintf(f, ", ");
        serialize_expr(f, vm, ctx, node->cas_addr, 2);
        fprintf(f, ")");
        break;
    }

    case ND_DYNOBJ_SIZE:
        fprintf(f, "__builtin_dynamic_object_size(");
        // #1042(b): see ND_OVERFLOW_ARITH above.
        serialize_expr(f, vm, ctx, node->lhs, 2);
        fprintf(f, ", %lld)", (long long)node->val);
        break;

    case ND_ALOAD:
        // codegen only takes the atomic path for 1/2/4/8-byte non-float
        // pointees and falls back to a plain load otherwise; mirror that,
        // since __atomic_load_n does not accept a float or aggregate pointee
        // and the VM is not being atomic there either.
        if (atomic_serializable_pointee(node->lhs)) {
            fprintf(f, "__atomic_load_n(");
            // #1042(b): see ND_OVERFLOW_ARITH above.
            serialize_expr(f, vm, ctx, node->lhs, 2);
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
            // #1042(b): real argument separator to the builtin.
            serialize_expr(f, vm, ctx, node->lhs, 2);
            fprintf(f, ", __cccc_astore_v, __ATOMIC_SEQ_CST); __cccc_astore_v; })");
        } else {
            fprintf(f, "(*(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ") = ");
            // #1042(b): rhs sits after `=` inside one shared enclosing
            // paren, not its own -- an unparenthesized top-level ND_COMMA
            // here would bind looser than `=` and change which value the
            // whole parenthesized expression yields.
            serialize_expr(f, vm, ctx, node->rhs, 2);
            fprintf(f, ")");
        }
        break;

    case ND_EXCH:
        // codegen rejects float/odd-size pointees outright for exchange and
        // compare-and-swap, so these two map 1:1 with no fallback arm.
        fprintf(f, "__atomic_exchange_n(");
        // #1042(b): see ND_OVERFLOW_ARITH above.
        serialize_expr(f, vm, ctx, node->lhs, 2);
        fprintf(f, ", ");
        serialize_expr(f, vm, ctx, node->rhs, 2);
        fprintf(f, ", __ATOMIC_SEQ_CST)");
        break;

    case ND_CAS:
        // (obj, *expected, desired) -> bool, matching codegen's ACAS contract:
        // cas_old is a *pointer* to the expected value, as __atomic_compare_
        // exchange_n also takes. weak = 0.
        fprintf(f, "__atomic_compare_exchange_n(");
        // #1042(b): see ND_OVERFLOW_ARITH above.
        serialize_expr(f, vm, ctx, node->cas_addr, 2);
        fprintf(f, ", ");
        serialize_expr(f, vm, ctx, node->cas_old, 2);
        fprintf(f, ", ");
        serialize_expr(f, vm, ctx, node->cas_new, 2);
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
            // #1042(b): single-argument builtin -- a bare top-level comma
            // in the operand would still misparse as an extra argument.
            serialize_expr(f, vm, ctx, node->lhs, 2);
            fprintf(f, ")");
        }
        break;
    }

    case ND_CONVERTVECTOR:
        fprintf(f, "__builtin_convertvector(");
        // #1042(b): the builtin's own ", " separates the operand from the
        // target type, same comma-in-arg-position hazard as ND_FUNCALL.
        serialize_expr(f, vm, ctx, node->lhs, 2);
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
            // #1042(b): same comma-in-arg-position hazard as ND_FUNCALL's
            // argument loop above.
            serialize_expr(f, vm, ctx, arg, 2);
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
            // #1042(b): this `=` is printed manually, outside
            // serialize_expr's own ND_ASSIGN case (which already protects
            // its rhs via node_prec + 1) -- an unparenthesized top-level
            // ND_COMMA here would bind looser than `=` and initialize the
            // declaration with the comma's first operand instead of its
            // value.
            serialize_expr(f, vm, ctx, node->lhs->rhs, 2);
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
                    // #1042(b): this loop already comma-joins multiple
                    // declarator inits itself (the ", " above) -- an
                    // unparenthesized ND_COMMA inside one declarator's own
                    // initializer would be indistinguishable from another
                    // separator comma.
                    serialize_expr(f, vm, ctx, s->lhs, 2);
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
        {
            // #1005: push a jump frame so a break/continue in the body
            // resolves back to this loop -- see the ND_GOTO arm below.
            SerJumpFrame frame = {ctx->jumps, node->brk_label, node->cont_label};
            ctx->jumps = &frame;
            serialize_stmt(f, vm, ctx, node->then, indent + 1);
            ctx->jumps = frame.parent;
        }
        break;

    case ND_DO:
        print_indent_level(f, indent);
        fprintf(f, "do\n");
        {
            SerJumpFrame frame = {ctx->jumps, node->brk_label, node->cont_label};
            ctx->jumps = &frame;
            serialize_stmt(f, vm, ctx, node->then, indent + 1);
            ctx->jumps = frame.parent;
        }
        print_indent_level(f, indent);
        fprintf(f, "while (");
        serialize_expr(f, vm, ctx, node->cond, 0);
        fprintf(f, ");\n");
        break;

    case ND_SWITCH:
        // #1005: previously reconstructed the switch from the case_next
        // chain instead of serializing node->then -- that walked only the
        // first statement following each `case`/`default` (everything after
        // it, including the case's own `break`, was silently dropped),
        // emitted cases in reverse source order (case_next is prepended at
        // parse time) with `default:` always forced last (destroying
        // fallthrough), and dropped GNU case ranges entirely. Serializing
        // the real body -- mirroring ND_IF/ND_FOR's shape -- fixes all four
        // at once; ND_CASE below now emits its own label in place and lets
        // the enclosing ND_BLOCK's ->next walk handle subsequent statements.
        print_indent_level(f, indent);
        fprintf(f, "switch (");
        serialize_expr(f, vm, ctx, node->cond, 0);
        fprintf(f, ")\n");
        {
            // A switch saves/restores only brk_label at parse time
            // (parse.c) -- cont_label is deliberately NULL here so a
            // `continue` inside this switch skips over this frame and
            // resolves to the nearest enclosing loop, not this switch.
            SerJumpFrame frame = {ctx->jumps, node->brk_label, NULL};
            Node *saved_switch = ctx->cur_switch;
            ctx->jumps = &frame;
            ctx->cur_switch = node;
            serialize_stmt(f, vm, ctx, node->then, indent + 1);
            ctx->cur_switch = saved_switch;
            ctx->jumps = frame.parent;
        }
        break;

    case ND_GOTO:
        print_indent_level(f, indent);
        if (node->label) {
            // A real source-level `goto label;` (parse.c sets ->label to
            // the source identifier; resolve_goto_labels also fills in
            // ->unique_label, but ->label is authoritative here).
            fprintf(f, "goto %s;\n", node->label);
        } else {
            // #1005: break/continue lower to an ND_GOTO with only
            // ->unique_label set (parse.c) -- a ".L..N" string that is not
            // a valid C identifier and, unlike a source goto's target, has
            // no ND_LABEL anywhere to jump to. Resolve which construct it
            // targets by walking the jump-frame stack built above (pointer
            // identity, matching parse.c's own nn_find_target) and emit the
            // real C keyword instead of a (nonexistent) label reference.
            const char *kw = NULL;
            for (SerJumpFrame *fr = ctx->jumps; fr && !kw; fr = fr->parent) {
                if (fr->brk_label && fr->brk_label == node->unique_label)
                    kw = "break";
                else if (fr->cont_label && fr->cont_label == node->unique_label)
                    kw = "continue";
            }
            if (!kw)
                error_tok(vm, node->tok,
                          "internal error: break/continue target not found "
                          "while serializing");
            fprintf(f, "%s;\n", kw);
        }
        break;

    case ND_LABEL:
        fprintf(f, "%s:\n", node->label);
        serialize_stmt(f, vm, ctx, node->lhs, indent);
        break;

    case ND_CASE:
        print_indent_level(f, indent);
        if (ctx->cur_switch && ctx->cur_switch->default_case == node) {
            fprintf(f, "default:\n");
        } else if (node->begin == node->end) {
            fprintf(f, "case %ld:\n", node->begin);
        } else {
            // [GNU] Case ranges, e.g. "case 1 ... 5:"
            fprintf(f, "case %ld ... %ld:\n", node->begin, node->end);
        }
        serialize_stmt(f, vm, ctx, node->lhs, indent);
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
            // #1042(b): real argument separator to the builtin.
            serialize_expr(f, vm, ctx, node->lhs, 2);
            fprintf(f, ", ");
            serialize_expr(f, vm, ctx, node->rhs, 2);
            fprintf(f, ", __ATOMIC_SEQ_CST);\n");
        } else {
            fprintf(f, "*(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ") = ");
            // #1042(b): `= rhs;` here is not wrapped in any enclosing
            // parens (unlike the expression-position ND_ASTORE case) -- an
            // unparenthesized top-level ND_COMMA in rhs would bind looser
            // than `=`, assigning the comma's *first* operand instead of
            // its value (the last operand), a silent wrong-answer bug, not
            // merely a compile error.
            serialize_expr(f, vm, ctx, node->rhs, 2);
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
    // #1025/#1039: an asm("symbol")-labeled block-scope declaration (`Put
    // local_puts asm("puts");`) aliases an *external* symbol -- internal
    // linkage on the declaration is meaningless for it and, since the
    // symbol is never defined under the local name, actively wrong (the
    // native compiler emits an internal-linkage reference nothing ever
    // defines, and the link fails). Originally worked around here by
    // suppressing `static` whenever an asm label was present despite
    // fn->is_static being forced true regardless (#1025); parse_decl.c now
    // only forces is_static on a nested/block-scope function when no asm
    // label is present (#1039), so fn->is_static alone is accurate here.
    if (fn->is_static)
        fprintf(f, "static ");

    // #1026: a function returning a function pointer (`int (*f(void))(int,
    // int)`) can't be spelled as "<return-type> <name>(<params>)" -- the
    // return type's own declarator has to wrap around the whole
    // "name(params)" unit, the same way TY_ARRAY/TY_PTR recurse in
    // serialize_type_decl. Render "name(params)[ asm("label")]" into a
    // buffer first, then hand it to serialize_type_decl as the declarator
    // name so a pointer/function return type nests correctly.
    char *decl = NULL;
    size_t declsz = 0;
    FILE *df = open_memstream(&decl, &declsz);
    fprintf(df, "%s(", fn->name);

    bool first = true;
    if (fn->params) {
        for (Obj *param = fn->params; param; param = param->next) {
            if (!first)
                fprintf(df, ", ");
            first = false;
            serialize_type_decl(df, ctx, param->ty, param->name);
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
                fprintf(df, ", ");
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
            serialize_type_decl(df, ctx, param, buf);
        }
    }

    if (fn->ty && fn->ty->is_variadic && !first) {
        fprintf(df, ", ...");
    } else if (first) {
        fprintf(df, "void");
    }
    fprintf(df, ")");

    if (fn->asm_label)
        // __CCCC_ASM_PREFIX__ (see serialize_asm_prefix_preamble) supplies
        // the platform's real symbol prefix; adjacent string literals
        // concatenate at translation time, so this reads as e.g.
        // asm("_puts") on Darwin and asm("puts") on Linux from one
        // platform-independent emission.
        fprintf(df, " asm(__CCCC_ASM_PREFIX__ \"%s\")", fn->asm_label);

    fclose(df);
    serialize_type_decl(f, ctx,
                        (fn->ty && fn->ty->return_ty) ? fn->ty->return_ty : ty_int,
                        decl ? decl : "");
    free(decl);
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
            else if (var->name[0] == '.')
                // #1034: a local named via new_unique_name() (parse_core.c)
                // -- a macro/comptime-generated compound literal or block
                // temp given the same ".L..N" dotted scheme as an anonymous
                // *global* (rename_anon_globals(), further down this file)
                // -- is not a legal C identifier either, and unlike the
                // empty-name case above was never renamed here. Deliberately
                // a distinct "__cccc_local_" prefix, not rename_anon_globals()'s
                // own "__cccc_%s_%d" scheme (which draws from a *different*
                // counter, anon_global_counter) -- reusing that scheme here
                // would let a renamed global and a renamed local collide on
                // the identical spelling (e.g. both landing on
                // "__cccc_anon_0", one per counter) and silently shadow each
                // other in this function's scope. Same display_name-or-
                // "anon" tag rule rename_anon_globals() uses, but sharing
                // anon_local_counter with the __cccc_tmp%d case above (whose
                // own prefix keeps it out of this collision class too).
                var->name = arena_format(vm, "__cccc_local_%s_%d",
                                          (var->display_name && var->display_name[0] != '.')
                                              ? var->display_name : "anon",
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
            // #1029: serialize_function hoists every local to a flat
            // declaration here, with any initializer lowered to a separate
            // assignment statement in the body below (const-qualified or
            // not -- the split itself is unconditional). A `const`-typed
            // local (`const long long max_spins = 2000000;`) would
            // therefore emit as `const long long max_spins;` here and
            // `max_spins = 2000000;` in the body -- an assignment to a
            // const object, which real C rejects outright even though the
            // VM (which never actually re-derives or enforces this split)
            // has no problem with the original, un-hoisted source. Strip
            // only the *top-level* const on the hoisted declarator; a
            // pointer-level const on the pointee (`const char *p`) lives on
            // the base type, one step down `var->ty->base`, and is
            // untouched by this.
            if (var->ty->is_const) {
                Type *mutable_ty = copy_type(vm, var->ty);
                mutable_ty->is_const = false;
                serialize_type_decl(f, ctx, mutable_ty, var->name);
            } else {
                serialize_type_decl(f, ctx, var->ty, var->name);
            }
            fprintf(f, ";\n");
        }

        // #1074: if `fn` directly parents at least one nested function,
        // declare and initialize its env struct instance here -- after
        // every ordinary local above so `&x` for an upvar field is always
        // already-declared storage, and before the body so a call to a
        // direct nested child (which reads `&__cccc_nenv`, see ND_FUNCALL's
        // own #1074 comment) always finds it initialized first. `__up`
        // carries `fn`'s own static link along for a deeper nest level to
        // chase; a non-nested `fn` (there's nothing to chase further) still
        // needs the field to exist so `struct __cccc_nenv_X`'s layout is
        // fixed regardless of which level owns it, but its value there is
        // never read.
        for (int __ne_i = 0; __ne_i < ctx->nested_envs_len; __ne_i++) {
            NestedEnvEntry *__ne = &ctx->nested_envs[__ne_i];
            if (__ne->owner_fn != fn)
                continue;
            fprintf(f, "    %s __cccc_nenv;\n", __ne->env_struct_name);
            fprintf(f, "    __cccc_nenv.__up = %s;\n",
                    fn->is_nested ? "__static_link" : "(void *)0");
            for (int __uv_i = 0; __uv_i < __ne->upvars_len; __uv_i++)
                fprintf(f, "    __cccc_nenv.__uv%d = &%s;\n", __uv_i,
                        __ne->upvars[__uv_i]->name);
            break;
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
        if (!serialize_flonum_special(f, (long double)fv, "f")) {
            char buf[64];
            format_float_literal(buf, sizeof buf, (double)fv);
            fprintf(f, "%sf", buf);
        }
        return;
    }

    case TY_DOUBLE:
    case TY_LDOUBLE: {
        // TY_LDOUBLE shares TY_DOUBLE's 8-byte read and unsuffixed %g here,
        // matching this function's pre-#918 behavior exactly -- a latent
        // long-double-precision/suffix gap, but pre-existing and unrelated
        // to #918's scope. Still present after #1038 (which fixed the
        // ND_NUM/expression-literal counterpart of this same gap, not this
        // global-initializer path) -- left alone here for the same reason:
        // out of scope, tracked separately, not touched incidentally.
        double dv; memcpy(&dv, var->init_data + offset, 8);
        if (!serialize_flonum_special(f, (long double)dv, ""))
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

    // #1047: a global whose declaration lives entirely in a replayed
    // header is already supplied by that header's own #include text --
    // see global_is_header_supplied()'s comment.
    if (global_is_header_supplied(vm, ctx, var))
        return;

    // #1011: the #918/#928 forward-declaration pass (cc_serialize_program,
    // further down this file) already emitted a line for this global ahead
    // of every definition -- `static T name;` for a static with no
    // initializer, or `extern T name;` for a declaration with no
    // definition (is_definition false). When this global also has no
    // init_data, what follows below would print the exact same text a
    // second time, back to back (e.g. a function-local `static struct Foo
    // a;` hoisted to file scope by rename_anon_globals() as `__cccc_a_0`).
    // A global that *does* have an initializer still needs both lines (the
    // forward declaration, then the real `T name = ...;` definition), so
    // this only skips the no-initializer case.
    if (!var->init_data && (var->is_static || !var->is_definition))
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
        fprintf(f, "    %s = %lld", enum_const_spelling(ctx, ty, ec->name),
                (long long)ec->value);
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
                       chosen_name, chosen_len, NULL, false, true, NULL, true);

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

// #891: in !generated_only mode (-c=native, -m without -c=generated), a
// header-sourced typedef would collide with the consumer's own #include of
// the same header (auto-capture re-emits that #include verbatim) -- e.g.
// `typedef void FILE;` from CCCC's own stdio.h polyfill alongside a real
// `#include <stdio.h>`. Comptime/reflection-synthesized aliases
// (always_emit) are exempt: they have no header of their own to collide
// with, and dropping them would silently delete macro-generated typedefs
// from the output. Shared by serialize_typedef_alias and (#1046)
// emit_typedef_and_deps's own aggregate-body emission, so both places gate
// on the exact same rule -- a divergent copy here is how #892's AttrTarget
// regression happened.
static bool typedef_alias_header_suppressed(SerializeContext *ctx,
                                            TypeName *alias) {
    return !ctx->generated_only && !ctx->emit_strict && alias->from_include &&
           !alias->always_emit;
}

static void serialize_typedef_alias(FILE *f, SerializeContext *ctx,
                                    TypeName *alias) {
    if (!alias || aggregate_typedef_is_definition(ctx, alias))
        return;
    if (typedef_alias_header_suppressed(ctx, alias))
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

static void emit_typedef_and_deps(FILE *f, SerializeContext *ctx, int idx,
                                  Obj *owner_fn, bool *typedef_done);

// #1027: walks the same PTR/ARRAY/VLA/FUNC declarator shape collect_type()
// peels, but instead of collecting aggregate completeness dependencies
// (already correctly ordered by collect_type's own post-order into
// ctx->defs), looks for a scalar typedef alias serialize_type() would
// spell for whatever leaf type this one bottoms out at -- e.g. a struct
// member declared `lu_byte tt;`. Nothing tracked this dependency before:
// collect_type() only ever walks toward a TY_STRUCT/TY_UNION/TY_ENUM
// member's own nested aggregate members, never toward a scalar typedef's
// declaring record, so every struct/union/enum definition used to print
// ahead of every typedef alias unconditionally (two independent loops
// below, aggregates first) -- correct only when no aggregate's member
// happened to spell a not-yet-emitted typedef name, which a large,
// realistic third-party source (tests/test_minilua.c) hits within the
// first handful of struct definitions.
//
// A tagged/aliased struct/union/enum member needs no typedef dependency
// tracked here: a tagged aggregate's pointer/array member spells via
// "struct Tag"/"union Tag", valid even before that tag's own definition (C
// implicitly forward-declares a struct/union tag on first mention) or, by
// value, is already correctly ordered by collect_type()'s own post-order;
// an untagged, alias-only aggregate member is likewise already walked into
// ctx->defs ahead of its container by that same existing recursion (see
// aggregate_typedef_is_definition -- its own combined `typedef struct {...}
// Name;` line is printed by serialize_struct_def itself, from ctx->defs,
// not by the separate typedef loop at all).
static void ensure_typedef_for_type_emitted(FILE *f, SerializeContext *ctx,
                                            Type *ty, Obj *owner_fn,
                                            bool *typedef_done) {
    if (!ty)
        return;
    if (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_VLA) {
        ensure_typedef_for_type_emitted(f, ctx, ty->base, owner_fn, typedef_done);
        return;
    }
    if (ty->kind == TY_FUNC) {
        ensure_typedef_for_type_emitted(f, ctx, ty->return_ty, owner_fn, typedef_done);
        for (Type *p = ty->params; p; p = p->next)
            ensure_typedef_for_type_emitted(f, ctx, p, owner_fn, typedef_done);
        return;
    }
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        // #1046: `ty` may itself be an untagged aggregate whose only
        // spelling is a typedef alias (e.g. `typedef struct { int a[2]; }
        // P, *Pp;`, `ty` here being the anonymous struct behind `Pp`'s
        // pointee) -- find_typedef_name() below would already match that
        // alias, so the "chase into members" branch further down never
        // runs for it, but nothing had ever chased forward to make sure the
        // alias's own aggregate BODY (not just its name) gets emitted before
        // this one. Do that explicitly: find_anonymous_typedef_name (#952,
        // not the broader find_typedef_name, so an unrelated same-shape
        // tagless typedef like va_list can't be matched instead) resolves
        // the defining record, and emit_typedef_and_deps prints its
        // `typedef struct {...} P;` line (idempotent via ctx->emitted_defs)
        // ahead of whatever depends on it.
        if (!find_tag_name(ctx, ty)) {
            TypeName *anon = find_anonymous_typedef_name(ctx, ty);
            if (anon) {
                int idx = (int)(anon - ctx->typedefs);
                emit_typedef_and_deps(f, ctx, idx, owner_fn, typedef_done);
                return;
            }
        }
        // An anonymous, alias-less aggregate (e.g. a `union { struct {
        // ... } l; ... } u;` member) is inlined at its point of use
        // (serialize_anon_aggregate) rather than getting its own standalone
        // definition -- the caller's own top-level ctx->defs loop skips
        // exactly this case (no tag, no alias, nothing to refer back to it
        // by), so it never gets its own turn to pull its members' typedefs
        // forward. Chase the dependency through here instead, into
        // whichever member of THIS aggregate will actually need it.
        if (!find_tag_name(ctx, ty) && !find_typedef_name(ctx, ty) &&
            !find_anonymous_typedef_name(ctx, ty))
            for (Member *m = ty->members; m; m = m->next)
                ensure_typedef_for_type_emitted(f, ctx, m->ty, owner_fn, typedef_done);
        return;
    }
    if (ty->kind == TY_ENUM) {
        // #1046: same reasoning as the TY_STRUCT/TY_UNION case just above --
        // a tagless `typedef enum { ... } E;` reached only via some other
        // typedef's dependency chase (an enum has no member types of its
        // own to chase further, so this is the whole fix for TY_ENUM).
        if (!find_tag_name(ctx, ty)) {
            TypeName *anon = find_anonymous_typedef_name(ctx, ty);
            if (anon) {
                int idx = (int)(anon - ctx->typedefs);
                emit_typedef_and_deps(f, ctx, idx, owner_fn, typedef_done);
            }
        }
        return;
    }

    TypeName *alias = find_typedef_name_exact(ctx, ty);
    if (!alias)
        return;
    int idx = (int)(alias - ctx->typedefs);
    emit_typedef_and_deps(f, ctx, idx, owner_fn, typedef_done);
}

static void emit_typedef_and_deps(FILE *f, SerializeContext *ctx, int idx,
                                  Obj *owner_fn, bool *typedef_done) {
    if (typedef_done[idx])
        return;
    typedef_done[idx] = true;

    TypeName *td = &ctx->typedefs[idx];
    // A typedef belonging to a different owner_fn scope than the one this
    // pass is currently emitting isn't this call's job to print (mirrors
    // the existing owner_fn filters on both loops below) -- structurally
    // this shouldn't arise (a file-scope aggregate can't reference a
    // function-local typedef), but matched defensively rather than assumed.
    if (td->owner_fn != owner_fn)
        return;

    // #999-style self-hide (see serialize_typedef_alias's own matching
    // comment): temporarily blank this record so a typedef-of-typedef's own
    // right-hand-side lookup (below) can't match itself, then pull in
    // whatever OTHER typedef this one's own spelling depends on (a chain
    // like `typedef lu_byte TStatus;`) before this one.
    Type *real_ty = td->ty;
    td->ty = NULL;
    ensure_typedef_for_type_emitted(f, ctx, real_ty, owner_fn, typedef_done);
    td->ty = real_ty;

    // #1046: when `td` is itself the defining alias of an anonymous
    // struct/union/enum (aggregate_typedef_is_definition -- the combined
    // `typedef struct {...} P;` shape), serialize_typedef_alias() below
    // deliberately does NOT print it, on the assumption that
    // serialize_struct_def()/serialize_enum_def() already did while walking
    // ctx->defs (that's the ordinary case: `P` used by value somewhere).
    // But ctx->defs is usage-collected -- if `P` is never used by value,
    // only ever reached through another typedef's pointer/array/function
    // indirection (e.g. `Pp` here), nothing in that loop ever visits it, and
    // this call -- reached via ensure_typedef_for_type_emitted's dependency
    // chase -- is the only place that still knows the body needs printing.
    // Emit it here, gated by the same emitted_defs dedup the ctx->defs loop
    // uses (a real test file can produce two independent TypeName records
    // for the same declaration -- comptime re-parse -- so this must be
    // idempotent) and the same header-suppression rule
    // serialize_typedef_alias applies to the alias line itself.
    if (aggregate_typedef_is_definition(ctx, td) &&
        !type_vec_contains(&ctx->emitted_defs, real_ty) &&
        !typedef_alias_header_suppressed(ctx, td)) {
        type_vec_push(&ctx->emitted_defs, real_ty);
        if (real_ty->kind == TY_ENUM)
            serialize_enum_def(f, ctx, real_ty);
        else
            serialize_struct_def(f, ctx, real_ty);
    }

    serialize_typedef_alias(f, ctx, td);
}

static void serialize_type_defs_for_owner(FILE *f, SerializeContext *ctx,
                                          Obj *owner_fn) {
    Obj *saved_fn = ctx->current_fn;
    ctx->current_fn = owner_fn;

    bool *typedef_done = ctx->typedefs_len > 0
        ? calloc((size_t)ctx->typedefs_len, sizeof(bool))
        : NULL;

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
        // #1010: use find_tag_name_for_provenance() rather than plain `tag`
        // when a tag exists -- see that function's comment. A tagless
        // typedef alias has exactly one record and no such ambiguity, so
        // `alias` (from find_typedef_name() above) is used unchanged.
        TypeName *provenance_source = tag ? find_tag_name_for_provenance(ctx, ty) : alias;
        if (!ctx->emit_strict && provenance_source &&
            provenance_source->from_include && !provenance_source->always_emit &&
            (!ctx->generated_only ||
             path_is_captured(ctx, provenance_source->file_path)))
            // #1031: this is correct for member *access* -- the replayed
            // `#include` (auto-capture, preprocess.c) hands member
            // resolution to the host header's real layout, which is often
            // more accurate than CCCC's own minimal projection (e.g.
            // `struct statfs`, ~56 bytes here vs. ~2100 on real macOS).
            // But it does NOT retroactively fix any `sizeof`/`offsetof`
            // of `ty` that guest-side constant folding already baked into
            // a plain integer literal elsewhere in this TU -- those still
            // reflect CCCC's projection, not the host's real layout, and
            // the host is none the wiser once the type body itself is
            // suppressed here. test_sys_mount_statfs.c is the confirmed
            // case: a `malloc(sizeof(struct statfs) + tail)` sized against
            // the guest's ~56-byte struct hands the real, much larger host
            // `statfs()` an undersized buffer. General soundness class,
            // not statfs-specific; sibling to the FP_* constant-folding
            // note in native_accessor_shims below. Still open.
            continue;
        // #1027: pull forward any scalar typedef this type's own direct
        // members (or, for an enum, its C23 underlying type) will spell by
        // name, before printing the body that references it -- see
        // ensure_typedef_for_type_emitted's comment. generated_only mode
        // skips this (and the typedef loop below) for the same reason it
        // skips typedefs entirely: the consumer's own headers already
        // define them.
        if (!ctx->generated_only && typedef_done) {
            if (ty->kind == TY_ENUM)
                ensure_typedef_for_type_emitted(f, ctx, ty->enum_base_type,
                                                owner_fn, typedef_done);
            else
                for (Member *m = ty->members; m; m = m->next)
                    ensure_typedef_for_type_emitted(f, ctx, m->ty, owner_fn,
                                                    typedef_done);
        }
        // #1046: the ensure_typedef_for_type_emitted() calls just above can,
        // via emit_typedef_and_deps(), already have printed THIS type's own
        // body -- e.g. `ty` is a tagless aggregate also reachable as some
        // other member's typedef dependency, chased and printed ahead of
        // this iteration reaching it directly. type_vec_push returns false
        // (a no-op) when already present, so this is the same emitted_defs
        // dedup emit_typedef_and_deps itself uses, just applied here too.
        if (type_vec_contains(&ctx->emitted_defs, ty))
            continue;
        type_vec_push(&ctx->emitted_defs, ty);
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
                emit_typedef_and_deps(f, ctx, i, owner_fn, typedef_done);
        }
    }

    free(typedef_done);
    ctx->current_fn = saved_fn;
}

// Public API: Serialize entire program to C source
// #904: CCCC's own polyfill headers (stdio.h/errno.h/getopt.h in src/std.c)
// define stdout/stderr/stdin/errno/optarg/optind/opterr/optopt as macros
// that expand to a call into an internal accessor shim (__cccc_stdout(),
// etc -- see src/stdlib/stdio.c and src/stdlib/posix_io.c) so they reflect
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
    // #1021: include/math.h's isnan/isinf/signbit/fpclassify are themselves
    // #defined as `_Generic((x), float: __cccc_isnan_f, default:
    // __cccc_isnan_d)(x)` etc -- a shim body that read the plain macro name
    // would, once math.h's replayed #include brings that definition into
    // scope, expand right back into a call to itself (infinite recursion),
    // the same trap FLT_ROUNDS sits in below. __builtin_{isnan,isinf,
    // signbit} are portable clang/gcc intrinsics with no such indirection.
    // __builtin_fpclassify takes the FP_* class codes as arguments and
    // returns whichever one matches. Every call site comparing against
    // FP_INFINITE/FP_NAN/FP_NORMAL/FP_SUBNORMAL/FP_ZERO was already
    // constant-folded to CCCC's OWN numeric values (include/math.h:23-27)
    // at guest compile time, baked into the emitted TU as plain integer
    // literals -- so the shim must return CCCC's numbering regardless of
    // which <math.h> the shim's own text ends up seeing (confirmed the two
    // can genuinely differ: real macOS FP_ZERO is 3, not CCCC's 5).
    // Spelled as literals here, not the FP_* macro names, so this stays
    // correct even on a platform where a real host <math.h>'s FP_* values
    // don't match CCCC's own.
    {"__cccc_isnan_f",      "static int __cccc_isnan_f(float x) { return __builtin_isnan(x); }\n"},
    {"__cccc_isnan_d",      "static int __cccc_isnan_d(double x) { return __builtin_isnan(x); }\n"},
    {"__cccc_isinf_f",      "static int __cccc_isinf_f(float x) { return __builtin_isinf(x); }\n"},
    {"__cccc_isinf_d",      "static int __cccc_isinf_d(double x) { return __builtin_isinf(x); }\n"},
    {"__cccc_signbit_f",    "static int __cccc_signbit_f(float x) { return __builtin_signbit(x); }\n"},
    {"__cccc_signbit_d",    "static int __cccc_signbit_d(double x) { return __builtin_signbit(x); }\n"},
    {"__cccc_fpclassify_f", "static int __cccc_fpclassify_f(float x) { return __builtin_fpclassify(2, 1, 3, 4, 5, x); }\n"},
    {"__cccc_fpclassify_d", "static int __cccc_fpclassify_d(double x) { return __builtin_fpclassify(2, 1, 3, 4, 5, x); }\n"},
    // #1021: include/float.h:73 defines `FLT_ROUNDS` itself as a call to
    // this shim (`#define FLT_ROUNDS (__cccc_flt_rounds())`) -- so a body
    // reading FLT_ROUNDS would textually expand right back into a call to
    // itself (infinite recursion) once float.h's replayed #include is in
    // scope. Signature matches float.h's own
    // `extern int __cccc_flt_rounds(void);` (:72), not src/stdlib/fenv.c's
    // VM-side long long version.
    //
    // #1071: this used to call __builtin_flt_rounds(), which clang
    // implements but GCC 13 does not ("implicit declaration of function
    // '__builtin_flt_rounds'", an undefined-symbol link error) -- not "the
    // portable clang/gcc intrinsic" it was previously documented as.
    // Replaced with the exact fegetround()-based mapping
    // src/stdlib/fenv.c's own __cccc_flt_rounds() already uses on the VM
    // side, so both paths agree by construction. The #include <fenv.h>
    // here follows the __cccc_iseqsig_{f,d} precedent just above (legal
    // mid-file, harmless if repeated thanks to the header's own include
    // guard) -- confirmed it resolves to the real host <fenv.h> under real
    // GCC too (angle-bracket #include from this synthetic shim body, found
    // at -I position 0, so include/fenv.h's own #include_next hand-off
    // resumes the search at position 1 and reaches /usr/include/fenv.h;
    // this is a different shape from #1070's still-open gap, which is a
    // *quoted* #include issued from another CCCC-owned header). The
    // switch is over the *host's* FE_* (host compiler, host header); the
    // returned 0/1/2/3/-1 are CCCC's own fixed encoding, spelled as bare
    // literals rather than any host macro name, since guest comparisons
    // against FLT_ROUNDS were already folded against that encoding at
    // guest compile time -- same asymmetry the __cccc_fpclassify_* shims
    // above already document for FP_*.
    {"__cccc_flt_rounds",   "#include <fenv.h>\n"
                             "static int __cccc_flt_rounds(void) {\n"
                             "    switch (fegetround()) {\n"
                             "    case FE_TOWARDZERO: return 0;\n"
                             "    case FE_TONEAREST:  return 1;\n"
                             "    case FE_UPWARD:     return 2;\n"
                             "    case FE_DOWNWARD:   return 3;\n"
                             "    default:            return -1;\n"
                             "    }\n"
                             "}\n"},
    // #1052: issignaling(x)/iseqsig(x,y) (include/math.h:530-541) are
    // CCCC-internal _Generic-dispatched macros with no real libc/libm
    // symbol behind them -- same shape as isnan/isinf/etc above, needing a
    // synthesized definition here too. The bit-pattern logic mirrors
    // cccc_issignaling_{f,d}/cccc_iseqsig_{f,d} (src/stdlib/math.c) exactly:
    // a signaling NaN is identified by its raw bit pattern, not via
    // isnan()/arithmetic, either of which would quiet it before it could be
    // observed. iseqsig's own shim inlines that same bit-pattern check
    // rather than calling __cccc_issignaling_{f,d} -- a program can use
    // iseqsig() without ever calling issignaling() directly, in which case
    // this loop (keyed off is_used) would never emit that other shim's own
    // definition, leaving an undefined reference to a name math.h only
    // declares, not defines. feraiseexcept()/FE_INVALID need <fenv.h>,
    // which -- unlike stdin/stdout/errno/optarg's already-guaranteed
    // headers above -- iseqsig()'s own call site has no guarantee already
    // reached; #include it directly in the shim text (legal mid-file,
    // harmless if repeated thanks to the header's own include guard).
    {"__cccc_issignaling_f", "static int __cccc_issignaling_f(float x) {\n"
                              "    union { float f; unsigned int u; } __v; __v.f = x;\n"
                              "    unsigned int u = __v.u;\n"
                              "    return ((u & 0x7F800000U) == 0x7F800000U) && (u & 0x003FFFFFU) != 0 && !(u & 0x00400000U);\n"
                              "}\n"},
    {"__cccc_issignaling_d", "static int __cccc_issignaling_d(double x) {\n"
                              "    union { double d; unsigned long long u; } __v; __v.d = x;\n"
                              "    unsigned long long u = __v.u;\n"
                              "    return ((u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (u & 0x0007FFFFFFFFFFFFULL) != 0 && !(u & 0x0008000000000000ULL);\n"
                              "}\n"},
    {"__cccc_iseqsig_f", "#include <fenv.h>\n"
                          "static int __cccc_iseqsig_f(float x, float y) {\n"
                          "    union { float f; unsigned int u; } __vx, __vy; __vx.f = x; __vy.f = y;\n"
                          "    unsigned int ux = __vx.u, uy = __vy.u;\n"
                          "    int sx = ((ux & 0x7F800000U) == 0x7F800000U) && (ux & 0x003FFFFFU) != 0 && !(ux & 0x00400000U);\n"
                          "    int sy = ((uy & 0x7F800000U) == 0x7F800000U) && (uy & 0x003FFFFFU) != 0 && !(uy & 0x00400000U);\n"
                          "    if (sx || sy) feraiseexcept(FE_INVALID);\n"
                          "    return x == y;\n"
                          "}\n"},
    {"__cccc_iseqsig_d", "#include <fenv.h>\n"
                          "static int __cccc_iseqsig_d(double x, double y) {\n"
                          "    union { double d; unsigned long long u; } __vx, __vy; __vx.d = x; __vy.d = y;\n"
                          "    unsigned long long ux = __vx.u, uy = __vy.u;\n"
                          "    int sx = ((ux & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (ux & 0x0007FFFFFFFFFFFFULL) != 0 && !(ux & 0x0008000000000000ULL);\n"
                          "    int sy = ((uy & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (uy & 0x0007FFFFFFFFFFFFULL) != 0 && !(uy & 0x0008000000000000ULL);\n"
                          "    if (sx || sy) feraiseexcept(FE_INVALID);\n"
                          "    return x == y;\n"
                          "}\n"},
    // #1069: include/stdlib.h defines MB_CUR_MAX itself as a call to this
    // shim (`#define MB_CUR_MAX (__cccc_mb_cur_max())`) -- same
    // infinite-recursion trap as FLT_ROUNDS/isnan/etc above, since a
    // replayed `#include <stdlib.h>` is what brings that macro into scope
    // in the first place. Unlike those, this shim does NOT resolve the
    // trap by re-#include-ing <stdlib.h> a second time: a first attempt at
    // exactly that (giving stdlib.h its own #include_next hand-off, same
    // shape as stdio.h/errno.h/fenv.h/math.h) chased the real host's own
    // <stdlib.h> chain deep enough to hit an unrelated, pre-existing class
    // of the SAME -I./include shadowing hazard #1054 first documented for
    // setjmp.h -- e.g. real macOS's own <_stdlib.h> pulls in <sys/time.h>,
    // and CCCC's own bundled (non-hand-off) copy of THAT header
    // unconditionally #includes CCCC's own top-level time.h, defining a
    // `clock_t` that later collides with the real one once sys/types.h's
    // own chain reaches it too ("typedef redefinition"). That hand-off
    // has no clean stopping point (fixing it would mean auditing every
    // header transitively reachable from <stdlib.h> on every supported
    // host), so instead this shim spells the host's own internal
    // accessor directly, verified against the real headers on both hosts:
    // glibc declares `extern size_t __ctype_get_mb_cur_max(void);`
    // (/usr/include/stdlib.h, MB_CUR_MAX's own macro expansion); macOS
    // declares `extern int __mb_cur_max;`, a plain global
    // (/usr/include/_stdlib.h). src/stdlib/stdlib.c's wrap_mb_cur_max
    // (the VM-side shim) instead just reads the VM's own host libc's
    // MB_CUR_MAX macro directly -- no -I./include shadowing exists there,
    // since it's compiled by the real host cc as part of CCCC itself, not
    // reached through this serializer's own replay machinery.
    {"__cccc_mb_cur_max",
#if defined(__linux__)
     "extern size_t __ctype_get_mb_cur_max(void);\n"
     "static size_t __cccc_mb_cur_max(void) { return __ctype_get_mb_cur_max(); }\n"
#else
     "extern int __mb_cur_max;\n"
     "static size_t __cccc_mb_cur_max(void) { return (size_t)__mb_cur_max; }\n"
#endif
    },
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

// #1025: an asm("symbol") label names the real linker symbol directly,
// bypassing the compiler's own C-name-mangling -- which on Darwin adds a
// leading '_' to every external symbol and on Linux/ELF does not. Writing
// the label as a plain string literal (`asm("puts")`) only links on a
// platform with no such prefix; on Darwin the linker looks for the raw
// "puts" symbol, which doesn't exist (the real one is "_puts"), and fails.
// __USER_LABEL_PREFIX__ is a *token* (an actual `_` character, or nothing)
// clang/gcc predefine for exactly this, string-pasted via the standard
// double-macro stringize idiom so it works with the token being empty (an
// empty macro argument stringizes to "", not an error). Emitted once, only
// when some function actually carries an asm label, and ahead of every
// asm-labeled declaration since it's used there as `asm(__CCCC_ASM_PREFIX__ "name")`.
//
// Deliberately does NOT check obj->is_used: serialize_function_signature
// prints the asm(...) clause purely off fn->asm_label, with no is_used
// gate of its own, and the function-prototype pass (cc_serialize_program,
// further down this file) can emit a bodiless declaration's prototype
// (e.g. `int f(void) asm("name");` with no definition anywhere in this TU)
// regardless of whether anything in the program actually calls it. Gating
// this preamble on is_used while that emission site isn't would leave
// __CCCC_ASM_PREFIX__ referenced-but-undefined for exactly that case
// ("expected string literal in 'asm'", confirmed) -- three unconditional
// #defines cost nothing, so match unconditionally instead.
static bool prog_uses_asm_label(Obj *prog) {
    for (Obj *obj = prog; obj; obj = obj->next)
        if (obj->is_function && obj->asm_label)
            return true;
    return false;
}

static void serialize_asm_prefix_preamble(FILE *f, Obj *prog) {
    if (!prog_uses_asm_label(prog))
        return;
    fprintf(f, "#define __cccc_asm_str2(x) #x\n"
               "#define __cccc_asm_str1(x) __cccc_asm_str2(x)\n"
               "#define __CCCC_ASM_PREFIX__ __cccc_asm_str1(__USER_LABEL_PREFIX__)\n\n");
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

// #1032: two File records can spell the identical on-disk header two
// different ways -- one command-line input given as an absolute path and
// another as a relative one (the ordinary shape when a build/test harness
// mixes both, e.g. tools/testing/native.py's own compile_cmd) causes each
// TU's own #include resolution (dirname(including file) + the quoted
// spelling) to record a differently-spelled-but-identical path for the same
// shared header. A raw strcmp of File.name (the #1006 "no canonicalization,
// exact command-line spelling" design, cc_file_is_command_line_input's own
// comment) then wrongly treats the two as different files. Used only by
// rename_colliding_static_names() below, where getting this wrong renames a
// header-defined static function's *call sites* (every use resolves through
// the Obj, so the rename is "free") while the function's own definition is
// never re-emitted at all -- it reaches the output solely via the replayed
// #include, still under its original name -- producing a call to an
// undeclared symbol. realpath() failing (a synthetic/embedded path with no
// real file, e.g. the src/std.c embedded-header table's own paths) falls
// back to the exact-string comparison this replaces, matching prior
// behavior for anything that was never a real difference anyway.
static bool files_are_same(const char *a, const char *b) {
    if (!a || !b)
        return false;
    if (strcmp(a, b) == 0)
        return true;
    char ra[PATH_MAX], rb[PATH_MAX];
    if (!realpath(a, ra) || !realpath(b, rb))
        return false;
    return strcmp(ra, rb) == 0;
}

// #1002: cc_link_progs (linker.c) deliberately never canonicalizes `static`
// (internal-linkage) Objs across translation units -- two different .c
// inputs each defining `static int helper(void)` contribute two distinct
// Objs, both named "helper", into the one flat merged `prog` list this file
// serializes. The VM doesn't care (each Obj has its own body/bytecode), but
// -c=native/-m print by name, so the host compiler sees two definitions of
// the same identifier ("redefinition of 'helper'"). Renames every
// static Obj's name but the first for any name shared by Objs declared in
// more than one distinct file, so a name with no collision -- the common
// case -- is left exactly as the user wrote it. Must run after
// rename_anon_globals() (whose own renames can't collide with a
// user-written name -- see that function) and before any pass that reads
// obj->name, since every emit site (serialize_function_signature, ND_VAR,
// serialize_global_var, ...) resolves the name through the Obj pointer, so
// a rename here is automatically consistent everywhere except the one
// string-keyed lookup, serialize_find_global()'s first-match strcmp scan
// over vm->compiler.globals -- that scan runs after this pass too, so it
// resolves a relocation's label against the (possibly already renamed)
// Obj it actually points at, not a stale name.
static void rename_colliding_static_names(VirtualMachine *vm, Obj *prog,
                                          SerializeContext *ctx) {
    HashMap first_seen = {0}; // name -> first Obj* claiming it
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_static || obj->is_macro_generated || obj->name[0] == '.')
            continue;
        // Only an Obj that actually reaches the output as a definition can
        // collide with another TU's same-named one -- a bodyless static
        // function prototype or a tentative (non-defining) declaration
        // prints nothing a host compiler would reject twice.
        bool is_defining = obj->is_function ? obj->body != NULL : obj->is_definition;
        if (!is_defining)
            continue;
        Obj *first = hashmap_get(&first_seen, obj->name);
        if (!first) {
            hashmap_put_borrowed(&first_seen, obj->name, obj);
            continue;
        }
        // Only a genuine cross-TU collision -- two Objs of the same name
        // declared in different files -- needs renaming; same-file
        // same-name would already be a parse-time redefinition error long
        // before serialization is reached, but check explicitly rather
        // than assume.
        const char *first_file = first->tok && first->tok->file ? first->tok->file->name : NULL;
        const char *this_file = obj->tok && obj->tok->file ? obj->tok->file->name : NULL;
        if (files_are_same(first_file, this_file))
            continue;
        obj->name = arena_format(vm, "%s__cccc_dup%d", obj->name,
                                 ctx->anon_global_counter++);
    }
    hashmap_deinit_borrowed(&first_seen);
}

// #1014: does `ty` (or anything reachable through a PTR/ARRAY/VLA/FUNC
// chain -- deliberately not through struct/union members, mirroring
// hoist_local_type_to_file_scope()'s own reasoning: a member's type is
// reached through its own uses/definitions elsewhere, and this scan doesn't
// need a cycle guard as a result) match `group_ty` under same_type_strong()?
// Used to decide whether an externally-visible Obj's signature "votes for"
// a tag-collision group (rename_colliding_type_tags()'s tier-2 keeper
// signal).
static bool type_reaches_group(Type *ty, Type *group_ty) {
    if (!ty)
        return false;
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION || ty->kind == TY_ENUM)
        return same_type_strong(ty, group_ty);
    if (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_VLA)
        return type_reaches_group(ty->base, group_ty);
    if (ty->kind == TY_FUNC) {
        if (type_reaches_group(ty->return_ty, group_ty))
            return true;
        for (Type *p = ty->params; p; p = p->next)
            if (type_reaches_group(p, group_ty))
                return true;
        return false;
    }
    return false;
}

// #1014: two translation units can each independently *complete* a
// same-named but differently-shaped struct/union/enum tag -- the
// opaque-handle idiom, where a shared header only forward-declares the tag
// and each .c privately completes it (e.g. one .c per backend
// implementation). same_type_or_origin() correctly treats the two
// completions as different types (tag matches, structural comparison
// fails), so they are never wrongly deduped by collect_type() -- but
// nothing renames them apart either, and both reach serialize_struct_def()/
// serialize_enum_def() under the identical plain tag name, producing a hard
// "redefinition of 'DyGC'" from the host compiler. This is the tag-level
// analogue of rename_colliding_static_names() just above -- that pass only
// ever renames Obj (function/variable) names, never a struct/union/enum
// tag.
//
// Unlike an Obj name collision, at most one of the colliding groups can
// keep the plain spelling: a replayed `#include` of the shared header binds
// its own uses of the tag *textually*, so whichever group is "header-
// exposed" (a from_include TypeName record resolves to it) MUST keep the
// plain name or the replayed header's own prototypes stop matching
// (verified: renaming the header-exposed group produces a host "conflicting
// types" error where the un-renamed one compiles clean). If more than one
// group is header-exposed -- a replayed header genuinely declares entities
// of *both* shapes -- the collision is unrepresentable in flat C by any
// renaming; this pass still renames deterministically (first-created wins)
// rather than leaving the collision maximally ambiguous, and the host
// compiler is left to report whatever residual conflict remains (see
// man/COVERAGE.md's serialized-output-divergences section).
//
// Renames every non-keeper group's records -- both in ctx->tags (spelling)
// and in ctx->typedefs (a `typedef struct DyGC DyGC;` written in the .c
// itself, not the header, would otherwise turn a struct redefinition into a
// typedef-redefinition-with-different-types error once the struct itself is
// renamed) -- to `<name>__cccc_dup<N>`, sharing rename_colliding_static_
// names()'s suffix and ctx->anon_global_counter. A from_include typedef
// record is left untouched: serialize_typedef_alias() already suppresses
// those (#891), and the header text supplying the plain spelling can't be
// rewritten anyway.
static void rename_colliding_type_tags(VirtualMachine *vm, Obj *prog,
                                       SerializeContext *ctx) {
    // One entry per distinct colliding group discovered so far.
    typedef struct {
        Type *rep;          // representative (first-seen) Type* for this group
        int name_len;
        char *name;
        int first_seen;     // lower = created earlier (creation-order index)
        bool header_exposed; // a from_include tag/typedef record names this group
        bool extern_ref;     // an externally-visible definition's type reaches this group
    } TagGroup;
    TagGroup *groups = NULL;
    int groups_len = 0, groups_cap = 0;
    // Names seen more than once by more than one distinct group -- only
    // these need renaming at all.
    bool *collided = NULL;

    // ctx->tags is in reverse record-creation order (record_type_name()
    // prepends; collect_scope_names() walks head-first) -- walk it back to
    // front so `first_seen` below is a true creation-order index, matching
    // the doc comment above and giving a deterministic first-created
    // tie-break.
    for (int i = ctx->tags_len - 1; i >= 0; i--) {
        TypeName *rec = &ctx->tags[i];
        if (rec->owner_fn != NULL) // function-local: hoist_local_type_to_file_scope()'s territory
            continue;
        if (!type_is_complete_tagged(rec->ty)) // an incomplete record must never define/claim a group
            continue;

        // A record only ever joins an *existing* group when it names the
        // same tag AND matches its shape; a same-named-but-differently-
        // shaped record instead falls through and becomes its own new
        // group below -- the dedicated pass right after this loop is what
        // actually detects and marks the resulting name collision.
        int found = -1;
        for (int g = 0; g < groups_len; g++) {
            if (groups[g].name_len == rec->name_len &&
                strncmp(groups[g].name, rec->name, rec->name_len) == 0 &&
                same_type_strong(groups[g].rep, rec->ty)) {
                found = g;
                break;
            }
        }
        if (found >= 0)
            continue;

        if (groups_len == groups_cap) {
            groups_cap = groups_cap ? groups_cap * 2 : 8;
            groups = realloc(groups, sizeof(TagGroup) * groups_cap);
            collided = realloc(collided, sizeof(bool) * groups_cap);
        }
        groups[groups_len].rep = rec->ty;
        groups[groups_len].name_len = rec->name_len;
        groups[groups_len].name = rec->name;
        groups[groups_len].first_seen = groups_len; // creation order, since we walk creation-ordered
        groups[groups_len].header_exposed = false;
        groups[groups_len].extern_ref = false;
        collided[groups_len] = false;
        groups_len++;
    }

    if (groups_len == 0)
        return;

    // Mark actual name collisions: any two distinct groups sharing a name.
    for (int g1 = 0; g1 < groups_len; g1++)
        for (int g2 = g1 + 1; g2 < groups_len; g2++)
            if (groups[g1].name_len == groups[g2].name_len &&
                strncmp(groups[g1].name, groups[g2].name, groups[g1].name_len) == 0) {
                collided[g1] = true;
                collided[g2] = true;
            }

    bool any_collision = false;
    for (int g = 0; g < groups_len; g++)
        if (collided[g])
            any_collision = true;
    if (!any_collision) {
        free(groups);
        free(collided);
        return;
    }

    // Tier 1: header-exposed -- a from_include tag or typedef record names
    // this group (from_include is command-line-input-keyed since #1006, so
    // this is exactly "a replayed #include names this tag").
    for (int i = 0; i < ctx->tags_len; i++) {
        if (!ctx->tags[i].from_include || ctx->tags[i].always_emit)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (collided[g] && same_type_strong(ctx->tags[i].ty, groups[g].rep))
                groups[g].header_exposed = true;
    }
    for (int i = 0; i < ctx->typedefs_len; i++) {
        if (!ctx->typedefs[i].from_include || ctx->typedefs[i].always_emit)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (collided[g] && same_type_strong(ctx->typedefs[i].ty, groups[g].rep))
                groups[g].header_exposed = true;
    }

    // Tier 2: an externally-visible definition's type reaches this group --
    // needed because tier 1 alone can't pick the implementation TU's group
    // when the private TU happens to be listed (and hence created) first.
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_static)
            continue;
        bool is_defining = obj->is_function ? obj->body != NULL : obj->is_definition;
        if (!is_defining || !obj->ty)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (collided[g] && type_reaches_group(obj->ty, groups[g].rep))
                groups[g].extern_ref = true;
    }

    // Keeper choice per colliding name, composed as successive filters:
    // restrict to header-exposed groups if any exist among that name's
    // colliding groups, then prefer extern_ref, then lowest first_seen.
    for (int g = 0; g < groups_len; g++) {
        if (!collided[g] || groups[g].rep == NULL)
            continue; // already resolved as part of an earlier group's pass, or not a collider

        // Gather every group sharing this exact name.
        int members[64];
        int members_len = 0;
        for (int g2 = g; g2 < groups_len && members_len < 64; g2++)
            if (collided[g2] && groups[g2].rep &&
                groups[g2].name_len == groups[g].name_len &&
                strncmp(groups[g2].name, groups[g].name, groups[g].name_len) == 0)
                members[members_len++] = g2;
        if (members_len < 2)
            continue;

        bool any_header_exposed = false;
        for (int m = 0; m < members_len; m++)
            if (groups[members[m]].header_exposed)
                any_header_exposed = true;

        int keeper = -1;
        for (int m = 0; m < members_len; m++) {
            int idx = members[m];
            if (any_header_exposed && !groups[idx].header_exposed)
                continue;
            if (keeper < 0)
                keeper = idx;
            else if (groups[idx].extern_ref && !groups[keeper].extern_ref)
                keeper = idx;
            else if (groups[idx].extern_ref == groups[keeper].extern_ref &&
                     groups[idx].first_seen < groups[keeper].first_seen)
                keeper = idx;
        }
        if (keeper < 0)
            keeper = members[0];

        for (int m = 0; m < members_len; m++) {
            int idx = members[m];
            if (idx == keeper) {
                groups[idx].rep = NULL; // mark resolved, skip on future outer iterations
                continue;
            }
            char *new_name = arena_format(vm, "%.*s__cccc_dup%d",
                                          groups[idx].name_len, groups[idx].name,
                                          ctx->anon_global_counter++);
            int new_len = (int)strlen(new_name);
            Type *victim = groups[idx].rep;
            for (int i = 0; i < ctx->tags_len; i++)
                if (same_type_strong(ctx->tags[i].ty, victim)) {
                    ctx->tags[i].name = new_name;
                    ctx->tags[i].name_len = new_len;
                }
            for (int i = 0; i < ctx->typedefs_len; i++)
                if (!(ctx->typedefs[i].from_include && !ctx->typedefs[i].always_emit) &&
                    same_type_strong(ctx->typedefs[i].ty, victim))
                    ctx->typedefs[i].name = new_name, ctx->typedefs[i].name_len = new_len;
            groups[idx].rep = NULL; // mark resolved
            ctx->tag_renamed = true;
        }
    }

    free(groups);
    free(collided);
}

// #1015: two translation units can each independently declare a same-named
// enumerator inside a differently-shaped enum -- reachable even when the
// enclosing enum's own tag doesn't collide (different tags, same
// enumerator) or has no tag at all (a tagless `typedef enum { ... } T;`,
// which never forms a group in rename_colliding_type_tags() above, since
// that pass only ever walks ctx->tags). Renaming the tag apart (#1014)
// does nothing for this: same_type_or_origin()'s TY_ENUM arm compares
// enumerators by strcmp on EnumConstant.name, entirely independent of
// whichever (possibly renamed) tag spelling ctx->tags now carries -- it's
// ec->name that collides in the emitted C, not the enum's own name.
//
// Groups every distinct complete enum Type (same_type_strong-deduped,
// found via either a tag or a typedef record so a tagless typedef'd enum
// is covered too), then for every enumerator name shared by two or more
// distinct groups, renames every group's copy but one -- via the print-
// time ctx->enum_renames table (consulted by enum_const_spelling()), never
// by mutating EnumConstant.name itself; see that field's doc comment on
// SerializeContext for why a mutation would silently reintroduce the
// collision (same_type_or_origin's own enumerator comparison would then
// disagree with the pre-rename groups this pass computed).
//
// Keeper selection deliberately mirrors rename_colliding_type_tags()'s own
// tiers, in the same order, so the two passes always agree on which group
// keeps the plain spelling -- disagreeing would print `enum E__cccc_dup0 {
// AA }` next to a *different* group's `enum E { AA__cccc_dup1 }`: legal C,
// but visibly incoherent output. Tier 1 here is a hard rule, not a
// preference: a header-exposed group's enumerators are never renamed, full
// stop -- the replayed #include binds AA textually inside the header's own
// code, and renaming it there breaks the header (the same failure #1014
// verified by hand-compiling a renamed header-exposed tag).
static void rename_colliding_enum_constants(VirtualMachine *vm, Obj *prog,
                                            SerializeContext *ctx) {
    typedef struct {
        Type *rep;
        int first_seen;
        bool header_exposed;
        bool extern_ref;
        // #1017: the from_include record's file_path, captured alongside
        // header_exposed below -- names the header in the residual warning
        // when this group collides with an un-renameable Obj. May stay NULL
        // (TypeName.file_path itself can be NULL), in which case the
        // warning falls back to not naming a header.
        const char *header_path;
    } EnumGroup;
    EnumGroup *groups = NULL;
    int groups_len = 0, groups_cap = 0;

    // #1016: neither this pass nor rename_colliding_type_tags()/
    // rename_colliding_static_names() looks at the other's namespace, but C
    // has one ordinary identifier namespace at file scope -- an enumerator
    // can collide with a plain static/extern/function name just as easily
    // as with another enumerator. Build the set of every emitted file-scope
    // Obj name once, up front, so the per-name loop below can also treat an
    // Obj as a (single, un-renameable) "group" occupying a name. Must run
    // after rename_anon_globals()/rename_colliding_static_names() -- reading
    // obj->name here needs their final, possibly-already-renamed spelling,
    // the same ordering requirement #1002's own comment documents for a
    // different reason. Deliberately no is_defining filter (unlike #1002's
    // own Obj scan just above): #1002 only cares about two *definitions*
    // colliding, but here a bare prototype (`int AA(void);`) or `extern`
    // declaration already occupies the ordinary-identifier namespace an
    // enum constant shares, so it must be in this set too.
    HashMap obj_names = {0};
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->name[0] == '.')
            continue;
        if (ctx->generated_only && !obj->is_macro_generated)
            continue;
        hashmap_put_borrowed(&obj_names, obj->name, obj);
    }

    // Collect one entry per distinct complete enum Type, from ctx->tags and
    // ctx->typedefs alike, each walked back-to-front for an approximate
    // creation order (exact only within each list -- the two don't share a
    // common index -- used only as a last-resort tie-break, the same rigor
    // rename_colliding_type_tags() itself relies on for its own first_seen).
    for (int pass = 0; pass < 2; pass++) {
        TypeName *recs = pass == 0 ? ctx->tags : ctx->typedefs;
        int recs_len = pass == 0 ? ctx->tags_len : ctx->typedefs_len;
        for (int i = recs_len - 1; i >= 0; i--) {
            TypeName *rec = &recs[i];
            if (rec->owner_fn != NULL) // function-local: not this pass's concern
                continue;
            if (!rec->ty || rec->ty->kind != TY_ENUM)
                continue;
            if (!type_is_complete_tagged(rec->ty))
                continue;

            bool found = false;
            for (int g = 0; g < groups_len; g++)
                if (same_type_strong(groups[g].rep, rec->ty)) {
                    found = true;
                    break;
                }
            if (found)
                continue;

            if (groups_len == groups_cap) {
                groups_cap = groups_cap ? groups_cap * 2 : 8;
                groups = realloc(groups, sizeof(EnumGroup) * groups_cap);
            }
            groups[groups_len].rep = rec->ty;
            groups[groups_len].first_seen = groups_len;
            groups[groups_len].header_exposed = false;
            groups[groups_len].extern_ref = false;
            groups[groups_len].header_path = NULL;
            groups_len++;
        }
    }

    // #1016: a single enum group can still collide with an Obj name, so the
    // old groups_len < 2 bail-out (nothing to compare a lone group against)
    // is only safe when there are no Obj names to check it against either.
    if (groups_len < 1 || (groups_len < 2 && obj_names.used == 0)) {
        free(groups);
        hashmap_deinit_borrowed(&obj_names);
        return;
    }

    // Tier 1: header-exposed -- a from_include tag or typedef record names
    // this group.
    for (int i = 0; i < ctx->tags_len; i++) {
        if (!ctx->tags[i].from_include || ctx->tags[i].always_emit)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (same_type_strong(ctx->tags[i].ty, groups[g].rep)) {
                groups[g].header_exposed = true;
                if (!groups[g].header_path)
                    groups[g].header_path = ctx->tags[i].file_path;
            }
    }
    for (int i = 0; i < ctx->typedefs_len; i++) {
        if (!ctx->typedefs[i].from_include || ctx->typedefs[i].always_emit)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (same_type_strong(ctx->typedefs[i].ty, groups[g].rep)) {
                groups[g].header_exposed = true;
                if (!groups[g].header_path)
                    groups[g].header_path = ctx->typedefs[i].file_path;
            }
    }

    // Tier 2: an externally-visible definition's type reaches this group.
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_static)
            continue;
        bool is_defining = obj->is_function ? obj->body != NULL : obj->is_definition;
        if (!is_defining || !obj->ty)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (type_reaches_group(obj->ty, groups[g].rep))
                groups[g].extern_ref = true;
    }

    // Every distinct enumerator name declared by at least one group,
    // resolved exactly once below.
    char **names = NULL;
    int names_len = 0, names_cap = 0;
    for (int g = 0; g < groups_len; g++)
        for (EnumConstant *ec = groups[g].rep->enum_constants; ec; ec = ec->next) {
            if (!ec->name)
                continue;
            bool seen = false;
            for (int n = 0; n < names_len; n++)
                if (strcmp(names[n], ec->name) == 0) {
                    seen = true;
                    break;
                }
            if (seen)
                continue;
            if (names_len == names_cap) {
                names_cap = names_cap ? names_cap * 2 : 16;
                names = realloc(names, sizeof(char *) * names_cap);
            }
            names[names_len++] = ec->name;
        }

    for (int n = 0; n < names_len; n++) {
        const char *name = names[n];

        // Every distinct group declaring this exact enumerator name.
        int members[64];
        int members_len = 0;
        for (int g = 0; g < groups_len && members_len < 64; g++) {
            for (EnumConstant *ec = groups[g].rep->enum_constants; ec; ec = ec->next)
                if (ec->name && strcmp(ec->name, name) == 0) {
                    members[members_len++] = g;
                    break;
                }
        }
        // #1016: does an emitted file-scope Obj already occupy this
        // spelling? An Obj is never renamed by this pass (external linkage
        // makes that unsafe in general, see the function's own doc comment
        // above), so when true no enum group below can keep the plain name
        // either -- the Obj holds it unconditionally. #1017: keep the Obj*
        // itself (not just a bool) so the tier-1 residual warning below can
        // name and point at it.
        Obj *colliding_obj = hashmap_get(&obj_names, name);
        bool obj_collision = colliding_obj != NULL;
        if (members_len < 2 && !obj_collision)
            continue;

        bool any_header_exposed = false;
        for (int m = 0; m < members_len; m++)
            if (groups[members[m]].header_exposed)
                any_header_exposed = true;

        int keeper = -1;
        if (!obj_collision) {
            for (int m = 0; m < members_len; m++) {
                int idx = members[m];
                if (any_header_exposed && !groups[idx].header_exposed)
                    continue;
                if (keeper < 0)
                    keeper = idx;
                else if (groups[idx].extern_ref && !groups[keeper].extern_ref)
                    keeper = idx;
                else if (groups[idx].extern_ref == groups[keeper].extern_ref &&
                         groups[idx].first_seen < groups[keeper].first_seen)
                    keeper = idx;
            }
            if (keeper < 0)
                keeper = members[0];
        }

        for (int m = 0; m < members_len; m++) {
            int idx = members[m];
            if (idx == keeper)
                continue;
            // #1016: tier 1 stays a hard rule even when an Obj occupies the
            // name -- a header-exposed group's enumerators are never
            // renamed (the replayed #include binds the name textually
            // inside the header's own code, same reasoning #1014/#1015
            // already established). The residual Obj-vs-header conflict is
            // genuinely unrepresentable in flat C (neither name can be
            // renamed without breaking something else) and is left for the
            // host compiler to report; see man/COVERAGE.md. #1017: at
            // least point at it first, since the host compiler's own
            // diagnostic names a deleted /tmp temp file under -c=native
            // with no indication cccc's renamer is involved. Guard
            // colliding_obj->tok != NULL -- a comptime-synthesized Obj
            // need not carry a token, and warn_tok() dereferences
            // tok->file->name unconditionally.
            if (obj_collision && groups[idx].header_exposed) {
                if (colliding_obj->tok) {
                    if (groups[idx].header_path)
                        warn_tok(vm, colliding_obj->tok, CCCC_WARN_NATIVE_NAME_COLLISION,
                                "enumerator '%s' is declared by an enum reached through a "
                                "replayed #include ('%s') and cannot be renamed; the "
                                "file-scope '%s' declared here cannot be renamed either, "
                                "so the generated C will not compile",
                                name, groups[idx].header_path, name);
                    else
                        warn_tok(vm, colliding_obj->tok, CCCC_WARN_NATIVE_NAME_COLLISION,
                                "enumerator '%s' is declared by an enum reached through a "
                                "replayed #include and cannot be renamed; the file-scope "
                                "'%s' declared here cannot be renamed either, so the "
                                "generated C will not compile",
                                name, name);
                }
                continue;
            }
            char *new_name = arena_format(vm, "%s__cccc_dup%d", name,
                                          ctx->anon_global_counter++);
            if (ctx->enum_renames_len >= ctx->enum_renames_cap) {
                ctx->enum_renames_cap = ctx->enum_renames_cap ? ctx->enum_renames_cap * 2 : 8;
                ctx->enum_renames = realloc(ctx->enum_renames,
                                            sizeof(EnumConstRename) * ctx->enum_renames_cap);
            }
            ctx->enum_renames[ctx->enum_renames_len].rep = groups[idx].rep;
            ctx->enum_renames[ctx->enum_renames_len].orig = (char *)name;
            ctx->enum_renames[ctx->enum_renames_len].new_name = new_name;
            ctx->enum_renames_len++;
        }
    }

    free(names);
    free(groups);
    hashmap_deinit_borrowed(&obj_names);
}

// #1015: print-time lookup for serialize_enum_def()'s enumerator loop,
// consulting the table rename_colliding_enum_constants() built above -- see
// ctx->enum_renames' doc comment on SerializeContext for why this is a
// lookup rather than an EnumConstant.name mutation. Returns `name`
// unchanged when nothing was renamed for this (ty, name) pair, so a
// program with no enumerator collision serializes byte-identically to
// before this pass existed.
static const char *enum_const_spelling(SerializeContext *ctx, Type *ty, const char *name) {
    if (!name)
        return name;
    for (int i = 0; i < ctx->enum_renames_len; i++)
        if (same_type_strong(ctx->enum_renames[i].rep, ty) &&
            strcmp(ctx->enum_renames[i].orig, name) == 0)
            return ctx->enum_renames[i].new_name;
    return name;
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

// #1050: a comptime builder (e.g. Serialize's Memcpy()) can call
// memcpy/strlen/strcmp/etc via a synthetic Obj that ensure_libc_fn_decl()
// (reflection.c) creates on the fly, with no #include of its own in the
// TU (the whole point -- it works even when the TU never #includes
// <string.h>). That Obj has no token/file, so the ordinary auto-capture
// machinery has nothing to replay for it, and -c=native would otherwise
// print a bare, undeclared call. Emit the real header instead of a
// prototype (a prototype's necessarily-loose signature -- void* args,
// unsigned long size -- could conflict with the exact one <string.h>
// itself brings in, transitively, elsewhere in the same TU); reuses
// node_calls_obj's identity match, same as the Block_copy/free check just
// above, so a program that never calls a given synthesized decl doesn't
// get its header either. Deliberately not attempted for -c=generated
// (generated_only returns earlier in cc_serialize_program, replaying
// includes via CCCC_EMIT_SOURCE events instead) -- residual, not this
// ticket's scope.
// #1050: true if `prog` contains a *different* Obj, written in one of the
// user's own command-line input files, sharing `name` -- i.e. the program
// declares its own memcpy/strlen/strcmp/etc (however unusual; shadowing a
// libc name at file scope is legal C). register_synth_libc_call()
// (reflection.c) already skips registering such an Obj directly, but the
// reflection API's own identifier resolution can still resolve a call to a
// *different*, CCCC-injected Obj of the same name first (var_ref_lookup's
// scope search finds whichever declaration is nearer, and reflection.h's
// own implicit `#include <string.h>` parse can sit ahead of the user's
// own declaration) -- so the registered entry's Obj identity alone isn't
// enough to rule this out. Forcing `#include <string.h>` in on top of the
// user's own real declaration is worse than the gap this ticket fixes (a
// straight 'static declaration follows non-static declaration' compile
// failure that didn't exist before); skip the header entirely when the
// user has their own colliding declaration; the ordinary auto-capture/
// forward-declare-every-function machinery already covers *that* Obj.
static bool has_colliding_user_decl(VirtualMachine *vm, Obj *prog,
                                    const char *name, Obj *registered_obj) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj == registered_obj || !obj->is_function)
            continue;
        if (strcmp(obj->name, name) != 0)
            continue;
        if (obj->tok && obj->tok->file &&
            cc_file_is_command_line_input(vm, obj->tok->file->name))
            return true;
    }
    return false;
}

static void serialize_synth_libc_includes(FILE *f, VirtualMachine *vm, Obj *prog) {
    SynthLibcDeclArray *reg = &vm->compiler.synth_libc_decls;
    const char *emitted[32];
    int emitted_len = 0;
    bool any = false;
    for (int i = 0; i < reg->len; i++) {
        SynthLibcDecl *entry = &reg->data[i];
        bool called = false;
        for (Obj *obj = prog; obj && !called; obj = obj->next) {
            if (!obj->is_function || !obj->body)
                continue;
            called = node_calls_obj(obj->body, entry->obj);
        }
        if (!called)
            continue;
        if (has_colliding_user_decl(vm, prog, entry->obj->name, entry->obj))
            continue;
        bool already = false;
        for (int j = 0; j < emitted_len; j++)
            if (!strcmp(emitted[j], entry->header)) {
                already = true;
                break;
            }
        if (already)
            continue;
        fprintf(f, "#include <%s>\n", entry->header);
        any = true;
        if (emitted_len < (int)(sizeof(emitted) / sizeof(emitted[0])))
            emitted[emitted_len++] = entry->header;
    }
    if (any)
        fprintf(f, "\n");
}

// #1054/#1030: setjmp/longjmp (and their _setjmp/_longjmp POSIX-variant
// aliases, parse_decl.c) are on is_compiler_owned_header's list -- CCCC's
// own jmp_buf is a VM-bytecode-specific 5-slot layout with no host ABI
// equivalent (include/setjmp.h) -- but unlike every *other* owned header
// (stdbool.h/stdint.h/etc, type-only, nothing to call), setjmp.h also
// declares real functions that -c=native's generated C needs to actually
// *call* into the real host libc. Relying on the auto-captured `#include
// <setjmp.h>` line to resolve to the real host header at native-compile
// time is fragile: it depends on include search-path ordering the
// generated C has no control over (a user -I path that happens to also
// contain CCCC's own bundled headers -- e.g. the whole test suite's own
// `-I./include` -- shadows the real header with CCCC's declaration-free
// copy, "call to undeclared library function"). Sidestep entirely: never
// replay `#include <setjmp.h>` into -c=native/-m output (see this
// function's caller), and instead declare exactly the two symbols these
// four builtins are unconditionally lowered to (see the ND_FUNCALL case
// below) ourselves, with a signature (`void *`) that needs no jmp_buf type
// at all. `_setjmp`/`_longjmp` are chosen over plain `setjmp`/`longjmp`
// deliberately: on macOS both pairs are ordinary exported functions, but
// on glibc `setjmp` is a *macro* (`#define setjmp(env) _setjmp(env)`,
// verified against the real glibc header) while `_setjmp`/`_longjmp`
// remain plain `extern` declarations on both platforms -- so declaring and
// calling them directly needs nothing from any header, on either host.
// This also matches VM semantics exactly: the VM's own SETJMP/LONGJMP
// opcodes never touch a signal mask (ops.c), the same behavior `_setjmp`/
// `_longjmp` document (parse_decl.c's own comment on this).
static void serialize_synth_setjmp_decls(FILE *f, VirtualMachine *vm, Obj *prog) {
    Obj *family[4] = {
        vm->compiler.builtin_setjmp, vm->compiler.builtin_longjmp,
        vm->compiler.builtin__setjmp, vm->compiler.builtin__longjmp,
    };
    bool used = false;
    for (Obj *obj = prog; obj && !used; obj = obj->next) {
        if (!obj->is_function || !obj->body)
            continue;
        for (int i = 0; i < 4 && !used; i++)
            if (family[i] && node_calls_obj(obj->body, family[i]))
                used = true;
    }
    if (!used)
        return;
    fprintf(f, "extern int _setjmp(void *);\n");
    fprintf(f, "extern void _longjmp(void *, int) __attribute__((noreturn));\n\n");
}

// #1057: type-name sibling of #1050's synth-libc-call mechanism just above.
// A comptime builder can fold a standard scalar typedef name -- GetType(
// "size_t")/"ptrdiff_t"/"wchar_t" -- into a generated function's signature
// or body via cc_comptime_resolve_type_name()'s demand-driven splice
// (macros.c), which re-parses the typedef out of CCCC's own bundled
// include/stddef.h with no #include of it ever appearing in the TU. That
// leaves the recorded TypeNameRecord marked from_include=true (record_type_
// name, parse_core.c), so typedef_alias_header_suppressed() drops its alias
// line under -c=native/-m -- correctly, since the ordinary assumption is
// that the user's own #include supplies it -- but nothing here ever does,
// leaving a bare, undeclared name. Scoped to exactly the trio verified to
// match the real host's own typedef on every supported combo (LP64 macOS/
// Linux x aarch64/x86_64): long/unsigned long/int respectively (include/
// stddef.h). nullptr_t excluded (C23-only, typeof(nullptr)-defined, no
// repro); stdint.h's fixed-width names left for their own ticket if a repro
// turns up.
static const struct { const char *name; const char *header; } synth_typedef_headers[] = {
    {"size_t", "stddef.h"},
    {"ptrdiff_t", "stddef.h"},
    {"wchar_t", "stddef.h"},
};

static const char *synth_typedef_header_for_name(const char *name, int name_len) {
    for (size_t i = 0; i < sizeof(synth_typedef_headers) / sizeof(synth_typedef_headers[0]); i++) {
        const char *cand = synth_typedef_headers[i].name;
        if ((int)strlen(cand) == name_len && !strncmp(cand, name, name_len))
            return synth_typedef_headers[i].header;
    }
    return NULL;
}

// #1057: pointer-identity lookup mirroring find_typedef_name_exact()'s #999
// ->origin-chain walk, deliberately *without* its name_visible() gate. That
// gate answers "would this name resolve inside function X's own scope" --
// the right question when deciding what to *print*, but the wrong one here:
// a comptime-spliced typedef's TypeNameRecord.owner_fn can point at whatever
// scratch/comptime context was current_fn when the splice ran, unrelated to
// which ordinary function's Type this is being checked against. This is only
// asking "does *any* recorded typedef, anywhere, identify this exact Type,"
// which is scope-independent.
static TypeName *find_typedef_record_any_scope(SerializeContext *ctx, Type *ty) {
    if (!ctx || !ty)
        return NULL;
    for (int hop = 0; ty && hop < 8; ty = ty->origin, hop++)
        for (int i = 0; i < ctx->typedefs_len; i++)
            if (ctx->typedefs[i].ty == ty)
                return &ctx->typedefs[i];
    return NULL;
}

// #1057: skip the compensating #include when the program already declares
// its own top-level typedef of the same name, however it's shaped -- type-
// side analogue of #1050's has_colliding_user_decl(). An *identical*-shape
// user redeclaration is legal C either way (C11 6.7p3, confirmed: `typedef
// unsigned long size_t;` alongside CCCC's own comptime-resolved size_t
// compiles fine with or without this guard), but a differently-shaped one
// (e.g. a user `typedef struct {...} size_t;`) turns the forced #include
// into a hard "typedef redefinition with different types" that didn't exist
// before this fix -- confirmed directly, not just by analogy to #1050.
// Deliberately doesn't try to tell the two apart: any user declaration of
// the name is enough to defer to it and skip the header, matching #1050's
// own "skip entirely" choice for the same shape of risk.
static bool has_colliding_user_typedef(SerializeContext *ctx, const char *name,
                                       int name_len) {
    for (int i = 0; i < ctx->typedefs_len; i++) {
        TypeName *tn = &ctx->typedefs[i];
        if (tn->from_include || tn->always_emit)
            continue;
        if (tn->name_len == name_len && !strncmp(tn->name, name, name_len))
            return true;
    }
    return false;
}

// #1057: does `ty` (or anything structurally reachable from it) resolve to
// one of synth_typedef_headers' names whose alias line is being suppressed
// -- i.e. reached the program only through the comptime splice described
// above, with no user #include for serialize_synth_typedef_includes()
// (below) to piggyback on. Mirrors type_mentions_block()'s PTR/ARRAY/VLA/
// FUNC traversal shape, plus struct/union member recursion (guarded by
// `seen`, since unlike type_mentions_block a self-referential struct is a
// realistic shape to hit here, e.g. a linked-list node with a `size_t`
// field alongside a `struct node *next`).
static bool type_needs_synth_typedef_header(SerializeContext *ctx, Type *ty,
                                            const char *header, TypeVec *seen) {
    if (!ty)
        return false;
    TypeName *tn = find_typedef_record_any_scope(ctx, ty);
    if (tn && typedef_alias_header_suppressed(ctx, tn)) {
        const char *want = synth_typedef_header_for_name(tn->name, tn->name_len);
        if (want && !strcmp(want, header) &&
            !has_colliding_user_typedef(ctx, tn->name, tn->name_len))
            return true;
    }
    if (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_VLA)
        return type_needs_synth_typedef_header(ctx, ty->base, header, seen);
    if (ty->kind == TY_FUNC) {
        if (type_needs_synth_typedef_header(ctx, ty->return_ty, header, seen))
            return true;
        for (Type *p = ty->params; p; p = p->next)
            if (type_needs_synth_typedef_header(ctx, p, header, seen))
                return true;
        return false;
    }
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        if (type_vec_contains(seen, ty))
            return false;
        type_vec_push(seen, ty);
        for (Member *m = ty->members; m; m = m->next)
            if (type_needs_synth_typedef_header(ctx, m->ty, header, seen))
                return true;
    }
    return false;
}

// #1057: mirrors collect_node_types()'s traversal shape (see also #990/#993's
// node_mentions_block, the same pattern for TY_BLOCK).
static bool node_needs_synth_typedef_header(SerializeContext *ctx, Node *node,
                                            const char *header, TypeVec *seen) {
    if (!node)
        return false;
    if (type_needs_synth_typedef_header(ctx, node->ty, header, seen))
        return true;
    if (node->var && type_needs_synth_typedef_header(ctx, node->var->ty, header, seen))
        return true;
    if (node->member && type_needs_synth_typedef_header(ctx, node->member->ty, header, seen))
        return true;
    if (node->func_ty && type_needs_synth_typedef_header(ctx, node->func_ty, header, seen))
        return true;

    return node_needs_synth_typedef_header(ctx, node->lhs, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->rhs, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->cond, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->then, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->els, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->init, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->inc, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->body, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->args, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->next, header, seen);
}

// #1057: mirrors collect_obj_types()'s traversal shape.
static bool obj_needs_synth_typedef_header(SerializeContext *ctx, Obj *obj,
                                           const char *header, TypeVec *seen) {
    if (type_needs_synth_typedef_header(ctx, obj->ty, header, seen))
        return true;
    if (node_needs_synth_typedef_header(ctx, obj->init_expr, header, seen))
        return true;
    for (Obj *param = obj->params; param; param = param->next)
        if (type_needs_synth_typedef_header(ctx, param->ty, header, seen))
            return true;
    for (Obj *local = obj->locals; local; local = local->next)
        if (type_needs_synth_typedef_header(ctx, local->ty, header, seen))
            return true;
    return node_needs_synth_typedef_header(ctx, obj->body, header, seen);
}

static void serialize_synth_typedef_includes(FILE *f, SerializeContext *ctx, Obj *prog) {
    const char *emitted[8];
    int emitted_len = 0;
    bool any = false;
    for (size_t i = 0; i < sizeof(synth_typedef_headers) / sizeof(synth_typedef_headers[0]); i++) {
        const char *header = synth_typedef_headers[i].header;
        bool already = false;
        for (int j = 0; j < emitted_len; j++)
            if (!strcmp(emitted[j], header)) {
                already = true;
                break;
            }
        if (already)
            continue;

        bool needed = false;
        for (Obj *obj = prog; obj && !needed; obj = obj->next) {
            if (obj->is_function && !obj->is_definition && !obj->body)
                continue;
            TypeVec seen = {0};
            needed = obj_needs_synth_typedef_header(ctx, obj, header, &seen);
            free(seen.data);
        }
        if (!needed)
            continue;

        fprintf(f, "#include <%s>\n", header);
        any = true;
        if (emitted_len < (int)(sizeof(emitted) / sizeof(emitted[0])))
            emitted[emitted_len++] = header;
    }
    if (any)
        fprintf(f, "\n");
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

// #990/#993: mirrors collect_node_types()'s traversal shape to find any node
// whose type -- or a var/member/func_ty attached to it -- mentions TY_BLOCK.
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

    // #1005: no ND_SWITCH/ND_CASE special case (see collect_node_types());
    // the generic traversal below already reaches every case via node->then.
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

// #1074: does `var` belong to `fn`'s own locals list (which, for a
// function Obj, always includes its params and __static_link too -- see
// parse_decl.c's `fn->params = vm->compiler.locals;`)? Independent copy of
// parse_analysis.c's identically-shaped (and identically-named-in-spirit)
// var_in_fn_locals() -- that one is `static` in a different translation
// unit, so it isn't reachable from here.
static bool nested_var_is_own(Obj *fn, Obj *var) {
    for (Obj *v = fn->locals; v; v = v->next)
        if (v == var)
            return true;
    return false;
}

// #1074: find (or, on first use, create) `owner`'s NestedEnvEntry.
static NestedEnvEntry *find_or_create_nested_env(VirtualMachine *vm,
                                                 SerializeContext *ctx,
                                                 Obj *owner) {
    for (int i = 0; i < ctx->nested_envs_len; i++)
        if (ctx->nested_envs[i].owner_fn == owner)
            return &ctx->nested_envs[i];
    if (ctx->nested_envs_len == ctx->nested_envs_cap) {
        ctx->nested_envs_cap = ctx->nested_envs_cap ? ctx->nested_envs_cap * 2 : 8;
        ctx->nested_envs = realloc(ctx->nested_envs,
                                   sizeof(NestedEnvEntry) * ctx->nested_envs_cap);
    }
    NestedEnvEntry *e = &ctx->nested_envs[ctx->nested_envs_len++];
    e->owner_fn = owner;
    e->env_struct_name = arena_format(vm, "struct __cccc_nenv_%s", owner->name);
    e->upvars = NULL;
    e->upvars_len = 0;
    e->upvars_cap = 0;
    return e;
}

// #1074: record `var` (owned by `e`'s function) as an upvar if it isn't
// already, returning its field index either way.
static int add_nested_upvar(Obj ***upvars_out, int *len, int *cap, Obj *var) {
    Obj **upvars = *upvars_out;
    for (int i = 0; i < *len; i++)
        if (upvars[i] == var)
            return i;
    if (*len == *cap) {
        *cap = *cap ? *cap * 2 : 4;
        upvars = realloc(upvars, sizeof(Obj *) * (*cap));
        *upvars_out = upvars;
    }
    upvars[*len] = var;
    return (*len)++;
}

// #1074: `node` (inside nested function `fn`'s own body) reads/writes
// `var`, which -- per the caller's own scan -- belongs to some ancestor of
// `fn`, not to `fn` itself. Reject the three shapes serialize_nested_
// preamble()'s env-struct lowering cannot represent (each needs `&var` to
// be a stable, already-valid address at the point the owning function's env
// is initialized -- serialize_function's hoist loop, mirrored in the
// comments below, is exactly what can't supply one for these), otherwise
// find `var`'s owning ancestor and register it as an upvar of that
// ancestor's env.
static void record_nested_upvar(VirtualMachine *vm, SerializeContext *ctx,
                                Obj *fn, Node *node, Obj *var) {
    Obj *owner = NULL;
    for (Obj *anc = fn->parent_fn; anc; anc = anc->parent_fn) {
        if (nested_var_is_own(anc, var)) { owner = anc; break; }
    }
    if (!owner)
        return; // defensive only -- the real scope chain guarantees this

    // #964: a VLA's declaration can't be hoisted ahead of the point it
    // reads its own length expression -- serialize_function's hoist loop
    // skips it for exactly this reason (see its own #964 comment), so no
    // `&var` is available yet when the owning function's env would need to
    // be initialized.
    if (var->ty && var->ty->kind == TY_VLA) {
        error_tok(vm, node->tok ? node->tok : fn->tok,
                  "cannot serialize to native code: variable-length-array "
                  "local '%s', read by a nested function, has no fixed "
                  "address to hand across the static link (#1074)",
                  var->name);
        return;
    }
    // #973: same reasoning, for a pointer-to-VLA local whose own declarator
    // reads a runtime variable and is likewise emitted in place rather than
    // hoisted.
    if (var->deferred_vla_ptr_init) {
        error_tok(vm, node->tok ? node->tok : fn->tok,
                  "cannot serialize to native code: pointer-to-VLA local "
                  "'%s', read by a nested function, is declared too late "
                  "for the static-link environment to capture its address "
                  "(#1074)", var->name);
        return;
    }
    // #965: a __block-storage local's own C storage is already a pointer
    // (its slot holds the shared heap box), so `&var` here would be a
    // pointer-to-pointer -- one level too many for the env field's plain
    // `T *` type (which assumes ordinary storage).
    if (var->is_block_var || var->block_desc_of) {
        error_tok(vm, node->tok ? node->tok : fn->tok,
                  "cannot serialize to native code: __block-storage local "
                  "'%s' cannot also be captured by a nested function's "
                  "static link (#1074)", var->name);
        return;
    }

    NestedEnvEntry *e = find_or_create_nested_env(vm, ctx, owner);
    add_nested_upvar(&e->upvars, &e->upvars_len, &e->upvars_cap, var);
}

// #1074: walks `fn`'s own body (a nested function) looking for two things:
// a reference to a local/param owned by an ancestor (an "upvar", handed to
// record_nested_upvar()), and a bare reference to another nested function's
// Obj that ISN'T the direct callee of a call to it -- e.g. `int (*p)(int) =
// inner;` or passing `inner` itself as a callback argument. The latter has
// no portable spelling: the hoisted signature carries a leading
// `__static_link` parameter no real function-pointer type can express, so
// it's rejected here rather than serialized wrong. The one legal bare
// reference (a direct call's own callee, handled by ND_FUNCALL's own
// emission -- see the #1074 comment there) is a leaf ND_VAR node with
// nothing beneath it to walk, so it's simply never descended into below,
// rather than needing its own exemption flag.
static void collect_nested_refs(VirtualMachine *vm, SerializeContext *ctx,
                                Obj *fn, Node *node) {
    for (; node; node = node->next) {
        if (node->kind == ND_VAR && node->var) {
            // #1074: the "is this an upvar of an ancestor" question only
            // makes sense when `fn` is itself a nested function -- pass 2
            // now walks EVERY function's body (including ordinary
            // top-level functions and Apple block literals, is_block,
            // which are their own separate Obj too) so the reference-check
            // below reaches every context a bad reference could appear in,
            // but a block's own capture of an outer local (block_fn's
            // locals never include what it captures either, by the same
            // shape) is a completely different, already-correct mechanism
            // (serialize_block_capture_ref) -- not an upvar, and must not
            // be misdetected as one here.
            if (fn->is_nested && !fn->is_block && node->var->is_local &&
                !nested_var_is_own(fn, node->var))
                record_nested_upvar(vm, ctx, fn, node, node->var);
            else if (node->var->is_function && node->var->is_nested &&
                     !node->var->is_block)
                error_tok(vm, node->tok ? node->tok : fn->tok,
                          "cannot serialize to native code: a reference to "
                          "nested function '%s' is only supported as the "
                          "direct callee of a call to it -- its native "
                          "signature carries a hidden static-link "
                          "parameter, so it has no portable function-"
                          "pointer type (#1074)", node->var->name);
        }

        // #1074 follow-up: a block literal directly inside a genuinely
        // nested function (fn->is_nested && !fn->is_block) that captures a
        // variable belonging to one of THAT function's own ancestors (an
        // upvar of `fn`, not `fn`'s own local) has no correct native
        // lowering here -- the block's own capture-copy code (ND_BLOCK_
        // LITERAL's case in serialize_expr) prints a plain `cap->name`,
        // which isn't nameable at file scope for a var owned outside `fn`.
        // Confirmed empirically to already be a pre-existing, unrelated VM
        // miscompile independent of native (wrong answer, not just a
        // native-side gap) -- filed as a follow-up rather than designed
        // around here; reject with a diagnostic instead of letting native
        // silently emit it (segfaults in practice: an uninitialized/
        // out-of-scope identifier reference).
        if (node->kind == ND_BLOCK_LITERAL && fn->is_nested && !fn->is_block) {
            for (int __bc_i = 0; __bc_i < node->num_block_captures; __bc_i++) {
                Obj *cap = node->block_captures[__bc_i];
                if (cap->is_local && !nested_var_is_own(fn, cap))
                    error_tok(vm, node->tok ? node->tok : fn->tok,
                              "cannot serialize to native code: a block "
                              "literal inside a nested function capturing "
                              "'%s', a variable owned by one of that "
                              "function's own ancestors, is not supported "
                              "(#1074 follow-up)", cap->name);
            }
        }

        bool lhs_is_direct_nested_call =
            node->kind == ND_FUNCALL && node->lhs && node->lhs->kind == ND_VAR &&
            node->lhs->var && node->lhs->var->is_function &&
            node->lhs->var->is_nested && !node->lhs->var->is_block;
        if (!lhs_is_direct_nested_call)
            collect_nested_refs(vm, ctx, fn, node->lhs);
        collect_nested_refs(vm, ctx, fn, node->rhs);
        collect_nested_refs(vm, ctx, fn, node->cond);
        collect_nested_refs(vm, ctx, fn, node->then);
        collect_nested_refs(vm, ctx, fn, node->els);
        collect_nested_refs(vm, ctx, fn, node->init);
        collect_nested_refs(vm, ctx, fn, node->inc);
        collect_nested_refs(vm, ctx, fn, node->body);
        collect_nested_refs(vm, ctx, fn, node->cas_addr);
        collect_nested_refs(vm, ctx, fn, node->cas_old);
        collect_nested_refs(vm, ctx, fn, node->cas_new);
        for (Node *a = node->args; a; a = a->next)
            collect_nested_refs(vm, ctx, fn, a);
    }
}

// #1074: emits one `struct __cccc_nenv_<name> { void *__up; T0 *__uv0; ...
// };` for every function that directly parents at least one nested
// function -- even one with an empty upvars list still needs `__up`, to
// carry an intervening level of a multi-level nest chain. Must run after
// serialize_type_defs_for_owner(f, ctx, NULL) (file-scope types) so an
// upvar whose own struct/union/enum type was declared inside a function can
// be hoisted ahead of it here, mirroring #989's identical reasoning for a
// block capture's type -- see hoist_local_type_to_file_scope()'s own
// comment. Called from cc_serialize_program next to serialize_block_
// preamble(), in both branches, at the same point in the emission order.
static void serialize_nested_preamble(FILE *f, VirtualMachine *vm,
                                      SerializeContext *ctx, Obj *prog) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        // is_block reuses is_nested for VM codegen purposes (parse_blocks.c)
        // -- a block literal is not one of "our" nested functions (it has
        // its own complete, separate lowering, #965) and must not trigger
        // creating an env for its parent here.
        if (!obj->is_function || !obj->is_nested || obj->is_block || !obj->body)
            continue;
        bool reachable = !ctx->generated_only || obj->is_macro_generated;
        if (!reachable)
            continue;
        // Pass 1: guarantee obj->parent_fn has an entry regardless of
        // whether it turns out to own any upvars -- an intervening level of
        // a multi-level nest needs one purely to carry __up.
        find_or_create_nested_env(vm, ctx, obj->parent_fn);
    }
    if (ctx->nested_envs_len == 0)
        return;

    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->body)
            continue;
        bool reachable = !ctx->generated_only || obj->is_macro_generated;
        if (!reachable)
            continue;
        // Pass 2: collect each owner's upvars, and reject an unsupported
        // bare reference to a nested function's value -- run over EVERY
        // function's body, not just nested ones. The bad-reference check
        // has no dependency on `obj` itself being nested (a nested
        // function's own *enclosing* function, or an unrelated sibling, can
        // just as easily write `int (*fp)(int) = inner;` or pass `inner` as
        // a callback); nested_var_is_own()'s climb up `obj->parent_fn`
        // naturally no-ops for a non-nested `obj` (parent_fn is NULL, the
        // loop never runs, `owner` stays NULL, record_nested_upvar()
        // returns immediately) so this is safe to run unconditionally.
        collect_nested_refs(vm, ctx, obj, obj->body);
    }

    for (int i = 0; i < ctx->nested_envs_len; i++)
        for (int j = 0; j < ctx->nested_envs[i].upvars_len; j++)
            hoist_local_type_to_file_scope(f, vm, ctx,
                                           ctx->nested_envs[i].upvars[j]->ty);

    for (int i = 0; i < ctx->nested_envs_len; i++) {
        NestedEnvEntry *e = &ctx->nested_envs[i];
        fprintf(f, "%s {\n    void *__up;\n", e->env_struct_name);
        for (int j = 0; j < e->upvars_len; j++) {
            char field_name[16];
            snprintf(field_name, sizeof(field_name), "__uv%d", j);
            fprintf(f, "    ");
            serialize_type_decl(f, ctx, pointer_to(vm, e->upvars[j]->ty), field_name);
            fprintf(f, ";\n");
        }
        fprintf(f, "};\n\n");
    }
}

// #1074: frees every NestedEnvEntry's own upvars array before freeing the
// table itself -- the table's realloc'd blocks (env_struct_name is
// arena-allocated, not heap) are the only per-entry heap allocation.
static void free_nested_envs(SerializeContext *ctx) {
    for (int i = 0; i < ctx->nested_envs_len; i++)
        free(ctx->nested_envs[i].upvars);
    free(ctx->nested_envs);
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
// itself reported, but blocks it -- see CLAUDE.md). #1006: promoted to a
// shared cc_file_is_command_line_input() (preprocess.c) so record_type_name()
// (parse.c) and the auto-capture gate (preprocess.c) could adopt the exact
// same test for their own primary_file-keyed drops; kept here as a thin
// wrapper so this file's existing call sites/comments didn't need to move.
static bool file_is_command_line_input(VirtualMachine *vm, const char *name) {
    return cc_file_is_command_line_input(vm, name);
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

// #1047: the global-variable counterpart to function_is_header_supplied()
// just above. Unlike functions, globals had no include-provenance gate at
// all -- the #918 forward-declare-every-global pass and serialize_global_var
// both only checked is_function/is_string_literal/init_data-presence, so a
// header declaring `static int x = 0;` produced three copies of `x` in
// -c=native output: the replayed `#include`, the forward declaration, and
// the definition -- a hard "redefinition" from the host compiler. Same
// safe-default guards as the function version (no token/file -> emit
// rather than silently drop; macro-generated -> emit, it has no header of
// its own to collide with), and the same `!generated_only ||
// path_is_captured(...)` tail, but without the is_static/body checks
// (function_is_header_supplied only suppresses a *definition*, since a
// bodyless declaration is handled by its own from_input branch further
// down; an ordinary global's replayed header line is its only
// declaration+definition either way, so there's no separate case to split
// out here).
static bool global_is_header_supplied(VirtualMachine *vm, SerializeContext *ctx,
                                      Obj *obj) {
    if (obj->is_macro_generated)
        return false;
    Token *t = obj->tok;
    if (!t || !t->file)
        return false;
    if (file_is_command_line_input(vm, t->file->name) ||
        cc_file_is_cccc_only(vm, t->file->name))
        return false;
    return !ctx->generated_only || path_is_captured(ctx, t->file->name);
}

// #1064: true if `line` (a raw captured directive line, `#...`, from
// copy_raw_directive_line()/copy_routed_directive_line() in preprocess.c) is
// one of the conditional-group directives -- see the call site in
// cc_serialize_program()'s emit_directives loop for why these are dropped
// from ordinary replay. Matches on the directive word after `#` and
// optional whitespace; deliberately textual rather than pp_directive()
// (token-level, not available on this already-flattened string).
static bool line_is_conditional_directive(const char *line) {
    if (!line || line[0] != '#')
        return false;
    const char *p = line + 1;
    while (*p == ' ' || *p == '\t')
        p++;
    static const char *const kw[] = {
        "if", "ifdef", "ifndef", "elif", "elifdef", "elifndef", "else", "endif",
    };
    for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) {
        size_t len = strlen(kw[i]);
        if (strncmp(p, kw[i], len) == 0) {
            char c = p[len];
            // Require a word boundary so "ifdef" doesn't also match a
            // (nonexistent) directive starting "ifdefine" etc, and "if"
            // doesn't wrongly match "ifdef"/"ifndef" as a prefix hit --
            // checked longest-first below via the table order isn't
            // relied on; the boundary check alone is sufficient since "if"
            // followed by 'd'/'n' fails the boundary test and falls
            // through to the next table entry.
            if (c == '\0' || c == ' ' || c == '\t' || c == '(')
                return true;
        }
    }
    return false;
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
    rename_colliding_static_names(vm, prog, &ctx); // #1002
    rename_colliding_type_tags(vm, prog, &ctx); // #1014
    rename_colliding_enum_constants(vm, prog, &ctx); // #1015
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
        serialize_nested_preamble(f, vm, &ctx, prog); // #1074
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
            // #1023: see type_needs_anon_aggregate's comment on the #918
            // loop below -- an untagged, alias-less struct/union global
            // can't be forward-declared at all without re-deriving a
            // structurally distinct anonymous type.
            if (type_needs_anon_aggregate(&ctx, obj->ty))
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
        free_nested_envs(&ctx);
        free(ctx.hoisted.data);
        free(ctx.emitted_defs.data);
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
        // #1054/#1030: setjmp.h is *owned* (VM-specific jmp_buf ABI), not
        // cccc-only, so it doesn't take the branch above -- but it still
        // must never reach native/-m output verbatim, for a different
        // reason: relying on this replayed line to resolve to the real
        // host <setjmp.h> at native-compile time is exactly the fragile
        // include-search-path dependency serialize_synth_setjmp_decls()'s
        // own comment (below) explains. `--emit-cccc` is exempted like the
        // cccc-only branch above -- its whole point is dialect fidelity,
        // and a cccc reader understands this header directly.
        if (!vm->compiler.emit_cccc && resolved &&
            path_basename_is(resolved, "setjmp.h"))
            continue;
        // #1064: a captured conditional-group directive line
        // (#if/#ifdef/#ifndef/#elif/#elifdef/#elifndef/#else/#endif) is
        // always an empty shell here -- CCCC's own preprocessor has already
        // resolved the guarded content (a skipped branch's body is never
        // captured at all; a taken branch's content is captured as its own
        // separate lines/directives), so the shell carries no information.
        // Replaying it anyway hands the *evaluation* to the host compiler a
        // second time, for no benefit and two real hazards: a host that
        // lacks a feature-test macro CCCC's own preprocessor already
        // resolved (e.g. clang 18 rejecting a captured
        // `#if __has_embed(...)` shell outright, "function-like macro
        // '__has_embed' is not defined" -- CCCC evaluated it fine, the
        // empty shell is the only thing reaching the host), and a captured
        // `#ifdef __CCCC__` shell being silently false at the host (which
        // never defines that macro), dropping whatever a taken branch
        // inside it captured. `--emit-cccc` is exempted like the two
        // filters above -- dialect-fidelity output expects a cccc-aware
        // reader.
        if (!vm->compiler.emit_cccc && line_is_conditional_directive(line))
            continue;
        fprintf(f, "%s\n", line);
    }
    if (vm->compiler.emit_directives.len > 0)
        fprintf(f, "\n");

    // #1050: headers for comptime-synthesized libc calls with no #include
    // of their own to auto-capture -- see serialize_synth_libc_includes's
    // own comment. Placed after the replayed user includes (so it can't
    // shadow them) and before the accessor shims (some of which reference
    // libc-declared types).
    if (!generated_only) {
        serialize_synth_libc_includes(f, vm, prog);
        if (!vm->compiler.emit_cccc)
            serialize_synth_setjmp_decls(f, vm, prog);
    }

    // #1057: headers for comptime-folded standard typedef names (size_t/
    // ptrdiff_t/wchar_t) with no #include of their own to auto-capture --
    // see serialize_synth_typedef_includes's own comment. Same placement
    // rationale as the synth-libc-call pass just above; ctx->typedefs is
    // already populated by collect_scope_names() near the top of this
    // function.
    if (!generated_only)
        serialize_synth_typedef_includes(f, &ctx, prog);

    // #904: real symbols for internal host-accessor shims (stdout/errno/
    // etc) -- only meaningful once the real headers above are visible, and
    // only outside generated_only (-c=generated), matching the from_include
    // filter's gating for the same reason (see the comment on this function).
    if (!generated_only)
        serialize_native_accessor_shims(f, prog);
    serialize_asm_prefix_preamble(f, prog);

    // Serialize file-scope type definitions before declarations that reference them.
    serialize_type_defs_for_owner(f, &ctx, NULL);

    // #965/#993: see the comment on the generated_only branch's own call
    // above -- must run after both the #include replay and the file-scope
    // type-def pass just above, so a capture's type (however it reaches the
    // output) is already visible.
    serialize_block_preamble(f, vm, &ctx, prog);
    serialize_nested_preamble(f, vm, &ctx, prog); // #1074

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
        // #1023: an untagged, alias-less struct/union global (e.g.
        // `static const struct { ... } codes[74]`) can't be
        // forward-declared -- see type_needs_anon_aggregate's comment.
        // Skipping it here is strictly better than the alternative
        // (re-deriving a second, structurally distinct anonymous type that
        // the host compiler rejects as a redefinition): the real
        // definition below (serialize_global_var) still carries the only
        // copy of the type, so nothing is lost except the (here,
        // impossible) forward reference this pass exists to support.
        if (type_needs_anon_aggregate(&ctx, obj->ty))
            continue;
        // #1047: a header-supplied global is already forward-visible via
        // the replayed #include -- see global_is_header_supplied()'s
        // comment.
        if (global_is_header_supplied(vm, &ctx, obj))
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
    free_nested_envs(&ctx);
    free(ctx.hoisted.data);
    free(ctx.emitted_defs.data);
    free(ctx.enum_renames); // #1016: was missing from this list
}

