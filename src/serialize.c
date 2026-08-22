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
    int    len;
    int    cap;
} TypeVec;

typedef struct {
    Type *ty;
    char *name;
    int   name_len;
    Obj  *owner_fn;
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
    Obj  *block_fn;
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
    Obj  *owner_fn;
    char *env_struct_name; // includes the leading "struct " keyword
    Obj **upvars;
    int   upvars_len;
    int   upvars_cap;
} NestedEnvEntry;

// #1044: one function-local `static` (or block-scope compound literal)
// whose initializer takes a label's address (`&&label`, GNU labels-as-
// values) and so cannot be hoisted to file scope the way rename_anon_
// globals() ordinarily hoists an anonymous global -- labels-as-values are
// only legal inside the function that defines the label (verified directly
// against real clang/GCC). Emitted instead inside owner_fn's own body by
// serialize_function(), right after the flat local-declaration hoist. See
// collect_deferred_static_labels()'s own comment.
typedef struct {
    Obj *var;
    Obj *owner_fn;
} DeferredStaticLabel;

// #1044: one `ND_LABEL` reachable from some function's body, recording both
// spellings a Relocation might need to resolve against: `unique_label` (a
// `.L..N` string, matched by pointer identity -- resolve_goto_labels(),
// parse_decl.c, hands the identical pointer to a label and to every
// reference that resolves against it, exactly like SerJumpFrame's
// brk_label/cont_label matching above) and `label` (the real source
// spelling a `&&L0` reference must print).
typedef struct {
    char *unique_label;
    char *label;
    Obj  *owner_fn;
} LabelOwner;

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
    char *brk_label;  // NULL if this construct isn't a break target
    char *cont_label; // NULL for switch -- parse.c's switch parsing only
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
    TypeVec   seen;
    TypeVec   defs;
    TypeName *tags;
    int       tags_len;
    int       tags_cap;
    TypeName *typedefs;
    int       typedefs_len;
    int       typedefs_cap;
    Obj      *current_fn;
    bool generated_only; // skip header typedefs; output is consumed alongside
                         // normal headers
    // #891: --emit-only suppresses auto-capture (preprocess.c), so under it
    // the primary file's own #include directives are NOT re-emitted -- a
    // header-sourced typedef/tag has no re-emitted #include to collide with
    // and must still be serialized. Only skip has_include gates when this
    // is false.
    bool emit_strict;
    bool emit_cccc; // --emit-cccc: serialize checked-pointer qualifiers instead
                    // of dropping them
    int anon_local_counter;  // names compiler-synthesized temps (e.g. ++/--
                             // desugaring)
    int anon_global_counter; // names non-string-literal `.L..N` globals (#925)
    // #953: resolved paths of headers actually auto-captured into
    // generated_only (-c=generated) output -- built once in
    // cc_serialize_program from vm->compiler.emit_include_paths. Only
    // consulted in generated_only mode; see serialize_type_defs_for_owner.
    // A VLA's length is an expression node, so serializing its declarator
    // (serialize_type_decl, which has no vm parameter) needs the vm the
    // expression serializer takes. Set once in cc_serialize_program.
    VirtualMachine *vm;
    char          **captured_paths;
    int             captured_paths_len;
    // #965: block-literal env structs -- see BlockEnvEntry and
    // serialize_block_preamble().
    BlockEnvEntry *block_envs;
    int            block_envs_len;
    int            block_envs_cap;
    // #1074: nested-function env structs -- see NestedEnvEntry and
    // serialize_nested_preamble(). Built (and every illegal non-call
    // reference to a nested function rejected) entirely during that one
    // preamble pass, before any function body is actually emitted -- so
    // serialize_expr's own ND_VAR/ND_FUNCALL arms never need to re-derive
    // "is this reference legal", only "what does a known-legal one print
    // as".
    NestedEnvEntry *nested_envs;
    int             nested_envs_len;
    int             nested_envs_cap;
    // #1044: every ND_LABEL reachable from any function's body (built by
    // collect_deferred_static_labels()), and the anonymous globals whose
    // relocations resolve against that table -- see DeferredStaticLabel/
    // LabelOwner's own comments.
    LabelOwner          *label_owners;
    int                  label_owners_len;
    int                  label_owners_cap;
    DeferredStaticLabel *deferred_label_statics;
    int                  deferred_label_statics_len;
    int                  deferred_label_statics_cap;
    // #989: types promoted from function-local to file scope (a block
    // capture's own struct/union/enum type declared inside a function,
    // needed because its lifted environment struct is emitted at file
    // scope). Doubles as the post-order seen-set during promotion and as
    // the skip-set serialize_type_defs_for_owner uses to avoid re-emitting
    // a definition the preamble already wrote out.
    TypeVec hoisted;
    int     hoisted_type_counter; // names renamed/synthesized hoisted tags,
                                  // parallel to anon_global_counter
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
    Node         *cur_switch;
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
    int              enum_renames_len;
    int              enum_renames_cap;
    // #1085: bumped once per va_list-forwarding call-site shim emitted (see
    // ND_FUNCALL below) so nested/repeated occurrences in one TU each get
    // their own __cccc_va_fwd_N local instead of shadowing.
    int va_fwd_seq;
    // #1095: true only while serialize_type_decl() is emitting the
    // declarator for a local, or a global with no byte-image initializer
    // (var->init_data == NULL) -- the two shapes where re-materializing a
    // host-owned sizeof/_Alignof array dimension (Type.array_len_layout_ty)
    // can't disagree with anything else CCCC also emits for the same
    // object. False everywhere else on purpose: a struct/union MEMBER's
    // array dimension must stay folded (the containing aggregate's layout,
    // and every other member's offset, is computed from that folded value
    // -- re-materializing would desync them, the same reasoning that rules
    // out bitfield widths, see man/COVERAGE.md), an INITIALIZED global's
    // dimension must stay folded (serialize_init_bytes' own byte image is
    // still sized off the folded value -- re-materializing only the
    // dimension would make the array declaration and its initializer
    // disagree), and a typedef/cast/type-name spelling must stay folded
    // (reused across every context, including the two excluded above).
    bool allow_layout_dims;
} SerializeContext;

// Forward declaration
static void serialize_expr(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                           Node *node, int parent_prec);
// #1124: serialize_expr's actual switch-on-node->kind body, renamed so the
// public serialize_expr can wrap it with the _BitInt width-mask check below
// without recursing into itself.
static void serialize_expr_raw(FILE *f, VirtualMachine *vm,
                               SerializeContext *ctx, Node *node,
                               int parent_prec);
// #1062/#1085: matches CCCC's own struct va_list structurally (defined
// further down, near its own long comment) -- forward-declared here so
// ND_FUNCALL (inside serialize_expr's own switch, above the definition
// textually) can call it too.
static bool type_is_cccc_va_list(Type *ty);
// #1074/#1080: does `var` belong to `fn`'s own locals list? Defined near
// serialize_nested_preamble() (with the rest of the nested-function-upvar
// machinery); forward-declared here so ND_BLOCK_LITERAL's capture-copy
// (serialize_expr, above that machinery in file order) can reuse it.
static bool nested_var_is_own(Obj *fn, Obj *var);
// #1136: defined near serialize_global_var (with the rest of the
// declaration-printing machinery); forward-declared here so the hoisted-local
// declarator (serialize_function, above that machinery in file order) can
// reuse it too.
static void serialize_alignas_if_needed(FILE *f, Obj *var);
static void serialize_stmt(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                           Node *node, int indent);
// #964: mutually recursive with serialize_stmt -- see the comment on its
// definition, near ND_BLOCK below.
static void serialize_stmt_list_item(FILE *f, VirtualMachine *vm,
                                     SerializeContext *ctx, Node *node,
                                     int indent);

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
    return n && n->ty &&
           (n->ty->kind == TY_PTR || n->ty->kind == TY_ARRAY ||
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
    if (!node)
        return true;
    if (node->kind == ND_NULL_EXPR)
        return true;
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
            if (ma->name &&
                (ma->name->len != mb->name->len ||
                 strncmp(ma->name->loc, mb->name->loc, ma->name->len) != 0))
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
        return a->array_len == b->array_len &&
               same_type_or_origin(a->base, b->base);

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
        vec->cap  = vec->cap ? vec->cap * 2 : 16;
        vec->data = realloc(vec->data, sizeof(Type *) * vec->cap);
    }
    vec->data[vec->len++] = ty;
}

static void type_name_push(TypeName **items, int *len, int *cap, Type *ty,
                           char *name, int name_len, Obj *owner_fn,
                           bool from_include, bool always_emit, char *file_path,
                           bool defines_type) {
    if (!ty || !name || name_len <= 0)
        return;

    if (*len >= *cap) {
        *cap   = *cap ? *cap * 2 : 16;
        *items = realloc(*items, sizeof(TypeName) * *cap);
    }

    (*items)[*len].ty           = ty;
    (*items)[*len].name         = name;
    (*items)[*len].name_len     = name_len;
    (*items)[*len].owner_fn     = owner_fn;
    (*items)[*len].from_include = from_include;
    (*items)[*len].always_emit  = always_emit;
    (*items)[*len].file_path    = file_path;
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
                           rec->name_len, rec->owner_fn, rec->from_include,
                           rec->always_emit, rec->file_path, rec->defines_type);
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
static const char *enum_const_spelling(SerializeContext *ctx, Type *ty,
                                       const char *name);

// #1047: forward-declared here since serialize_global_var() (below) needs
// it but its definition, next to function_is_header_supplied() (the
// function-side counterpart it mirrors), comes much later in this file.
static bool global_is_header_supplied(VirtualMachine *vm, SerializeContext *ctx,
                                      Obj *obj);

// #1091: true when `ty` has no tag of its own but `cand` does -- spelling
// `ty` with `cand`'s tag would be wrong (a tagless aggregate is a distinct
// type from a same-shaped tagged one; C only lets same_type_or_origin's
// structural fallback treat them as interchangeable because it also backs
// the #1006/#1046 same-declaration-reparsed dedup, which never needs to
// distinguish the two). This can never reject a genuine origin-identity
// match: two Type objects sharing a pointer necessarily agree on
// struct_tag, since it's the same object -- so this only ever fires against
// a match same_type_or_origin() found through its structural fallback.
static bool tag_spelling_mismatch(Type *ty, Type *cand) {
    return ty && cand && !ty->struct_tag && cand->struct_tag;
}

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
                !tag_spelling_mismatch(ty, ctx->tags[i].ty) &&
                same_type_strong(ctx->tags[i].ty, ty))
                return &ctx->tags[i];

    for (int i = 0; i < ctx->tags_len; i++)
        if (name_visible(&ctx->tags[i], ctx->current_fn) &&
            !tag_spelling_mismatch(ty, ctx->tags[i].ty) &&
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

static TypeName *find_typedef_name_exact_vis(SerializeContext *ctx, Type *ty,
                                             bool require_visible);

static TypeName *find_typedef_name_exact(SerializeContext *ctx, Type *ty);

// #1091: two tagless typedefs of structurally-identical aggregates (e.g.
// `typedef struct { long quot, rem; } ldiv_t;` / `... lldiv_t;`, byte-
// identical on any 64-bit target CCCC's `long`/`long long` share a
// representation on) used to collapse into one spelling here, since the
// loop below only ever compared members. Try exact (pointer-identity, via
// the ->origin chain find_typedef_name_exact() already walks) first --
// resolves every ordinary reference to the typedef that actually declared
// it, including a return type's own Type -- and only fall back to the
// structural scan when identity finds nothing, which keeps every existing
// caller relying on the structural match (e.g. spelling a use-site copy
// with no traceable origin) working exactly as before.
static TypeName *find_typedef_name(SerializeContext *ctx, Type *ty) {
    if (!ctx || !ty)
        return NULL;

    TypeName *exact = find_typedef_name_exact(ctx, ty);
    if (exact)
        return exact;

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
static TypeName *find_typedef_name_exact_vis(SerializeContext *ctx, Type *ty,
                                             bool require_visible) {
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
                (!require_visible ||
                 name_visible(&ctx->typedefs[i], ctx->current_fn)))
                return &ctx->typedefs[i];
    return NULL;
}

static TypeName *find_typedef_name_exact(SerializeContext *ctx, Type *ty) {
    return find_typedef_name_exact_vis(ctx, ty, true);
}

// #1116: pointer-identity counterpart to find_tag_name()'s structural
// match, resolving whichever tag record names `ty` itself (via the same
// ->origin-chain walk find_typedef_name_exact_vis() uses). Deliberately
// UNFILTERED by name_visible(): nominal identity -- "which declaration
// owns this type" -- must not depend on which scope the pass asking
// happens to be emitting or collecting under. The global collect_obj_types()
// pre-pass runs with ctx->current_fn == NULL, so a visibility-filtered
// lookup sees no function-local record at all there, and each function's
// own emission pass sees only its own -- exactly the asymmetry that made
// the structural dedup below merge distinct types. Returns false when no
// record names `ty`; true otherwise, with *owner set to the record's
// owner_fn -- which may legitimately be NULL (file scope), so callers must
// branch on the return value, not the pointer.
static bool tag_record_owner_exact(SerializeContext *ctx, Type *ty,
                                   Obj **owner) {
    if (owner)
        *owner = NULL;
    if (!ctx || !ty)
        return false;
    for (int hop = 0; ty && hop < 8; ty = ty->origin, hop++)
        for (int i = 0; i < ctx->tags_len; i++)
            if (ctx->tags[i].ty == ty) {
                if (owner)
                    *owner = ctx->tags[i].owner_fn;
                return true;
            }
    return false;
}

// #1091: true when `a` and `b` are structurally identical (the caller has
// already checked same_type_or_origin) but each is its OWN tagless
// typedef -- e.g. `typedef struct { long quot, rem; } ldiv_t;` next to
// `typedef struct { long long quot, rem; } lldiv_t;`, byte-identical on any
// 64-bit target CCCC's `long`/`long long` share a representation on. Both
// need their own printed definition, not just the first one collected.
// A tagged struct/union is excluded: same_type_or_origin()'s own tag check
// already keeps two differently-tagged complete aggregates apart, so this
// only needs to cover the tagless case that check can't reach. Falls back
// to "not distinct" (today's behavior) whenever identity can't resolve
// BOTH sides to their own typedef record -- e.g. the #1006/#1046 same-
// declaration-reparsed case, which must keep deduping exactly as before.
static bool nominally_distinct_typedefs(SerializeContext *ctx, Type *a,
                                        Type *b) {
    if (!ctx || !a || !b || a == b)
        return false;
    // same_type_or_origin() may have matched `a` and `b` two different ways:
    // a genuine origin-identity match (one is a copy_type() descendant of
    // the other -- e.g. a per-declarator copy made while parsing a local
    // variable's type, which keeps its own struct_tag field untouched even
    // after rename_anon_globals() mutates the CANONICAL Type's struct_tag
    // in place), or a purely structural coincidence between two otherwise
    // unrelated Type objects. Only the latter is what this function needs
    // to catch -- an origin-identical pair is the same type by construction
    // and must never be treated as nominally distinct, regardless of which
    // copy happens to carry the tag right now.
    for (Type *pa = a; pa; pa = pa->origin)
        for (Type *pb = b; pb; pb = pb->origin)
            if (pa == pb)
                return false;
    // A tag identifies a type on its own; same_type_or_origin()'s own tag
    // check (#892) already keeps two DIFFERENTLY-tagged complete aggregates
    // from ever reaching here structurally-matched, so both sides tagged
    // means either the same tag (a genuine re-parse, #1006/#1046 -- keep
    // deduping) or this path wasn't reached at all. The combination that
    // check can't see is one side tagged and the other not: a tagless
    // aggregate is a distinct type from a same-shaped tagged one (#1091
    // symptom 3) and must never share its printed definition.
    //
    // #1116: EXCEPT when the same-tag pair is genuinely two declarations --
    // a function-local `struct Point { ...; };` shadows a same-shaped
    // file-scope `struct Point` (C11 6.2.1p4), and two sibling functions'
    // locals shadow each other just as well. Both sides complete and both
    // resolvable to their own tag record but with different owner_fn means
    // different declaration scopes, i.e. distinct types that must each get
    // their own ctx->defs entry (the file-scope one emitted by the file-
    // scope pass, each local inside its own function -- legal shadowed
    // redefinition). Same owner on both sides covers every dedup contract
    // this check exists to protect: the comptime re-parse (#1006/#1046)
    // re-records at the same scope, header-supplied types record at file
    // scope (owner NULL), and #989's hoist_local_type_to_file_scope()
    // rewrites its capture type's records to owner NULL precisely so the
    // file-scope pass owns them.
    if (type_is_complete_tagged(a) && type_is_complete_tagged(b)) {
        Obj *oa = NULL, *ob = NULL;
        bool fa = tag_record_owner_exact(ctx, a, &oa);
        bool fb = tag_record_owner_exact(ctx, b, &ob);
        if (fa && fb && oa != ob)
            return true;
    }
    if (a->struct_tag && b->struct_tag)
        return false;
    if (a->struct_tag || b->struct_tag)
        return true;

    // #1116: resolve both sides through the UNFILTERED exact lookup (not
    // find_typedef_name_exact's name_visible gate). The global collection
    // pass runs with current_fn == NULL, where NO function-local alias is
    // visible, and each function's own emission pass sees only its own --
    // under either of those, one side's record always came back NULL here,
    // the `!na || !nb` fallback then declared the pair "not distinct", and
    // the second function's anonymous-aggregate typedef silently merged
    // into the first one's (TdSize vs TdComp_Rectangle in
    // test_suite_typesystem.c): its body was never collected into
    // ctx->defs, and emit_typedef_and_deps' emitted_defs nominal check
    // suppressed the combined `typedef struct {...} Name;` line too.
    TypeName *na = find_typedef_name_exact_vis(ctx, a, false);
    TypeName *nb = find_typedef_name_exact_vis(ctx, b, false);
    if (!na || !nb)
        return false;
    return na->name_len != nb->name_len ||
           strncmp(na->name, nb->name, na->name_len) != 0;
}

// #1091: type_vec_contains()/type_vec_find()/type_vec_push(), but a
// structural match against a nominally-distinct tagless typedef (see just
// above) doesn't count as "already present" -- used only at the two sites
// that decide whether a struct/union/enum DEFINITION gets printed
// (collect_type()'s ctx->seen/ctx->defs, and emit_typedef_and_deps()'s
// ctx->emitted_defs), never at the many other type_vec_* call sites, whose
// looser structural dedup (#1006/#1046) must stay untouched.
static bool type_vec_contains_nominal(SerializeContext *ctx, TypeVec *vec,
                                      Type *ty) {
    for (int i = 0; i < vec->len; i++) {
        if (!same_type_or_origin(vec->data[i], ty))
            continue;
        if (nominally_distinct_typedefs(ctx, vec->data[i], ty))
            continue;
        return true;
    }
    return false;
}

static int type_vec_find_nominal(SerializeContext *ctx, TypeVec *vec,
                                 Type *ty) {
    for (int i = 0; i < vec->len; i++) {
        if (!same_type_or_origin(vec->data[i], ty))
            continue;
        if (nominally_distinct_typedefs(ctx, vec->data[i], ty))
            continue;
        return i;
    }
    return -1;
}

static void type_vec_push_nominal(SerializeContext *ctx, TypeVec *vec,
                                  Type *ty) {
    if (!ty || type_vec_contains_nominal(ctx, vec, ty))
        return;

    if (vec->len >= vec->cap) {
        vec->cap  = vec->cap ? vec->cap * 2 : 16;
        vec->data = realloc(vec->data, sizeof(Type *) * vec->cap);
    }
    vec->data[vec->len++] = ty;
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
    // #1116: resolve `ty`'s own record by pointer identity first. Once
    // nominally_distinct_typedefs() splits two structurally identical
    // aggregates into separate ctx->defs entries, the structural scan below
    // can no longer tell them apart -- whichever record comes first in
    // ctx->tags/ctx->typedefs order wins, returning the OTHER declaration's
    // owner and making serialize_type_defs_for_owner() skip this entry in
    // its own function's pass ("owner mismatch") even though it was just
    // split out precisely so that pass could emit it. The identity match is
    // strictly more precise; the structural fallback keeps every pre-#1116
    // caller (use-site copies with no traceable record of their own)
    // behaving exactly as before.
    Type *orig = ty;
    for (int hop = 0; ty && hop < 8; ty = ty->origin, hop++) {
        for (int i = 0; i < ctx->tags_len; i++)
            if (ctx->tags[i].ty == ty)
                return ctx->tags[i].owner_fn;
        for (int i = 0; i < ctx->typedefs_len; i++)
            if (ctx->typedefs[i].ty == ty)
                return ctx->typedefs[i].owner_fn;
    }
    ty = orig;
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

    // #1091: type_vec_find_nominal() (not the plain, purely-structural
    // type_vec_find()) so a tagless typedef that's structurally identical
    // to -- but nominally distinct from -- an already-collected one (e.g.
    // ldiv_t/lldiv_t) isn't treated as already seen here; it falls through
    // to its own seen/defs entry below instead, exactly as if it were an
    // unrelated shape.
    int seen_idx = type_vec_find_nominal(ctx, &ctx->seen, ty);
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
    type_vec_push_nominal(ctx, &ctx->seen, ty);

    for (Member *m = ty->members; m; m = m->next)
        collect_type(ctx, m->ty);

    type_vec_push_nominal(ctx, &ctx->defs, ty);
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

// #1042(a): ctx->defs is populated in first-collected order -- a post-order
// walk per collect_type()'s member recursion -- which is a legal declaration
// order UNLESS a struct/union's own body is first reached only through an
// early POINTER reference (e.g. a function-pointer typedef parameter, the
// minilua repro's `lua_CFunction` == `int (*)(struct lua_State *)`), pushing
// its (then-incomplete) Type into ctx->defs at that early position, and a
// BY-VALUE member of some other struct collected in between then needs the
// full body before it's available. The #1010 swap in collect_type() above
// repromotes the incomplete entry to the complete Type once collection
// reaches it, but re-pushes at the TAIL of ctx->defs, after every entry
// collected since -- including a later by-value user of the same type, e.g.
// minilua's `struct LX { ...; struct lua_State l; };`, which still lands
// ahead of `struct lua_State`'s own body. serialize_type_defs_for_owner()
// just walks ctx->defs in order, so the by-value user prints first and the
// host compiler sees "field has incomplete type".
//
// Fixed with a stable topological reorder pass over ctx->defs, run once
// after collection: an edge M -> E for every struct/union def E and every
// by-value (non-pointer) member of E whose own type resolves to another
// entry M. Kahn's algorithm, always picking the lowest-index ready node, so
// an entry only moves when an edge actually forces it -- output stays
// byte-identical wherever the existing order was already legal.
static bool member_needs_own_def(Type *mty) {
    while (mty && mty->kind == TY_ARRAY)
        mty = mty->base;
    return mty && (mty->kind == TY_STRUCT || mty->kind == TY_UNION);
}

// Resolve a by-value member's type to its ctx->defs index. same_type_or_
// origin() deliberately treats an incomplete tagged aggregate as equal to
// the complete one sharing its tag (#892/#1010) -- prefer a match passing
// type_is_complete_tagged() (an incomplete entry prints a bare `struct X;`
// and constrains nothing, so it's not a useful edge target); if only an
// incomplete match exists, report no edge at all.
static int find_complete_def_index(TypeVec *defs, Type *ty) {
    for (int i = 0; i < defs->len; i++)
        if (same_type_or_origin(defs->data[i], ty) &&
            type_is_complete_tagged(defs->data[i]))
            return i;
    return -1;
}

// Records one M -> container_idx edge per by-value member, recursing through
// a TAGLESS member's own members instead of stopping: a tagless aggregate
// has no standalone ctx->defs entry serialize_type_defs_for_owner() ever
// prints (it's inlined at its use site), so ITS by-value members are the
// real ordering constraint on `container_idx`, not the tagless type itself.
// A tagless struct can't legally embed itself by value (infinite size), so
// this recursion can't cycle.
static void collect_byval_edges(SerializeContext *ctx, Type *mty, TypeVec *defs,
                                bool *depends, int n, int container_idx) {
    while (mty && mty->kind == TY_ARRAY)
        mty = mty->base;
    if (!mty || (mty->kind != TY_STRUCT && mty->kind != TY_UNION))
        return;

    bool tagged = find_tag_name(ctx, mty) || find_typedef_name(ctx, mty) ||
                  find_anonymous_typedef_name(ctx, mty);
    if (!tagged) {
        for (Member *mm = mty->members; mm; mm = mm->next)
            collect_byval_edges(ctx, mm->ty, defs, depends, n, container_idx);
        return;
    }

    int target = find_complete_def_index(defs, mty);
    if (target < 0 || target == container_idx)
        return;
    depends[container_idx * n + target] = true;
}

static void reorder_defs_by_byval_deps(SerializeContext *ctx) {
    TypeVec *defs = &ctx->defs;
    int      n    = defs->len;
    if (n < 2)
        return;

    bool *depends = calloc((size_t)n * (size_t)n, sizeof(bool));
    if (!depends)
        return;

    for (int i = 0; i < n; i++) {
        Type *ty = defs->data[i];
        if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
            continue; // enums have no by-value members to order against
        for (Member *m = ty->members; m; m = m->next)
            if (member_needs_own_def(m->ty))
                collect_byval_edges(ctx, m->ty, defs, depends, n, i);
    }

    int   *indeg = calloc((size_t)n, sizeof(int));
    bool  *done  = calloc((size_t)n, sizeof(bool));
    Type **order = malloc(sizeof(Type *) * (size_t)n);
    if (!indeg || !done || !order) {
        free(depends);
        free(indeg);
        free(done);
        free(order);
        return;
    }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (depends[i * n + j])
                indeg[i]++;

    int placed = 0;
    while (placed < n) {
        int pick = -1;
        for (int i = 0; i < n; i++) {
            if (!done[i] && indeg[i] == 0) {
                pick = i;
                break;
            }
        }
        if (pick < 0) {
            // By-value aggregate nesting can't legally cycle in valid C --
            // fail-soft rather than lose a definition if it somehow does:
            // append whatever remains in its existing relative order.
            for (int i = 0; i < n; i++)
                if (!done[i]) {
                    order[placed++] = defs->data[i];
                    done[i]         = true;
                }
            break;
        }
        order[placed++] = defs->data[pick];
        done[pick]      = true;
        for (int i = 0; i < n; i++)
            if (!done[i] && depends[i * n + pick])
                indeg[i]--;
    }

    memcpy(defs->data, order, sizeof(Type *) * (size_t)n);
    free(order);
    free(depends);
    free(indeg);
    free(done);
}

// #956: -c=generated support -- tracks which macro-generated functions
// already have a prototype in the output (either from a preceding
// forward-declare or their own definition), so a function body that
// references another generated function whose own emit event hasn't been
// reached yet can have that callee's prototype inserted just ahead of it.
typedef struct {
    Obj **data;
    int   len;
    int   cap;
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
        vec->cap  = vec->cap ? vec->cap * 2 : 8;
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
// #1031: defined near serialize_type_defs_for_owner (which shares
// type_layout_is_host_owned's own from_include-suppression logic via
// type_def_is_from_include_suppressed), forward-declared here so
// serialize_expr's ND_NUM case -- much earlier in this file -- can call
// them.
static bool type_layout_is_host_owned(SerializeContext *ctx, Type *ty,
                                      int depth);
static bool type_has_printable_name(SerializeContext *ctx, Type *ty);
// #1095: factored out of serialize_expr's own ND_NUM arm below so array
// dimensions/case labels/enum values (none of which have a Node to walk by
// the time serialization runs -- see const_expr_layout(), parse_analysis.c)
// can share the exact same host-owned/printable-name gate rather than a
// parallel copy. Prints "sizeof(T)"/"_Alignof(T)" and returns true when
// `layout_ty` qualifies; otherwise prints nothing and returns false, so
// every caller's own fallback (the plain folded literal) still applies.
static bool serialize_layout_const(FILE *f, SerializeContext *ctx,
                                   Type *layout_ty, bool is_align);
// #1098: forward-declared for the same reason as the two above -- defined
// near type_layout_is_host_owned (which expr_has_host_owned_layout calls),
// but serialize_stmt's ND_BLOCK case, much earlier in this file, needs to
// call serialize_static_assert.
static bool expr_has_host_owned_layout(SerializeContext *ctx, Node *node,
                                       int depth);
static void serialize_static_assert(FILE *f, VirtualMachine *vm,
                                    SerializeContext *ctx, Node *cond,
                                    const char *msg, int msg_len, Token *tok,
                                    int indent);

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
    if (n > 0 && (size_t)n < cap && !strpbrk(buf, ".eEnN")) {
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
    if (n > 0 && (size_t)n < cap && !strpbrk(buf, ".eEnN")) {
        snprintf(buf + n, cap - (size_t)n, ".0");
    }
}

// #1121: decode a wb/uwb literal's full-precision digit text (base 2/8/10/16,
// tokenize.c) into a 128-bit value. Mirrors __cccc_bitint_from_str's
// shift-and-add algorithm (wide_bitint.c, the VM's own runtime copy of this
// same conversion) but stays host-side and width-capped at 128 bits, since
// this compiler's own build is a real host toolchain with unsigned __int128
// available -- serialize_type() (case TY_BITINT above) has already refused
// any type wider than that before an expression of that type can reach here.
static unsigned __int128 decode_wide_digits(const char *digits, int base) {
    unsigned __int128 v = 0;
    for (const char *p = digits; *p; p++) {
        if (*p == '\'')
            continue; // digit separator
        int  digit;
        char c = *p;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            break;
        if (digit >= base)
            break;
        v = v * (unsigned __int128)base + (unsigned __int128)digit;
    }
    return v;
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
                            : ty->checked_kind == CHECKED_ARRAY ? "array"
                                                                : "ntarray";
    int         n         = snprintf(buf, cap, " [[cccc::%s]]", kind_name);
    if (n < 0 || (size_t)n >= cap)
        return;
    switch (ty->checked_bounds_form) {
        case CB_COUNT:
        case CB_BYTE_COUNT:
            if (ty->checked_bounds_arg1)
                snprintf(buf + n, cap - (size_t)n, " [[cccc::%s(%.*s)]]",
                         ty->checked_bounds_form == CB_COUNT ? "count"
                                                             : "byte_count",
                         ty->checked_bounds_arg1->len,
                         ty->checked_bounds_arg1->loc);
            break;
        case CB_RANGE:
            if (ty->checked_bounds_arg1 && ty->checked_bounds_arg2)
                snprintf(
                    buf + n, cap - (size_t)n, " [[cccc::bounds(%.*s, %.*s)]]",
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
        // #1095: re-materialize the dimension as "sizeof(T)"/"_Alignof(T)"
        // rather than the folded int, but only when the caller has opted
        // in (ctx->allow_layout_dims -- see its own comment) and this
        // dimension really was such a fold (array_len_layout_ty, set by
        // array_dimensions(), parse_types.c). serialize_layout_const()
        // itself still declines (returns false, prints nothing) when the
        // type isn't actually from_include-suppressed or has no printable
        // name, so this falls through to the plain folded literal exactly
        // as before whenever re-materializing wouldn't be sound/possible.
        char  *dimbuf       = NULL;
        size_t dimsz        = 0;
        bool   wrote_layout = false;
        if (ty->array_len >= 0 && ctx->allow_layout_dims &&
            ty->array_len_layout_ty) {
            FILE *df = open_memstream(&dimbuf, &dimsz);
            wrote_layout =
                serialize_layout_const(df, ctx, ty->array_len_layout_ty,
                                       ty->array_len_layout_is_align);
            fclose(df);
        }
        if (ty->array_len < 0)
            snprintf(buf, sizeof(buf), "%s[]", name ? name : "");
        else if (wrote_layout)
            snprintf(buf, sizeof(buf), "%s[%s]", name ? name : "", dimbuf);
        else
            snprintf(buf, sizeof(buf), "%s[%d]", name ? name : "",
                     ty->array_len);
        free(dimbuf);
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
        char  *lenbuf = NULL;
        size_t lensz  = 0;
        FILE  *lf     = open_memstream(&lenbuf, &lensz);
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
        if (ty->base && (ty->base->kind == TY_ARRAY ||
                         ty->base->kind == TY_VLA || ty->base->kind == TY_FUNC))
            snprintf(buf, sizeof(buf), "(*%s%s%s)", qual, sep,
                     name ? name : "");
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
    while (ty &&
           (ty->kind == TY_ARRAY || ty->kind == TY_PTR || ty->kind == TY_VLA))
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

// #1109: is this alias the one bundled-header scalar typedef whose host
// meaning diverges structurally -- `atomic_flag`? CCCC's stdatomic.h spells
// it as an integer-flavoured `typedef _Atomic _Bool atomic_flag;`, while a
// real host <stdatomic.h> follows C11 7.17 and makes it a struct, so any
// generated-C use resolved through the host header is a type error (see the
// #1109 comment in serialize_type). Matched by name AND shape (kind +
// is_atomic): a user's own unrelated typedef that happens to reuse the name,
// or an alias to any other underlying type, keeps its normal spelling.
static bool is_host_divergent_atomic_flag_alias(Type *ty, TypeName *alias) {
    return ty && ty->kind == TY_BOOL && ty->is_atomic && alias &&
           alias->name_len == 11 &&
           strncmp(alias->name, "atomic_flag", 11) == 0;
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
    bool suppress_ptr_const =
        ty->kind == TY_PTR && !find_typedef_name_exact(ctx, ty);
    if (ty->is_const && !suppress_ptr_const)
        fprintf(f, "const ");

    // Deliberately no output for ty->checked_kind (#770/#482-484): a
    // checked pointer's [[cccc::single/array/ntarray]] qualifier is a
    // cccc-internal VM-side check, not a real C construct -- gcc/clang would
    // reject the attribute names outright, and #488 requires -E/-c=generated
    // native output to be unchanged for a checked declaration ("no change to
    // ABI or to unchecked callers"). Falls out for free today since this
    // function only ever emits is_const anyway (is_volatile/is_restrict are
    // likewise never serialized), but noted explicitly so it isn't "fixed" by a
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
            // #1109: atomic_flag is the one bundled-header typedef whose
            // host spelling diverges structurally -- C11 7.17 makes it a
            // *struct* type (macOS SDK/glibc: `typedef struct atomic_flag
            // atomic_flag;`) while CCCC's own stdatomic.h spells it as an
            // integer-flavoured `_Atomic _Bool`. The generated C re-includes
            // <stdatomic.h>, so whichever header the HOST compiler resolves
            // decides: with CCCC's own (the -I./include the native harness
            // always passes) everything compiles, but against a real host
            // header every integer-style use dies ("used type 'atomic_flag'
            // (aka 'struct atomic_flag') where arithmetic or pointer type is
            // required"). Spell the canonical `_Atomic _Bool` instead --
            // valid C11 on every host, needs no header at all, and denotes
            // exactly the type the VM modelled, so output compiled through
            // either header is unaffected. Scoped to this one name + shape:
            // every other atomic_* typedef means the same _Atomic-qualified
            // scalar on both sides, so their aliases keep printing (and
            // `atomic_bool`, whose host spelling agrees with CCCC's, is
            // deliberately left alone).
            if (!is_host_divergent_atomic_flag_alias(ty, alias)) {
                fprintf(f, "%.*s", alias->name_len, alias->name);
                return;
            }
            fprintf(f, "_Atomic ");
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
            // ty_ldcomplex in type.c), so the spelling falls out of it
            // directly.
            fprintf(f, "_Complex ");
            serialize_type(f, ctx, ty->base);
            break;
        case TY_VECTOR:
            // GNU vector: element type + vector_size in *bytes* (ty->size is
            // the total, which is what vector_size takes -- not vec_len). clang
            // and gcc both accept the attribute in this position.
            serialize_type(f, ctx, ty->base);
            fprintf(f, " __attribute__((vector_size(%d)))", ty->size);
            break;
        case TY_STRUCT: {
            TypeName *tag   = find_tag_name(ctx, ty);
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
            TypeName *tag   = find_tag_name(ctx, ty);
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
            TypeName *tag   = find_tag_name(ctx, ty);
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
            // Emit as the underlying container integer type. bitint_type()
            // (type.c) rounds bit_width up to whole 8-byte words, so
            // size==16 covers every width in (64, 128] -- __int128/unsigned
            // __int128 is supported by clang and gcc on every host this
            // project targets (macOS/Linux x aarch64/x86_64), including
            // under the strict -std=cNN native_resolve_std_ladder passes,
            // and round-trips back through this compiler's own parser
            // (DK_INT128, parse_types.c) for -c=bytecode re-serialization.
            // #1121: previously fell into the size==8 "long" arm below,
            // silently truncating every 128-bit value/operation to 64 bits.
            if (ty->size == 1)
                fprintf(f, ty->is_unsigned ? "unsigned char" : "signed char");
            else if (ty->size == 2)
                fprintf(f, ty->is_unsigned ? "unsigned short" : "short");
            else if (ty->size == 4)
                fprintf(f, ty->is_unsigned ? "unsigned int" : "int");
            else if (ty->size == 8)
                fprintf(f, ty->is_unsigned ? "unsigned long" : "long");
            else if (ty->size == 16)
                fprintf(f, ty->is_unsigned ? "unsigned __int128" : "__int128");
            else
                // #1121: no multi-word lowering exists for _BitInt(N>128) in
                // the native/-m serializer (only the VM's address-based
                // wide_bitint.c path handles it) -- refuse loudly per the
                // no-lossy-emulation policy (#824) rather than silently
                // truncating to a container that cannot hold the value.
                // Implementing a real lowering is deferred to #1123. No
                // Token is available at a bare type-emission site (unlike
                // error_tok's usual call sites), so this can't go through
                // the batched cc_print_all_errors() summary path -- the
                // trailer is appended by hand to match the "N error(s)
                // generated." phrasing the test harness's compile-error
                // heuristic (tools/testing/runner.py) already scans for.
                error("cccc: _BitInt(%d) exceeds 128 bits, which has no "
                      "native/-m lowering (VM and -c=bytecode only)\n\n1 "
                      "error generated.",
                      ty->bit_width);
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
            error(
                "cccc: internal error: TypeKind '%s' reached the serializer "
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
                  "(kind %d)",
                  cc_type_kind_name(ty->kind), ty->kind);
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
static bool serialize_block_capture_ref(FILE *f, SerializeContext *ctx,
                                        Obj *var) {
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
    Obj  *p = from_fn->parent_fn;
    char *expr =
        arena_format(vm, "((%s *)__static_link)", find_nested_env_name(ctx, p));
    while (p != target) {
        p    = p->parent_fn;
        expr = arena_format(vm, "((%s *)%s->__up)",
                            find_nested_env_name(ctx, p), expr);
    }
    return expr;
}

// #1081: builds a C expression, of type `<block_anc's own descriptor
// struct> *`, giving block_anc's real descriptor pointer as seen from
// inside from_fn's own serialized body. from_fn must be nested and
// block_anc one of its ancestors that is a genuine Apple block
// (Obj.is_block) not directly owning the variable being reached (see
// serialize_nested_upvar_ref()'s own climb). Reuses nested_env_ptr_expr's
// existing chase up to block_anc's own env struct -- populated exactly
// like any other nested function's env, purely to pass block_anc's own
// directly-owned locals/params to ITS OWN nested children (e.g. a nested
// function reading the block's own param, the already-correct single-hop
// case) -- and adds one more `.__up` hop: serialize_function's populator
// sets a nested function's own env's `.__up` field to its own
// `__static_link` verbatim (`fn->is_nested ? "__static_link" : "(void
// *)0"`), and block_anc's REAL `__static_link` IS its descriptor pointer
// (ND_BLOCK_CALL always passes the descriptor as its callee's A0/
// __static_link) -- so this single extra hop reaches it exactly, whatever
// its own env's `.__up` field looks like is irrelevant here.
static char *block_ancestor_desc_ptr_expr(VirtualMachine   *vm,
                                          SerializeContext *ctx, Obj *from_fn,
                                          Obj *block_anc) {
    const char *env = find_block_env(ctx, block_anc);
    if (!env)
        env = "struct __cccc_block_env_?"; // defensive only, see find_block_env
    return arena_format(vm, "((%s *)%s->__up)", env,
                        nested_env_ptr_expr(vm, ctx, from_fn, block_anc));
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
    if (!ctx->current_fn || !ctx->current_fn->is_nested ||
        ctx->current_fn->is_block)
        return false;
    for (Obj *v = ctx->current_fn->locals; v; v = v->next)
        if (v == var)
            return false; // owned by the nested function itself, not an upvar
    Obj *owner       = NULL;
    Obj *block_owner = NULL;
    for (Obj *anc = ctx->current_fn->parent_fn; anc; anc = anc->parent_fn) {
        bool is_own = false;
        for (Obj *v = anc->locals; v; v = v->next)
            if (v == var) {
                is_own = true;
                break;
            }
        if (is_own) {
            owner = anc;
            break;
        }
        // #1081: `anc` doesn't own `var` directly -- if `anc` is a block,
        // it's a hard boundary (see record_nested_upvar()'s identical
        // climb): `var` is owned by one of THAT BLOCK's own ancestors, but
        // was already captured transitively into the block's own
        // descriptor at parse time, and must be read from there rather
        // than by continuing this climb toward the real, further-out
        // owner.
        if (anc->is_block) {
            block_owner = anc;
            break;
        }
    }
    if (block_owner) {
        int idx = block_capture_index(block_owner, var);
        if (idx < 0)
            return false; // defensive only -- record_nested_upvar() already
                          // validated this at parse-adjacent analysis time
        char *desc_ptr =
            block_ancestor_desc_ptr_expr(vm, ctx, ctx->current_fn, block_owner);
        // Mirrors serialize_block_capture_ref()'s identical is_block_var
        // arm: the descriptor's own capture slot holds the shared heap
        // box's pointer for a __block local, one extra dereference to
        // reach the value.
        if (var->is_block_var)
            fprintf(f, "(*%s->__cap%d)", desc_ptr, idx);
        else
            fprintf(f, "%s->__cap%d", desc_ptr, idx);
        return true;
    }
    if (!owner)
        return false; // defensive only -- see the identical scan below
    NestedEnvEntry *e = NULL;
    for (int i = 0; i < ctx->nested_envs_len; i++)
        if (ctx->nested_envs[i].owner_fn == owner) {
            e = &ctx->nested_envs[i];
            break;
        }
    if (!e)
        return false; // defensive only -- owner must already have an entry
    int idx = -1;
    for (int i = 0; i < e->upvars_len; i++)
        if (e->upvars[i] == var) {
            idx = i;
            break;
        }
    if (idx < 0)
        return false; // defensive only

    // #1080: a __block-storage upvar's field holds a T ** (address of the
    // shared heap-box pointer) -- one more dereference than an ordinary
    // upvar's T * to reach the actual value, mirroring
    // serialize_block_capture_ref()'s identical is_block_var arm.
    char *env_ptr = nested_env_ptr_expr(vm, ctx, ctx->current_fn, owner);
    if (var->is_block_var)
        fprintf(f, "(**%s->__uv%d)", env_ptr, idx);
    else
        fprintf(f, "(*%s->__uv%d)", env_ptr, idx);
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
    return base->size == 1 || base->size == 2 || base->size == 4 ||
           base->size == 8;
}

// #1101: an address operand handed to a __atomic_* builtin must not carry
// the _Atomic qualifier in its static type -- clang rejects
// `__atomic_store_n(&x, ...)` outright when x is spelled through a host
// atomic typedef ("address argument to atomic operation must be a pointer
// to integer or pointer ('atomic_int *' (aka '_Atomic(int) *') invalid)"),
// because declarations print through those typedef names (atomic_int -> the
// real <stdatomic.h> _Atomic int) and so &x has type _Atomic(int)*. The
// builtins' own contract wants a pointer to the *unqualified* pointee --
// atomicity lives in the object, not the argument's spelling, exactly as
// the hand-written __cccc_ensure_mtx/__cccc_ensure_cnd shims below already
// assume when they cast once_flag/mtx->__handle for their own __atomic_*
// calls. So when the operand is a pointer to an _Atomic-qualified pointee,
// emit a cast to that pointee with the qualifier stripped: a shallow copy
// of the pointee Type with origin severed, so find_typedef_name_exact
// cannot re-spell the atomic typedef by identity and the canonical spelling
// ("int", "long", "_Bool", "void *"...) falls out instead. Anything else
// serializes byte-identical to before.
static void serialize_atomic_addr(FILE *f, VirtualMachine *vm,
                                  SerializeContext *ctx, Node *addr) {
    if (!addr || !addr->ty || addr->ty->kind != TY_PTR || !addr->ty->base ||
        !addr->ty->base->is_atomic) {
        serialize_expr(f, vm, ctx, addr, 2);
        return;
    }
    Type pointee      = *addr->ty->base;
    pointee.is_atomic = false;
    pointee.origin    = NULL;
    fprintf(f, "(");
    serialize_type(f, ctx, &pointee);
    fprintf(f, " *)");
    // Same comma-guarded argument position the bare operand occupied
    // before (#1042(b)).
    serialize_expr(f, vm, ctx, addr, 2);
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

// #1102: the rightmost operand of a comma chain, drilling through nested
// ND_COMMA nodes (an initializer's balanced-comma tree nests both ways, but
// the compound literal's hidden temp always sits on the right spine).
static Node *comma_chain_tail(Node *node) {
    while (node && node->kind == ND_COMMA && node->rhs)
        node = node->rhs;
    return node;
}

// #1102: is this chain's tail something whose address C allows taking? A
// plain variable, a member access, or a dereference -- exactly the lvalue
// shapes a lowered expression can end in. Anything else (a number, a binary
// result, another rvalue) falls back to the generic spelling.
static bool addr_comma_tail_is_lvalue(Node *chain) {
    Node *tail = comma_chain_tail(chain);
    return tail && (tail->kind == ND_VAR || tail->kind == ND_MEMBER ||
                    tail->kind == ND_DEREF);
}

// #1102 followup: drill through a postfix lvalue shell -- member accesses,
// plus the explicit ND_DEREF that `->` lowers to -- down to whatever sits
// at its base (`&((struct P){30, 12}).x` is ADDR(MEMBER(COMMA))). Returns
// that base only when it is a comma chain; NULL otherwise, leaving every
// other shape to the generic spelling.
static Node *addr_comma_base(Node *shell) {
    Node *n = shell;
    while (n && (n->kind == ND_MEMBER || n->kind == ND_DEREF))
        n = n->lhs;
    return n && n->kind == ND_COMMA ? n : NULL;
}

// #1102 followup: spell a MEMBER/DEREF shell exactly as serialize_expr()
// would, except that the comma chain at its bottom -- already emitted
// earlier in the surrounding `(chain..., ...)` sequence -- is replaced by
// the bare tail operand. This is what lets the & bind *inside* the chain
// while still covering every `.member`/`*` step above it:
// `&(memset(...) , t, t).x` re-spells as `(memset(...) , &t.x)`. Precedence
// bookkeeping mirrors serialize_expr's own (MEMBER 15 / DEREF 14), so a
// deref under a member access keeps its parentheses -- `->` lowers to
// MEMBER(DEREF(base)), which must come out `(*t).m`, not `*t.m`.
static void serialize_addr_shell(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Node *n, Node *base,
                                 Node *tail, int parent_prec) {
    if (!n || n == base) {
        serialize_expr(f, vm, ctx, n == base ? tail : n, parent_prec);
        return;
    }
    int  node_prec   = get_precedence(n->kind);
    bool need_parens = node_prec < parent_prec;
    if (need_parens)
        fprintf(f, "(");
    if (n->kind == ND_MEMBER) {
        serialize_addr_shell(f, vm, ctx, n->lhs, base, tail, node_prec);
        if (n->member && n->member->name)
            fprintf(f, ".%.*s", n->member->name->len, n->member->name->loc);
        else if (!n->member)
            // Same unresolved-member placeholder ND_MEMBER itself emits.
            fprintf(f, "./* unknown */");
        // else: an anonymous struct/union member -- transparent in C, no
        // spelling of its own (see ND_MEMBER's case for both).
    } else { // ND_DEREF -- the only other kind addr_comma_base() accepts
        fprintf(f, "*");
        serialize_addr_shell(f, vm, ctx, n->lhs, base, tail, node_prec);
    }
    if (need_parens)
        fprintf(f, ")");
}

// #1102: serialize `&(A, B, x)` as `(A, B, &x)` -- see the ND_ADDR case in
// serialize_expr(). Flattens nested comma levels into one parenthesized
// sequence (the comma operator is associative, so grouping is free --
// balanced_comma() in parse_init.c relies on the same fact), binds the &
// to the rightmost operand, and leaves every other operand to the ordinary
// serializer. `end` is the pre-computed rightmost operand -- compared by
// pointer identity so the &, and only the &, lands there: an initializer's
// balanced-comma tree terminates on *both* sides, and spelling `&` at the
// first non-comma child would bind it to a mid-chain assignment instead.
// #1102 followup: `wrap` is the addressed expression *above* the chain's
// tail -- a MEMBER/DEREF shell such as `.x` from `&((struct P){30, 12}).x`
// -- spelled by serialize_addr_shell() with the chain itself elided. The
// caller emits the enclosing parentheses unconditionally: a bare comma
// chain must never leak into a context that saw `&(...)`'s precedence
// (e.g. `p = &(cl)` would otherwise re-parse as `(p = a), b`).
static void serialize_addr_comma(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Node *comma, Node *end,
                                 Node *wrap) {
    // Left side: flatten nested comma chains into the same sequence. The
    // tail can only sit on the right spine, so lhs never gets the &.
    if (comma->lhs && comma->lhs->kind == ND_COMMA &&
        !is_noop_expr(comma->lhs->lhs) && !is_noop_expr(comma->lhs->rhs))
        serialize_addr_comma(f, vm, ctx, comma->lhs, NULL, NULL);
    else
        // Argument-separator precedence (2), same guard ND_FUNCALL's
        // argument loop uses (#1042(b)) -- keeps a top-level `=` or `,`
        // inside the chain from leaking.
        serialize_expr(f, vm, ctx, comma->lhs, 2);
    fprintf(f, " , ");
    if (comma->rhs == end) {
        fprintf(f, "&");
        serialize_addr_shell(f, vm, ctx, wrap, comma, end,
                             get_precedence(ND_ADDR) + 1);
    } else if (comma->rhs && comma->rhs->kind == ND_COMMA) {
        serialize_addr_comma(f, vm, ctx, comma->rhs, end, wrap);
    } else {
        // A flattened left branch's own last element: plain spelling.
        serialize_expr(f, vm, ctx, comma->rhs, 2);
    }
}

// #1102: spell the synthesized byte-offset cast-back -- the leading `(T *)`
// of `((T *)((char *)ptr + off))` from #918's pointer-arithmetic spelling --
// with any element/pointee qualifier stripped. A const-qualified element
// (`const int arr[3]`) makes usual_arith_conv's decay cast and this
// cast-back print `(const int *)`, and clang rejects ANY store through a
// statically-qualified lvalue ("read-only variable is not assignable") even
// though every explicit cast in the chain legally strips the qualifier and
// the underlying object is genuinely mutable -- an aggregate initializer's
// assignment sequence, whose declaration hoist_mutable_type() has already
// de-qualified (#1029/#1102). Reads are unaffected by the missing
// qualifier, so strip whenever present; anything unqualified keeps its
// exact previous spelling. Shallow copy with origin severed so
// find_typedef_name_exact() cannot re-spell a qualified typedef alias
// (same trick serialize_atomic_addr()/#1101 uses).
static void serialize_offset_cast_type(FILE *f, VirtualMachine *vm,
                                       SerializeContext *ctx, Type *ty) {
    bool qualified =
        ty && ty->base &&
        (ty->base->is_const || ty->base->is_volatile || ty->base->is_atomic);
    fprintf(f, "(");
    if (!qualified) {
        if (ty && (ty->kind == TY_ARRAY || ty->kind == TY_VLA)) {
            serialize_type(f, ctx, ty->base);
            fprintf(f, " *");
        } else {
            serialize_type(f, ctx, ty);
        }
    } else {
        // Strip the qualifiers onto a fresh unqualified pointee, then
        // re-spell a plain pointer TO it through serialize_type() rather
        // than printing `pointee *` by hand -- the pointee may itself be
        // a pointer (an array of const-qualified function pointers, e.g.
        // `static int (*const table[])(void)`), where naive concatenation
        // produces the unparseable `(int (*)(void) *)`.
        Type pointee        = *ty->base;
        pointee.is_const    = false;
        pointee.is_volatile = false;
        pointee.is_atomic   = false;
        pointee.origin      = NULL;
        Type ptr            = {0};
        ptr.kind            = TY_PTR;
        ptr.size            = 8;
        ptr.align           = 8;
        ptr.is_unsigned     = true;
        ptr.base            = &pointee;
        serialize_type(f, ctx, &ptr);
    }
    fprintf(f, ")");
}

// Print indentation
static void print_indent_level(FILE *f, int indent) {
    for (int i = 0; i < indent; i++)
        fprintf(f, "    ");
}

// Serialize an expression
static void serialize_expr_raw(FILE *f, VirtualMachine *vm,
                               SerializeContext *ctx, Node *node,
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

    int  node_prec   = get_precedence(node->kind);
    bool need_parens = (node_prec < parent_prec);

    if (need_parens)
        fprintf(f, "(");

    switch (node->kind) {
        case ND_NUM:
            if (node->ty && is_decimal(node->ty)) {
                // #402: node->fval/val are never populated for a decimal
                // literal (see tokenize.c) -- dec_digits plus the
                // width-appropriate suffix is the only way to round-trip it
                // back to valid C source.
                const char *suffix = dec_width_code(node->ty) == 0   ? "df"
                                     : dec_width_code(node->ty) == 1 ? "dd"
                                                                     : "dl";
                fprintf(f, "%s%s", node->dec_digits ? node->dec_digits : "0",
                        suffix);
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
                        format_float_literal(buf, sizeof buf,
                                             (double)node->fval);
                        fprintf(f, "%sf", buf);
                    }
                } else if (node->ty->kind == TY_LDOUBLE) {
                    // Builtin family name suffix ("l") and the literal suffix
                    // ("L") are cased differently --
                    // __builtin_infl/__builtin_nanl vs. the `L` token suffix --
                    // don't conflate them.
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
                        int  n = snprintf(buf, sizeof buf, "%.17g",
                                          (double)node->fval);
                        if (n > 0 && (size_t)n < sizeof buf &&
                            !strpbrk(buf, ".eEnN"))
                            snprintf(buf + n, sizeof(buf) - (size_t)n, ".0");
                        fprintf(f, "%s", buf);
                    }
                }
            } else if (node->layout_ty &&
                       serialize_layout_const(f, ctx, node->layout_ty,
                                              node->layout_is_align)) {
                // #1031: this ND_NUM was folded from sizeof/_Alignof of a
                // type whose own definition is suppressed elsewhere in
                // this TU (a from_include struct/union, or something that
                // transitively contains one -- see
                // type_layout_is_host_owned()) in favor of the replayed
                // #include's real host layout. The folded literal here
                // (CCCC's own, possibly smaller, projection) would go
                // stale the moment that suppression takes effect --
                // test_sys_mount_statfs.c is the confirmed case: a
                // `malloc(sizeof(struct statfs) + tail)` sized against the
                // guest's ~56-byte projection hands the real, much larger
                // host `statfs()` an undersized buffer. serialize_layout_
                // const() re-emits the operator textually instead, so the
                // *host's* sizeof/_Alignof is what the emitted C actually
                // evaluates -- primary-precedence, so no parenthesization
                // is needed at any parent_prec. Its own internal
                // type_has_printable_name() check is the fallback guard: an
                // anonymous from_include aggregate with no tag/typedef
                // would otherwise force serialize_type() to print a
                // re-derived body right here, reinstating CCCC's own
                // projection -- strictly worse than the literal this is
                // trying to fix, so that case falls through to the
                // ordinary folded-literal path below instead (nothing was
                // printed; serialize_layout_const() returned false).
                //
                // #1095 closed three more of this same residual: array
                // dimensions, case labels, and enum values now share this
                // exact helper (see their own call sites). Bitfield widths,
                // _Static_assert, and a global initializer's byte image
                // remain -- see man/COVERAGE.md's own entry for why those
                // three are not merely deferred.
            } else if (node->ty && node->ty->kind == TY_BITINT &&
                       node->ty->bit_width > 64 && node->wide_digits) {
                // #1121: a wb/uwb literal beyond 64 bits carries its full
                // precision out-of-band in wide_digits/wide_base (cccc.h) --
                // node->val alone is truncated to 64 bits by the tokenizer
                // (tokenize.c). Only the VM path previously consumed
                // wide_digits (codegen_expr.c); this arm is the -m/-c=native
                // counterpart. bit_width > 128 already can't reach here: the
                // enclosing type would have hard-errored out of
                // serialize_type() (case TY_BITINT above) first.
                unsigned __int128 v =
                    decode_wide_digits(node->wide_digits, node->wide_base);
                fprintf(f,
                        "((%s%s)(((unsigned __int128)0x%llxULL << 64) | "
                        "0x%llxULL))",
                        node->ty->is_unsigned ? "unsigned " : "", "__int128",
                        (unsigned long long)(uint64_t)(v >> 64),
                        (unsigned long long)(uint64_t)v);
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
                // A dotted `.L..N` name means "anonymous global"
                // (new_anon_gvar, parse.c) -- shared by string literals, static
                // locals, and compound literals (#925). Only a genuine string
                // literal inlines as string text here; the other two are
                // renamed to a valid identifier and given a real definition by
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
                    // #1024: declare_builtin_functions (parse_decl.c) names
                    // this Obj literally "alloca" so the VM's own symbol table
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
            bool  lhs_ptr   = node_is_pointerish(lhs_inner);
            bool  rhs_ptr   = node_is_pointerish(rhs_inner);
            bool  lhs_int   = node_is_integerish(lhs_inner);
            bool  rhs_int   = node_is_integerish(rhs_inner);

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
                // MakeSubscript() on an array-typed anon global) --
                // serialize_type would print `(int [3])`, and a cast to array
                // type is not valid C. Cast to pointer-to-element instead; the
                // ND_DEREF this node is wrapped in still reads the right value
                // through it. #964: node->ty can also be TY_VLA (`int[n]`),
                // same fix applies.
                // #1102: the cast-back drops pointee qualifiers -- see
                // serialize_offset_cast_type().
                serialize_offset_cast_type(f, vm, ctx, node->ty);
                fprintf(f, "((char *)");
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
                // extends it to TY_VLA (a cast to `int[n]` is equally invalid
                // C). #1102: qualifier-stripping cast-back, as above.
                serialize_offset_cast_type(f, vm, ctx, node->ty);
                fprintf(f, "((char *)");
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
        case ND_DEREF: {
            // #1102: & never distributes over a comma chain -- C's comma
            // operator yields an rvalue even when its last operand is an
            // lvalue, so the naive spelling of `&(struct P){30, 12}`
            // (a block-scope compound literal lowers to
            // ND_ADDR(ND_COMMA(memzero+assigns..., hidden temp var)))
            // came out as `&(memset(...), t.x = 30, t)` and clang
            // rejected it outright ("cannot take the address of an
            // rvalue"). Re-spell with the & binding to the chain's
            // addressable tail instead: `(memset(...), t.x = 30, &t)`.
            // Same evaluation order, same resulting pointer; the VM
            // never re-parses the text, but every host compiler does.
            // #1102 followup: the addressed expression can also carry a
            // postfix shell above the chain -- `&((struct P){30, 12}).x`
            // (parenthesized literals take the ordinary postfix path, so
            // `.x`/`->x` layer MEMBER (and DEREF) nodes over the comma) --
            // which used to emit the equally-invalid `&(..., t).x`. The &
            // binds inside the chain there too, covering the whole shell:
            // `(..., &t.x)`.
            Node *addr_base = node->kind == ND_ADDR && node->lhs
                                  ? addr_comma_base(node->lhs)
                                  : NULL;
            if (node->kind == ND_ADDR && addr_base &&
                !is_noop_expr(addr_base->lhs) &&
                !is_noop_expr(addr_base->rhs) &&
                addr_comma_tail_is_lvalue(addr_base)) {
                fprintf(f, "(");
                serialize_addr_comma(f, vm, ctx, addr_base,
                                     comma_chain_tail(addr_base), node->lhs);
                fprintf(f, ")");
                break;
            }
            fprintf(f, "%s", get_unary_op_str(node->kind));
            // #1102: two adjacent '-' tokens re-lex as pre-decrement, so
            // `-(-5)` -- ND_NEG over ND_NEG, typically from a macro like
            // `#define abs(x) ((x) < 0 ? -(x) : (x))` expanded on -5 --
            // must keep its inner operand parenthesized; the old flat
            // spelling emitted `--5` and every host compiler read it as
            // "--5" ("expression is not assignable"). The check drills
            // through ND_CAST chains: a widening cast serializes as
            // nothing (the suppression arm below), so the nested '-'
            // can hide behind one (_Generic-selected arms are exactly
            // this shape). Only this pairing glues: `!!x`, `~~x` and
            // `**p` are ordinary token sequences, and every other prefix
            // operator's own spelling ends in a non-'-' character, so
            // all other operands keep the exact spelling they had.
            if (node->kind == ND_NEG && node->lhs) {
                Node *op = node->lhs;
                while (op->kind == ND_CAST && op->lhs)
                    op = op->lhs;
                if (op->kind == ND_NEG) {
                    fprintf(f, "(");
                    serialize_expr(f, vm, ctx, node->lhs, 2);
                    fprintf(f, ")");
                    break;
                }
            }
            serialize_expr(f, vm, ctx, node->lhs, node_prec);
            break;
        }

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
            // Only emit a cast if it crosses a type category or narrows/changes
            // signedness.
            Type *dst = node->ty;
            Type *src = node->lhs ? node->lhs->ty : NULL;
            // #1111: casting TO nullptr_t is not valid C23 syntax even where
            // assignment/conversion would be -- every implicit conversion the
            // type checker inserts when nullptr meets an assignment or
            // comparison (assignment conversion, null-pointer-constant
            // equalization) serializes as an explicit cast, and
            // serialize_type() above would spell its destination through its
            // own #999 scalar typedef-name lookup as the bundled <stddef.h>
            // name "nullptr_t". Real clang rejects every such cast outright
            // ("cannot cast an object of type 'int' to 'nullptr_t'"). The host
            // nullptr_t is typeof(nullptr) == void *, same size and
            // representation, so spelling the destination "(void *)" keeps
            // every assignment and comparison meaning-preserving while
            // compiling under any host. Scoped to cast destinations only:
            // declarations of nullptr_t objects keep their typedef name (valid
            // C23, resolves via the output's <stddef.h>).
            if (dst && dst->kind == TY_NULLPTR_T) {
                fprintf(f, "(void *)");
                serialize_expr(f, vm, ctx, node->lhs, node_prec);
                break;
            }
            // #1068: a bare "(dst_type)float_expr" cast is UB in the *host*
            // compiler for NaN/out-of-range values, same reason cccc_f64_to_i64
            // et al exist in src/internal.h for the VM's own F2I3/F2U3 opcodes
            // (#775/#780) -- the VM defines this conversion as saturating with
            // FE_INVALID raised, but a real host compiler is free to do
            // anything, and x86_64 clang/gcc both demonstrably do worse than
            // "anything": the common branchless double/float->uint64 lowering
            // spuriously raises FE_INVALID even for an in-range value (measured
            // directly, x86_64 only -- aarch64's FCVTZS/FCVTZU already saturate
            // correctly and never raise it, so this is a no-op there). Route
            // every real-floating -> non-floating cast through one of four
            // on-demand helpers (serialize_synth_f2i_helpers below) that are a
            // near-verbatim port of internal.h's own VM helpers, so native
            // output agrees with the VM by construction. Mirrors
            // codegen_expr.c's own opcode selection exactly: F2U3 (unsigned
            // helper) only for an unsigned 64-bit integer destination (matching
            // is_u64_int(), codegen_emit.c -- narrower unsigned destinations
            // are already correct via F2I3's signed saturation plus an ordinary
            // truncating narrow cast, which the outer "(dst_type)" below still
            // supplies), F2I3 (signed helper) for everything else including
            // TY_BOOL. A destination wider than 64 bits (an over-64-bit
            // _BitInt) takes a completely different VM codegen path (raw
            // bit-copy, not this numeric conversion) and is excluded here to
            // match. A TY_VECTOR destination is also excluded: that's #1019's
            // own scalar-broadcast ND_CAST marker just below
            // (usual_arith_conv's "vector op scalar" internal annotation, not a
            // genuine value conversion) -- without this exclusion a float
            // scalar broadcast into a vector op wrongly got routed through the
            // integer-saturating helper instead of staying a plain float,
            // corrupting the vector arithmetic itself.
            bool f2i_native = src && dst && is_flonum(src) && !is_flonum(dst) &&
                              dst->kind != TY_VECTOR &&
                              !(dst->kind == TY_BITINT && dst->bit_width > 64);
            if (f2i_native) {
                bool u64_dst =
                    is_integer(dst) && dst->is_unsigned && dst->size == 8;
                bool        f32_src = src->kind == TY_FLOAT;
                const char *fn =
                    u64_dst ? (f32_src ? "__cccc_f2u64_f32" : "__cccc_f2u64")
                            : (f32_src ? "__cccc_f2i64_f32" : "__cccc_f2i64");
                fprintf(f, "(");
                serialize_type(f, ctx, node->ty);
                fprintf(f, ")%s(", fn);
                serialize_expr(f, vm, ctx, node->lhs, 2);
                fprintf(f, ")");
                break;
            }
            bool dst_int =
                dst && (dst->kind == TY_BOOL || dst->kind == TY_CHAR ||
                        dst->kind == TY_SHORT || dst->kind == TY_INT ||
                        dst->kind == TY_LONG);
            bool src_int =
                src && (src->kind == TY_BOOL || src->kind == TY_CHAR ||
                        src->kind == TY_SHORT || src->kind == TY_INT ||
                        src->kind == TY_LONG);
            static const int int_rank[] = {[TY_BOOL]  = 0,
                                           [TY_CHAR]  = 1,
                                           [TY_SHORT] = 2,
                                           [TY_INT]   = 3,
                                           [TY_LONG]  = 4};
            bool widening = dst_int && src_int &&
                            dst->is_unsigned == src->is_unsigned &&
                            int_rank[dst->kind] >= int_rank[src->kind];
            // #1019: a scalar operand of a `vector op scalar` binary op gets an
            // implicit ND_CAST(vector_ty, scalar) inserted by usual_arith_conv
            // (type.c) as its internal marker for "broadcast this scalar across
            // the vector's lanes" -- it is not source-level C. GCC/clang
            // perform that broadcast themselves inside the operator and reject
            // the same thing spelled as an explicit cast ("invalid conversion
            // between vector type and integer type of different size"). Emit
            // the bare scalar operand instead and let the host compiler's own
            // vector extension do the broadcast, exactly as real vector_size
            // source would. Only the scalar-source case is suppressed here -- a
            // vector-to-vector cast (same-type no-op, or a genuine bitcast
            // between differently-shaped vectors) still needs to print.
            bool scalar_splat =
                dst && dst->kind == TY_VECTOR && src && src->kind != TY_VECTOR;
            if (widening || scalar_splat) {
                serialize_expr(f, vm, ctx, node->lhs, parent_prec);
            } else {
                fprintf(f, "(");
                // #1107: a pointer-typed scalar typedef reached only through
                // a DERIVED pointer-to-it (e.g. `pthread_t *`, the type
                // pthread_create()'s first arg coerces to) used to always
                // fully decompose here -- serialize_type()/serialize_type_
                // decl() recurse straight into ty->base with no alias
                // lookup at this level, so only a cast to the *scalar*
                // typedef itself (`(pthread_t)x`) ever spelled its alias;
                // a cast to `pthread_t *` printed the fully-decomposed
                // structural spelling ("void **") instead. Harmless when
                // the aliased and canonical spellings denote the same real
                // host type (e.g. macOS's own pthread_t is a real pointer),
                // but a hard "incompatible pointer type" error when they
                // don't -- glibc's pthread_t is `unsigned long int`, so the
                // decomposed "void **" cast rejected the real `pthread_t *`
                // parameter type from the host's own #include_next'd
                // <pthread.h> (#1022) outright. Scoped narrowly to this one
                // cast-expression site (not a general serialize_type_decl()
                // fix): a from_include pointer typedef's own real
                // declaration is always already visible here (the host's
                // #include_next runs before any guest declaration), so
                // there is no forward-reference hazard the way a plain
                // struct/user typedef's own declarator position would have
                // (verified against tests/test_minilua.c's own
                // `lua_CFunction`, whose typedef line is only emitted late
                // in ctx->typedefs' own pass -- an early cast to it here
                // would need a typedef this file hasn't printed yet).
                TypeName *ptr_alias =
                    dst && dst->kind == TY_PTR && dst->base
                        ? find_typedef_name_exact(ctx, dst->base)
                        : NULL;
                if (ptr_alias && ptr_alias->from_include)
                    fprintf(f, "%.*s *", ptr_alias->name_len, ptr_alias->name);
                else
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
            // table it reads exists natively, so there is nothing to lower to
            // -- reject here rather than emit a call the host compiler rejects
            // by its internal name. Deliberately not rejected at parse time
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
                              "which does not exist natively",
                              pc_builtin);
                }
            }
            // #1054/#1030: setjmp/longjmp/_setjmp/_longjmp all print as calls
            // to exactly `_setjmp`/`_longjmp` -- real, plain `extern`-declared
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
            // #1085: any argument whose type is CCCC's own struct va_list must
            // arrive at the callee as an independent copy, never the caller's
            // own storage. A struct/union-by-value argument is passed here by
            // the caller's own address (#714/#1078), so *any* callee that
            // itself calls va_arg on the parameter -- a CCCC-emitted callee
            // (which also gets its own copy via #1062's shim, so a second copy
            // here is redundant but harmless) or, the case this fixes, a host
            // libc v*-family function (vprintf/vsnprintf/vsscanf/vsyslog/...,
            // which has no CCCC-emitted prologue to shim into and, on glibc,
            // has its own array-typed va_list that decays to a pointer in
            // parameter position, C17 6.7.6.3p7) -- would otherwise silently
            // advance the *caller's* va_list. Deliberately not narrowed to
            // "callee has no body" (host libc only): a call through a function
            // pointer would slip past that predicate and still alias on glibc,
            // and widening it costs nothing here. Wraps the whole call
            // (including the #1074 static-link injection just below, so a
            // nested-function callee stays correct too) in a statement
            // expression that declares one va_copy'd local per such argument
            // and substitutes it in the argument list below; no va_end on the
            // copy, mirroring #1062's own reasoning (verified there that
            // va_end expands to nothing observable for either va_list
            // representation this project targets).
            bool has_va_list_arg = false;
            for (Node *va_arg_scan = node->args; va_arg_scan;
                 va_arg_scan       = va_arg_scan->next) {
                if (type_is_cccc_va_list(va_arg_scan->ty)) {
                    has_va_list_arg = true;
                    break;
                }
            }
            char va_fwd_names[8][32];
            if (has_va_list_arg) {
                fprintf(f, "__extension__ ({ ");
                int va_fwd_idx = 0;
                for (Node *arg = node->args; arg;
                     arg       = arg->next, va_fwd_idx++) {
                    if (va_fwd_idx >= 8 || !type_is_cccc_va_list(arg->ty))
                        continue;
                    snprintf(va_fwd_names[va_fwd_idx],
                             sizeof va_fwd_names[va_fwd_idx],
                             "__cccc_va_fwd_%d", ctx->va_fwd_seq++);
                    fprintf(f, "va_list %s; va_copy(%s, ",
                            va_fwd_names[va_fwd_idx], va_fwd_names[va_fwd_idx]);
                    serialize_expr(f, vm, ctx, arg, 2);
                    fprintf(f, "); ");
                }
            }
            serialize_expr(f, vm, ctx, node->lhs, node_prec);
            fprintf(f, "(");
            // #1074: a direct call to a nested function needs its hidden
            // __static_link argument supplied explicitly -- the parser already
            // gave the callee's own signature a leading `void *__static_link`
            // parameter (parse_decl.c), but nothing else ever passed it.
            // Mirrors codegen_expr.c's calling_nested value selection exactly:
            // calling one's own direct child passes that child's own env
            // (declared as
            // `__cccc_nenv` at the top of the function currently being
            // serialized, ctx->current_fn -- see serialize_function); calling a
            // sibling or an ancestor's nested function (only reachable from
            // inside that ancestor's own nest, so ctx->current_fn must itself
            // be nested) chases ->__up via nested_env_ptr_expr().
            // serialize_nested_ preamble() has already rejected, at compile
            // time, every reference to a nested function that ISN'T a direct
            // callee, so `node->lhs` here is guaranteed to be exactly this
            // shape whenever the check below matches.
            if (node->lhs && node->lhs->kind == ND_VAR && node->lhs->var &&
                node->lhs->var->is_function && node->lhs->var->is_nested &&
                !node->lhs->var->is_block) {
                Obj *callee_parent = node->lhs->var->parent_fn;
                Obj *current_fn    = ctx->current_fn;
                fprintf(f, "(void *)");
                if (callee_parent == current_fn)
                    fprintf(f, "&__cccc_nenv");
                else if (current_fn && current_fn->is_nested &&
                         !current_fn->is_block)
                    fprintf(f, "%s",
                            nested_env_ptr_expr(vm, ctx, current_fn,
                                                callee_parent));
                else
                    // Unreachable in valid C (a nested function's name has
                    // block scope, only visible inside its own parent's nest)
                    // -- mirror codegen_expr.c's identical fallback rather than
                    // emit nothing.
                    fprintf(f, "&__cccc_nenv");
                if (node->args)
                    fprintf(f, ", ");
            }
            {
                int va_fwd_idx = 0;
                for (Node *arg = node->args; arg;
                     arg       = arg->next, va_fwd_idx++) {
                    // #1042(b): a comma-expression argument (e.g. from a macro
                    // like ivalue(r) that expands to one) must stay
                    // parenthesized here -- the comma is the argument separator
                    // in this context, so an unparenthesized ND_COMMA silently
                    // splits into extra arguments
                    // ("too many arguments to function call"). parent_prec 2 is
                    // above get_precedence(ND_COMMA)'s 1 but below every other
                    // node kind's precedence, so only a bare top-level comma
                    // gets wrapped.
                    //
                    // #1085: a va_list-typed argument prints the va_copy'd
                    // local declared just above instead of re-serializing the
                    // original expression -- both for correctness (the callee
                    // must see the copy, not the caller's own storage) and to
                    // avoid evaluating a (rare, but legal) side-effecting
                    // va_list expression twice.
                    if (va_fwd_idx < 8 && has_va_list_arg &&
                        type_is_cccc_va_list(arg->ty))
                        fprintf(f, "%s", va_fwd_names[va_fwd_idx]);
                    else
                        serialize_expr(f, vm, ctx, arg, 2);
                    if (arg->next)
                        fprintf(f, ", ");
                }
            }
            fprintf(f, ")");
            if (has_va_list_arg)
                fprintf(f, "; })");
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
            // The *value* diverges by design: under the VM this is a bytecode
            // pc cast to void*, natively it is a real host return address. Both
            // are "the return address n frames up" in their own runtime, which
            // is the most faithful mapping available -- see COVERAGE.md.
            fprintf(f, "__builtin_return_address(%lld)", (long long)node->val);
            break;

        case ND_UNREACHABLE:
            // __builtin_unreachable, __builtin_trap and __builtin_debugtrap all
            // lower to the same BTRAP opcode, so the VM traps for all three and
            // the original spelling is not recoverable here. __builtin_trap()
            // is the emission that matches that behaviour;
            // __builtin_unreachable() would be UB natively and the optimizer
            // would delete the path.
            fprintf(f, "__builtin_trap()");
            break;

        case ND_BITOP: {
            // val = (op << 8) | width. popcount/parity encode width 0 (see
            // parse.c), so the `ll` variant has to come from the argument's own
            // type -- emitting __builtin_popcount for a 64-bit argument would
            // compile cleanly and silently truncate.
            int  op    = (int)(node->val >> 8);
            int  width = (int)(node->val & 0xff);
            bool wide  = node->lhs && node->lhs->ty && node->lhs->ty->size == 8;
            const char *name;
            switch (op) {
                case 0:
                    name = (width == 64) ? "__builtin_clzll" : "__builtin_clz";
                    break;
                case 1:
                    name = (width == 64) ? "__builtin_ctzll" : "__builtin_ctz";
                    break;
                case 2:
                    name = wide ? "__builtin_popcountll" : "__builtin_popcount";
                    break;
                case 3:
                    name = wide ? "__builtin_parityll" : "__builtin_parity";
                    break;
                case 4:
                    name = (width == 64) ? "__builtin_ffsll" : "__builtin_ffs";
                    break;
                default:
                    // bswap: `width` is the byte count, not a bit width.
                    name = (width == 2)   ? "__builtin_bswap16"
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
                "__builtin_add_overflow",
                "__builtin_sub_overflow",
                "__builtin_mul_overflow",
            };
            const char *name = (node->val >= 0 && node->val <= 2)
                                   ? names[node->val]
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
            // since __atomic_load_n does not accept a float or aggregate
            // pointee and the VM is not being atomic there either.
            if (atomic_serializable_pointee(node->lhs)) {
                fprintf(f, "__atomic_load_n(");
                // #1101: strip _Atomic from the operand's static type.
                serialize_atomic_addr(f, vm, ctx, node->lhs);
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
            // statement expression rather than evaluating the operand twice.
            // The common statement-position case is handled in serialize_stmt
            // and emits the plain call.
            if (atomic_serializable_pointee(node->lhs)) {
                fprintf(f, "__extension__ ({ __typeof__(*(");
                serialize_expr(f, vm, ctx, node->lhs, 0);
                fprintf(f, ")) __cccc_astore_v = (");
                serialize_expr(f, vm, ctx, node->rhs, 0);
                fprintf(f, "); __atomic_store_n(");
                // #1101: strip _Atomic from the operand's static type.
                serialize_atomic_addr(f, vm, ctx, node->lhs);
                fprintf(f, ", __cccc_astore_v, __ATOMIC_SEQ_CST); "
                           "__cccc_astore_v; })");
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
            // #1101: strip _Atomic from the operand's static type.
            serialize_atomic_addr(f, vm, ctx, node->lhs);
            fprintf(f, ", ");
            serialize_expr(f, vm, ctx, node->rhs, 2);
            fprintf(f, ", __ATOMIC_SEQ_CST)");
            break;

        case ND_CAS:
            // (obj, *expected, desired) -> bool, matching codegen's ACAS
            // contract: cas_old is a *pointer* to the expected value, as
            // __atomic_compare_ exchange_n also takes. weak = 0.
            fprintf(f, "__atomic_compare_exchange_n(");
            // #1101: strip _Atomic from both pointer operands' static types
            // (the address, and the pointer to the expected value -- the
            // builtin wants both to point at unqualified objects).
            serialize_atomic_addr(f, vm, ctx, node->cas_addr);
            fprintf(f, ", ");
            serialize_atomic_addr(f, vm, ctx, node->cas_old);
            fprintf(f, ", ");
            serialize_expr(f, vm, ctx, node->cas_new, 2);
            fprintf(f, ", 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)");
            break;

        case ND_LABEL_VAL:
            // [GNU] labels-as-values. node->label is the source identifier, the
            // same one ND_LABEL/ND_GOTO already serialize.
            fprintf(f, "&&%s",
                    node->label ? node->label : "/* unknown label */");
            break;

        case ND_COMPLEX: {
            // val: 0 = construct from (real, imag), 1 = creal, 2 = cimag,
            // 3 = conj. The f/l suffix follows the element float type.
            Type *elem = node->ty;
            if (elem && elem->kind == TY_COMPLEX && elem->base)
                elem = elem->base;
            const char *suffix = !elem                        ? ""
                                 : (elem->kind == TY_FLOAT)   ? "f"
                                 : (elem->kind == TY_LDOUBLE) ? "l"
                                                              : "";
            if (node->val == 0) {
                // __builtin_complex requires both operands to have the same
                // real floating type, so each is cast to the element type
                // explicitly.
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
                const char *name = (node->val == 1)   ? "creal"
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
            // here there is no host equivalent to lower to -- clang and gcc
            // have no _Decimal support at all -- so this fails loudly rather
            // than fabricating a call that would not link.
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
            Obj        *block_fn = node->block_fn;
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
                    error("cccc: a block literal cannot be serialized at file "
                          "scope");
            }

            const char *desc = node->block_desc_var->name;
            fprintf(f,
                    "(%s.__invoke = (void *)%s, %s.__size = (long)sizeof(%s)",
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
                bool is_array_cap =
                    !cap->is_block_var && cap->ty->kind == TY_ARRAY;
                if (is_array_cap)
                    fprintf(f, ", __builtin_memcpy(%s.__cap%d, ", desc, i);
                else
                    fprintf(f, ", %s.__cap%d = ", desc, i);

                // Mirrors codegen's ND_BLOCK_LITERAL capture-copy loop
                // (codegen.c) exactly, three sources in the same order:
                int enc_idx = (ctx->current_fn->is_block)
                                  ? block_capture_index(ctx->current_fn, cap)
                                  : -1;
                const char *enc_env =
                    enc_idx >= 0 ? find_block_env(ctx, ctx->current_fn) : NULL;
                // #1080: capture owned by an ancestor of the enclosing
                // *nested function* itself (not by the enclosing block) --
                // registered as an upvar of the real owner by
                // collect_nested_refs()'s ND_BLOCK_LITERAL arm. Read it back
                // through the same env chase an ordinary nested-function
                // upvar reference uses (nested_env_ptr_expr), one
                // dereference either way -- for an is_block_var capture that
                // yields the shared box pointer (copied verbatim, same as
                // the enc_idx branch above); for a plain capture it yields
                // the value itself.
                Obj *anc_owner = NULL;
                if (enc_idx < 0 && cap->is_local &&
                    ctx->current_fn->is_nested && !ctx->current_fn->is_block &&
                    !nested_var_is_own(ctx->current_fn, cap)) {
                    for (Obj *anc = ctx->current_fn->parent_fn; anc;
                         anc      = anc->parent_fn)
                        if (nested_var_is_own(anc, cap)) {
                            anc_owner = anc;
                            break;
                        }
                }
                int anc_idx = -1;
                if (anc_owner) {
                    for (int i = 0; i < ctx->nested_envs_len; i++)
                        if (ctx->nested_envs[i].owner_fn == anc_owner) {
                            for (int j = 0; j < ctx->nested_envs[i].upvars_len;
                                 j++)
                                if (ctx->nested_envs[i].upvars[j] == cap) {
                                    anc_idx = j;
                                    break;
                                }
                            break;
                        }
                }
                if (enc_idx >= 0 && enc_env) {
                    // Transitive capture: read from the enclosing block's own
                    // descriptor via __static_link. Exactly one dereference
                    // either way -- for an is_block_var capture the parent's
                    // field already holds the box pointer (copied verbatim
                    // below); for a plain capture the parent's field holds the
                    // value itself.
                    fprintf(f, "((%s *)__static_link)->__cap%d", enc_env,
                            enc_idx);
                } else if (anc_owner && anc_idx >= 0) {
                    fprintf(f, "(*%s->__uv%d)",
                            nested_env_ptr_expr(vm, ctx, ctx->current_fn,
                                                anc_owner),
                            anc_idx);
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
            fprintf(f, "({ struct __cccc_block *__cccc_blk = (struct "
                       "__cccc_block *)(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, "); ((");
            serialize_type(f, ctx, node->ty);
            fprintf(f, " (*)(void *");
            Type *block_ty =
                (node->lhs && node->lhs->ty && node->lhs->ty->kind == TY_BLOCK)
                    ? node->lhs->ty
                    : NULL;
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
                      "macro/comptime expansion)",
                      cc_node_kind_name(node->kind));
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
                error(
                    "cccc: internal error: no serializer case for %s (kind %d)",
                    cc_node_kind_name(node->kind), node->kind);
            break;
    }

    if (need_parens)
        fprintf(f, ")");
}

// #1124: does `ty` need an explicit width mask after a value-producing
// operation under -c=native/-m? serialize_type's TY_BITINT case (above)
// picks a container purely from ty->size -- signed char/short/int/long/
// __int128 -- and nowhere re-masks a computed value back down to
// ty->bit_width the way the VM's own emit_bitint_trunc (src/codegen_emit.c)
// does. The host container only truncates for free when its own width
// exactly matches the declared _BitInt(N) width (N in {8,16,32,64,128});
// every other N needs the mask this wrapper emits. ty->size > 16 already
// hard-errors at serialize_type (#1121/#1123), so this never fires for
// those.
static bool bitint_needs_mask(Type *ty) {
    return ty && ty->kind == TY_BITINT && ty->size <= 16 &&
           ty->bit_width != ty->size * 8;
}

// Kinds whose *result* can carry bits above the declared bit_width that the
// host container wouldn't otherwise truncate: arithmetic that can overflow
// the value's own N bits, a cast that (re)establishes the type, and the two
// unary ops that can set bits above N (~x always can; -x can too, e.g. the
// INT_MIN-style all-ones-into-sign-bit case). Deliberately excluded:
// comparisons/logical ops (int-valued, never produce a _BitInt result) and
// ND_MOD/ND_SHR/ND_BITAND/ND_BITOR/ND_BITXOR (structurally cannot widen an
// already-in-range operand's own value beyond N bits).
static bool bitint_op_needs_mask(NodeKind kind) {
    switch (kind) {
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_SHL:
        case ND_NEG:
        case ND_BITNOT:
        case ND_CAST:
            return true;
        default:
            return false;
    }
}

// #1124: public serialize_expr entry point. Wraps serialize_expr_raw's own
// switch-on-node->kind body with a width mask when `node` is a _BitInt(N)
// result that needs one -- every recursive serialize_expr call inside the
// raw body (sub-expressions) re-enters here, so nested operands are masked
// at their own width independently.
//
// The shift-pair mirrors emit_bitint_trunc exactly, but must run in a fixed
// *computation* width (WT: `long`/`unsigned long`, 64 bits, or __int128 for
// a size==16 container) rather than the container type T itself -- casting
// straight to a narrow T (e.g. `signed char` for _BitInt(5)) and shifting
// there would be silently wrong, since C's integer promotions re-widen a
// char/short operand to `int` for the shift regardless of the preceding
// cast, making the shift amount (computed against T's own width) apply in
// the wrong width. Casting to WT first pins the shift's actual width; the
// outer "(T)" then narrows the already-correctly-truncated result back down
// to the container type, which is well-defined (two's-complement wrap on
// every host this project targets, same as any ordinary narrowing
// conversion).
//
// The left shift always runs on the *unsigned* wide type (UWT), even when
// `ty` is signed: `x << S` where x is a negative signed value is only
// well-defined starting C23 (two's-complement wraparound; UB before that),
// and clang warns on it (-Wshift-negative-value) regardless of the TU's own
// -std, since the generated file's #include chain can pull in headers
// parsed under a different effective mode. Shifting the unsigned bit
// pattern instead sidesteps the question entirely -- left shift of an
// unsigned type is always well-defined overflow-as-wraparound. The
// right shift then runs on the signed WT so it arithmetic-shifts (sign-
// extends) exactly like the VM's own SHR3, which is implementation-defined
// but universal on every real gcc/clang target.
static void serialize_expr(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                           Node *node, int parent_prec) {
    if (node && bitint_needs_mask(node->ty) &&
        bitint_op_needs_mask(node->kind)) {
        Type       *ty      = node->ty;
        bool        wide_wt = ty->size == 16;
        int         wt_bits = wide_wt ? 128 : 64;
        int         shift   = wt_bits - ty->bit_width;
        const char *wt =
            wide_wt ? (ty->is_unsigned ? "unsigned __int128" : "__int128")
                    : (ty->is_unsigned ? "unsigned long" : "long");
        const char *uwt = wide_wt ? "unsigned __int128" : "unsigned long";
        fprintf(f, "((");
        serialize_type(f, ctx, ty);
        fprintf(f, ")((%s)((%s)(", wt, uwt);
        serialize_expr_raw(f, vm, ctx, node, 0);
        fprintf(f, ") << %d) >> %d))", shift, shift);
        return;
    }
    serialize_expr_raw(f, vm, ctx, node, parent_prec);
}

// Serialize a statement
static void serialize_stmt(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                           Node *node, int indent) {
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
            if (is_noop_expr(node->lhs))
                break;
            // #964: `v = alloca(tmp)` is declaration()'s lowering of a VLA
            // local
            // -- re-emitting it literally would diverge from VM semantics
            // (alloca in a loop body is not freed per iteration the way a real
            // VLA is, so a loop declaring a VLA would grow the host stack
            // unboundedly) and the assignment target isn't a valid C lvalue
            // once `v` is a genuine array. Emit a real declaration in its place
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
            // discards its value, so hand it to the ND_ASTORE statement case
            // and emit the plain void-returning call rather than the
            // value-producing statement expression.
            if (node->lhs && node->lhs->kind == ND_ASTORE) {
                serialize_stmt(f, vm, ctx, node->lhs, indent);
                break;
            }
            print_indent_level(f, indent);
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ";\n");
            break;

        case ND_BLOCK:
            // #1098: a block-scope _Static_assert/static_assert parses to
            // an otherwise-empty ND_BLOCK with its condition/message
            // stashed on it (static_assert_decl(), parse_stmt.c) -- emit
            // the real assert here instead of an empty `{}` when it
            // qualifies (see serialize_static_assert's own gate); an
            // unqualifying one is left as the pre-existing empty block,
            // same as before this ticket.
            if (node->static_assert_cond) {
                serialize_static_assert(f, vm, ctx, node->static_assert_cond,
                                        node->static_assert_msg,
                                        node->static_assert_msg_len, node->tok,
                                        indent);
                break;
            }
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
                if (node->init->kind == ND_BLOCK &&
                    block_defines_vla(node->init))
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
                SerJumpFrame frame = {ctx->jumps, node->brk_label,
                                      node->cont_label};
                ctx->jumps         = &frame;
                serialize_stmt(f, vm, ctx, node->then, indent + 1);
                ctx->jumps = frame.parent;
            }
            break;

        case ND_DO:
            print_indent_level(f, indent);
            fprintf(f, "do\n");
            {
                SerJumpFrame frame = {ctx->jumps, node->brk_label,
                                      node->cont_label};
                ctx->jumps         = &frame;
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
            // the enclosing ND_BLOCK's ->next walk handle subsequent
            // statements.
            print_indent_level(f, indent);
            fprintf(f, "switch (");
            serialize_expr(f, vm, ctx, node->cond, 0);
            fprintf(f, ")\n");
            {
                // A switch saves/restores only brk_label at parse time
                // (parse.c) -- cont_label is deliberately NULL here so a
                // `continue` inside this switch skips over this frame and
                // resolves to the nearest enclosing loop, not this switch.
                SerJumpFrame frame        = {ctx->jumps, node->brk_label, NULL};
                Node        *saved_switch = ctx->cur_switch;
                ctx->jumps                = &frame;
                ctx->cur_switch           = node;
                serialize_stmt(f, vm, ctx, node->then, indent + 1);
                ctx->cur_switch = saved_switch;
                ctx->jumps      = frame.parent;
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
                for (SerJumpFrame *fr = ctx->jumps; fr && !kw;
                     fr               = fr->parent) {
                    if (fr->brk_label && fr->brk_label == node->unique_label)
                        kw = "break";
                    else if (fr->cont_label &&
                             fr->cont_label == node->unique_label)
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
                // #1095: re-materialize a host-owned sizeof/_Alignof label
                // the same way #1031's ND_NUM arm does -- a case label is
                // only ever compared against a runtime value, never fed
                // into a layout CCCC itself emits, so there's no
                // consistency hazard to guard here the way array
                // dimensions/initializers have. serialize_layout_const()
                // itself still declines (prints nothing, returns false)
                // when re-materializing isn't sound/possible, so the plain
                // folded value is the fallback exactly as before.
                fprintf(f, "case ");
                if (!serialize_layout_const(f, ctx, node->case_begin_layout_ty,
                                            node->case_begin_layout_is_align))
                    fprintf(f, "%ld", node->begin);
                fprintf(f, ":\n");
            } else {
                // [GNU] Case ranges, e.g. "case 1 ... 5:"
                fprintf(f, "case ");
                if (!serialize_layout_const(f, ctx, node->case_begin_layout_ty,
                                            node->case_begin_layout_is_align))
                    fprintf(f, "%ld", node->begin);
                fprintf(f, " ... ");
                if (!serialize_layout_const(f, ctx, node->case_end_layout_ty,
                                            node->case_end_layout_is_align))
                    fprintf(f, "%ld", node->end);
                fprintf(f, ":\n");
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
            // Statement position discards the result, so the plain
            // void-returning call is enough -- no statement expression needed.
            // See the ND_ASTORE case in serialize_expr for the value-producing
            // form.
            print_indent_level(f, indent);
            if (atomic_serializable_pointee(node->lhs)) {
                fprintf(f, "__atomic_store_n(");
                // #1101: strip _Atomic from the operand's static type.
                serialize_atomic_addr(f, vm, ctx, node->lhs);
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
            // asm is the one construct deliberately emitted verbatim even
            // though the VM does not execute it by default (--asm-passthru opts
            // into VM execution): there is no way to evaluate host assembly in
            // the VM, so native output hands it to the host compiler. See
            // COVERAGE.md.
            print_indent_level(f, indent);
            fprintf(f, "asm(");
            if (node->asm_str)
                serialize_string_n(f, node->asm_str,
                                   (int)strlen(node->asm_str));
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
static void serialize_stmt_list_item(FILE *f, VirtualMachine *vm,
                                     SerializeContext *ctx, Node *node,
                                     int indent) {
    if (node && node->kind == ND_BLOCK && block_defines_vla(node)) {
        for (Node *s = node->body; s; s = s->next)
            serialize_stmt(f, vm, ctx, s, indent);
        return;
    }
    serialize_stmt(f, vm, ctx, node, indent);
}

// #1062: does `ty` name CCCC's own struct va_list (include/stdarg.h --
// reg_ptr/stack_ptr/reg_count/__reserved)? Matched structurally (member
// names/kinds), not by typedef spelling -- a user's own `typedef va_list
// mylist;` still forwards through this the same way a bare `va_list`
// parameter does, where a name-based match would silently miss it.
static bool member_named(Member *m, const char *name) {
    if (!m || !m->name)
        return false;
    size_t len = strlen(name);
    return m->name->len == (int)len && strncmp(m->name->loc, name, len) == 0;
}

static bool type_is_cccc_va_list(Type *ty) {
    if (!ty || ty->kind != TY_STRUCT || !ty->members)
        return false;
    Member *m = ty->members;
    if (!member_named(m, "reg_ptr") || !m->ty || m->ty->kind != TY_PTR)
        return false;
    m = m->next;
    if (!m || !member_named(m, "stack_ptr") || !m->ty || m->ty->kind != TY_PTR)
        return false;
    m = m->next;
    if (!m || !member_named(m, "reg_count") || !m->ty || m->ty->kind != TY_INT)
        return false;
    m = m->next;
    if (!m || !member_named(m, "__reserved") || !m->ty ||
        m->ty->kind != TY_ARRAY)
        return false;
    return m->next == NULL;
}

// #1062: CCCC's own va_list (a plain struct, always genuinely by-value
// since #1078) is forwarded verbatim as a function parameter's *type* under
// -c=native -- the type name resolves correctly (the user's own `#include
// <stdarg.h>` is replayed and picks up the real host header), but the real
// host's own va_list has different by-value semantics depending on host:
// macOS's is a bare `char *` (an ordinary scalar, genuinely by-value,
// matching the VM), while glibc's is `typedef struct __va_list_tag
// va_list[1]` -- an array type, which decays to a pointer in parameter
// position (C17 6.7.6.3p7), aliasing the caller's own va_list. A callee
// that does `va_arg(ap, T)` on such a parameter silently advances the
// *caller's* va_list on glibc, never on macOS or on the VM -- the same
// program gives two different answers depending on backend, no build
// failure, no diagnostic.
//
// Fixed with a callee-side va_copy shim rather than either alternative
// considered: (a) making CCCC's own va_list an array-of-one-struct on
// Linux specifically, to alias like glibc's -- a real, platform-divergent
// change to the VM's own variadic ABI for no benefit, since the VM's
// current by-value semantics are the parity target (this batch's own
// scope: native must match the VM, not match a native gcc build of the
// same source); or (b) diagnosing/rejecting a va_list parameter under
// -c=native on Linux -- the batch's own policy reserves rejection for "no
// cheap translation available", which isn't true here.
//
// The shim changes only the emitted parameter's *name*, never the
// function's type, so address-taken calls and calls through function
// pointers are unaffected (a pointer-parameter rewrite would need
// call-site changes and would break those). It's applied uniformly on
// every host (no #ifdef __linux__): `va_copy` of a scalar `char *` on
// Darwin is a trivial, harmless copy.
//
// Deliberately does NOT touch the parameter's own Obj (no rename, no new
// state) -- only the *printed* name in the signature differs from what the
// body's own ND_VAR references print (both read the same Obj->name). Only
// applied when fn->body is set: a bodiless prototype has no body to inject
// the shim into, and C doesn't require declaration/definition parameter
// names to match anyway, so a plain `va_list ap` prototype (unmodified) and
// a shimmed `va_list __cccc_va_param_ap` definition for the same function
// are both legal, compatible declarations of the same function type.
//
// Residual this shim alone doesn't close, fixed separately by #1085: host
// libc `v*`-family consumers (vprintf/vsnprintf/vfprintf/vsscanf/vsyslog/
// ...) are the *host's own* functions, with no callee prologue of ours to
// inject this shim into. See the ND_FUNCALL case above (the
// has_va_list_arg / va_fwd_names block) for the call-site fix: any call
// passing a va_list-typed argument gets its own va_copy'd statement
// expression there instead, which covers this case too (and, since it's
// not narrowed to bodiless callees, indirect calls through a function
// pointer as well). See man/COVERAGE.md's <stdarg.h> row for the full
// writeup.
static const char *va_list_shim_param_name(char *buf, size_t bufsz,
                                           const char *orig) {
    snprintf(buf, bufsz, "__cccc_va_param_%s", orig ? orig : "ap");
    return buf;
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

    // #1020:
    // __attribute__((constructor[(priority)]))/((destructor[(priority)])) was
    // never lowered here at all -- under -c=native the function was emitted as
    // an ordinary function nothing calls, so it simply never ran. Emitted as a
    // *prefix* attribute (not appended after the declarator the way asm_label
    // is below): GCC rejects a trailing attribute on a function *definition*
    // while clang accepts it, so a suffix form would pass on macOS and fail to
    // compile on Linux.
    if (fn->is_constructor) {
        if (fn->init_priority == CCCC_NO_INIT_PRIORITY)
            fprintf(f, "__attribute__((constructor)) ");
        else
            fprintf(f, "__attribute__((constructor(%d))) ", fn->init_priority);
    }
    if (fn->is_destructor) {
        if (fn->init_priority == CCCC_NO_INIT_PRIORITY)
            fprintf(f, "__attribute__((destructor)) ");
        else
            fprintf(f, "__attribute__((destructor(%d))) ", fn->init_priority);
    }

    if (fn->is_static)
        fprintf(f, "static ");

    // #1026: a function returning a function pointer (`int (*f(void))(int,
    // int)`) can't be spelled as "<return-type> <name>(<params>)" -- the
    // return type's own declarator has to wrap around the whole
    // "name(params)" unit, the same way TY_ARRAY/TY_PTR recurse in
    // serialize_type_decl. Render "name(params)[ asm("label")]" into a
    // buffer first, then hand it to serialize_type_decl as the declarator
    // name so a pointer/function return type nests correctly.
    char  *decl   = NULL;
    size_t declsz = 0;
    FILE  *df     = open_memstream(&decl, &declsz);
    fprintf(df, "%s(", fn->name);

    bool first = true;
    if (fn->params) {
        for (Obj *param = fn->params; param; param = param->next) {
            if (!first)
                fprintf(df, ", ");
            first = false;
            // #1062: only when this signature is for a body-having
            // definition (see va_list_shim_param_name's own comment) --
            // fn->body's own local-decl emission (serialize_function)
            // injects the matching `va_list <param->name>; va_copy(...)`
            // shim right after this signature is printed.
            if (fn->body && type_is_cccc_va_list(param->ty)) {
                char shimbuf[64];
                serialize_type_decl(df, ctx, param->ty,
                                    va_list_shim_param_name(
                                        shimbuf, sizeof shimbuf, param->name));
            } else {
                serialize_type_decl(df, ctx, param->ty, param->name);
            }
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
    serialize_type_decl(
        f, ctx, (fn->ty && fn->ty->return_ty) ? fn->ty->return_ty : ty_int,
        decl ? decl : "");
    free(decl);
}

// #1044: forward declaration -- serialize_function() (immediately below)
// needs to print a deferred static's own initializer inside the owning
// function's body, but serialize_init_bytes() isn't defined until further
// down this file (it recurses through serialize_reloc_init(), which itself
// needs find_label_owner() just below). Mirrors the existing forward
// declaration ahead of serialize_reloc_init()'s own definition.
static void serialize_init_bytes(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Obj *var, Type *ty,
                                 int offset);

// #1044: look up a label by the same pointer-identity key a Relocation's
// `*rel->label` carries (see LabelOwner's own comment). NULL if `name`
// doesn't match any label collect_deferred_static_labels() found -- the
// ordinary case for every relocation target that resolves to a real Obj.
static const LabelOwner *find_label_owner(SerializeContext *ctx,
                                          const char       *name) {
    for (int i = 0; i < ctx->label_owners_len; i++)
        if (ctx->label_owners[i].unique_label == name)
            return &ctx->label_owners[i];
    return NULL;
}

// #1044: true iff `var` was deferred into its owning function's own body by
// collect_deferred_static_labels() -- serialize_global_var() and the #918
// forward-declaration passes (both above and below this point in the file)
// must all skip it, since it has no file-scope declaration to forward-
// declare or definition to emit at all.
static bool var_is_deferred_label_static(SerializeContext *ctx, Obj *var) {
    for (int i = 0; i < ctx->deferred_label_statics_len; i++)
        if (ctx->deferred_label_statics[i].var == var)
            return true;
    return false;
}

// #1102: does this type, or anything reachable down its aggregate spine
// (array -> array -> ... -> element), carry a qualifier? A multi-dimensional
// `const int a[2][3]` spells const on the innermost element, arbitrarily
// far down.
static bool aggregate_spine_is_qualified(Type *ty) {
    while (ty) {
        if (ty->is_const)
            return true;
        if (ty->kind != TY_ARRAY && ty->kind != TY_VLA && ty->kind != TY_VECTOR)
            return false;
        ty = ty->base;
    }
    return false;
}

// #1102: a hoisted local's declarator must not carry any qualifier the
// split declaration/assignment scheme can't honour. #1029 already strips a
// scalar's top-level `const` (the declaration is hoisted, the initializer
// became an assignment in the body below). Aggregate locals need one more
// step: for `const int arr[3]` (or a const-element VLA/vector) C spells the
// qualifier on the *element* type, so is_const lives on ty->base -- one
// level further down than #1029's arm looked -- and the per-element
// initializer assignments (`*(const int *)((char *)arr + i) = v`) would
// still store into a genuinely-qualified object, which every host compiler
// rejects ("read-only variable is not assignable"). Copy the type chain,
// clearing is_const on the aggregate itself and recursively down its array/
// vector bases only; a pointer local's pointee qualifier (`const char *p`)
// is deliberately left alone, same as #1029. Returns ty untouched when
// there is nothing to strip.
static Type *hoist_mutable_type(VirtualMachine *vm, Type *ty) {
    if (!ty)
        return ty;
    bool agg =
        ty->kind == TY_ARRAY || ty->kind == TY_VLA || ty->kind == TY_VECTOR;
    if (!aggregate_spine_is_qualified(ty))
        return ty;
    Type *cpy     = copy_type(vm, ty);
    cpy->is_const = false;
    cpy->origin   = NULL;
    if (agg)
        cpy->base = hoist_mutable_type(vm, cpy->base);
    return cpy;
}

// Serialize a function
static void serialize_function(FILE *f, VirtualMachine *vm,
                               SerializeContext *ctx, Obj *fn) {
    if (!fn->is_function)
        return;

    // Skip pragma macro functions (they were consumed)
    // Skip non-definitions
    if (!fn->is_definition && !fn->body)
        return;

    serialize_function_signature(f, ctx, fn);

    if (fn->body) {
        fprintf(f, " {\n");
        Obj *saved_fn   = ctx->current_fn;
        ctx->current_fn = fn;

        // Function-local typedefs/tags are emitted at the top of the function,
        // matching the serializer's existing local declaration hoisting.
        serialize_type_defs_for_owner(f, ctx, fn);

        // #1062: for each va_list parameter, pair the shim-named parameter
        // serialize_function_signature() just printed (see its own comment
        // and va_list_shim_param_name()) with a genuinely independent local
        // under the *original* parameter name, initialized via va_copy.
        // Every ND_VAR reference inside the body still reads Obj->name
        // (unchanged, still the original name) and now resolves to this
        // local instead of the parameter -- restoring the VM's own
        // by-value va_list semantics under -c=native on every host,
        // including glibc (whose real va_list is an array type that would
        // otherwise decay to a pointer in parameter position and alias the
        // caller's va_list, C17 6.7.6.3p7). va_end is deliberately not
        // called on the shim copy: verified directly (both the macOS SDK
        // and, via the cccc-linux-amd64/cccc-linux-arm64 containers, real
        // glibc) that va_end expands to nothing observable for either
        // va_list representation (a no-op on Darwin's, a no-op on glibc's
        // struct-tag array), so skipping it costs nothing and avoids
        // needing to track an extra cleanup point for a function that may
        // return from multiple places.
        for (Obj *param = fn->params; param; param = param->next) {
            if (!type_is_cccc_va_list(param->ty))
                continue;
            char shimbuf[64];
            va_list_shim_param_name(shimbuf, sizeof shimbuf, param->name);
            fprintf(f, "    va_list %s; va_copy(%s, %s);\n", param->name,
                    param->name, shimbuf);
        }

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
                if (p == var) {
                    is_actual_param = true;
                    break;
                }
            if (is_actual_param)
                continue;

            if (var->name[0] == '\0')
                // Compiler-synthesized temporaries (e.g. from ++/--/op=
                // desugaring) have an empty name; give them one so they can
                // be declared and referenced as valid C identifiers.
                var->name =
                    arena_format(vm, "__cccc_tmp%d", ctx->anon_local_counter++);
            else if (var->name[0] == '.')
                // #1034: a local named via new_unique_name() (parse_core.c)
                // -- a macro/comptime-generated compound literal or block
                // temp given the same ".L..N" dotted scheme as an anonymous
                // *global* (rename_anon_globals(), further down this file)
                // -- is not a legal C identifier either, and unlike the
                // empty-name case above was never renamed here. Deliberately
                // a distinct "__cccc_local_" prefix, not
                // rename_anon_globals()'s own "__cccc_%s_%d" scheme (which
                // draws from a *different* counter, anon_global_counter) --
                // reusing that scheme here would let a renamed global and a
                // renamed local collide on the identical spelling (e.g. both
                // landing on
                // "__cccc_anon_0", one per counter) and silently shadow each
                // other in this function's scope. Same display_name-or-
                // "anon" tag rule rename_anon_globals() uses, but sharing
                // anon_local_counter with the __cccc_tmp%d case above (whose
                // own prefix keeps it out of this collision class too).
                var->name = arena_format(
                    vm, "__cccc_local_%s_%d",
                    (var->display_name && var->display_name[0] != '.')
                        ? var->display_name
                        : "anon",
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
                    var->name     = arena_format(vm, "%s__cccc_%d", var->name,
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
            // #1095: a hoisted local has no byte-image initializer here --
            // any initializer was already split into a separate assignment
            // statement in the body (see #1029's own comment just above)
            // -- so re-materializing a host-owned sizeof/_Alignof array
            // dimension can't disagree with anything else emitted for this
            // object. See SerializeContext.allow_layout_dims's own comment.
            ctx->allow_layout_dims = true;
            // #1136: see serialize_alignas_if_needed's own comment.
            serialize_alignas_if_needed(f, var);
            // #1029: strip the top-level const on the hoisted declarator;
            // #1102: and any qualifier spelled on an aggregate's *element*
            // type (`const int a[3]`, arbitrarily deep for multi-dimensional
            // arrays) -- see hoist_mutable_type(), which returns the type
            // untouched when there is nothing to strip. A pointer-level
            // const on a pointee (`const char *p`) is untouched by both.
            serialize_type_decl(f, ctx, hoist_mutable_type(vm, var->ty),
                                var->name);
            ctx->allow_layout_dims = false;
            fprintf(f, ";\n");
        }

        // #1044: a static (or compound-literal) global deferred here by
        // collect_deferred_static_labels() because its initializer takes
        // the address of one of `fn`'s own labels -- print its real
        // definition now, inside the one function whose labels it's legal
        // to reference. Ordinary anonymous-global naming already gave it a
        // legal C identifier (rename_anon_globals()); every reference to it
        // elsewhere in this function's body already reads that name via its
        // Obj pointer (ND_VAR), unaffected by where the declaration itself
        // ends up.
        for (int __dl_i = 0; __dl_i < ctx->deferred_label_statics_len;
             __dl_i++) {
            DeferredStaticLabel *__dl = &ctx->deferred_label_statics[__dl_i];
            if (__dl->owner_fn != fn)
                continue;
            print_indent_level(f, 1);
            fprintf(f, "static ");
            serialize_type_decl(f, ctx, __dl->var->ty, __dl->var->name);
            if (__dl->var->init_data) {
                fprintf(f, " = ");
                serialize_init_bytes(f, vm, ctx, __dl->var, __dl->var->ty, 0);
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

        // Function body — unpack a single ND_BLOCK to avoid double-brace
        // wrapping. Both the parser and FunctionSetBody store the body as an
        // ND_BLOCK node.
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

static void serialize_init_bytes(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Obj *var, Type *ty,
                                 int offset);

// A pointer-typed initializer slot backed by a Relocation (#918 defect C):
// previously the zeroed init_data bytes were printed verbatim as a null
// pointer -- silent miscompilation, valid C that runs wrong. `rel->label`
// names the target Obj by its ->name field.
static void serialize_reloc_init(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Obj *var, Type *ty,
                                 Relocation *rel) {
    if (!rel->label || !*rel->label)
        error("cccc: invalid relocation in initializer for global '%s'",
              var->name);

    const char *target_name = *rel->label;
    Obj        *target      = serialize_find_global(vm, target_name);
    if (!target) {
        // #1044: no Obj was ever created for a label -- codegen.c's own
        // text-segment label map is the only thing that ordinarily
        // resolves a `&&label` relocation, and that state never reaches
        // this file (see this function's own comment, further up, on why).
        // find_label_owner() rebuilds just enough of that mapping (built by
        // collect_deferred_static_labels()) to spell the label's address
        // back out as real C: `var` must already be one of ctx->deferred_
        // label_statics (only those globals' relocations are checked
        // against the label table, and only when serialize_function() is
        // about to print `var`'s own definition inside its owning
        // function's body, where the label is legal to name).
        const LabelOwner *label = find_label_owner(ctx, target_name);
        // Only spell `&&label` when `var` itself is one of ctx->deferred_
        // label_statics -- i.e. this call is reached from serialize_
        // function(), printing `var`'s definition inside its owning
        // function's body, where the address is legal. `var` can still
        // reach this arm un-deferred (collect_deferred_static_labels()
        // declines a candidate another global's own relocation points at,
        // e.g. `static void **p = tab;` sitting next to `static void
        // *tab[] = {&&L};`) -- falling through to the diagnostic below
        // for that case is correct: emitting `&&L` at file scope here
        // would be invalid C the host compiler would (and, pre-#1044,
        // did) reject anyway, so failing loudly here keeps the fail-
        // loudly policy intact rather than deferring to the host's own,
        // less specific error.
        if (!label || !var_is_deferred_label_static(ctx, var))
            error("cccc: cannot serialize initializer for global '%s' in "
                  "native mode: unresolved relocation target '%s'",
                  var->name, target_name);
        fprintf(f, "(");
        serialize_type(f, ctx, ty);
        fprintf(f, ")((char *)&&%s + %lld)", label->label,
                (long long)rel->addend);
        return;
    }

    // Anonymous string-literal global -- serialize_global_var() never
    // emits these on their own (see is_string_literal skip below), so
    // inline the literal here instead of naming a symbol that doesn't
    // exist in the output.
    if (target->is_string_literal && target->init_data) {
        int  len            = (target->ty && target->ty->kind == TY_ARRAY)
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
static void serialize_init_bytes(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Obj *var, Type *ty,
                                 int offset) {
    if (!ty)
        error("cccc: cannot serialize initializer for global '%s' in native "
              "mode: unknown type",
              var->name);

    if (ty->kind == TY_PTR || ty->kind == TY_NULLPTR_T) {
        Relocation *rel = serialize_find_reloc(var, offset);
        if (rel) {
            serialize_reloc_init(f, vm, ctx, var, ty, rel);
            return;
        }
    }

    switch (ty->kind) {
        case TY_ARRAY:
            if (ty->base->kind == TY_CHAR &&
                !serialize_find_reloc(var, offset)) {
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
                    continue; // anonymous bitfield: padding, nothing to
                              // designate
                if (!first)
                    fprintf(f, ", ");
                first = false;
                if (m->name)
                    fprintf(f, ".%.*s = ", m->name->len, m->name->loc);
                if (m->is_bitfield) {
                    // #1126: `sz` clamps to 8 regardless of m->ty->size (16
                    // for a wide-_BitInt-typed bitfield, e.g. `_BitInt(128)
                    // f : 100;`), and `bits` is printed as a plain %llu
                    // literal -- so any bit at or above bit 64 of the
                    // field's own value is silently dropped for such a
                    // member. Not fixed here: found while adding native
                    // coverage for #1125 (the runtime codegen path this
                    // bug is unrelated to), filed separately.
                    int64_t container = 0;
                    int     sz        = m->ty->size < 8 ? m->ty->size : 8;
                    memcpy(&container, var->init_data + offset + m->offset, sz);
                    uint64_t mask = (m->bit_width >= 64)
                                        ? ~0ULL
                                        : ((1ULL << m->bit_width) - 1);
                    uint64_t bits =
                        ((uint64_t)container >> m->bit_offset) & mask;
                    fprintf(f, "%lluu", (unsigned long long)bits);
                } else {
                    serialize_init_bytes(f, vm, ctx, var, m->ty,
                                         offset + m->offset);
                }
            }
            fprintf(f, " }");
            return;
        }

        case TY_UNION: {
            // #1115: an empty (0-byte) union has no members at all, so the
            // largest-member reconstruction below would refuse it -- but there
            // is nothing to reconstruct. An empty brace initializer is
            // accepted by every host for a zero-sized object and matches the
            // VM's own semantics exactly (no bytes to represent).
            if (ty->size == 0) {
                fprintf(f, "{ }");
                return;
            }
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
                      "%d-byte object",
                      var->name, ty->size);
            fprintf(f, "{ .%.*s = ", largest->name->len, largest->name->loc);
            serialize_init_bytes(f, vm, ctx, var, largest->ty, offset);
            fprintf(f, " }");
            return;
        }

        case TY_FLOAT: {
            float fv;
            memcpy(&fv, var->init_data + offset, 4);
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
            double dv;
            memcpy(&dv, var->init_data + offset, 8);
            if (!serialize_flonum_special(f, (long double)dv, ""))
                fprintf(f, "%.17g", dv);
            return;
        }

        case TY_COMPLEX: {
            // #1122: write_gvar_data's TY_COMPLEX arm (src/parse_init.c)
            // only ever folds a real-valued complex initializer -- this
            // compiler has no imaginary-literal syntax, so the imaginary
            // half of init_data is always zero. A real constant assigned to
            // a complex-typed target performs the usual real->complex
            // conversion in C, so printing just the real part is exact; no
            // need to reconstruct an `x + y*I` expression.
            int part_size = ty->base ? ty->base->size : 8;
            if (ty->base && ty->base->kind == TY_FLOAT) {
                float fv;
                memcpy(&fv, var->init_data + offset, 4);
                if (!serialize_flonum_special(f, (long double)fv, "f")) {
                    char buf[64];
                    format_float_literal(buf, sizeof buf, (double)fv);
                    fprintf(f, "%sf", buf);
                }
            } else {
                double dv;
                memcpy(&dv, var->init_data + offset, part_size >= 8 ? 8 : 4);
                if (!serialize_flonum_special(f, (long double)dv, ""))
                    fprintf(f, "%.17g", dv);
            }
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
        char        buf[80];
        int         w      = dec_width_code(ty);
        const char *suffix = w == 0 ? "df" : w == 1 ? "dd" : "dl";
        if (cccc_dec_format(buf, sizeof buf, var->init_data + offset, w) >= 0)
            fprintf(f, "%s%s", buf, suffix);
        else
            fprintf(f, "0%s", suffix);
        return;
    }

    if (ty->kind == TY_BOOL || ty->kind == TY_CHAR || ty->kind == TY_SHORT ||
        ty->kind == TY_INT || ty->kind == TY_LONG || ty->kind == TY_ENUM ||
        ty->kind == TY_PTR || ty->kind == TY_NULLPTR_T ||
        ty->kind == TY_BLOCK) {
        // #965: TY_BLOCK is 8 bytes, pointer-shaped (see block_type(),
        // type.c) -- a block value can only be a compile-time-constant
        // global initializer as a null pointer anyway (the VM's own
        // is_const_expr rejects a real block literal there before this is
        // ever reached), so it reads exactly like TY_PTR.
        int64_t iv = 0;
        int     sz = ty->size < 8 ? ty->size : 8;
        memcpy(&iv, var->init_data + offset, sz);
        if (sz < 8 && ty->kind != TY_PTR && ty->kind != TY_NULLPTR_T &&
            (iv >> (sz * 8 - 1)) & 1)
            iv |= (-1LL << (sz * 8));
        fprintf(f, "%lld", (long long)iv);
        return;
    }

    if (ty->kind == TY_BITINT) {
        // #1121: was entirely absent from this function -- any _BitInt(N)
        // global initializer (narrow or wide) fell straight through to the
        // "cannot serialize" error below. size<=8 mirrors the TY_LONG-family
        // narrow-integer read just above (sign-extend by size); size==16
        // (__int128/unsigned __int128, the only wide width serialize_type()
        // now supports -- case TY_BITINT there refuses anything larger)
        // reads both little-endian words and reassembles the same
        // ((unsigned __int128)hi << 64) | lo shape the wide-literal ND_NUM
        // arm above uses.
        if (ty->size <= 8) {
            int64_t iv = 0;
            memcpy(&iv, var->init_data + offset, ty->size);
            if (ty->size < 8 && !ty->is_unsigned &&
                (iv >> (ty->size * 8 - 1)) & 1)
                iv |= (-1LL << (ty->size * 8));
            fprintf(f, "%lld", (long long)iv);
            return;
        }
        if (ty->size == 16) {
            uint64_t lo, hi;
            memcpy(&lo, var->init_data + offset, 8);
            memcpy(&hi, var->init_data + offset + 8, 8);
            fprintf(f,
                    "((%s__int128)(((unsigned __int128)0x%llxULL << 64) | "
                    "0x%llxULL))",
                    ty->is_unsigned ? "unsigned " : "", (unsigned long long)hi,
                    (unsigned long long)lo);
            return;
        }
        // size > 16 (bit_width > 128): fall through to the loud error below,
        // consistent with serialize_type()'s own refusal for this width.
    }

    // TY_COMPLEX and anything else with no verified byte layout here: fail
    // loudly rather than guess (#918's whole point -- emitting a plausible-
    // but-wrong initializer is the bug class being fixed, not a shape to
    // reproduce for cases this function doesn't yet handle).
    error("cccc: cannot serialize initializer for global '%s' in native "
          "mode: unsupported initializer type (kind %d)",
          var->name, ty->kind);
}

// Serialize global variable
// #1022: `include/pthread.h` hands off `-c=native`'s replayed
// `#include <pthread.h>` to the real host header (#1021/#1040-style
// #include_next guard), so a static of type pthread_mutex_t/pthread_cond_t
// initialized with PTHREAD_MUTEX_INITIALIZER/PTHREAD_COND_INITIALIZER can no
// longer be serialized as CCCC's own projected designated-initializer image
// (`{ .__handle = 0, .__state = 0, .__type = 0 }`) -- the real host struct
// doesn't have those members at all. Narrow, type-keyed fix (not the general
// #1018 macro-provenance annotation, per user sign-off): if the global's
// type is one of these `from_include` pthread types and its init image is
// all-zero -- the only image CCCC's own macros ever produce -- print the
// bare host macro name instead of walking the projected members. Known
// limitation, documented rather than silently assumed away: a user's own
// literal `= {0}` on one of these types is indistinguishable from the macro
// and also becomes the macro spelling -- semantically equivalent either way
// (both are "the type's zero/default-initialized state"), so this is a safe
// over-approximation, not a soundness gap.
static const char *pthread_initializer_macro(SerializeContext *ctx, Obj *var) {
    if (!var->ty || !var->init_data)
        return NULL;
    TypeName *tn = find_typedef_name(ctx, var->ty);
    if (!tn || !tn->from_include)
        return NULL;

    // Only pthread_mutex_t/pthread_cond_t are declared in include/pthread.h
    // today -- rwlock/once have no CCCC type or FFI wrappers at all yet, so
    // there's no initializer image for either to collide with.
    static const struct {
        const char *type_name;
        const char *macro;
    } table[] = {
        {"pthread_mutex_t", "PTHREAD_MUTEX_INITIALIZER"},
        {"pthread_cond_t", "PTHREAD_COND_INITIALIZER"},
    };
    const char *macro = NULL;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (tn->name_len == (int)strlen(table[i].type_name) &&
            !strncmp(tn->name, table[i].type_name, tn->name_len)) {
            macro = table[i].macro;
            break;
        }
    }
    if (!macro)
        return NULL;

    for (int i = 0; i < var->ty->size; i++)
        if (var->init_data[i] != 0)
            return NULL;
    return macro;
}

// #1136: an explicit _Alignas(N) (Obj.align) requesting more than the
// type's own natural alignment is otherwise dropped by -c=native -- neither
// _Alignas nor __attribute__((aligned)) was emitted anywhere in this file,
// so `_Alignas(32) int g;` compiled fine but round-tripped through native
// output as a plain `int g;`, silently losing the requested alignment (the
// same "stated vs actual" bug class as the VM-side data-segment allocator
// this ticket also fixes). Natural (<=type-align) cases need nothing here:
// the host compiler derives those from the emitted type on its own.
// Called at every declaration site for one Obj (definition and forward
// declarations alike) -- C11 6.7.5p7 requires every declaration of an
// object to carry equivalent alignment, so they must all agree, not just
// the definition.
static void serialize_alignas_if_needed(FILE *f, Obj *var) {
    if (var->align > var->ty->align)
        fprintf(f, "_Alignas(%d) ", var->align);
}

static void serialize_global_var(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Obj *var) {
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

    // #1044: deferred into its owning function's own body instead --
    // serialize_function() emits its real definition; see
    // collect_deferred_static_labels()'s own comment.
    if (var_is_deferred_label_static(ctx, var))
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

    // #1022: Obj.is_tls (_Thread_local/__thread storage class) was parsed
    // and tracked but never re-emitted here -- a `_Thread_local` global
    // silently serialized as an ordinary global, so every thread shared one
    // instance instead of getting its own copy (confirmed:
    // test_thread_local_isolation.c's cross-thread-visibility check would
    // pass, i.e. the isolation it exists to test would be gone, under
    // -c=native). Emitted right after static/extern per C11 6.7.1's
    // storage-class-specifier ordering.
    if (var->is_tls)
        fprintf(f, "_Thread_local ");

    // #1136: see serialize_alignas_if_needed's own comment.
    serialize_alignas_if_needed(f, var);

    // #1095: only when no byte-image initializer follows -- an initialized
    // global's array dimension must stay folded so it can't disagree with
    // serialize_init_bytes' own byte image below, sized off the same
    // folded value. See SerializeContext.allow_layout_dims's own comment.
    ctx->allow_layout_dims = !var->init_data;
    serialize_type_decl(f, ctx, var->ty, var->name);
    ctx->allow_layout_dims = false;

    if (var->init_data) {
        fprintf(f, " = ");
        const char *pthread_init_macro = pthread_initializer_macro(ctx, var);
        if (pthread_init_macro)
            fprintf(f, "%s", pthread_init_macro);
        else
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

    TypeName *tag   = find_tag_name(ctx, ty);
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

    TypeName *tag   = find_tag_name(ctx, ty);
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
        fprintf(f, "    %s = ", enum_const_spelling(ctx, ty, ec->name));
        // #1095: re-materialize a host-owned sizeof/_Alignof enumerator
        // value the same way #1031's ND_NUM arm does -- see
        // serialize_layout_const()'s own comment. Every *use* of this
        // enumerator elsewhere in the TU carries the same provenance (see
        // parse_postfix.c's primary(), sc->enum_layout_ty), so the body and
        // its uses can't disagree the way an array's declaration and
        // initializer could -- no consistency hazard to guard here.
        if (!serialize_layout_const(f, ctx, ec->layout_ty, ec->layout_is_align))
            fprintf(f, "%lld", (long long)ec->value);
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
            // #1091: same guard as find_tag_name() -- a tagless `ty`
            // structurally matching a *differently*-tagged record (e.g. a
            // tagless typedef next to a same-shaped tagged struct) does not
            // mean `ty` itself "has" that tag; without this,
            // aggregate_typedef_is_definition() wrongly concludes the
            // tagless typedef isn't the definition, and its own standalone
            // body (correctly printed elsewhere, via serialize_struct_def)
            // gets a second, redundant copy from serialize_typedef_alias.
            !tag_spelling_mismatch(ty, ctx->tags[i].ty) &&
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
        if (ctx->tags[i].owner_fn == NULL &&
            same_type_or_origin(ctx->tags[i].ty, ty))
            return true;
    for (int i = 0; i < ctx->typedefs_len; i++)
        if (ctx->typedefs[i].owner_fn == NULL &&
            same_type_or_origin(ctx->typedefs[i].ty, ty))
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
    int   chosen_len;
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
            chosen_len  = existing_tag->name_len;
        } else {
            chosen_name =
                arena_format(vm, "__cccc_local_%.*s_%d", existing_tag->name_len,
                             existing_tag->name, ctx->hoisted_type_counter++);
            chosen_len = (int)strlen(chosen_name);
        }
    } else {
        chosen_name = arena_format(vm, "__cccc_local_anon_%d",
                                   ctx->hoisted_type_counter++);
        chosen_len  = (int)strlen(chosen_name);
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
            ctx->tags[i].name     = chosen_name;
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

    Obj *saved_fn   = ctx->current_fn;
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

// #1031: true when `ty`'s own standalone definition is suppressed because
// a replayed `#include` (auto-capture) supplies it instead -- the single-
// type predicate factored out of serialize_type_defs_for_owner's own
// from_include check below, so that function and
// type_layout_is_host_owned() (which recurses through it) can never
// disagree by parallel edit -- same discipline
// typedef_alias_header_suppressed() already applies for typedef aliases,
// and the shape #892's AttrTarget regression showed a divergent copy here
// can break.
static bool type_def_is_from_include_suppressed(SerializeContext *ctx,
                                                Type             *ty) {
    if (!ty || ctx->emit_strict)
        return false;
    TypeName *tag   = find_tag_name(ctx, ty);
    TypeName *alias = find_typedef_name(ctx, ty);
    TypeName *provenance_source =
        tag ? find_tag_name_for_provenance(ctx, ty) : alias;
    return provenance_source && provenance_source->from_include &&
           !provenance_source->always_emit &&
           (!ctx->generated_only ||
            path_is_captured(ctx, provenance_source->file_path));
}

// #1031: true when `ty`'s from_include-suppressed definition is owned by
// one of is_compiler_owned_header()'s fixed list (stdarg.h/setjmp.h/etc,
// src/preprocess.c) -- excluded from type_layout_is_host_owned() below
// even though its body IS suppressed the same way an ordinary from_include
// type's is (confirmed: `struct va_list`'s body does not appear in -m
// output either). stdarg.h's va_list and setjmp.h's jmp_buf specifically
// use the *opposite* strategy from an ordinary from_include type: CCCC's
// own layout is deliberately widened to cover every supported host's real
// one (see their own man/COVERAGE.md entries), so the *guest-folded*
// sizeof/_Alignof is already a safe, correct-by-construction upper bound
// on purpose -- re-materializing the operator would replace that safe
// padded literal with whatever the real host's own (possibly smaller, via
// the header's own #include_next hand-off) va_list/jmp_buf size happens
// to be, defeating the padding #1054/#1059 built specifically to avoid
// depending on that. (Found via comptime_native_smoke.py's own case 97
// regressing when this exclusion was missing -- its "-m folds
// sizeof(va_list) to exactly 64" assertion is exactly this invariant.)
static bool type_header_is_compiler_owned(SerializeContext *ctx, Type *ty) {
    TypeName *tag   = find_tag_name(ctx, ty);
    TypeName *alias = find_typedef_name(ctx, ty);
    TypeName *provenance_source =
        tag ? find_tag_name_for_provenance(ctx, ty) : alias;
    if (!provenance_source || !provenance_source->file_path)
        return false;
    const char *path = provenance_source->file_path;
    const char *base = strrchr(path, '/');
    return is_compiler_owned_header(base ? base + 1 : path);
}

// #1031: true when a folded `sizeof`/`_Alignof` of `ty` can no longer be
// trusted under -c=native -- either `ty` itself is a from_include struct/
// union whose body serialize_type_defs_for_owner() suppresses (member
// access re-resolves correctly against the replayed #include's real host
// layout, but a value guest-side parsing already folded into a plain
// integer literal does not), or `ty` transitively contains such a type
// (an array of it, or a struct/union with it as a direct or nested
// member) -- recursion stops at TY_PTR: a pointer's own size is uniform
// across every supported platform x arch combination regardless of what
// it points to, and following pointee types would risk a cycle through a
// self-referential struct. depth guards against a pathological type
// graph; the real-world nesting this addresses is at most a few levels.
static bool type_layout_is_host_owned(SerializeContext *ctx, Type *ty,
                                      int depth) {
    if (!ty || depth > 32)
        return false;
    if (ty->kind == TY_ARRAY || ty->kind == TY_VLA)
        return type_layout_is_host_owned(ctx, ty->base, depth + 1);
    if (ty->kind == TY_PTR)
        return false;
    if (type_def_is_from_include_suppressed(ctx, ty) &&
        !type_header_is_compiler_owned(ctx, ty))
        return true;
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
        for (Member *m = ty->members; m; m = m->next)
            if (type_layout_is_host_owned(ctx, m->ty, depth + 1))
                return true;
    return false;
}

// #1031: true when serialize_type() can spell `ty` by a real name (tag,
// typedef alias, or anonymous-typedef alias) rather than falling through
// to serialize_anon_aggregate() and printing a re-derived *body* -- which
// for a from_include type would reinstate CCCC's own (possibly wrong)
// projection right where re-materializing `sizeof(ty)` is trying to avoid
// exactly that. Recurses through TY_ARRAY the same way
// type_layout_is_host_owned() does, since `sizeof(some_array_type)`
// re-materializes as `sizeof(<element type name>) * N`-shaped only
// indirectly -- serialize_type() on the array type itself already handles
// element naming, so this only needs to confirm the base is nameable.
static bool type_has_printable_name(SerializeContext *ctx, Type *ty) {
    if (!ty)
        return false;
    if (ty->kind == TY_ARRAY || ty->kind == TY_VLA)
        return type_has_printable_name(ctx, ty->base);
    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
        return true; // scalars, enums (fall back to "int"), etc. always print
    return find_tag_name(ctx, ty) || find_typedef_name(ctx, ty) ||
           find_anonymous_typedef_name(ctx, ty);
}

// #1095: the gate + emission serialize_expr's own ND_NUM arm used before
// this was factored out (see #1031's own comment there, and this
// function's callers for the three sites #1095 added: array dimensions,
// case labels, enum values). Prints "sizeof(T)"/"_Alignof(T)" and returns
// true when `layout_ty`'s own definition is from_include-suppressed
// (type_layout_is_host_owned()) and nameable (type_has_printable_name());
// prints nothing and returns false otherwise, so every call site's own
// fallback -- the plain folded literal -- still applies unchanged.
static bool serialize_layout_const(FILE *f, SerializeContext *ctx,
                                   Type *layout_ty, bool is_align) {
    if (!layout_ty || !type_layout_is_host_owned(ctx, layout_ty, 0) ||
        !type_has_printable_name(ctx, layout_ty))
        return false;
    fprintf(f, "%s(", is_align ? "_Alignof" : "sizeof");
    serialize_type(f, ctx, layout_ty);
    fprintf(f, ")");
    return true;
}

// #1098: true when `node` (a `_Static_assert` condition tree) contains, at
// any depth, a bare sizeof/_Alignof-of-a-from_include-type leaf
// (node_layout_const(), parse_analysis.c -- the same #1031/#1095 stash
// used by serialize_expr's own ND_NUM arm) whose type is a from_include
// struct/union (or an array thereof) that is host-owned
// (type_layout_is_host_owned()) and nameable (type_has_printable_name()).
// This is the "does this assert actually depend on a layout the VM might
// have gotten wrong" gate -- an ordinary compile-time-only assert (no
// from_include type involved anywhere in it) gets no redundant host-side
// re-check. Deliberately narrower than type_layout_is_host_owned()'s own
// implementation (which accepts any Type kind, not just struct/union,
// despite its own doc comment saying "struct/union"): a bare scalar type
// like plain `int` can spuriously same_type_or_origin()-match an unrelated
// from_include *typedef* of `int` (e.g. sys/types.h's __int32_t, reached
// merely by including <sys/mount.h>) via same_type_or_origin()'s pointer-
// identity walk up the origin chain -- harmless for #1031's own
// re-materialization (sizeof(int) prints identically either way) but would
// make this gate fire on ordinary, fully portable asserts having nothing
// to do with a host-divergent layout. Restricting to aggregates (matching
// every real-world case in this batch's own tickets, e.g. struct statfs)
// sidesteps that without touching the shared same_type_or_origin() itself.
// Plain recursion over lhs/rhs/cond/then/els covers every shape a
// constant-expression tree can take (unary: lhs only; binary: lhs+rhs;
// ternary: cond/then/els; cast: lhs) -- this walks a single expression
// tree, not a whole function body, so the explicit-stack/pointer-identity
// discipline SCRATCH.md documents for AST-wide walkers doesn't apply here.
// depth cap mirrors type_layout_is_host_owned()'s own guard.
static bool expr_has_host_owned_layout(SerializeContext *ctx, Node *node,
                                       int depth) {
    if (!node || depth > 32)
        return false;
    Type *layout_ty    = NULL;
    bool  layout_align = false;
    if (node_layout_const(node, &layout_ty, &layout_align)) {
        Type *base_ty = layout_ty;
        while (base_ty &&
               (base_ty->kind == TY_ARRAY || base_ty->kind == TY_VLA))
            base_ty = base_ty->base;
        if (base_ty &&
            (base_ty->kind == TY_STRUCT || base_ty->kind == TY_UNION) &&
            type_layout_is_host_owned(ctx, layout_ty, 0) &&
            type_has_printable_name(ctx, layout_ty))
            return true;
    }
    return expr_has_host_owned_layout(ctx, node->lhs, depth + 1) ||
           expr_has_host_owned_layout(ctx, node->rhs, depth + 1) ||
           expr_has_host_owned_layout(ctx, node->cond, depth + 1) ||
           expr_has_host_owned_layout(ctx, node->then, depth + 1) ||
           expr_has_host_owned_layout(ctx, node->els, depth + 1);
}

// #1098: emits `_Static_assert(cond, "msg");` (always the two-arg
// spelling, even for a C23 single-arg `static_assert` source -- valid
// C11-and-later on any supported host, needs no <assert.h>) so the host
// compiler re-checks an assertion whose condition depends on a
// from_include type's real layout, which CCCC only ever checked against
// its own (possibly wrong-for-this-host) projection at parse time. Two
// gates, both required, mirroring the #901/#1096 bodiless-declaration
// provenance gate (serialize.c's function-prototype loop) so this can
// never fire on one of CCCC's own bundled headers' own layout asserts
// (include/sys/stat.h, signal.h, fts.h, aio.h, mqueue.h, ndbm.h all carry
// per-platform `_Static_assert(sizeof(struct X) == N)` on types that ARE
// from_include and not compiler-owned -- re-emitting those against the
// real host would fail for reasons the user never wrote):
//   1. expr_has_host_owned_layout(cond) -- the condition actually depends
//      on a host-owned layout, not just an ordinary compile-time fact.
//   2. `tok` is from a command-line input file (the same
//      file_is_command_line_input()/cc_file_is_cccc_only() test #901/#1096
//      use) -- a header-sourced assert (bundled OR a real host header
//      reached via a replayed #include) is left unemitted.
// Prints nothing when either gate fails.
static void serialize_static_assert(FILE *f, VirtualMachine *vm,
                                    SerializeContext *ctx, Node *cond,
                                    const char *msg, int msg_len, Token *tok,
                                    int indent) {
    if (!cond || !tok || !tok->file)
        return;
    if (!cc_file_is_command_line_input(vm, tok->file->name) &&
        !cc_file_is_cccc_only(vm, tok->file->name))
        return;
    if (!expr_has_host_owned_layout(ctx, cond, 0))
        return;
    print_indent_level(f, indent);
    fprintf(f, "_Static_assert(");
    serialize_expr(f, vm, ctx, cond, 0);
    fprintf(f, ", ");
    serialize_string_n(f, msg, msg_len);
    fprintf(f, ");\n");
}

static bool aggregate_typedef_is_definition(SerializeContext *ctx,
                                            TypeName         *alias) {
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
                                            TypeName         *alias) {
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
    int  len = alias->name_len;
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
    alias->ty     = NULL;
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
        ensure_typedef_for_type_emitted(f, ctx, ty->base, owner_fn,
                                        typedef_done);
        return;
    }
    if (ty->kind == TY_FUNC) {
        ensure_typedef_for_type_emitted(f, ctx, ty->return_ty, owner_fn,
                                        typedef_done);
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
                ensure_typedef_for_type_emitted(f, ctx, m->ty, owner_fn,
                                                typedef_done);
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

    TypeName *td      = &ctx->typedefs[idx];
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
    td->ty        = NULL;
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
        !type_vec_contains_nominal(ctx, &ctx->emitted_defs, real_ty) &&
        !typedef_alias_header_suppressed(ctx, td)) {
        type_vec_push_nominal(ctx, &ctx->emitted_defs, real_ty);
        if (real_ty->kind == TY_ENUM)
            serialize_enum_def(f, ctx, real_ty);
        else
            serialize_struct_def(f, ctx, real_ty);
    }

    serialize_typedef_alias(f, ctx, td);
}

// Side discovery filing #1042(c)'s minilua audit: a struct/union member
// declaring a function-pointer parameter of another tag (e.g.
// `int (*f)(struct lua_State *);` inside `union Value`, minilua's own repro)
// gives that parameter's `struct lua_State` PROTOTYPE SCOPE (C11 6.2.1p4) --
// a distinct type from the file-scope `struct lua_State` its own later
// definition introduces, even though both spell the same tag. No forward
// declarations were ever emitted for file-scope tags (`grep
// '^struct [A-Za-z_]*;'` on -m output returns nothing), so a real host
// compiler reaching a later assignment between the two (minilua's own
// `(*io).value_.f = (lua_CFunction)fn;`) sees two "different", identically-
// spelled `struct lua_State *` function-pointer types and rejects it as
// "incompatible function pointer types".
//
// Deliberately narrow, NOT "forward-declare every tagged struct/union" (the
// first version of this fix, reverted): that blanket form regressed
// test_serialize_opaque_handle_1010.c's own `CCCC_REJECT_STDOUT: struct
// DyAtoms1010;\n` -- an ordinary pointer member (`DyAtoms1010 *`) does NOT
// introduce prototype scope, it declares the tag at the SAME (enclosing,
// typically file) scope as the containing struct itself (C11 6.7.2.3p11), so
// a forward declaration ahead of it is pure unwanted noise that test's own
// #1010 regression guard is right to reject. Only a struct/union pointer
// reached through a nested FUNCTION TYPE's parameter-type-list or return
// type is the actual C11 6.2.1p4 hazard -- so only those tags are collected.
static void collect_proto_scope_targets(SerializeContext *ctx, Type *mty,
                                        TypeVec *targets) {
    if (!mty)
        return;
    while (mty->kind == TY_PTR || mty->kind == TY_ARRAY || mty->kind == TY_VLA)
        mty = mty->base;

    if (mty->kind == TY_FUNC) {
        Type *ret = mty->return_ty;
        while (ret && ret->kind == TY_PTR)
            ret = ret->base;
        if (ret && (ret->kind == TY_STRUCT || ret->kind == TY_UNION))
            type_vec_push(targets, ret);
        for (Type *p = mty->params; p; p = p->next) {
            Type *pt = p;
            while (pt && pt->kind == TY_PTR)
                pt = pt->base;
            if (pt && (pt->kind == TY_STRUCT || pt->kind == TY_UNION))
                type_vec_push(targets, pt);
        }
        return;
    }

    // Not a function (pointer): if this member is itself a TAGLESS
    // struct/union, its body is inlined at this same use site (per
    // serialize_type_defs_for_owner's own "nothing to refer back to them
    // by" skip), so its own function-pointer members are printed at this
    // same textual position too -- recurse, mirroring collect_byval_edges'
    // identical tagless handling above.
    if ((mty->kind == TY_STRUCT || mty->kind == TY_UNION) &&
        !find_tag_name(ctx, mty) && !find_typedef_name(ctx, mty) &&
        !find_anonymous_typedef_name(ctx, mty))
        for (Member *mm = mty->members; mm; mm = mm->next)
            collect_proto_scope_targets(ctx, mm->ty, targets);
}

static void serialize_tag_forward_decls(FILE *f, SerializeContext *ctx) {
    TypeVec targets = {0};
    for (int i = 0; i < ctx->defs.len; i++) {
        Type *ty = ctx->defs.data[i];
        if ((ty->kind != TY_STRUCT && ty->kind != TY_UNION) ||
            type_decl_owner(ctx, ty) != NULL)
            continue; // file scope only -- prototype scope only bites here
        for (Member *m = ty->members; m; m = m->next)
            collect_proto_scope_targets(ctx, m->ty, &targets);
    }

    bool any = false;
    for (int i = 0; i < targets.len; i++) {
        int idx = find_complete_def_index(&ctx->defs, targets.data[i]);
        if (idx < 0)
            continue; // no real file-scope definition to disambiguate against
        Type *def_ty = ctx->defs.data[idx];
        if (type_decl_owner(ctx, def_ty) != NULL ||
            type_vec_contains(&ctx->hoisted, def_ty)) // #989: printed elsewhere
            continue;
        TypeName *tag = find_tag_name(ctx, def_ty);
        if (!tag)
            continue; // tagless: nothing to forward-declare by
        fprintf(f, "%s %.*s;\n", aggregate_keyword(def_ty), tag->name_len,
                tag->name);
        any = true;
    }
    if (any)
        fprintf(f, "\n");
    free(targets.data);
}

static void serialize_type_defs_for_owner(FILE *f, SerializeContext *ctx,
                                          Obj *owner_fn) {
    Obj *saved_fn   = ctx->current_fn;
    ctx->current_fn = owner_fn;

    if (!owner_fn)
        serialize_tag_forward_decls(f, ctx);

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
        TypeName *tag   = find_tag_name(ctx, ty);
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
        // type_def_is_from_include_suppressed() (factored out so this and
        // the ND_NUM re-materialization it enables, in serialize_expr,
        // can never disagree by parallel edit) folds in exactly this same
        // tag/alias/provenance/generated_only logic.
        //
        // #1031: suppressing this body is correct for member *access* --
        // the replayed `#include` (auto-capture, preprocess.c) hands
        // member resolution to the host header's real layout, which is
        // often more accurate than CCCC's own minimal projection (e.g.
        // `struct statfs`, ~56 bytes here vs. ~2100 on real macOS). It does
        // NOT retroactively fix a `sizeof`/`_Alignof` of `ty` that guest-
        // side parsing already folded into a plain integer literal
        // elsewhere in this TU (`offsetof` is unaffected -- it lowers to
        // an address expression, stddef.h, which re-resolves against the
        // replayed header like any other member access) -- that residual
        // gap is closed by ND_NUM re-materializing the operator textually
        // when type_layout_is_host_owned() says so, see that function and
        // its own caller in serialize_expr. A folded layout constant
        // reached through a context other than a bare `sizeof`/`_Alignof`
        // expression node (array dimensions, `_Static_assert`, case
        // labels, bitfield widths, enum values, global-initializer byte
        // images -- anything that consumes const_expr()/eval() and
        // discards the node) is still open; sibling to the FP_*
        // constant-folding note in native_accessor_shims below.
        if (type_def_is_from_include_suppressed(ctx, ty))
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
        if (type_vec_contains_nominal(ctx, &ctx->emitted_defs, ty))
            continue;
        type_vec_push_nominal(ctx, &ctx->emitted_defs, ty);
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
    {"__cccc_stdin", "static FILE *__cccc_stdin(void) { return stdin; }\n"},
    {"__cccc_stdout", "static FILE *__cccc_stdout(void) { return stdout; }\n"},
    {"__cccc_stderr", "static FILE *__cccc_stderr(void) { return stderr; }\n"},
    {"__cccc_errno_ptr",
     "static int *__cccc_errno_ptr(void) { return &errno; }\n"},
    {"__cccc_optarg_ptr",
     "static char **__cccc_optarg_ptr(void) { return &optarg; }\n"},
    {"__cccc_optind_ptr",
     "static int *__cccc_optind_ptr(void) { return &optind; }\n"},
    {"__cccc_opterr_ptr",
     "static int *__cccc_opterr_ptr(void) { return &opterr; }\n"},
    {"__cccc_optopt_ptr",
     "static int *__cccc_optopt_ptr(void) { return &optopt; }\n"},
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
    {"__cccc_isnan_f",
     "static int __cccc_isnan_f(float x) { return __builtin_isnan(x); }\n"},
    {"__cccc_isnan_d",
     "static int __cccc_isnan_d(double x) { return __builtin_isnan(x); }\n"},
    {"__cccc_isinf_f",
     "static int __cccc_isinf_f(float x) { return __builtin_isinf(x); }\n"},
    {"__cccc_isinf_d",
     "static int __cccc_isinf_d(double x) { return __builtin_isinf(x); }\n"},
    {"__cccc_signbit_f",
     "static int __cccc_signbit_f(float x) { return __builtin_signbit(x); }\n"},
    {"__cccc_signbit_d", "static int __cccc_signbit_d(double x) { return "
                         "__builtin_signbit(x); }\n"},
    {"__cccc_fpclassify_f", "static int __cccc_fpclassify_f(float x) { return "
                            "__builtin_fpclassify(2, 1, 3, 4, 5, x); }\n"},
    {"__cccc_fpclassify_d", "static int __cccc_fpclassify_d(double x) { return "
                            "__builtin_fpclassify(2, 1, 3, 4, 5, x); }\n"},
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
    {"__cccc_flt_rounds", "#include <fenv.h>\n"
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
    {"__cccc_issignaling_f",
     "static int __cccc_issignaling_f(float x) {\n"
     "    union { float f; unsigned int u; } __v; __v.f = x;\n"
     "    unsigned int u = __v.u;\n"
     "    return ((u & 0x7F800000U) == 0x7F800000U) && (u & 0x003FFFFFU) != 0 "
     "&& !(u & 0x00400000U);\n"
     "}\n"},
    {"__cccc_issignaling_d",
     "static int __cccc_issignaling_d(double x) {\n"
     "    union { double d; unsigned long long u; } __v; __v.d = x;\n"
     "    unsigned long long u = __v.u;\n"
     "    return ((u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (u "
     "& 0x0007FFFFFFFFFFFFULL) != 0 && !(u & 0x0008000000000000ULL);\n"
     "}\n"},
    {"__cccc_iseqsig_f", "#include <fenv.h>\n"
                         "static int __cccc_iseqsig_f(float x, float y) {\n"
                         "    union { float f; unsigned int u; } __vx, __vy; "
                         "__vx.f = x; __vy.f = y;\n"
                         "    unsigned int ux = __vx.u, uy = __vy.u;\n"
                         "    int sx = ((ux & 0x7F800000U) == 0x7F800000U) && "
                         "(ux & 0x003FFFFFU) != 0 && !(ux & 0x00400000U);\n"
                         "    int sy = ((uy & 0x7F800000U) == 0x7F800000U) && "
                         "(uy & 0x003FFFFFU) != 0 && !(uy & 0x00400000U);\n"
                         "    if (sx || sy) feraiseexcept(FE_INVALID);\n"
                         "    return x == y;\n"
                         "}\n"},
    {"__cccc_iseqsig_d",
     "#include <fenv.h>\n"
     "static int __cccc_iseqsig_d(double x, double y) {\n"
     "    union { double d; unsigned long long u; } __vx, __vy; __vx.d = x; "
     "__vy.d = y;\n"
     "    unsigned long long ux = __vx.u, uy = __vy.u;\n"
     "    int sx = ((ux & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && "
     "(ux & 0x0007FFFFFFFFFFFFULL) != 0 && !(ux & 0x0008000000000000ULL);\n"
     "    int sy = ((uy & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && "
     "(uy & 0x0007FFFFFFFFFFFFULL) != 0 && !(uy & 0x0008000000000000ULL);\n"
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
     "static size_t __cccc_mb_cur_max(void) { return __ctype_get_mb_cur_max(); "
     "}\n"
#else
     "extern int __mb_cur_max;\n"
     "static size_t __cccc_mb_cur_max(void) { return (size_t)__mb_cur_max; }\n"
#endif
    },
};

static void serialize_native_accessor_shims(FILE *f, Obj *prog) {
    bool any = false;
    for (size_t i = 0;
         i < sizeof(native_accessor_shims) / sizeof(native_accessor_shims[0]);
         i++) {
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

// #1088: real definitions for the C11 <threads.h> family (thrd_*/mtx_*/
// cnd_*/tss_*/call_once) under -c=native. <threads.h> is on
// is_cccc_supplied_only_header() (preprocess.c) -- its own #include is
// suppressed and its types (mtx_t/cnd_t/thrd_t/tss_t/etc) are re-derived like
// any other cccc-only header, but until now no *definition* of any of these
// functions existed anywhere reachable from the generated TU: they're VM
// cfuncs (src/stdlib/pthread.c), and a native binary has no VM to call into
// -- every use failed at the host linker with an undefined symbol.
//
// Deliberately a self-contained shim written over the real host <pthread.h>
// (already replayed via the #1022-widened auto-capture gate,
// preprocess.c:5304), NOT a #include_next hand-off onto a real host
// <threads.h> the way include/pthread.h itself hands off -- two reasons,
// both load-bearing (user sign-off): (1) CCCC's own thrd_error/thrd_timedout/
// thrd_busy/thrd_nomem encoding (include/threads.h) does not match glibc's,
// and those values are folded to bare integer literals at guest compile
// time, so any comparison other than `!= thrd_success` would silently change
// meaning once glibc's own enum reached the output -- the same FP_*/isnan
// asymmetry native_accessor_shims's own comment documents above; (2) Darwin
// has no <threads.h> at all, so a hand-off would leave macOS permanently
// unsupported. A self-contained shim closes both platforms in one change,
// consulting the host's own <threads.h> on neither.
//
// Each function below is a near-verbatim port of its VM cfunc counterpart in
// src/stdlib/pthread.c (named in each comment), minus the GIL save/release
// dance and the --thread-safety lock-order bookkeeping -- both meaningless
// without a VM -- but NOT a verbatim port of the VM's lazy mtx_t/cnd_t
// handle allocation: ensure_mtx/ensure_cond (pthread.c:991/463) are
// check-then-malloc-then-store, safe only because the GIL serializes every
// VM cfunc call. Two real threads racing that check under -c=native's actual
// parallelism could each allocate a host mutex and store its own, silently
// locking two different mutexes -- a wrong answer, not a crash, and the
// wrong side of this batch's own "works on the VM -> correct natively" bar.
// __cccc_ensure_mtx/__cccc_ensure_cnd below use a real atomic
// compare-exchange on the ->__handle field instead, so exactly one raced
// allocation wins and every other caller adopts it.
static bool threads_shim_fn_is_used(VirtualMachine *vm, Obj *prog,
                                    const char *name) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->is_used || obj->body)
            continue;
        if (strcmp(obj->name, name) != 0)
            continue;
        Token *t = obj->tok;
        if (!t || !t->file)
            continue;
        if (!cc_file_is_cccc_only(vm, t->file->name))
            continue;
        if (!path_basename_is(t->file->name, "threads.h"))
            continue;
        return true;
    }
    return false;
}

static void serialize_threads_shims(FILE *f, VirtualMachine *vm, Obj *prog) {
    // #1088's own --emit-cccc exemption: under --emit-cccc the cccc-only
    // suppression above is exempted (see the include-replay loop's own
    // gate, cc_serialize_program) so `#include <threads.h>` IS replayed,
    // reaching a consumer cccc that already has the real cfuncs registered
    // -- emitting definitions here would shadow them with a second,
    // divergent implementation. Same gating as serialize_synth_setjmp_decls.
    if (vm->compiler.emit_cccc)
        return;

    bool use_thrd_create  = threads_shim_fn_is_used(vm, prog, "thrd_create");
    bool use_thrd_join    = threads_shim_fn_is_used(vm, prog, "thrd_join");
    bool use_thrd_exit    = threads_shim_fn_is_used(vm, prog, "thrd_exit");
    bool use_thrd_detach  = threads_shim_fn_is_used(vm, prog, "thrd_detach");
    bool use_thrd_yield   = threads_shim_fn_is_used(vm, prog, "thrd_yield");
    bool use_thrd_sleep   = threads_shim_fn_is_used(vm, prog, "thrd_sleep");
    bool use_thrd_current = threads_shim_fn_is_used(vm, prog, "thrd_current");
    bool use_thrd_equal   = threads_shim_fn_is_used(vm, prog, "thrd_equal");
    bool any_thrd     = use_thrd_create || use_thrd_join || use_thrd_exit ||
                        use_thrd_detach || use_thrd_yield || use_thrd_sleep ||
                        use_thrd_current || use_thrd_equal;

    bool use_mtx_init = threads_shim_fn_is_used(vm, prog, "mtx_init");
    bool use_mtx_lock = threads_shim_fn_is_used(vm, prog, "mtx_lock");
    bool use_mtx_trylock   = threads_shim_fn_is_used(vm, prog, "mtx_trylock");
    bool use_mtx_timedlock = threads_shim_fn_is_used(vm, prog, "mtx_timedlock");
    bool use_mtx_unlock    = threads_shim_fn_is_used(vm, prog, "mtx_unlock");
    bool use_mtx_destroy   = threads_shim_fn_is_used(vm, prog, "mtx_destroy");
    bool any_mtx      = use_mtx_init || use_mtx_lock || use_mtx_trylock ||
                        use_mtx_timedlock || use_mtx_unlock || use_mtx_destroy;

    bool use_cnd_init = threads_shim_fn_is_used(vm, prog, "cnd_init");
    bool use_cnd_wait = threads_shim_fn_is_used(vm, prog, "cnd_wait");
    bool use_cnd_signal    = threads_shim_fn_is_used(vm, prog, "cnd_signal");
    bool use_cnd_broadcast = threads_shim_fn_is_used(vm, prog, "cnd_broadcast");
    bool use_cnd_timedwait = threads_shim_fn_is_used(vm, prog, "cnd_timedwait");
    bool use_cnd_destroy   = threads_shim_fn_is_used(vm, prog, "cnd_destroy");
    bool any_cnd = use_cnd_init || use_cnd_wait || use_cnd_signal ||
                   use_cnd_broadcast || use_cnd_timedwait || use_cnd_destroy;

    bool use_tss_create = threads_shim_fn_is_used(vm, prog, "tss_create");
    bool use_tss_get    = threads_shim_fn_is_used(vm, prog, "tss_get");
    bool use_tss_set    = threads_shim_fn_is_used(vm, prog, "tss_set");
    bool use_tss_delete = threads_shim_fn_is_used(vm, prog, "tss_delete");
    bool any_tss =
        use_tss_create || use_tss_get || use_tss_set || use_tss_delete;

    bool use_call_once = threads_shim_fn_is_used(vm, prog, "call_once");

    if (!any_thrd && !any_mtx && !any_cnd && !any_tss && !use_call_once)
        return;

    // Self-contained #includes rather than trusting the nested-include
    // capture, following the __cccc_iseqsig_* precedent above -- harmless if
    // repeated thanks to each header's own include guard.
    fprintf(f, "#include <pthread.h>\n"
               "#include <time.h>\n"
               "#include <errno.h>\n"
               "#include <stdlib.h>\n");
    // #1054-class hazard: CCCC's own bundled include/sched.h and
    // include/string.h have no #include_next hand-off, so a plain
    // `#include` of either here (under the same -I./include forwarding
    // every other replayed header sees) would re-pull CCCC's own
    // polyfill copies, colliding with the real ones already reached via
    // <pthread.h>'s own hand-off (struct sched_param redefinition,
    // confirmed). sched_yield() is declared directly instead
    // (POSIX-portable, no header needed); memcpy is replaced by the
    // portable __builtin_memcpy below, avoiding <string.h> entirely.
    // call_once's spin-wait (below) also needs sched_yield -- see its own
    // comment for why.
    if (use_thrd_yield || use_call_once)
        fprintf(f, "extern int sched_yield(void);\n");
    // <stdatomic.h> is NOT usable here for the same reason <sched.h>/
    // <string.h> aren't above, but for a stricter cause: it's on
    // is_compiler_owned_header() (preprocess.c), so force_cccc makes
    // search_include_paths() resolve a plain #include to CCCC's own
    // macro-based polyfill unconditionally -- even under
    // --use-system-headers -- rather than ever reaching the real host
    // <stdatomic.h>. CCCC's own copy expands atomic_compare_exchange_strong
    // to __builtin_compare_and_swap, a CCCC-internal builtin absent on a
    // real clang/gcc ("use of undeclared identifier", confirmed). The
    // call_once shim below uses the plain __atomic_compare_exchange_n
    // builtin on a pointer-to-plain-int instead (like __cccc_ensure_mtx's
    // ->__handle CAS above), reached by casting away once_flag's own
    // _Atomic qualifier -- the same reason that cast is needed here as for
    // the VM-side wrap_call_once (src/stdlib/pthread.c): passing a pointer
    // to an _Atomic-qualified type straight to the GCC/clang __atomic_*
    // builtins is rejected outright ("address argument to atomic operation
    // must be a pointer to integer or pointer"), since the compiler treats
    // that argument shape as a request for the C11 stdatomic API instead.

    if (any_thrd) {
        // thrd_t is re-derived as CCCC's own `pthread_t` polyfill
        // (include/pthread.h: `typedef void *pthread_t;`), i.e. a plain
        // void*, while the real host pthread_t is `unsigned long` on glibc
        // and an opaque pointer on Darwin -- both exactly pointer-sized, but
        // not the same *type*, so a plain cast is not portable. The shims
        // below round-trip through __builtin_memcpy instead of a cast
        // (avoiding a <string.h> dependency -- see the sched_yield comment
        // below for why that header can't just be #include-d here); the
        // _Static_assert makes the sizing assumption checked rather than
        // silently assumed.
        fprintf(f,
                "_Static_assert(sizeof(pthread_t) <= sizeof(void *),\n"
                "               \"cccc: host pthread_t must fit in a "
                "pointer-sized thrd_t\");\n"
                "struct __cccc_thrd_args { int (*fn)(void *); void *arg; };\n"
                "static void *__cccc_thrd_trampoline(void *argp) {\n"
                "    struct __cccc_thrd_args *a = "
                "(struct __cccc_thrd_args *)argp;\n"
                "    int rc = a->fn(a->arg);\n"
                "    free(a);\n"
                "    return (void *)(long)rc;\n"
                "}\n");
    }

    if (any_mtx || any_cnd) {
        // Port of ensure_mtx (src/stdlib/pthread.c:991-1014), with the
        // lazy-allocation race closed by an atomic compare-exchange on
        // ->__handle instead of the VM's GIL-only check-then-store (see
        // this function's own comment above). mtx_recursive (1, CCCC's own
        // enum, include/threads.h) is remapped to the real host
        // PTHREAD_MUTEX_RECURSIVE -- forwarding ->__type straight through
        // would be wrong, since CCCC's C11 enum and the host's pthread
        // mutex-type constants don't share a numbering.
        fprintf(f, "static pthread_mutex_t *__cccc_ensure_mtx(mtx_t *mtx) {\n"
                   "    if (!mtx) return NULL;\n"
                   "    void *h = __atomic_load_n(&mtx->__handle, "
                   "__ATOMIC_ACQUIRE);\n"
                   "    if (h) return (pthread_mutex_t *)h;\n"
                   "    pthread_mutex_t *host = malloc(sizeof(*host));\n"
                   "    if (!host) return NULL;\n"
                   "    pthread_mutexattr_t attr;\n"
                   "    pthread_mutexattr_init(&attr);\n"
                   "    if (mtx->__type == 1)\n"
                   "        pthread_mutexattr_settype(&attr, "
                   "PTHREAD_MUTEX_RECURSIVE);\n"
                   "    if (pthread_mutex_init(host, &attr) != 0) {\n"
                   "        pthread_mutexattr_destroy(&attr);\n"
                   "        free(host);\n"
                   "        return NULL;\n"
                   "    }\n"
                   "    pthread_mutexattr_destroy(&attr);\n"
                   "    void *expected = NULL;\n"
                   "    if (!__atomic_compare_exchange_n(&mtx->__handle, "
                   "&expected, host, 0,\n"
                   "                                      __ATOMIC_ACQ_REL, "
                   "__ATOMIC_ACQUIRE)) {\n"
                   "        pthread_mutex_destroy(host);\n"
                   "        free(host);\n"
                   "        return (pthread_mutex_t *)expected;\n"
                   "    }\n"
                   "    mtx->__state = 1;\n"
                   "    return host;\n"
                   "}\n");
    }
    if (any_cnd) {
        // Port of ensure_cond (src/stdlib/pthread.c:463-478); same
        // atomic-compare-exchange race closure as __cccc_ensure_mtx above.
        fprintf(f, "static pthread_cond_t *__cccc_ensure_cnd(cnd_t *cond) {\n"
                   "    if (!cond) return NULL;\n"
                   "    void *h = __atomic_load_n(&cond->__handle, "
                   "__ATOMIC_ACQUIRE);\n"
                   "    if (h) return (pthread_cond_t *)h;\n"
                   "    pthread_cond_t *host = malloc(sizeof(*host));\n"
                   "    if (!host) return NULL;\n"
                   "    if (pthread_cond_init(host, NULL) != 0) {\n"
                   "        free(host);\n"
                   "        return NULL;\n"
                   "    }\n"
                   "    void *expected = NULL;\n"
                   "    if (!__atomic_compare_exchange_n(&cond->__handle, "
                   "&expected, host, 0,\n"
                   "                                      __ATOMIC_ACQ_REL, "
                   "__ATOMIC_ACQUIRE)) {\n"
                   "        pthread_cond_destroy(host);\n"
                   "        free(host);\n"
                   "        return (pthread_cond_t *)expected;\n"
                   "    }\n"
                   "    cond->__state = 1;\n"
                   "    return host;\n"
                   "}\n");
    }

    // ---- Thread lifecycle (port of pthread.c:923-981) ----
    if (use_thrd_create)
        fprintf(f,
                "int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {\n"
                "    struct __cccc_thrd_args *a = malloc(sizeof(*a));\n"
                "    if (!a) return ENOMEM;\n"
                "    a->fn = func;\n"
                "    a->arg = arg;\n"
                "    pthread_t host;\n"
                "    int rc = pthread_create(&host, NULL, "
                "__cccc_thrd_trampoline, a);\n"
                "    if (rc != 0) {\n"
                "        free(a);\n"
                "        return rc == ENOMEM ? ENOMEM : 1;\n"
                "    }\n"
                "    thrd_t out = 0;\n"
                "    __builtin_memcpy(&out, &host, sizeof(host));\n"
                "    *thr = out;\n"
                "    return 0;\n"
                "}\n");
    if (use_thrd_join)
        fprintf(f, "int thrd_join(thrd_t thr, int *res) {\n"
                   "    pthread_t host;\n"
                   "    __builtin_memcpy(&host, &thr, sizeof(host));\n"
                   "    void *retval = NULL;\n"
                   "    if (pthread_join(host, &retval) != 0) return 1;\n"
                   "    if (res) *res = (int)(long)retval;\n"
                   "    return 0;\n"
                   "}\n");
    if (use_thrd_exit)
        // Matches thrd_create's trampoline encoding: returning `rc` from the
        // thread function is equivalent (POSIX) to pthread_exit() with that
        // same value, so thrd_join's narrowing agrees regardless of which
        // path a thread actually exits through.
        fprintf(f, "_Noreturn void thrd_exit(int res) {\n"
                   "    pthread_exit((void *)(long)res);\n"
                   "}\n");
    if (use_thrd_detach)
        fprintf(f, "int thrd_detach(thrd_t thr) {\n"
                   "    pthread_t host;\n"
                   "    __builtin_memcpy(&host, &thr, sizeof(host));\n"
                   "    return pthread_detach(host) == 0 ? 0 : "
                   "1;\n"
                   "}\n");
    if (use_thrd_yield)
        fprintf(f, "void thrd_yield(void) { sched_yield(); }\n");
    if (use_thrd_sleep)
        fprintf(f, "int thrd_sleep(const struct timespec *duration, struct "
                   "timespec *remaining) {\n"
                   "    if (!duration) return -2;\n"
                   "    int rc = nanosleep(duration, remaining);\n"
                   "    if (rc == 0) return 0;\n"
                   "    return errno == EINTR ? -1 : -2;\n"
                   "}\n");
    if (use_thrd_current)
        fprintf(f, "thrd_t thrd_current(void) {\n"
                   "    pthread_t self = pthread_self();\n"
                   "    thrd_t out = 0;\n"
                   "    __builtin_memcpy(&out, &self, sizeof(self));\n"
                   "    return out;\n"
                   "}\n");
    if (use_thrd_equal)
        fprintf(f, "int thrd_equal(thrd_t a, thrd_t b) {\n"
                   "    pthread_t pa, pb;\n"
                   "    __builtin_memcpy(&pa, &a, sizeof(pa));\n"
                   "    __builtin_memcpy(&pb, &b, sizeof(pb));\n"
                   "    return pthread_equal(pa, pb) != 0;\n"
                   "}\n");

    // ---- Mutex (port of pthread.c:1016-1130) ----
    if (use_mtx_init)
        fprintf(f,
                "int mtx_init(mtx_t *mtx, int type) {\n"
                "    if (!mtx) return 1;\n"
                "    if (__atomic_load_n(&mtx->__handle, __ATOMIC_ACQUIRE))\n"
                "        return 1;\n"
                "    mtx->__type = type;\n"
                "    return __cccc_ensure_mtx(mtx) ? 0 : "
                "1;\n"
                "}\n");
    if (use_mtx_lock)
        fprintf(f, "int mtx_lock(mtx_t *mtx) {\n"
                   "    pthread_mutex_t *host = __cccc_ensure_mtx(mtx);\n"
                   "    if (!host) return 1;\n"
                   "    return pthread_mutex_lock(host) == 0 ? 0 : "
                   "1;\n"
                   "}\n");
    if (use_mtx_trylock)
        fprintf(f, "int mtx_trylock(mtx_t *mtx) {\n"
                   "    pthread_mutex_t *host = __cccc_ensure_mtx(mtx);\n"
                   "    if (!host) return 1;\n"
                   "    int rc = pthread_mutex_trylock(host);\n"
                   "    if (rc == 0) return 0;\n"
                   "    return rc == EBUSY ? EBUSY : 1;\n"
                   "}\n");
    if (use_mtx_timedlock)
        // #824 note: this is not new lossy emulation -- it is byte-for-byte
        // the same __linux__ / trylock-poll split the VM's own
        // wrap_mtx_timedlock already ships (pthread.c:1067-1105), matching
        // existing CCCC behaviour rather than inventing a new one. macOS has
        // no pthread_mutex_timedlock at all.
        //
        // The macOS branch's own clock_gettime(CLOCK_REALTIME, ...) can't
        // reach either declaration via a plain #include: <time.h> is NOT on
        // this function's own #include list above (nor is it usable if it
        // were -- same #1054-class hazard as <sched.h>/<string.h>, and
        // documented in man/HEADERS.md's own pthread_native_1022 writeup as
        // the reason CCCC never gave <time.h> itself a full #include_next
        // hand-off: the cascade has no clean stopping point). clock_gettime
        // is declared directly instead (POSIX-portable, no header needed);
        // CLOCK_REALTIME is spelled as its own literal value (0 on both
        // glibc and Darwin, confirmed) rather than the macro name, the same
        // "spell CCCC's own fixed values as literals" precedent
        // native_accessor_shims's own FP_*/fpclassify comment documents --
        // this branch never runs on Linux, so only Darwin's value matters.
        fprintf(f,
                "int mtx_timedlock(mtx_t *mtx, const struct timespec *ts) {\n"
                "    pthread_mutex_t *host = __cccc_ensure_mtx(mtx);\n"
                "    if (!host || !ts) return 1;\n"
                "    int rc;\n"
                "#if defined(__linux__)\n"
                "    rc = pthread_mutex_timedlock(host, ts);\n"
                "#else\n"
                "    extern int clock_gettime(int, struct timespec *);\n"
                "    for (;;) {\n"
                "        rc = pthread_mutex_trylock(host);\n"
                "        if (rc == 0) break;\n"
                "        struct timespec now;\n"
                "        clock_gettime(0 /* CLOCK_REALTIME */, &now);\n"
                "        if (now.tv_sec > ts->tv_sec ||\n"
                "            (now.tv_sec == ts->tv_sec && now.tv_nsec >= "
                "ts->tv_nsec)) {\n"
                "            rc = ETIMEDOUT;\n"
                "            break;\n"
                "        }\n"
                "        struct timespec delay = {0, 1000000};\n"
                "        nanosleep(&delay, NULL);\n"
                "    }\n"
                "#endif\n"
                "    if (rc == 0) return 0;\n"
                "    return rc == ETIMEDOUT ? ETIMEDOUT : 1;\n"
                "}\n");
    if (use_mtx_unlock)
        fprintf(f, "int mtx_unlock(mtx_t *mtx) {\n"
                   "    if (!mtx || !mtx->__handle) return 1;\n"
                   "    return pthread_mutex_unlock((pthread_mutex_t "
                   "*)mtx->__handle) == 0 ? 0 : 1;\n"
                   "}\n");
    if (use_mtx_destroy)
        fprintf(f, "void mtx_destroy(mtx_t *mtx) {\n"
                   "    if (!mtx || !mtx->__handle) return;\n"
                   "    pthread_mutex_destroy((pthread_mutex_t "
                   "*)mtx->__handle);\n"
                   "    free(mtx->__handle);\n"
                   "    mtx->__handle = NULL;\n"
                   "    mtx->__state = 0;\n"
                   "}\n");

    // ---- Condition variable (port of pthread.c:1132-1178) ----
    if (use_cnd_init)
        fprintf(f, "int cnd_init(cnd_t *cond) {\n"
                   "    if (!cond) return 1;\n"
                   "    return __cccc_ensure_cnd(cond) ? 0 : "
                   "1;\n"
                   "}\n");
    if (use_cnd_wait)
        fprintf(f, "int cnd_wait(cnd_t *cond, mtx_t *mtx) {\n"
                   "    pthread_cond_t *c = __cccc_ensure_cnd(cond);\n"
                   "    pthread_mutex_t *m = __cccc_ensure_mtx(mtx);\n"
                   "    if (!c || !m) return 1;\n"
                   "    return pthread_cond_wait(c, m) == 0 ? 0 : "
                   "1;\n"
                   "}\n");
    if (use_cnd_signal)
        fprintf(f, "int cnd_signal(cnd_t *cond) {\n"
                   "    pthread_cond_t *c = __cccc_ensure_cnd(cond);\n"
                   "    if (!c) return 1;\n"
                   "    return pthread_cond_signal(c) == 0 ? 0 : "
                   "1;\n"
                   "}\n");
    if (use_cnd_broadcast)
        fprintf(f, "int cnd_broadcast(cnd_t *cond) {\n"
                   "    pthread_cond_t *c = __cccc_ensure_cnd(cond);\n"
                   "    if (!c) return 1;\n"
                   "    return pthread_cond_broadcast(c) == 0 ? 0 : "
                   "1;\n"
                   "}\n");
    if (use_cnd_timedwait)
        fprintf(f, "int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct "
                   "timespec *ts) {\n"
                   "    pthread_cond_t *c = __cccc_ensure_cnd(cond);\n"
                   "    pthread_mutex_t *m = __cccc_ensure_mtx(mtx);\n"
                   "    if (!c || !m || !ts) return 1;\n"
                   "    int rc = pthread_cond_timedwait(c, m, ts);\n"
                   "    if (rc == 0) return 0;\n"
                   "    return rc == ETIMEDOUT ? ETIMEDOUT : 1;\n"
                   "}\n");
    if (use_cnd_destroy)
        fprintf(f, "void cnd_destroy(cnd_t *cond) {\n"
                   "    if (!cond || !cond->__handle) return;\n"
                   "    pthread_cond_destroy((pthread_cond_t "
                   "*)cond->__handle);\n"
                   "    free(cond->__handle);\n"
                   "    cond->__handle = NULL;\n"
                   "    cond->__state = 0;\n"
                   "}\n");

    // ---- Thread-specific storage (port of pthread.c:1180-1195) ----
    // tss_t is re-derived as a plain alias of the host's own pthread_key_t
    // (include/threads.h: `typedef pthread_key_t tss_t;`, and pthread_key_t
    // itself comes from the replayed real <pthread.h>) and tss_dtor_t
    // (`void (*)(void *)`) already matches pthread's own destructor
    // signature exactly -- so these forward straight through, no adapter
    // needed.
    if (use_tss_create)
        fprintf(f, "int tss_create(tss_t *key, tss_dtor_t dtor) {\n"
                   "    return pthread_key_create(key, dtor) == 0 ? "
                   "0 : 1;\n"
                   "}\n");
    if (use_tss_get)
        fprintf(f,
                "void *tss_get(tss_t key) { return pthread_getspecific(key); "
                "}\n");
    if (use_tss_set)
        fprintf(f, "int tss_set(tss_t key, void *val) {\n"
                   "    return pthread_setspecific(key, val) == 0 ? "
                   "0 : 1;\n"
                   "}\n");
    if (use_tss_delete)
        fprintf(f, "void tss_delete(tss_t key) { pthread_key_delete(key); "
                   "}\n");

    // ---- call_once (#1088; see include/threads.h's own comment on why
    // this is a real function now, not a macro) ----
    //
    // Three states, not a plain two-state CAS: 0 (not started) -> 1 (in
    // progress) -> 2 (done). A first attempt used a plain 0->1
    // compare-exchange with no wait for the losing side, mirroring
    // wrap_call_once's own VM-side CAS -- but that's only correct there
    // because the GIL serializes every cfunc call end-to-end: a losing
    // guest thread can't even enter wrap_call_once until the winning
    // thread's own call (guest callback included) has already returned and
    // released the GIL, so the winner's func() is unconditionally done by
    // the time any loser observes the flag. -c=native has no GIL, so a
    // losing thread reaching the two-state version could return, and a
    // caller relying on call_once to have initialized shared state before
    // proceeding (the standard idiom) would race -- caught by stress-
    // running tests/test_threads_call_once_1088.c (occasional non-42 exit
    // out of dozens of runs). The 1 (in-progress) state gives every losing
    // thread something to spin-wait on until the winner stores 2, matching
    // real pthread_once/glibc's own blocking behaviour, which is what
    // C11 programs actually rely on in practice even though 7.26.6.2p2's
    // literal text only promises a happens-before ordering.
    if (use_call_once)
        fprintf(f,
                "void call_once(once_flag *flag, void (*func)(void)) {\n"
                "    int *raw = (int *)flag;\n"
                "    int expected = 0;\n"
                "    if (__atomic_compare_exchange_n(raw, &expected, 1, 0,\n"
                "                                     __ATOMIC_ACQ_REL, "
                "__ATOMIC_ACQUIRE)) {\n"
                "        func();\n"
                "        __atomic_store_n(raw, 2, __ATOMIC_RELEASE);\n"
                "    } else {\n"
                "        while (__atomic_load_n(raw, __ATOMIC_ACQUIRE) != 2)\n"
                "            sched_yield();\n"
                "    }\n"
                "}\n");

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
// asm-labeled declaration since it's used there as `asm(__CCCC_ASM_PREFIX__
// "name")`.
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
               "#define __CCCC_ASM_PREFIX__ "
               "__cccc_asm_str1(__USER_LABEL_PREFIX__)\n\n");
}

// #925/#928: new_anon_gvar() (parse.c) and reflect_new_anon_gvar()
// (reflection.c) both hand out the same `.L..N` name to string literals,
// static locals, and compound literals alike -- a dot isn't a valid C
// identifier character, so every non-string-literal use needs a real name
// before anything below references it. Runs once, before any
// collection/emission pass, so every later `is_string_literal`/dotted-name
// check sees the final state. Also runs under generated_only (-c=generated):
// #928 found that reflection API compound-literal/init-struct globals built
// while running under -c=generated (e.g. a comptime macro calling
// CompoundLiteral()/ InitArray()/InitStruct() at file scope) hit this exact gap
// when renaming was skipped here -- the emit-event walk's own dotted-name skip
// (see `obj->name[0] != '.'` further down) only prevented emitting a bogus
// reference, it never gave the global a real name or definition.
static void rename_anon_globals(VirtualMachine *vm, Obj *prog,
                                SerializeContext *ctx) {
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
                              ? obj->display_name
                              : "anon";
        obj->name =
            arena_format(vm, "__cccc_%s_%d", tag, ctx->anon_global_counter++);
        // An anonymous global (compound literal or static local) can never
        // be referenced from another translation unit -- internal linkage
        // makes the #918 forward-declaration pass ahead of global
        // definitions emit a valid `static T name;` + `static T name = ...;`
        // tentative-definition pair instead of `extern` plus an external
        // definition.
        obj->is_static = true;
    }
}

// #1044: best-effort descent over every statement/expression child field a
// Node can have, used by collect_deferred_static_labels() below for two
// purposes -- finding every ND_LABEL reachable from a function body, and
// checking whether a given Obj is referenced from outside its supposed
// owner. A node kind this doesn't know to visit through (there is no
// exhaustive list of Node's child fields, unlike serialize_stmt/
// serialize_expr's own kind-by-kind switches) just means a label or
// reference goes unnoticed -- which only ever makes collect_deferred_
// static_labels() more conservative, falling back to today's existing
// "unresolved relocation target" hard error rather than emitting broken C.
// A node reachable through more than one field (e.g. a switch's case_next
// list overlaps its body's next-chain) is visited only once -- see the
// dedup set below, load-bearing rather than an optimization.
//
// Deliberately an explicit heap-backed work stack, not recursion: a real-
// world program's AST (tests/test_minilua.c, a ~30k-line single-file Lua
// interpreter) both chains thousands of statements through `next` and
// nests expressions (long `lhs`/`rhs` chains from deeply parenthesized or
// chained-operator source) deep enough to overflow the C call stack either
// way -- confirmed by AddressSanitizer during development (SIGSEGV/stack-
// overflow at what looked, from the outside, like an unrelated later
// function, since the corrupted-vs-overflowed stack's actual fault site is
// disconnected from which AST this walk was ever asked to cover).
//
// A dedup set (open-addressed, keyed by pointer identity) is load-bearing,
// not an optimization: a node reachable through more than one of these
// fields (the same case_next/body overlap noted above) would otherwise get
// its *entire* subtree re-pushed once per incoming path -- for a large,
// heavily-shared DAG this is exponential, not merely wasteful, and blew up
// into an integer-overflowing allocation request during development on
// this exact file. Marking a node visited the moment it's popped, before
// its children are ever pushed, bounds total work to one push per (node,
// field) pair regardless of how many paths reach that node.
typedef struct {
    Node **slots;
    int    cap; // power of two, 0 means not yet allocated
    int    count;
} NodeSet;

static bool node_set_add(NodeSet *set, Node *n) {
    if (set->count * 4 >= set->cap * 3) { // grow at 75% load (also the
                                          // initial 0/0 case)
        int    old_cap = set->cap;
        Node **old     = set->slots;
        set->cap       = old_cap ? old_cap * 2 : 1024;
        set->slots     = calloc(set->cap, sizeof(*set->slots));
        set->count     = 0;
        for (int i = 0; i < old_cap; i++)
            if (old[i])
                node_set_add(set, old[i]); // reinsert into the new table
        free(old);
    }
    uintptr_t h = (uintptr_t)n >> 4;       // Node* is always more than 16-byte
                                           // aligned in practice; spreads bits
    int idx = (int)(h & (uintptr_t)(set->cap - 1));
    while (set->slots[idx] && set->slots[idx] != n)
        idx = (idx + 1) & (set->cap - 1);
    if (set->slots[idx] == n)
        return false; // already present
    set->slots[idx] = n;
    set->count++;
    return true;
}

static void ast_walk_1044(Node *root, void (*visit)(Node *, void *),
                          void *ctx) {
    Node  **stack = NULL;
    int     len = 0, cap = 0;
    NodeSet seen = {0};
#define AST_WALK_1044_PUSH(child)                                              \
    do {                                                                       \
        Node *__c = (child);                                                   \
        if (__c) {                                                             \
            if (len == cap) {                                                  \
                cap   = cap ? cap * 2 : 256;                                   \
                stack = realloc(stack, cap * sizeof(*stack));                  \
            }                                                                  \
            stack[len++] = __c;                                                \
        }                                                                      \
    } while (0)

    AST_WALK_1044_PUSH(root);
    while (len > 0) {
        Node *n = stack[--len];
        if (!node_set_add(&seen, n))
            continue; // already visited via another path
        visit(n, ctx);
        AST_WALK_1044_PUSH(n->lhs);
        AST_WALK_1044_PUSH(n->rhs);
        AST_WALK_1044_PUSH(n->cond);
        AST_WALK_1044_PUSH(n->then);
        AST_WALK_1044_PUSH(n->els);
        AST_WALK_1044_PUSH(n->init);
        AST_WALK_1044_PUSH(n->inc);
        AST_WALK_1044_PUSH(n->body);
        AST_WALK_1044_PUSH(n->args);
        AST_WALK_1044_PUSH(n->case_next);
        AST_WALK_1044_PUSH(n->default_case);
        AST_WALK_1044_PUSH(n->next);
    }
#undef AST_WALK_1044_PUSH
    free(stack);
    free(seen.slots);
}

typedef struct {
    SerializeContext *ctx;
    Obj              *owner_fn;
} LabelCollectCtx;

static void collect_label_visit(Node *n, void *vctx) {
    if (n->kind != ND_LABEL || !n->unique_label)
        return;
    LabelCollectCtx *lc = vctx;
    if (find_label_owner(lc->ctx, n->unique_label))
        return; // already recorded (e.g. reached twice via case_next)
    if (lc->ctx->label_owners_len == lc->ctx->label_owners_cap) {
        lc->ctx->label_owners_cap =
            lc->ctx->label_owners_cap ? lc->ctx->label_owners_cap * 2 : 8;
        lc->ctx->label_owners =
            realloc(lc->ctx->label_owners,
                    lc->ctx->label_owners_cap * sizeof(*lc->ctx->label_owners));
    }
    LabelOwner *entry   = &lc->ctx->label_owners[lc->ctx->label_owners_len++];
    entry->unique_label = n->unique_label;
    entry->label        = n->label;
    entry->owner_fn     = lc->owner_fn;
}

typedef struct {
    Obj *var;
    bool found;
} VarRefCtx;

static void var_ref_visit(Node *n, void *vctx) {
    VarRefCtx *vr = vctx;
    if (n->var == vr->var)
        vr->found = true;
}

// #1044: an anonymous global whose relocation(s) resolve only against a
// label (never against a real Obj -- see serialize_reloc_init()'s own
// comment) must be defined inside the one function that owns that label
// instead of at file scope. Builds ctx->label_owners (every label in the
// program) and then ctx->deferred_label_statics (the subset of globals that
// actually need this treatment), run once from cc_serialize_program()
// immediately after the renaming passes above and before anything else
// reads obj->name/rel. A candidate referenced from more than one function
// (a block literal or nested function lexically inside the owner, which
// -c=native lifts to its own separate file-scope C function, #965/#1074) is
// deliberately left undeferred -- deferring it would only trade today's
// clean "unresolved relocation target" diagnostic for a "use of undeclared
// identifier" one from the host compiler, the opposite of the #918
// fail-loudly policy this file follows throughout.
static void collect_deferred_static_labels(VirtualMachine *vm, Obj *prog,
                                           SerializeContext *ctx) {
    // Cheap early-out for the common case: this whole pass (two full-
    // program AST walks below, one per candidate) only matters when at
    // least one non-function global has a Relocation that doesn't resolve
    // to a real Obj -- true only for a labels-as-values dispatch table, a
    // vanishingly rare construct. Every other program (the overwhelming
    // majority compiled with `-m`/`-c=native`) skips straight past this
    // function for free.
    bool any_unresolved_reloc = false;
    for (Obj *var = prog; var && !any_unresolved_reloc; var = var->next) {
        if (var->is_function || !var->rel)
            continue;
        for (Relocation *rel = var->rel; rel; rel = rel->next) {
            if (rel->label && *rel->label &&
                !serialize_find_global(vm, *rel->label)) {
                any_unresolved_reloc = true;
                break;
            }
        }
    }
    if (!any_unresolved_reloc)
        return;

    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function || !fn->body)
            continue;
        LabelCollectCtx lc = {.ctx = ctx, .owner_fn = fn};
        ast_walk_1044(fn->body, collect_label_visit, &lc);
    }

    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function || !var->rel)
            continue;
        Obj *owner = NULL;
        for (Relocation *rel = var->rel; rel; rel = rel->next) {
            if (!rel->label || !*rel->label)
                continue;
            if (serialize_find_global(vm, *rel->label))
                continue; // resolves to a real Obj -- not a label reference
            const LabelOwner *lo = find_label_owner(ctx, *rel->label);
            if (lo)
                owner = lo->owner_fn;
        }
        if (!owner)
            continue;

        // Cross-function reference guard -- see this function's own
        // comment above.
        bool referenced_elsewhere = false;
        for (Obj *fn = prog; fn; fn = fn->next) {
            if (!fn->is_function || !fn->body || fn == owner)
                continue;
            VarRefCtx vr = {.var = var, .found = false};
            ast_walk_1044(fn->body, var_ref_visit, &vr);
            if (vr.found) {
                referenced_elsewhere = true;
                break;
            }
        }
        // Another global's own initializer taking `var`'s address (e.g.
        // `static void **p = tab;` reading a `static void *tab[] = {&&L};`
        // declared alongside it) is legal C, and `p` itself has nothing
        // wrong with its own relocation -- it resolves to a real Obj, so
        // it is never itself a deferral candidate and keeps its ordinary
        // file-scope definition. But once `var` moves inside its owner
        // function's body, that file-scope reference to it would name a
        // symbol that no longer exists at file scope ("use of undeclared
        // identifier"). Declining the deferral here falls back to the
        // pre-existing "unresolved relocation target" diagnostic for `var`
        // itself, same fail-loudly policy as the cross-function guard
        // above.
        if (!referenced_elsewhere) {
            for (Obj *other = prog; other && !referenced_elsewhere;
                 other      = other->next) {
                if (other == var || other->is_function || !other->rel)
                    continue;
                for (Relocation *rel = other->rel; rel; rel = rel->next) {
                    if (rel->label && *rel->label &&
                        serialize_find_global(vm, *rel->label) == var) {
                        referenced_elsewhere = true;
                        break;
                    }
                }
            }
        }
        if (referenced_elsewhere)
            continue;

        if (ctx->deferred_label_statics_len ==
            ctx->deferred_label_statics_cap) {
            ctx->deferred_label_statics_cap =
                ctx->deferred_label_statics_cap
                    ? ctx->deferred_label_statics_cap * 2
                    : 8;
            ctx->deferred_label_statics =
                realloc(ctx->deferred_label_statics,
                        ctx->deferred_label_statics_cap *
                            sizeof(*ctx->deferred_label_statics));
        }
        DeferredStaticLabel *entry =
            &ctx->deferred_label_statics[ctx->deferred_label_statics_len++];
        entry->var      = var;
        entry->owner_fn = owner;
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
// #1075: a nested function is always Obj.is_static (#1039), but its name
// may now legally collide with an ordinary, non-static, SAME-FILE outer
// function (distinct scope+linkage, C17 6.2.1p4) -- something this pass
// previously assumed could never happen (see the "same-file same-name
// would already be a parse-time redefinition error" comment below, still
// true for two ordinary statics). Handled with two hashmaps: `anchors`
// captures every non-static defining Obj's name in its own pass, up
// front, so a static/nested Obj can detect the collision regardless of
// prog's own ordering -- a nested function's Obj is always pushed ahead of
// its enclosing function's own (see codegen_func.c's "nested functions are
// compiled before their parents" comment), so a single combined pass would
// see the nested name registered FIRST and rename the wrong (non-static)
// side. A non-static name is never a rename candidate, so `anchors` is
// populated once and never mutated again.
// #1042(c): resolve the same platform-specific libc path find_libc()
// (src/vm.c) does, so this probe queries the SAME library the VM's own FFI
// path would -- deliberately NOT RTLD_DEFAULT/dlopen(NULL): those also
// search the main executable (this compiler process itself), so a static
// name matching one of cccc's OWN exported symbols would rename differently
// depending on which cccc binary happened to run it (stage0 `./cccc` vs.
// the full `build/cccc` with libbacktrace/readline linked in) -- emitted C
// must be a function of the host libc only, never of the compiler build.
static void *open_libc_handle_for_probe(void) {
#if defined(_WIN32)
    return NULL; // no dlopen/dlsym on this target; probe is a no-op there
#else
    static const char *const candidates[] = {
#if defined(__APPLE__)
        "/usr/lib/libSystem.dylib",
#elif defined(__linux__)
        "/lib64/libc.so.6",
        "/lib/x86_64-linux-gnu/libc.so.6",
        "/lib/aarch64-linux-gnu/libc.so.6", // glibc's aarch64 multiarch dir --
                                            // find_libc() (src/vm.c) is
                                            // missing this one too, but the
                                            // trailing bare "libc.so.6" below
                                            // still resolves it there via the
                                            // dynamic linker's own search path
        "/lib/libc.so.6",
        "/usr/lib64/libc.so.6",
        "/usr/lib/libc.so.6",
        "libc.so.6",
#elif defined(__FreeBSD__)
        "/lib/libc.so.7",
        "/usr/lib/libc.so.7",
#else
        "/lib/libc.so",
        "/usr/lib/libc.so",
#endif
        NULL,
    };
    for (int i = 0; candidates[i]; i++) {
        void *h = dlopen(candidates[i], RTLD_LAZY);
        if (h)
            return h;
    }
    return NULL;
#endif
}

// #1042(c) regression found on Linux/glibc 2.39 (Ubuntu 24.04): glibc has
// started exporting real symbols for some of the newer C23 <stdbit.h>
// functions (confirmed directly -- `dlsym` finds `stdc_leading_zeros_ui`
// there), so the probe below fired against a name that could never
// actually collide -- `<stdbit.h>` is `is_cccc_supplied_only_header`, its
// own `#include` is deliberately NEVER replayed to the host compiler
// (test_serialize_polyfill_header_not_replayed.c's own point), so no real
// declaration of that name ever reaches the emitted C for the host to see
// a "static declaration follows non-static declaration" collision against
// in the first place. Gate the whole probe on there being at least one
// ACTUALLY-replayed (non-cccc-only, non-setjmp.h, non-conditional-shell)
// `#include` anywhere in the program -- mirrors exactly the filter the
// `#include`-replay loop itself applies (`cc_serialize_program`, below in
// this file) -- so a program that never hands the host compiler a real
// header at all (this test; also any program with zero `#include`s) can
// never trip the probe, matching the actual hazard's own precondition.
// `emit_directives` captures every top-level directive CCCC replays --
// #include lines AND ordinary ones (#define/#pragma/...), the latter with
// no entry in `emit_include_paths` at all (only ever populated for the
// PP_INCLUDE case). Only a line WITH a resolved path is an #include in the
// first place; anything else (a re-derived cccc-only header's own #define
// lines included) must be skipped outright, not fall through to "no
// suppression rule matched, so this counts as real" -- that fallthrough was
// the actual bug in an earlier version of this function: a re-derived
// polyfill header's own #define lines have no `resolved` path either, so
// they hit every one of the three `resolved &&`-guarded skip conditions
// below as false and were wrongly counted as a real replayed include.
static bool any_real_include_replayed(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.emit_directives.len; i++) {
        char *line     = vm->compiler.emit_directives.data[i];
        char *resolved = hashmap_get(&vm->compiler.emit_include_paths, line);
        if (!resolved)
            continue; // not a captured #include line at all
        if (!vm->compiler.emit_cccc && cc_file_is_cccc_only(vm, resolved))
            continue;
        if (!vm->compiler.emit_cccc && path_basename_is(resolved, "setjmp.h"))
            continue;
        return true;
    }
    return false;
}

static void rename_colliding_static_names(VirtualMachine *vm, Obj *prog,
                                          SerializeContext *ctx) {
    HashMap anchors = {0}; // name -> the non-static Obj* that owns it
    HashMap claimed = {
        0}; // name -> first static Obj* claiming it (old semantics)
    // #1042(c): tests/test_minilua.c's own `static int getmode(...)` is
    // legal C in the source's own declaration order (its `#include
    // <unistd.h>` comes AFTER the static definition -- a later, weaker
    // declaration of an already-defined static doesn't redefine it) --
    // confirmed directly, `clang -fsyntax-only` on the real source compiles
    // clean. -c=native's own #include-replay block hoists every captured
    // include to the very top of the output, unconditionally, ahead of
    // every prototype/definition -- inverting that legal order and
    // manufacturing a "static declaration follows non-static declaration"
    // collision against macOS libc's real `mode_t getmode(...)` that the
    // user's program never actually has. Any static, defining Obj whose
    // name resolves in the host libc's own symbol namespace gets the same
    // "%s__cccc_dupN" rename this pass already applies for an ordinary
    // cross-TU collision -- renaming a static is always safe (file-local,
    // every reference resolves through the same Obj) regardless of what
    // name it lands on. Known, deliberate over-approximation: dlsym proves
    // a DEFINITION exists in the host's symbol namespace, not that a
    // replayed header actually DECLARES it -- harmless, since the rename is
    // invisible outside this one translation unit. `main` is excluded: it's
    // never actually a libc symbol collision candidate (dlsym would find
    // the process's own libc startup glue, not a real hazard), and renaming
    // it would break the emitted binary's entry point.
    void *libc_handle =
        any_real_include_replayed(vm) ? open_libc_handle_for_probe() : NULL;

    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_static || obj->is_macro_generated || obj->name[0] == '.')
            continue;
        bool is_defining =
            obj->is_function ? obj->body != NULL : obj->is_definition;
        if (!is_defining)
            continue;
        if (!hashmap_get(&anchors, obj->name))
            hashmap_put_borrowed(&anchors, obj->name, obj);
    }

    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_static || obj->is_macro_generated || obj->name[0] == '.')
            continue;
        // Only an Obj that actually reaches the output as a definition can
        // collide with another TU's same-named one -- a bodyless static
        // function prototype or a tentative (non-defining) declaration
        // prints nothing a host compiler would reject twice.
        bool is_defining =
            obj->is_function ? obj->body != NULL : obj->is_definition;
        if (!is_defining)
            continue;

        if (libc_handle && strcmp(obj->name, "main") != 0 &&
            dlsym(libc_handle, obj->name)) {
            obj->name = arena_format(vm, "%s__cccc_dup%d", obj->name,
                                     ctx->anon_global_counter++);
            continue;
        }

        if (hashmap_get(&anchors, obj->name)) {
            // Collides with a non-static name -- the non-static side must
            // keep it; always rename this one (covers #1075's nested
            // function shadowing a same-named outer function, same file or
            // not).
            obj->name = arena_format(vm, "%s__cccc_dup%d", obj->name,
                                     ctx->anon_global_counter++);
            continue;
        }

        Obj *first = hashmap_get(&claimed, obj->name);
        if (!first) {
            hashmap_put_borrowed(&claimed, obj->name, obj);
            continue;
        }
        // Only a genuine cross-TU collision -- two Objs of the same name
        // declared in different files -- needs renaming; same-file
        // same-name would already be a parse-time redefinition error for
        // two ordinary statics, EXCEPT when at least one is a nested
        // function's own hoisted Obj (#1075's other shape: two distinct
        // same-named nested functions, or a nested one colliding with an
        // outer static of the same name -- both now legal same-file C).
        const char *first_file =
            first->tok && first->tok->file ? first->tok->file->name : NULL;
        const char *this_file =
            obj->tok && obj->tok->file ? obj->tok->file->name : NULL;
        bool same_file       = files_are_same(first_file, this_file);
        bool nested_involved = (obj->is_nested && !obj->is_block) ||
                               (first->is_nested && !first->is_block);
        if (same_file && !nested_involved)
            continue;
        obj->name = arena_format(vm, "%s__cccc_dup%d", obj->name,
                                 ctx->anon_global_counter++);
    }
    hashmap_deinit_borrowed(&anchors);
    hashmap_deinit_borrowed(&claimed);
#if !defined(_WIN32)
    if (libc_handle)
        dlclose(libc_handle);
#endif
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
        Type *rep;        // representative (first-seen) Type* for this group
        int   name_len;
        char *name;
        int   first_seen; // lower = created earlier (creation-order index)
        bool  header_exposed; // a from_include tag/typedef record names this
                              // group
        bool extern_ref; // an externally-visible definition's type reaches this
                         // group
    } TagGroup;
    TagGroup *groups     = NULL;
    int       groups_len = 0, groups_cap = 0;
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
        if (rec->owner_fn !=
            NULL) // function-local: hoist_local_type_to_file_scope()'s
                  // territory
            continue;
        if (!type_is_complete_tagged(rec->ty)) // an incomplete record must
                                               // never define/claim a group
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
            groups     = realloc(groups, sizeof(TagGroup) * groups_cap);
            collided   = realloc(collided, sizeof(bool) * groups_cap);
        }
        groups[groups_len].rep      = rec->ty;
        groups[groups_len].name_len = rec->name_len;
        groups[groups_len].name     = rec->name;
        groups[groups_len].first_seen =
            groups_len; // creation order, since we walk creation-ordered
        groups[groups_len].header_exposed = false;
        groups[groups_len].extern_ref     = false;
        collided[groups_len]              = false;
        groups_len++;
    }

    if (groups_len == 0)
        return;

    // Mark actual name collisions: any two distinct groups sharing a name.
    for (int g1 = 0; g1 < groups_len; g1++)
        for (int g2 = g1 + 1; g2 < groups_len; g2++)
            if (groups[g1].name_len == groups[g2].name_len &&
                strncmp(groups[g1].name, groups[g2].name,
                        groups[g1].name_len) == 0) {
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
            if (collided[g] &&
                same_type_strong(ctx->typedefs[i].ty, groups[g].rep))
                groups[g].header_exposed = true;
    }

    // Tier 2: an externally-visible definition's type reaches this group --
    // needed because tier 1 alone can't pick the implementation TU's group
    // when the private TU happens to be listed (and hence created) first.
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_static)
            continue;
        bool is_defining =
            obj->is_function ? obj->body != NULL : obj->is_definition;
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
            continue; // already resolved as part of an earlier group's pass, or
                      // not a collider

        // Gather every group sharing this exact name.
        int members[64];
        int members_len = 0;
        for (int g2 = g; g2 < groups_len && members_len < 64; g2++)
            if (collided[g2] && groups[g2].rep &&
                groups[g2].name_len == groups[g].name_len &&
                strncmp(groups[g2].name, groups[g].name, groups[g].name_len) ==
                    0)
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
                groups[idx].rep =
                    NULL; // mark resolved, skip on future outer iterations
                continue;
            }
            char *new_name =
                arena_format(vm, "%.*s__cccc_dup%d", groups[idx].name_len,
                             groups[idx].name, ctx->anon_global_counter++);
            int   new_len = (int)strlen(new_name);
            Type *victim  = groups[idx].rep;
            for (int i = 0; i < ctx->tags_len; i++)
                if (same_type_strong(ctx->tags[i].ty, victim)) {
                    ctx->tags[i].name     = new_name;
                    ctx->tags[i].name_len = new_len;
                }
            for (int i = 0; i < ctx->typedefs_len; i++)
                if (!(ctx->typedefs[i].from_include &&
                      !ctx->typedefs[i].always_emit) &&
                    same_type_strong(ctx->typedefs[i].ty, victim))
                    ctx->typedefs[i].name     = new_name,
                    ctx->typedefs[i].name_len = new_len;
            groups[idx].rep  = NULL; // mark resolved
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
        int   first_seen;
        bool  header_exposed;
        bool  extern_ref;
        // #1017: the from_include record's file_path, captured alongside
        // header_exposed below -- names the header in the residual warning
        // when this group collides with an un-renameable Obj. May stay NULL
        // (TypeName.file_path itself can be NULL), in which case the
        // warning falls back to not naming a header.
        const char *header_path;
    } EnumGroup;
    EnumGroup *groups     = NULL;
    int        groups_len = 0, groups_cap = 0;

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
        TypeName *recs     = pass == 0 ? ctx->tags : ctx->typedefs;
        int       recs_len = pass == 0 ? ctx->tags_len : ctx->typedefs_len;
        for (int i = recs_len - 1; i >= 0; i--) {
            TypeName *rec = &recs[i];
            if (rec->owner_fn !=
                NULL) // function-local: not this pass's concern
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
                groups     = realloc(groups, sizeof(EnumGroup) * groups_cap);
            }
            groups[groups_len].rep            = rec->ty;
            groups[groups_len].first_seen     = groups_len;
            groups[groups_len].header_exposed = false;
            groups[groups_len].extern_ref     = false;
            groups[groups_len].header_path    = NULL;
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
        bool is_defining =
            obj->is_function ? obj->body != NULL : obj->is_definition;
        if (!is_defining || !obj->ty)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (type_reaches_group(obj->ty, groups[g].rep))
                groups[g].extern_ref = true;
    }

    // Every distinct enumerator name declared by at least one group,
    // resolved exactly once below.
    char **names     = NULL;
    int    names_len = 0, names_cap = 0;
    for (int g = 0; g < groups_len; g++)
        for (EnumConstant *ec = groups[g].rep->enum_constants; ec;
             ec               = ec->next) {
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
                names     = realloc(names, sizeof(char *) * names_cap);
            }
            names[names_len++] = ec->name;
        }

    for (int n = 0; n < names_len; n++) {
        const char *name = names[n];

        // Every distinct group declaring this exact enumerator name.
        int members[64];
        int members_len = 0;
        for (int g = 0; g < groups_len && members_len < 64; g++) {
            for (EnumConstant *ec = groups[g].rep->enum_constants; ec;
                 ec               = ec->next)
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
                        warn_tok(vm, colliding_obj->tok,
                                 CCCC_WARN_NATIVE_NAME_COLLISION,
                                 "enumerator '%s' is declared by an enum "
                                 "reached through a "
                                 "replayed #include ('%s') and cannot be "
                                 "renamed; the "
                                 "file-scope '%s' declared here cannot be "
                                 "renamed either, "
                                 "so the generated C will not compile",
                                 name, groups[idx].header_path, name);
                    else
                        warn_tok(vm, colliding_obj->tok,
                                 CCCC_WARN_NATIVE_NAME_COLLISION,
                                 "enumerator '%s' is declared by an enum "
                                 "reached through a "
                                 "replayed #include and cannot be renamed; the "
                                 "file-scope "
                                 "'%s' declared here cannot be renamed either, "
                                 "so the "
                                 "generated C will not compile",
                                 name, name);
                }
                continue;
            }
            char *new_name = arena_format(vm, "%s__cccc_dup%d", name,
                                          ctx->anon_global_counter++);
            if (ctx->enum_renames_len >= ctx->enum_renames_cap) {
                ctx->enum_renames_cap =
                    ctx->enum_renames_cap ? ctx->enum_renames_cap * 2 : 8;
                ctx->enum_renames =
                    realloc(ctx->enum_renames,
                            sizeof(EnumConstRename) * ctx->enum_renames_cap);
            }
            ctx->enum_renames[ctx->enum_renames_len].rep      = groups[idx].rep;
            ctx->enum_renames[ctx->enum_renames_len].orig     = (char *)name;
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
static const char *enum_const_spelling(SerializeContext *ctx, Type *ty,
                                       const char *name) {
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
static int collect_captured_path(char *key, int keylen, void *val,
                                 void *user_data) {
    (void)key;
    (void)keylen;
    SerializeContext *ctx = user_data;
    ctx->captured_paths   = realloc(
        ctx->captured_paths, sizeof(char *) * (ctx->captured_paths_len + 1));
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

static void serialize_synth_libc_includes(FILE *f, VirtualMachine *vm,
                                          Obj *prog) {
    SynthLibcDeclArray *reg = &vm->compiler.synth_libc_decls;
    const char         *emitted[32];
    int                 emitted_len = 0;
    bool                any         = false;
    for (int i = 0; i < reg->len; i++) {
        SynthLibcDecl *entry  = &reg->data[i];
        bool           called = false;
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
static void serialize_synth_setjmp_decls(FILE *f, VirtualMachine *vm,
                                         Obj *prog) {
    Obj *family[4] = {
        vm->compiler.builtin_setjmp,
        vm->compiler.builtin_longjmp,
        vm->compiler.builtin__setjmp,
        vm->compiler.builtin__longjmp,
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
    fprintf(f,
            "extern void _longjmp(void *, int) __attribute__((noreturn));\n\n");
}

// #1068: real-floating -> non-floating cast helpers, emitted on demand for
// -c=native/-m output -- see the ND_CAST case in serialize_expr (above in
// this file) for the full rationale. Near-verbatim ports of
// cccc_f64_to_i64/cccc_f32_to_i64/cccc_f64_to_u64/cccc_f32_to_u64
// (src/internal.h, #775/#780) so native output agrees with the VM's own
// F2I3/F2U3 opcodes by construction. Deliberately avoid <math.h>/<limits.h>:
// <math.h> is a bundled header whose own polyfill content (isnan() among
// it) would need the identical #include_next hand-off <fenv.h>/<setjmp.h>
// already need (see include/fenv.h's own comment) to resolve correctly
// under the real -I./include-forwarding harness, and there is no reason to
// take on that dependency here when a bit-pattern NaN test needs nothing
// from any header -- `x != x` is a reliable IEEE-754 NaN test (NaN is the
// only value unequal to itself) as long as the host isn't built with
// -ffast-math, which -c=native's own invocation never passes. The 2^63/2^64
// bounds and INT64_MIN/UINT64_MAX values are spelled as literals for the
// same reason internal.h's own versions are: `(double)LLONG_MAX` rounds
// *up* to exactly 2^63, so a "<=" guard against it would wrongly admit
// x == 2^63. `#pragma STDC FENV_ACCESS ON` is block-scoped to each
// function body (verified in-container to survive -O2/-O3 there; a
// file-scope pragma placed just once before all four would also work but
// would needlessly extend to the rest of the generated TU) -- without it,
// clang can fold the u64 helpers' guard back into the branchless
// double/float->uint64 lowering that spuriously raises FE_INVALID on
// x86_64 even for a proven in-range value, defeating the whole point of
// the trailing feclearexcept(FE_INVALID) below.
static const char *const f2i64_def =
    "static long long __cccc_f2i64(double x) {\n"
    "#pragma STDC FENV_ACCESS ON\n"
    "    if (x != x) { feraiseexcept(FE_INVALID); return 0; }\n"
    "    if (x >= 9223372036854775808.0) { feraiseexcept(FE_INVALID); return "
    "9223372036854775807LL; }\n"
    "    if (x < -9223372036854775808.0) { feraiseexcept(FE_INVALID); return "
    "(-9223372036854775807LL - 1); }\n"
    "    return (long long)x;\n"
    "}\n";
static const char *const f2i64_f32_def =
    "static long long __cccc_f2i64_f32(float x) {\n"
    "#pragma STDC FENV_ACCESS ON\n"
    "    if (x != x) { feraiseexcept(FE_INVALID); return 0; }\n"
    "    if (x >= 9223372036854775808.0f) { feraiseexcept(FE_INVALID); return "
    "9223372036854775807LL; }\n"
    "    if (x < -9223372036854775808.0f) { feraiseexcept(FE_INVALID); return "
    "(-9223372036854775807LL - 1); }\n"
    "    return (long long)x;\n"
    "}\n";
static const char *const f2u64_def =
    "static unsigned long long __cccc_f2u64(double x) {\n"
    "#pragma STDC FENV_ACCESS ON\n"
    "    if (x != x) { feraiseexcept(FE_INVALID); return 0; }\n"
    "    if (x >= 18446744073709551616.0) { feraiseexcept(FE_INVALID); return "
    "0xFFFFFFFFFFFFFFFFULL; }\n"
    "    if (x <= -1.0) { feraiseexcept(FE_INVALID); return 0; }\n"
    "    unsigned long long r = (unsigned long long)x;\n"
    "    feclearexcept(FE_INVALID);\n"
    "    return r;\n"
    "}\n";
static const char *const f2u64_f32_def =
    "static unsigned long long __cccc_f2u64_f32(float x) {\n"
    "#pragma STDC FENV_ACCESS ON\n"
    "    if (x != x) { feraiseexcept(FE_INVALID); return 0; }\n"
    "    if (x >= 18446744073709551616.0f) { feraiseexcept(FE_INVALID); return "
    "0xFFFFFFFFFFFFFFFFULL; }\n"
    "    if (x <= -1.0f) { feraiseexcept(FE_INVALID); return 0; }\n"
    "    unsigned long long r = (unsigned long long)x;\n"
    "    feclearexcept(FE_INVALID);\n"
    "    return r;\n"
    "}\n";

typedef struct {
    bool want_i64, want_i64_f32, want_u64, want_u64_f32;
} F2ISynthNeed;

// Same recursive-field traversal shape as node_calls_obj (above) --
// exhaustive over every child-pointing field Node has, not just the ones
// this particular predicate happens to reach in this repo's own test
// corpus.
static void node_scan_f2i_native(Node *node, F2ISynthNeed *need) {
    if (!node)
        return;
    if (node->kind == ND_CAST && node->lhs) {
        Type *dst = node->ty;
        Type *src = node->lhs->ty;
        if (src && dst && is_flonum(src) && !is_flonum(dst) &&
            dst->kind != TY_VECTOR &&
            !(dst->kind == TY_BITINT && dst->bit_width > 64)) {
            bool u64_dst =
                is_integer(dst) && dst->is_unsigned && dst->size == 8;
            bool f32_src = src->kind == TY_FLOAT;
            if (u64_dst && f32_src)
                need->want_u64_f32 = true;
            else if (u64_dst)
                need->want_u64 = true;
            else if (f32_src)
                need->want_i64_f32 = true;
            else
                need->want_i64 = true;
        }
    }
    node_scan_f2i_native(node->lhs, need);
    node_scan_f2i_native(node->rhs, need);
    node_scan_f2i_native(node->cond, need);
    node_scan_f2i_native(node->then, need);
    node_scan_f2i_native(node->els, need);
    node_scan_f2i_native(node->init, need);
    node_scan_f2i_native(node->inc, need);
    node_scan_f2i_native(node->body, need);
    node_scan_f2i_native(node->args, need);
    node_scan_f2i_native(node->next, need);
}

static void serialize_synth_f2i_helpers(FILE *f, Obj *prog) {
    F2ISynthNeed need = {0};
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->body)
            continue;
        node_scan_f2i_native(obj->body, &need);
    }
    if (!need.want_i64 && !need.want_i64_f32 && !need.want_u64 &&
        !need.want_u64_f32)
        return;
    fprintf(f, "#include <fenv.h>\n\n");
    if (need.want_i64)
        fprintf(f, "%s", f2i64_def);
    if (need.want_i64_f32)
        fprintf(f, "%s", f2i64_f32_def);
    if (need.want_u64)
        fprintf(f, "%s", f2u64_def);
    if (need.want_u64_f32)
        fprintf(f, "%s", f2u64_f32_def);
    fprintf(f, "\n");
}

// #1117: the bundled include/complex.h and include/tgmath.h spell every
// complex accessor as a cccc-internal builtin -- creal(z) -> __cccc_creal(z)
// etc. (include/complex.h), and tgmath's type-generic _Generic arms reach
// the l/f variants through cabsl/cargl (include/tgmath.h). Those names only
// have definitions inside the VM; parse_postfix.c lowers the calls to
// __builtin_creal*/__builtin_cimag*/__builtin_conj at AST level, but any
// plain spelled call that survives into the generated text as an ordinary
// identifier gets expanded by the HOST compiler instead -- and because
// run_native_backend forwards the guest's -I paths to the host cc
// (src/main.c), a replayed `#include <complex.h>`/`#include <tgmath.h>`
// resolves to CCCC's OWN bundled copies, whose macros then expand to
// __cccc_* names nothing ever defined for the host ("use of undeclared
// identifier '__cccc_creall'", test_suite_floats.c). Only the long-double
// arms happened to error under that corpus's exact invocation, but
// preprocessing the same output shows every double/float arm equally
// exposed depending on host/std -- so the whole family is emitted, not just
// the failing pair.
//
// Fix follows the #1050/#1054 synth-decl precedent: emit static inline
// definitions mapping each helper straight onto its __builtin_*, whenever
// the names are reachable. Reachable means ANY of:
//   - a captured #include replay resolved to a bundled complex.h or
//     tgmath.h (the replay re-defines creal/cabs/I/... as macros pointing
//     at __cccc_* for everything the host compiles afterwards),
//   - the program contains any TY_COMPLEX-typed object (the AST-level
//     lowering emits __builtin_* calls directly, but the same TU usually
//     spells accessors too, and there is no downside to covering it),
//   - the program declares any Obj whose name is one of the __cccc_c*
//     helpers themselves (e.g. reached via a private-header parse such as
//     reflection.h's implicit includes, which are deliberately NOT
//     auto-captured and so never replayed).
// The __cccc_cmplx/f/l constructors are included alongside the nine
// accessors: the replayed complex.h spells _Complex_I/I and CMPLX() in
// terms of them, so any macro text the host expands reaches them too.
// Emitted unconditionally rather than emit_cccc-gated, mirroring #1068's
// f2i reasoning: spelled text can appear under --emit-cccc output too.
// Unused static inline functions cost no codegen, so over-triggering is
// harmless; under-triggering is what produced #1117.
static const char *const complex_shim_defs[] = {
    "static inline double _Complex __cccc_cmplx(double re, double im) {\n"
    "    return __builtin_complex(re, im);\n"
    "}\n",
    "static inline float _Complex __cccc_cmplxf(float re, float im) {\n"
    "    return __builtin_complex(re, im);\n"
    "}\n",
    "static inline long double _Complex __cccc_cmplxl(long double re, long "
    "double im) {\n"
    "    return __builtin_complex(re, im);\n"
    "}\n",
#define CCCC_COMPLEX_SHIM(name, ret, arg, builtin)                             \
    "static inline " ret " " name "(" arg " _Complex z) {\n"                   \
    "    return __builtin_" builtin "(z);\n"                                   \
    "}\n",
    CCCC_COMPLEX_SHIM("__cccc_creal", "double", "double", "creal")
        CCCC_COMPLEX_SHIM("__cccc_crealf", "float", "float", "crealf")
            CCCC_COMPLEX_SHIM("__cccc_creall", "long double", "long double",
                              "creall") CCCC_COMPLEX_SHIM("__cccc_cimag",
                                                          "double", "double",
                                                          "cimag")
                CCCC_COMPLEX_SHIM("__cccc_cimagf", "float", "float", "cimagf")
                    CCCC_COMPLEX_SHIM("__cccc_cimagl", "long double",
                                      "long double", "cimagl")
                        CCCC_COMPLEX_SHIM("__cccc_conj", "double _Complex",
                                          "double", "conj")
                            CCCC_COMPLEX_SHIM("__cccc_conjf", "float _Complex",
                                              "float", "conjf")
                                CCCC_COMPLEX_SHIM("__cccc_conjl",
                                                  "long double _Complex",
                                                  "long double", "conjl")
#undef CCCC_COMPLEX_SHIM
};

// hashmap_foreach callback over emit_include_paths: true when any captured
// include resolved to a bundled complex.h/tgmath.h (#1117 -- see the shim
// table above for why those two specifically).
static int collect_complex_header_path(char *key, int keylen, void *val,
                                       void *user_data) {
    (void)key;
    (void)keylen;
    bool *want = user_data;
    if (path_basename_is(val, "complex.h") || path_basename_is(val, "tgmath.h"))
        *want = true;
    return 0;
}

// Cycle guard for type_scan_complex_native: a struct that points to its own
// kind (tree nodes, lua_State links, ...) would otherwise recurse forever,
// since peeling the pointer lands back on the same aggregate. collect_type()
// guards the equivalent walk with ctx->seen; this scanner is standalone, so
// it carries its own (tiny) visited list.
typedef struct {
    Type **data;
    int    len, cap;
} TypeSeenSet;

static bool type_seen_set_has(TypeSeenSet *set, Type *ty) {
    for (int i = 0; i < set->len; i++)
        if (set->data[i] == ty)
            return true;
    return false;
}

static void type_seen_set_push(TypeSeenSet *set, Type *ty) {
    if (set->len >= set->cap) {
        set->cap  = set->cap ? set->cap * 2 : 8;
        set->data = realloc(set->data, sizeof(Type *) * set->cap);
    }
    set->data[set->len++] = ty;
}

static void type_scan_complex_native(Type *ty, bool *want, TypeSeenSet *seen) {
    while (ty &&
           (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_VLA))
        ty = ty->base;
    if (!ty || *want)
        return;
    if (ty->kind == TY_COMPLEX) {
        *want = true;
        return;
    }
    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->members &&
        !type_seen_set_has(seen, ty)) {
        type_seen_set_push(seen, ty);
        for (Member *m = ty->members; m; m = m->next)
            type_scan_complex_native(m->ty, want, seen);
        return;
    }
    if (ty->kind == TY_FUNC) {
        type_scan_complex_native(ty->return_ty, want, seen);
        for (Type *p = ty->params; p; p = p->next)
            type_scan_complex_native(p, want, seen);
    }
}

// Same recursive-field traversal shape as node_scan_f2i_native (above) --
// exhaustive over every child-pointing field Node has.
static void node_scan_complex_native(Node *node, bool *want,
                                     TypeSeenSet *seen) {
    if (!node || *want)
        return;
    type_scan_complex_native(node->ty, want, seen);
    if (node->var)
        type_scan_complex_native(node->var->ty, want, seen);
    if (node->member)
        type_scan_complex_native(node->member->ty, want, seen);
    if (node->func_ty)
        type_scan_complex_native(node->func_ty, want, seen);
    node_scan_complex_native(node->lhs, want, seen);
    node_scan_complex_native(node->rhs, want, seen);
    node_scan_complex_native(node->cond, want, seen);
    node_scan_complex_native(node->then, want, seen);
    node_scan_complex_native(node->els, want, seen);
    node_scan_complex_native(node->init, want, seen);
    node_scan_complex_native(node->inc, want, seen);
    node_scan_complex_native(node->body, want, seen);
    node_scan_complex_native(node->args, want, seen);
    node_scan_complex_native(node->next, want, seen);
}

// True when `name` is one of the cccc-internal complex helpers themselves
// (#1117): reachable via a private-header parse (e.g. reflection.h's
// implicit includes), which is deliberately never auto-captured and hence
// never replayed -- nothing else would define them for the host.
static bool obj_name_is_complex_shim(const char *name) {
    static const char *const prefixes[] = {"__cccc_creal", "__cccc_cimag",
                                           "__cccc_conj", "__cccc_cmplx"};
    if (!name)
        return false;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
        if (!strncmp(name, prefixes[i], strlen(prefixes[i])))
            return true;
    return false;
}

static void serialize_synth_complex_decls(FILE *f, VirtualMachine *vm,
                                          Obj *prog) {
    bool        want = false;
    TypeSeenSet seen = {0};
    hashmap_foreach(&vm->compiler.emit_include_paths,
                    collect_complex_header_path, &want);
    for (Obj *obj = prog; obj && !want; obj = obj->next) {
        if (obj_name_is_complex_shim(obj->name))
            want = true;
        else {
            type_scan_complex_native(obj->ty, &want, &seen);
            for (Obj *param = obj->params; param && !want; param = param->next)
                type_scan_complex_native(param->ty, &want, &seen);
            for (Obj *local = obj->locals; local && !want; local = local->next)
                type_scan_complex_native(local->ty, &want, &seen);
        }
        if (!want && obj->is_function && obj->body)
            node_scan_complex_native(obj->body, &want, &seen);
    }
    free(seen.data);
    if (!want)
        return;
    fprintf(f, "\n/* #1117: cccc-internal complex accessors the bundled "
               "complex.h/tgmath.h\n   macros expand to; mapped onto the "
               "host's own builtins */\n\n");
    for (size_t i = 0;
         i < sizeof(complex_shim_defs) / sizeof(complex_shim_defs[0]); i++)
        fprintf(f, "%s", complex_shim_defs[i]);
    fprintf(f, "\n");
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
static const struct {
    const char *name;
    const char *header;
} synth_typedef_headers[] = {
    {"size_t", "stddef.h"},
    {"ptrdiff_t", "stddef.h"},
    {"wchar_t", "stddef.h"},
};

static const char *synth_typedef_header_for_name(const char *name,
                                                 int         name_len) {
    for (size_t i = 0;
         i < sizeof(synth_typedef_headers) / sizeof(synth_typedef_headers[0]);
         i++) {
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
static TypeName *find_typedef_record_any_scope(SerializeContext *ctx,
                                               Type             *ty) {
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
        const char *want =
            synth_typedef_header_for_name(tn->name, tn->name_len);
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
    if (node->var &&
        type_needs_synth_typedef_header(ctx, node->var->ty, header, seen))
        return true;
    if (node->member &&
        type_needs_synth_typedef_header(ctx, node->member->ty, header, seen))
        return true;
    if (node->func_ty &&
        type_needs_synth_typedef_header(ctx, node->func_ty, header, seen))
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

static void serialize_synth_typedef_includes(FILE *f, SerializeContext *ctx,
                                             Obj *prog) {
    const char *emitted[8];
    int         emitted_len = 0;
    bool        any         = false;
    for (size_t i = 0;
         i < sizeof(synth_typedef_headers) / sizeof(synth_typedef_headers[0]);
         i++) {
        const char *header  = synth_typedef_headers[i].header;
        bool        already = false;
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
static NestedEnvEntry *find_or_create_nested_env(VirtualMachine   *vm,
                                                 SerializeContext *ctx,
                                                 Obj              *owner) {
    for (int i = 0; i < ctx->nested_envs_len; i++)
        if (ctx->nested_envs[i].owner_fn == owner)
            return &ctx->nested_envs[i];
    if (ctx->nested_envs_len == ctx->nested_envs_cap) {
        ctx->nested_envs_cap =
            ctx->nested_envs_cap ? ctx->nested_envs_cap * 2 : 8;
        ctx->nested_envs = realloc(ctx->nested_envs, sizeof(NestedEnvEntry) *
                                                         ctx->nested_envs_cap);
    }
    NestedEnvEntry *e  = &ctx->nested_envs[ctx->nested_envs_len++];
    e->owner_fn        = owner;
    e->env_struct_name = arena_format(vm, "struct __cccc_nenv_%s", owner->name);
    e->upvars          = NULL;
    e->upvars_len      = 0;
    e->upvars_cap      = 0;
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
        *cap        = *cap ? *cap * 2 : 4;
        upvars      = realloc(upvars, sizeof(Obj *) * (*cap));
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
    Obj *owner       = NULL;
    Obj *block_owner = NULL;
    for (Obj *anc = fn->parent_fn; anc; anc = anc->parent_fn) {
        if (nested_var_is_own(anc, var)) {
            owner = anc;
            break;
        }
        // #1081: a block ancestor that does NOT directly own `var` is a
        // hard boundary for outward resolution -- `var` belongs to one of
        // ITS OWN ancestors, which this climb must not chase past. Rather
        // than an env-struct upvar of the real, further-out owner, `var`
        // was already captured transitively by this block at parse time
        // (block_literal()'s nested_children climb, parse_blocks.c) the
        // exact same way a sibling direct block read already sees it --
        // and must be read the same way here too (serialize_nested_upvar_
        // ref(), block_ancestor_desc_ptr_expr()), by-value snapshot rather
        // than a live read of the real owner's frame (no reference
        // implementation to defer to for this combination -- internal
        // consistency with the block's own direct captures is the spec).
        // A block ancestor that DOES directly own `var` (its own local/
        // param, matched by nested_var_is_own above first) is a distinct,
        // already-correct shape -- see this function's own upvar-of-a-
        // block-owner arm below, unaffected by this branch.
        if (anc->is_block) {
            block_owner = anc;
            break;
        }
    }
    if (block_owner) {
        // Nothing to register here -- serialize_nested_upvar_ref() finds
        // the same block ancestor independently at each read site and
        // reads `var` out of its own capture descriptor. Validate now,
        // at the point a diagnostic can still point at the reference,
        // that the capture the read side will assume really exists.
        if (block_capture_index(block_owner, var) < 0)
            error_tok(vm, node->tok ? node->tok : fn->tok,
                      "internal error: '%s' is read by a nested function "
                      "through block ancestor but was never captured by "
                      "it (#1081)",
                      var->name);
        return;
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
                  "(#1074)",
                  var->name);
        return;
    }
    // #965/#1080: a block descriptor local itself (block_desc_of) has no
    // meaningful "value" to hand across a static link -- reject that one
    // shape outright. A __block-storage local's own C storage is already a
    // pointer (its slot holds the shared heap box), so the env field for it
    // is one level of indirection deeper (`T **` instead of `T *`,
    // serialize_nested_preamble()) and every read/write goes through an
    // extra dereference (serialize_nested_upvar_ref()) -- no longer
    // rejected as of #1080.
    if (var->block_desc_of) {
        error_tok(vm, node->tok ? node->tok : fn->tok,
                  "cannot serialize to native code: a block literal's own "
                  "descriptor local '%s' cannot be captured by a nested "
                  "function's static link (#1074)",
                  var->name);
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
                          "pointer type (#1074)",
                          node->var->name);
        }

        // #1080 (was a #1074-follow-up rejection): a block literal directly
        // inside a genuinely nested function (fn->is_nested && !fn->is_block)
        // that captures a variable belonging to one of THAT function's own
        // ancestors (an upvar of `fn`, not `fn`'s own local) now registers
        // the capture as an upvar of the real owner, exactly like a bare
        // ND_VAR reference above -- ND_BLOCK_LITERAL's own serialize_expr
        // case grows a matching source arm that reads it back out through
        // the same env chase (nested_env_ptr_expr) instead of printing an
        // unnameable `cap->name`.
        if (node->kind == ND_BLOCK_LITERAL && fn->is_nested && !fn->is_block) {
            for (int __bc_i = 0; __bc_i < node->num_block_captures; __bc_i++) {
                Obj *cap = node->block_captures[__bc_i];
                if (cap->is_local && !nested_var_is_own(fn, cap))
                    record_nested_upvar(vm, ctx, fn, node, cap);
            }
        }

        bool lhs_is_direct_nested_call =
            node->kind == ND_FUNCALL && node->lhs &&
            node->lhs->kind == ND_VAR && node->lhs->var &&
            node->lhs->var->is_function && node->lhs->var->is_nested &&
            !node->lhs->var->is_block;
        // #1081 residual (tracked separately, not fixed here): calling a
        // nested function whose own parent sits beyond a block ancestor of
        // `fn` (a sibling/cousin call reached only by climbing OUT of a
        // block first) needs the block's *enclosing frame*, which a
        // heap-copyable block's descriptor deliberately never stores (the
        // same reason a plain variable read through such a chain is
        // rejected in codegen -- see emit_static_chain_var_addr's own
        // #1081 fix, codegen_addr.c). serialize_expr's ND_FUNCALL case
        // (nested_env_ptr_expr) has no equivalent fix, so reject here
        // rather than let it cast a block descriptor as if it were an
        // ordinary nested-function env struct.
        if (lhs_is_direct_nested_call && fn->is_nested && !fn->is_block &&
            node->lhs->var->parent_fn != fn) {
            for (Obj *anc                                     = fn->parent_fn;
                 anc && anc != node->lhs->var->parent_fn; anc = anc->parent_fn)
                if (anc->is_block) {
                    error_tok(vm, node->tok ? node->tok : fn->tok,
                              "cannot serialize to native code: calling "
                              "nested function '%s', whose own parent is "
                              "beyond a block ancestor, is not supported "
                              "(#1081 residual)",
                              node->lhs->var->name);
                    break;
                }
        }
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
            // #1080: a __block-storage upvar's own C storage is already a
            // pointer to the shared heap box (T *) -- the env field holding
            // its address is one level deeper, T **, so
            // serialize_nested_upvar_ref() can deref twice to reach the
            // value.
            Type *field_ty =
                e->upvars[j]->is_block_var
                    ? pointer_to(vm, pointer_to(vm, e->upvars[j]->ty))
                    : pointer_to(vm, e->upvars[j]->ty);
            serialize_type_decl(f, ctx, field_ty, field_name);
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
static void serialize_block_preamble(FILE *f, VirtualMachine *vm,
                                     SerializeContext *ctx, Obj *prog) {
    bool any_block       = false;
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
    bool copy_used    = false;
    bool release_used = false;
    for (Obj *obj = prog; obj && (!copy_used || !release_used);
         obj      = obj->next) {
        if (!obj->is_function || !obj->body)
            continue;
        bool reachable = !ctx->generated_only || obj->is_macro_generated;
        if (!reachable)
            continue;
        if (!copy_used && vm->compiler.builtin_block_copy)
            copy_used =
                node_calls_obj(obj->body, vm->compiler.builtin_block_copy);
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
    size_t             prefix_len      = strlen(BLOCK_FN_PREFIX);

    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->is_block)
            continue;

        // obj->name was rewritten to "__cccc_block_<N>" by
        // rename_anon_globals() just above -- reuse its numeric suffix so
        // the env struct name pairs with it without extra state.
        const char *suffix =
            (strncmp(obj->name, BLOCK_FN_PREFIX, prefix_len) == 0)
                ? obj->name + prefix_len
                : obj->name;
        char *env_name = arena_format(vm, "struct __cccc_block_env_%s", suffix);

        fprintf(f, "%s {\n    void *__invoke;\n    long __size;\n", env_name);
        for (int i = 0; i < obj->num_captures; i++) {
            Obj *cap = obj->captures[i];

            // #989: a capture whose own struct/union/enum type was declared
            // inside a function is already hoisted to file scope (with
            // renaming on collision) by the loop above, before this one
            // runs -- see hoist_local_type_to_file_scope(). Previously
            // (#965) this was a hard error; the fix landed here.
            Type *field_ty =
                cap->is_block_var ? pointer_to(vm, cap->ty) : cap->ty;
            char field_name[32];
            snprintf(field_name, sizeof(field_name), "__cap%d", i);
            fprintf(f, "    ");
            serialize_type_decl(f, ctx, field_ty, field_name);
            fprintf(f, ";\n");
        }
        fprintf(f, "};\n\n");

        if (ctx->block_envs_len == ctx->block_envs_cap) {
            ctx->block_envs_cap =
                ctx->block_envs_cap ? ctx->block_envs_cap * 2 : 8;
            ctx->block_envs = realloc(ctx->block_envs, sizeof(BlockEnvEntry) *
                                                           ctx->block_envs_cap);
        }
        ctx->block_envs[ctx->block_envs_len].block_fn        = obj;
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

static bool function_is_header_supplied(VirtualMachine   *vm,
                                        SerializeContext *ctx, Obj *obj) {
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

// #1118: true if `line` (a raw captured directive line, `#...`, from
// copy_raw_directive_line()/copy_routed_directive_line() in preprocess.c) is
// a #define or #undef whose macro NAME starts with a non-ASCII byte (UTF-8
// lead/continuation bytes -- emoji and other non-ASCII identifiers, a CCCC
// extension the host preprocessor rejects outright: "macro name must be an
// identifier"). See the call site in cc_serialize_program()'s
// emit_directives loop for why these lines are dropped from ordinary
// replay. Matches on the directive word after `#` and optional whitespace,
// deliberately textual rather than pp_directive() (token-level, not
// available on this already-flattened string), same style as
// line_is_conditional_directive above.
static bool line_macro_name_is_non_ascii(const char *line) {
    if (!line || line[0] != '#')
        return false;
    const char *p = line + 1;
    while (*p == ' ' || *p == '\t')
        p++;
    static const char *const kw[] = {"define", "undef"};
    for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) {
        size_t len = strlen(kw[i]);
        if (strncmp(p, kw[i], len) != 0)
            continue;
        // Word boundary: "#defined" is not a directive (and a function-like
        // "#define NAME(" still has whitespace before NAME, so the plain
        // space/tab boundary covers both spellings).
        char c = p[len];
        if (c != '\0' && c != ' ' && c != '\t')
            continue;
        const char *name = p + len;
        while (*name == ' ' || *name == '\t')
            name++;
        // Only the NAME's first byte matters: any UTF-8 encoding of a
        // non-ASCII identifier starts with a byte >= 0x80, and an ASCII name
        // never does. A replacement list referencing an ASCII macro is not
        // touched -- only names that are themselves non-ASCII are filtered.
        return (unsigned char)*name >= 0x80;
    }
    return false;
}

// #1033: real C operator text for a CmpOp, so a return= comparison can be
// baked directly into the generated C as `if (!(__ret <op> <expect>))`
// instead of a runtime dispatch -- the operator is already known at
// serialize time (r->ret_op), same as every other test-table field.
static const char *cmp_op_c_operator(CmpOp op) {
    switch (op) {
        case CMP_NE:
            return "!=";
        case CMP_LT:
            return "<";
        case CMP_LE:
            return "<=";
        case CMP_GT:
            return ">";
        case CMP_GE:
            return ">=";
        case CMP_EQ:
        default:
            return "==";
    }
}

// #1033: the ~28 __builtin_assert_* functions include/cccc/testing.h
// declares (a cccc-private header, never replayed to the host compiler --
// see cc_serialize_program's #include-replay loop) transliterated into real
// C with the same typed prototypes the guest program's own already-
// type-checked call sites expect. Behaviorally identical to testing.c's
// impl_assert_* family: on failure, snprintf a diagnostic into the current
// __cccc_test_run_state and longjmp back to the per-test wrapper's setjmp.
// _setjmp/_longjmp themselves reuse serialize_synth_setjmp_decls's own
// raw-extern pattern rather than #include <setjmp.h> -- see the redundant
// declaration below for why a real jmp_buf type would conflict when the
// same TU also lowers the guest setjmp builtin.
static const char *const CCCC_TEST_ASSERT_RUNTIME_SRC =
    // #1054/#1030: setjmp.h is compiler-owned (see
    // serialize_synth_setjmp_decls's own comment) -- reuse its exact
    // raw-extern pattern (_setjmp/_longjmp over a void* buffer) instead of
    // #include <setjmp.h>, so a TU that also uses the guest setjmp builtin
    // never sees two conflicting declarations of the same symbol.
    "extern int _setjmp(void *);\n"
    "extern void _longjmp(void *, int) __attribute__((noreturn));\n"
    "#include <string.h>\n"
    "#include <stdio.h>\n"
    "typedef struct {\n"
    // long long (not unsigned char) so the buffer inherits 8-byte natural
    // alignment -- _setjmp on some hosts (e.g. glibc/aarch64's
    // __sigsetjmp) writes callee-saved FP registers with real alignment
    // requirements a byte-aligned buffer wouldn't satisfy.
    "    long long jmp[512];\n"
    "    int failed;\n"
    "    char fail_msg[512];\n"
    "} __cccc_test_run_state;\n"
    "static __cccc_test_run_state *__cccc_s_run = NULL;\n"
    "static void __builtin_assert(int cond, const char *expr, const char "
    "*file, int line) {\n"
    "    if (!cond) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"Assert called outside a "
    "test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s (%s:%d)\", expr, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_false(int cond, const char *expr, const "
    "char *file, int line) {\n"
    "    if (cond) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertFalse called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"!%s (%s:%d)\", expr, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_fail(const char *file, int line) {\n"
    "    if (!__cccc_s_run) { fprintf(stderr, \"AssertFail called outside a "
    "test run at %s:%d\\n\", file, line); return; }\n"
    "    snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"forced failure (%s:%d)\", file, line);\n"
    "    __cccc_s_run->failed = 1;\n"
    "    _longjmp(__cccc_s_run->jmp, 1);\n"
    "}\n"
    "static void __builtin_assert_fail_msg(const char *msg, const char "
    "*file, int line) {\n"
    "    if (!__cccc_s_run) { fprintf(stderr, \"AssertFailMsg called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "    snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s (%s:%d)\", msg, file, line);\n"
    "    __cccc_s_run->failed = 1;\n"
    "    _longjmp(__cccc_s_run->jmp, 1);\n"
    "}\n"
    "static void __builtin_assert_eq(long long a, long long b, const char "
    "*as, const char *bs, const char *file, int line) {\n"
    "    if (a != b) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertEq called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s != %s (%lld != %lld) (%s:%d)\", as, bs, a, b, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_neq(long long a, long long b, const char "
    "*as, const char *bs, const char *file, int line) {\n"
    "    if (a == b) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertNeq called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s == %s (both %lld) (%s:%d)\", as, bs, a, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_gt(long long a, long long b, const char "
    "*as, const char *bs, const char *file, int line) {\n"
    "    if (!(a > b)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertGt called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s <= %s (%lld <= %lld) (%s:%d)\", as, bs, a, b, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_lt(long long a, long long b, const char "
    "*as, const char *bs, const char *file, int line) {\n"
    "    if (!(a < b)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertLt called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s >= %s (%lld >= %lld) (%s:%d)\", as, bs, a, b, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_ge(long long a, long long b, const char "
    "*as, const char *bs, const char *file, int line) {\n"
    "    if (!(a >= b)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertGe called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s < %s (%lld < %lld) (%s:%d)\", as, bs, a, b, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_le(long long a, long long b, const char "
    "*as, const char *bs, const char *file, int line) {\n"
    "    if (!(a <= b)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertLe called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s > %s (%lld > %lld) (%s:%d)\", as, bs, a, b, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_within(long long delta, long long "
    "expected, long long actual, const char *ds, const char *es, const char "
    "*as, const char *file, int line) {\n"
    "    long long diff = expected - actual;\n"
    "    if (diff < 0) diff = -diff;\n"
    "    if (diff > delta) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertWithin called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s |%s - %s| = %lld > %s (%lld) (%s:%d)\", as, es, as, diff, ds, "
    "delta, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_null(const void *p, const char *ps, const "
    "char *file, int line) {\n"
    "    if (p != NULL) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertNull called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s is not null (%s:%d)\", ps, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_not_null(const void *p, const char *ps, "
    "const char *file, int line) {\n"
    "    if (p == NULL) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertNotNull called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s is null (%s:%d)\", ps, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_streq(const char *a, const char *b, const "
    "char *as, const char *bs, const char *file, int line) {\n"
    "    if (strcmp(a, b) != 0) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertStrEq called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s != %s (\\\"%s\\\" != \\\"%s\\\") (%s:%d)\", as, bs, a, b, file, "
    "line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_streq_len(const char *a, const char *b, "
    "long long len, const char *as, const char *bs, const char *file, int "
    "line) {\n"
    "    if (strncmp(a, b, (size_t)len) != 0) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertStrEqLen called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s != %s (first %lld chars differ) (%s:%d)\", as, bs, len, file, "
    "line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_mem_eq(const void *expected, const void "
    "*actual, long long len, const char *es, const char *as, const char "
    "*file, int line) {\n"
    "    if (memcmp(expected, actual, (size_t)len) != 0) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertMemEq called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s != %s (%lld bytes differ) (%s:%d)\", es, as, len, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_float_within(double delta, double "
    "expected, double actual, const char *ds, const char *es, const char "
    "*as, const char *file, int line) {\n"
    "    double diff = expected - actual;\n"
    "    if (diff < 0) diff = -diff;\n"
    "    if (diff > delta) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertFloatWithin called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s |%s - %s| = %g > %s (%g) (%s:%d)\", as, es, as, diff, ds, delta, "
    "file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_double_within(double delta, double "
    "expected, double actual, const char *ds, const char *es, const char "
    "*as, const char *file, int line) {\n"
    "    __builtin_assert_float_within(delta, expected, actual, ds, es, as, "
    "file, line);\n"
    "}\n"
    "static void __builtin_assert_bits(long long mask, long long expected, "
    "long long actual, const char *ms, const char *es, const char *as, "
    "const char *file, int line) {\n"
    "    if ((actual & mask) != (expected & mask)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertBits called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s & %s = 0x%llx != %s & %s = 0x%llx (%s:%d)\", as, ms, (unsigned "
    "long long)(actual & mask), es, ms, (unsigned long long)(expected & "
    "mask), file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_bit_high(int bit, long long actual, const "
    "char *bs, const char *as, const char *file, int line) {\n"
    "    if (!(actual & (1LL << bit))) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertBitHigh called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s bit %d of %s is low (%s:%d)\", bs, bit, as, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_bit_low(int bit, long long actual, const "
    "char *bs, const char *as, const char *file, int line) {\n"
    "    if (actual & (1LL << bit)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertBitLow called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s bit %d of %s is high (%s:%d)\", bs, bit, as, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_eq_array(const void *expected, const void "
    "*actual, long long elem_size, long long count, const char *es, const "
    "char *as, const char *file, int line) {\n"
    "    size_t total = (size_t)elem_size * (size_t)count;\n"
    "    if (memcmp(expected, actual, total) != 0) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertArrayEq called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s[0..%lld] != %s[0..%lld] (%lld bytes differ) (%s:%d)\", es, "
    "count - 1, as, count - 1, (long long)total, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_msg(int cond, const char *expr, const "
    "char *msg, const char *file, int line) {\n"
    "    if (!cond) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertMsg called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s (%s:%d) - %s\", expr, file, line, msg);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_eq_msg(long long a, long long b, const "
    "char *as, const char *bs, const char *msg, const char *file, int "
    "line) {\n"
    "    if (a != b) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertEqMsg called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s != %s (%lld != %lld) (%s:%d) - %s\", as, bs, a, b, file, line, "
    "msg);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_streq_msg(const char *a, const char *b, "
    "const char *as, const char *bs, const char *msg, const char *file, "
    "int line) {\n"
    "    if (strcmp(a, b) != 0) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertStrEqMsg called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s != %s (\\\"%s\\\" != \\\"%s\\\") (%s:%d) - %s\", as, bs, a, b, "
    "file, line, msg);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_null_msg(const void *p, const char *ps, "
    "const char *msg, const char *file, int line) {\n"
    "    if (p != NULL) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertNullMsg called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s is not null (%s:%d) - %s\", ps, file, line, msg);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_not_null_msg(const void *p, const char "
    "*ps, const char *msg, const char *file, int line) {\n"
    "    if (p == NULL) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertNotNullMsg called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s is null (%s:%d) - %s\", ps, file, line, msg);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_bits_msg(long long mask, long long "
    "expected, long long actual, const char *ms, const char *es, const "
    "char *as, const char *msg, const char *file, int line) {\n"
    "    if ((actual & mask) != (expected & mask)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertBitsMsg called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s & %s = 0x%llx != %s & %s = 0x%llx (%s:%d) - %s\", as, ms, "
    "(unsigned long long)(actual & mask), es, ms, (unsigned long "
    "long)(expected & mask), file, line, msg);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n";

// #1033: fork-per-test TAP harness. See cc_serialize_program's own
// emit_test_harness gate for the CLI-side refusals (test_setup/teardown,
// negative tests) that keep this function's own scope narrow -- by the
// time this runs, vm->compiler.test_setups is guaranteed NULL and no
// vm->compiler.test_fns record has error_pat/expect_compile_error set.
static void serialize_test_harness(FILE *f, VirtualMachine *vm, Obj *prog) {
    if (vm->compiler.emit_cccc || !vm->compiler.test_fns)
        return;

    fputs("\n/* #1033: --testing=native generated test harness */\n", f);
    fputs(CCCC_TEST_ASSERT_RUNTIME_SRC, f);
    fputs("#include <sys/types.h>\n"
          "#include <sys/time.h>\n"
          "#include <unistd.h>\n"
          "#include <fcntl.h>\n"
          "#include <regex.h>\n"
          "#include <stdlib.h>\n"
          "#include <errno.h>\n"
          // #1033: when -I./include is on the compile line (as
          // tools/tests.py always passes it), CCCC's own bundled
          // signal.h/sys/wait.h -- polyfills for VM-internal use, see
          // man/HEADERS.md -- shadow the real host headers. That's merely
          // inconvenient for the missing kill() prototype below, but
          // outright breaks the build under a per-test `--std=c89
          // -Wpedantic`: the bundled signal.h uses the C99 `restrict`
          // keyword (a syntax error, not just a warning, under host
          // -std=c89), and bundled sys/wait.h itself #includes signal.h
          // for siginfo_t. Sidestepped entirely: no #include <signal.h>
          // or <sys/wait.h>, just the handful of symbols this harness
          // actually needs, declared directly. SIGALRM/SIGKILL/SIG_DFL
          // values and the WIFEXITED/WEXITSTATUS/WIFSIGNALED/WTERMSIG
          // status-word encoding are POSIX-traditional and identical on
          // Linux and Darwin, this project's only two supported
          // platforms (CLAUDE.md).\n"
          "extern pid_t waitpid(pid_t, int *, int);\n"
          "#define __CCCC_WIFEXITED(s) (((s) & 0x7f) == 0)\n"
          "#define __CCCC_WEXITSTATUS(s) (((s) >> 8) & 0xff)\n"
          "#define __CCCC_WIFSIGNALED(s) ((((signed char)(((s) & 0x7f) + "
          "1)) >> 1) > 0)\n"
          "#define __CCCC_WTERMSIG(s) ((s) & 0x7f)\n"
          "#define __CCCC_SIGALRM 14\n"
          "#define __CCCC_SIGKILL 9\n"
          "#define __CCCC_SIG_DFL ((void (*)(int))0)\n"
          "extern void (*signal(int, void (*)(int)))(int);\n"
          "extern int kill(pid_t, int);\n\n",
          f);

    // Reverse to declaration order (test_fns is built by prepending, same
    // as cc_run_tests, testing.c:1165).
    int n = 0;
    for (TestFnRecord *r = vm->compiler.test_fns; r; r = r->next)
        n++;
    TestFnRecord **ordered = malloc((size_t)n * sizeof(TestFnRecord *));
    {
        int i = n - 1;
        for (TestFnRecord *r = vm->compiler.test_fns; r; r = r->next)
            ordered[i--] = r;
    }

    // Per-test wrapper functions. status: 0 = will run, 1 = SKIP (not
    // found / per-test flags=).
    int *skip = calloc((size_t)n, sizeof(int));
    for (int i = 0; i < n; i++) {
        TestFnRecord *r  = ordered[i];
        Obj          *fn = NULL;
        for (Obj *o = prog; o; o = o->next) {
            if (o->is_function && o->name && strcmp(o->name, r->name) == 0) {
                fn = o;
                break;
            }
        }
        bool has_flags = r->test_flags_mask || r->test_opt_set ||
                         r->test_warn_mask || r->test_warn_errors_mask ||
                         r->test_warn_as_errors_set || r->test_f_set ||
                         r->test_ffi_allow_count > 0;
        if (!fn || has_flags) {
            skip[i] = 1;
            continue;
        }

        if (r->expect_exit_code >= 0) {
            // exit_code= tests skip the assertion-comparison wrapper
            // entirely -- the child _exit()s with the guest function's own
            // return value (VM parity: cc_run_at's fork path never sets up
            // __cccc_s_run either, so an Assert* inside such a test's body
            // degrades to a stderr warning rather than failing the test,
            // same as testing.c's own exit_code fork branch).
            bool is_void = (fn->ty && fn->ty->return_ty) &&
                           fn->ty->return_ty->kind == TY_VOID;
            fprintf(f, "static int __cccc_test_exit_%d(void) {\n", i);
            if (is_void)
                fprintf(f, "    %s();\n    return 0;\n", fn->name);
            else
                fprintf(f, "    return (int)(long long)%s();\n", fn->name);
            fputs("}\n\n", f);
            continue;
        }

        fprintf(f,
                "static int __cccc_test_run_%d(char *__msg, size_t __cap) "
                "{\n",
                i);
        fputs("    __cccc_test_run_state __st;\n"
              "    __st.failed = 0;\n"
              "    __st.fail_msg[0] = '\\0';\n"
              "    __cccc_s_run = &__st;\n"
              "    if (_setjmp(__st.jmp)) {\n"
              "        __cccc_s_run = NULL;\n"
              "        if (__msg) snprintf(__msg, __cap, \"%s\", "
              "__st.fail_msg);\n"
              "        return 0;\n"
              "    }\n",
              f);

        TypeKind ret_kind =
            (fn->ty && fn->ty->return_ty) ? fn->ty->return_ty->kind : TY_VOID;
        const char *op = cmp_op_c_operator(r->ret_op);
        switch (r->ret_kind) {
            case RET_INT:
                fprintf(f, "    long long __ret = (long long)%s();\n",
                        fn->name);
                fprintf(f,
                        "    if (!(__ret %s (long long)%lldLL)) {\n"
                        "        __cccc_s_run = NULL;\n"
                        "        if (__msg) snprintf(__msg, __cap, "
                        "\"expected return value %%s %%lld, got %%lld\", "
                        "\"%s\", (long long)%lldLL, __ret);\n"
                        "        return 0;\n"
                        "    }\n",
                        op, (long long)r->ret_expect.ret_int, op,
                        (long long)r->ret_expect.ret_int);
                break;
            case RET_FLOAT: {
                double eps = (r->ret_epsilon > 0.0) ? r->ret_epsilon : 1e-9;
                fprintf(f, "    double __ret = (double)%s();\n", fn->name);
                fprintf(f,
                        "    { double __diff = __ret - (%.17g); if (__diff "
                        "< 0) __diff = -__diff;\n"
                        "      if (__diff > %.17g) {\n"
                        "        __cccc_s_run = NULL;\n"
                        "        if (__msg) snprintf(__msg, __cap, "
                        "\"expected return value %%s %%g, got %%g\", "
                        "\"%s\", (double)(%.17g), __ret);\n"
                        "        return 0;\n"
                        "      } }\n",
                        r->ret_expect.ret_float, eps, op,
                        r->ret_expect.ret_float);
                break;
            }
            case RET_STR: {
                fprintf(f, "    const char *__ret = (const char *)%s();\n",
                        fn->name);
                fputs("    { const char *__exp = ", f);
                if (r->ret_expect.ret_str)
                    serialize_string_n(f, r->ret_expect.ret_str,
                                       (int)strlen(r->ret_expect.ret_str));
                else
                    fputs("NULL", f);
                fputs(";\n"
                      "      int __cmp = (__ret && __exp) ? strcmp(__ret, "
                      "__exp) : (__ret ? 1 : (__exp ? -1 : 0));\n",
                      f);
                fprintf(f,
                        "      if (!(__cmp %s 0)) {\n"
                        "        __cccc_s_run = NULL;\n"
                        "        if (__msg) snprintf(__msg, __cap, "
                        "\"expected return string %%s \\\"%%s\\\", got "
                        "\\\"%%s\\\"\", \"%s\", __exp ? __exp : \"(null)\", "
                        "__ret ? __ret : \"(null)\");\n"
                        "        return 0;\n"
                        "      } }\n",
                        op, op);
                break;
            }
            case RET_STRUCT:
                // #1033 v1: struct return= assertions need type-directed
                // per-field comparison codegen (see testing.c's
                // cmp_ret_aggregate) -- narrow enough in practice (a
                // handful of tests repo-wide) that it's deferred rather
                // than attempted here. The test still runs (asserts inside
                // it are honored); only the return-value check is skipped.
                (void)ret_kind;
                fprintf(f, "    (void)%s();\n", fn->name);
                break;
            case RET_NONE:
            default:
                fprintf(f, "    (void)%s();\n", fn->name);
                break;
        }
        fputs("    __cccc_s_run = NULL;\n"
              "    return 1;\n"
              "}\n\n",
              f);
    }

    // Test table.
    fputs("typedef struct {\n"
          "    const char *name;\n"
          "    const char *suite;\n"
          "    int (*run)(char *, size_t);\n"
          "    int (*run_exit)(void);\n"
          "    long timeout_ms;\n"
          "    int expect_exit_code;\n"
          "    const char *expect_stdout;\n"
          "    const char *reject_stdout;\n"
          "    const char *expect_stderr;\n"
          "    const char *reject_stderr;\n"
          "    const char *skip_reason;\n"
          "} __cccc_test_case;\n\n",
          f);
    fprintf(f, "static __cccc_test_case __cccc_tests[%d] = {\n", n > 0 ? n : 1);
    for (int i = 0; i < n; i++) {
        TestFnRecord *r    = ordered[i];
        const char   *disp = r->display_name ? r->display_name : r->name;
        fputs("    { ", f);
        serialize_string_n(f, disp, (int)strlen(disp));
        fputs(", ", f);
        if (r->suite)
            serialize_string_n(f, r->suite, (int)strlen(r->suite));
        else
            fputs("NULL", f);
        if (skip[i]) {
            fputs(", NULL, NULL, ", f);
        } else if (r->expect_exit_code >= 0) {
            fprintf(f, ", NULL, __cccc_test_exit_%d, ", i);
        } else {
            fprintf(f, ", __cccc_test_run_%d, NULL, ", i);
        }
        fprintf(f, "%ldL, %d, ", r->timeout_ms, r->expect_exit_code);
        const char *strs[4] = {r->expect_stdout, r->reject_stdout,
                               r->expect_stderr, r->reject_stderr};
        for (int k = 0; k < 4; k++) {
            if (strs[k])
                serialize_string_n(f, strs[k], (int)strlen(strs[k]));
            else
                fputs("NULL", f);
            fputs(", ", f);
        }
        if (skip[i])
            fputs("\"not supported by --testing=native (#1033 v1)\" },\n", f);
        else
            fputs("NULL },\n", f);
    }
    fputs("};\n\n", f);

    // main(): fork-per-test TAP runner.
    fputs("static volatile int __cccc_alarm_fired = 0;\n"
          "static volatile pid_t __cccc_fork_child = 0;\n"
          "static void __cccc_test_alarm(int sig) {\n"
          "    (void)sig;\n"
          "    __cccc_alarm_fired = 1;\n"
          "    if (__cccc_fork_child > 0) kill(__cccc_fork_child, "
          "__CCCC_SIGKILL);\n"
          "}\n"
          "static void __cccc_set_timeout(long ms) {\n"
          "    struct itimerval itv;\n"
          "    if (ms > 0) {\n"
          "        itv.it_interval.tv_sec = 0; itv.it_interval.tv_usec = 0;\n"
          "        itv.it_value.tv_sec = ms / 1000; itv.it_value.tv_usec = "
          "(ms % 1000) * 1000;\n"
          "    } else {\n"
          "        itv.it_interval.tv_sec = 0; itv.it_interval.tv_usec = 0;\n"
          "        itv.it_value.tv_sec = 0; itv.it_value.tv_usec = 0;\n"
          "    }\n"
          "    setitimer(ITIMER_REAL, &itv, NULL);\n"
          "}\n"
          "static char *__cccc_drain(int fd) {\n"
          "    if (fd < 0) return NULL;\n"
          "    size_t cap = 4096, len = 0;\n"
          "    char *buf = malloc(cap);\n"
          "    if (!buf) return NULL;\n"
          "    for (;;) {\n"
          "        if (len + 1024 > cap) { cap *= 2; char *nb = realloc(buf, "
          "cap); if (!nb) break; buf = nb; }\n"
          "        ssize_t r = read(fd, buf + len, cap - len - 1);\n"
          "        if (r <= 0) break;\n"
          "        len += (size_t)r;\n"
          "    }\n"
          "    buf[len] = '\\0';\n"
          "    return buf;\n"
          "}\n"
          "static int __cccc_check_pattern(const char *pat, const char *buf, "
          "int negate) {\n"
          "    if (!pat) return 1;\n"
          "    regex_t re;\n"
          "    if (regcomp(&re, pat, REG_EXTENDED) != 0) return 1;\n"
          "    int m = regexec(&re, buf ? buf : \"\", 0, NULL, 0) == 0;\n"
          "    regfree(&re);\n"
          "    return negate ? !m : m;\n"
          "}\n\n",
          f);

    fputs("int main(void) {\n", f);
    fprintf(f, "    int n = %d;\n", n);
    fputs("    int passed = 0, failed = 0, skipped = 0, timedout = 0;\n"
          "    printf(\"TAP version 13\\n\");\n"
          "    printf(\"1..%d\\n\", n);\n"
          "    const char *prev_suite = NULL;\n"
          "    int have_prev_suite = 1;\n"
          "    signal(__CCCC_SIGALRM, __cccc_test_alarm);\n"
          "    for (int i = 0; i < n; i++) {\n"
          "        __cccc_test_case *tc = &__cccc_tests[i];\n"
          "        int suite_changed = !have_prev_suite ||\n"
          "            ((tc->suite == NULL) != (prev_suite == NULL)) ||\n"
          "            (tc->suite && prev_suite && strcmp(tc->suite, "
          "prev_suite) != 0);\n"
          "        if (suite_changed) {\n"
          "            printf(\"# Suite: %s\\n\", tc->suite ? tc->suite : "
          "\"(none)\");\n"
          "            prev_suite = tc->suite;\n"
          "            have_prev_suite = 1;\n"
          "        }\n"
          "        if (!tc->run && !tc->run_exit) {\n"
          "            printf(\"ok %d - %s # SKIP %s\\n\", i + 1, tc->name, "
          "tc->skip_reason ? tc->skip_reason : \"\");\n"
          "            skipped++;\n"
          "            continue;\n"
          "        }\n"
          "        int out_pipe[2] = {-1, -1}, err_pipe[2] = {-1, -1}, "
          "msg_pipe[2] = {-1, -1};\n"
          "        int need_out = tc->expect_stdout || tc->reject_stdout;\n"
          "        int need_err = tc->expect_stderr || tc->reject_stderr;\n"
          "        if (need_out) pipe(out_pipe);\n"
          "        if (need_err) pipe(err_pipe);\n"
          "        pipe(msg_pipe);\n"
          "        fflush(stdout);\n"
          "        fflush(stderr);\n"
          "        __cccc_alarm_fired = 0;\n"
          "        pid_t pid = fork();\n"
          "        if (pid == 0) {\n"
          "            signal(__CCCC_SIGALRM, __CCCC_SIG_DFL);\n"
          "            close(msg_pipe[0]);\n"
          "            if (need_out) { close(out_pipe[0]); dup2(out_pipe[1], "
          "STDOUT_FILENO); close(out_pipe[1]); }\n"
          "            if (need_err) { close(err_pipe[0]); dup2(err_pipe[1], "
          "STDERR_FILENO); close(err_pipe[1]); }\n"
          "            if (tc->expect_exit_code >= 0) {\n"
          "                int rc = tc->run_exit();\n"
          "                _exit((unsigned char)rc);\n"
          "            }\n"
          "            char msg[512] = {0};\n"
          "            int ok = tc->run(msg, sizeof(msg));\n"
          "            if (!ok) write(msg_pipe[1], msg, strlen(msg));\n"
          "            close(msg_pipe[1]);\n"
          "            fflush(stdout);\n"
          "            fflush(stderr);\n"
          "            _exit(ok ? 0 : 1);\n"
          "        }\n"
          "        close(msg_pipe[1]);\n"
          "        if (need_out) close(out_pipe[1]);\n"
          "        if (need_err) close(err_pipe[1]);\n"
          "        __cccc_fork_child = pid;\n"
          "        __cccc_set_timeout(tc->timeout_ms);\n"
          "        int wstatus = 0;\n"
          "        pid_t waited;\n"
          "        do { waited = waitpid(pid, &wstatus, 0); } while "
          "(waited < 0 && errno == EINTR && !__cccc_alarm_fired);\n"
          "        __cccc_set_timeout(0);\n"
          "        __cccc_fork_child = 0;\n"
          "        int timed_out = __cccc_alarm_fired;\n"
          "        char *msg = __cccc_drain(msg_pipe[0]);\n"
          "        char *cap_out = __cccc_drain(need_out ? out_pipe[0] : "
          "-1);\n"
          "        char *cap_err = __cccc_drain(need_err ? err_pipe[0] : "
          "-1);\n"
          "        if (msg_pipe[0] >= 0) close(msg_pipe[0]);\n"
          "        if (out_pipe[0] >= 0) close(out_pipe[0]);\n"
          "        if (err_pipe[0] >= 0) close(err_pipe[0]);\n"
          "        if (timed_out) {\n"
          "            waitpid(pid, NULL, 0);\n"
          "            printf(\"not ok %d - %s\\n# TIMEOUT\\n\", i + 1, "
          "tc->name);\n"
          "            timedout++;\n"
          "        } else if (tc->expect_exit_code >= 0) {\n"
          "            int actual = -1;\n"
          "            if (__CCCC_WIFEXITED(wstatus)) actual = "
          "__CCCC_WEXITSTATUS(wstatus);\n"
          "            else if (__CCCC_WIFSIGNALED(wstatus)) actual = 128 + "
          "__CCCC_WTERMSIG(wstatus);\n"
          "            if (actual == tc->expect_exit_code) { printf(\"ok "
          "%d - %s\\n\", i + 1, tc->name); passed++; }\n"
          "            else { printf(\"not ok %d - %s\\n# expected "
          "exit_code %d, got %d\\n\", i + 1, tc->name, tc->expect_exit_code, "
          "actual); failed++; }\n"
          "        } else if (__CCCC_WIFSIGNALED(wstatus)) {\n"
          "            printf(\"not ok %d - %s\\n# aborted by signal "
          "%d\\n\", i + 1, tc->name, __CCCC_WTERMSIG(wstatus));\n"
          "            failed++;\n"
          "        } else if (__CCCC_WIFEXITED(wstatus) && "
          "__CCCC_WEXITSTATUS(wstatus) == "
          "0 &&\n"
          "                   __cccc_check_pattern(tc->expect_stdout, "
          "cap_out, 0) &&\n"
          "                   __cccc_check_pattern(tc->reject_stdout, "
          "cap_out, 1) &&\n"
          "                   __cccc_check_pattern(tc->expect_stderr, "
          "cap_err, 0) &&\n"
          "                   __cccc_check_pattern(tc->reject_stderr, "
          "cap_err, 1)) {\n"
          "            printf(\"ok %d - %s\\n\", i + 1, tc->name);\n"
          "            passed++;\n"
          "        } else {\n"
          "            printf(\"not ok %d - %s\\n# %s\\n\", i + 1, tc->name, "
          "(msg && msg[0]) ? msg : \"failed\");\n"
          "            failed++;\n"
          "        }\n"
          "        free(msg); free(cap_out); free(cap_err);\n"
          "    }\n"
          "    printf(\"# passed: %d, failed: %d, skipped: %d, timed out: "
          "%d\\n\", passed, failed, skipped, timedout);\n"
          "    return (passed + skipped == n) ? 0 : 1;\n"
          "}\n",
          f);

    free(ordered);
    free(skip);
}

void cc_serialize_program(FILE *f, VirtualMachine *vm, Obj *prog,
                          bool generated_only, bool emit_test_harness) {
    if (!f || !prog)
        return;

    SerializeContext ctx = {.generated_only = generated_only,
                            .emit_strict    = vm->compiler.emit_strict != 0,
                            .emit_cccc      = vm->compiler.emit_cccc,
                            .vm             = vm};
    // #1096: populated unconditionally now, not only under generated_only --
    // the bodiless-declaration prototype pass below needs path_is_captured()
    // to tell a *replayed* bundled-header #include (already supplying the
    // declaration) apart from an unreplayed one (which does not) in plain
    // -m/-c=native output too. Safe for every existing caller:
    // type_def_is_from_include_suppressed()'s own path_is_captured() call is
    // still gated `!ctx->generated_only || path_is_captured(...)`, so it
    // short-circuits before ever consulting captured_paths whenever
    // generated_only is false, exactly as before this change.
    hashmap_foreach(&vm->compiler.emit_include_paths, collect_captured_path,
                    &ctx);
    collect_scope_names(&ctx, vm);
    rename_anon_globals(vm, prog, &ctx);
    rename_colliding_static_names(vm, prog, &ctx);   // #1002
    rename_colliding_type_tags(vm, prog, &ctx);      // #1014
    rename_colliding_enum_constants(vm, prog, &ctx); // #1015
    collect_deferred_static_labels(vm, prog, &ctx);  // #1044
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (generated_only && !obj->is_macro_generated)
            continue;
        if (obj->is_function && !obj->is_definition && !obj->body)
            continue;
        if (!obj->is_function && obj->name[0] == '.')
            continue;
        collect_obj_types(&ctx, obj);
    }
    reorder_defs_by_byval_deps(&ctx); // #1042(a)

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
            if (!obj || !obj->is_macro_generated || obj->is_function ||
                obj->name[0] == '.')
                continue;
            // #1023: see type_needs_anon_aggregate's comment on the #918
            // loop below -- an untagged, alias-less struct/union global
            // can't be forward-declared at all without re-deriving a
            // structurally distinct anonymous type.
            if (type_needs_anon_aggregate(&ctx, obj->ty))
                continue;
            // #1044: deferred into its owning function's own body -- see
            // collect_deferred_static_labels()'s own comment.
            if (var_is_deferred_label_static(&ctx, obj))
                continue;
            fprintf(f, obj->is_static ? "static " : "extern ");
            if (obj->is_tls) // #1022: see serialize_global_var's own comment
                fprintf(f, "_Thread_local ");
            // #1136: see serialize_alignas_if_needed's own comment.
            serialize_alignas_if_needed(f, obj);
            // #1095: same rule as serialize_global_var's own -- only when
            // no byte-image initializer will follow for this object.
            ctx.allow_layout_dims = !obj->init_data;
            serialize_type_decl(f, &ctx, obj->ty, obj->name);
            ctx.allow_layout_dims = false;
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
    // doesn't understand (see run_native_backend, main.c).
    // serialize_typedef_alias / serialize_type_defs_for_owner compensate by no
    // longer treating that file's types as from_include, so their definitions
    // are still emitted below instead of being silently dropped.
    for (int i = 0; i < vm->compiler.emit_directives.len; i++) {
        char *line     = vm->compiler.emit_directives.data[i];
        char *resolved = hashmap_get(&vm->compiler.emit_include_paths, line);
        // --emit-cccc: re-emit cccc-only includes too -- the caller has
        // opted into dialect-fidelity output, so a downstream reader is
        // expected to understand the routing syntax those files carry.
        if (!vm->compiler.emit_cccc && resolved &&
            cc_file_is_cccc_only(vm, resolved)) {
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
        // #1114: a captured #embed line must never be replayed. The
        // directive's whole effect -- reading the file and splicing its
        // bytes (plus prefix/suffix/limit/if_empty) into the token stream --
        // already happened at parse time (handle_embed_directive,
        // preprocess.c), so the serialized AST carries the evaluated bytes
        // and the replay would duplicate them at top level, where a host's
        // expansion of the directive is a bare byte list with no initializer
        // context (a syntax error even when the file resolves). Replaying
        // also re-resolves the filename against the native compile's own
        // temp directory (make_tmp_path, exec.c) instead of the original
        // source file's directory, so a source-relative operand breaks
        // outright ("file not found"). `--emit-cccc` is exempted like the
        // filters above -- dialect-fidelity output expects a cccc-aware
        // reader.
        if (!vm->compiler.emit_cccc && !strncmp(line, "#embed", 6))
            continue;
        // #1118: a captured #define/#undef whose macro NAME contains
        // non-ASCII bytes (emoji identifiers -- an accepted CCCC extension,
        // e.g. tests/suites/test_suite_misc.c's worm/snake operator macros)
        // must never be replayed: every in-AST use of the macro was already
        // expanded at parse time, and the host preprocessor rejects a
        // non-ASCII macro name outright ("macro name must be an identifier",
        // xN for the defines plus their matching #undefs), so replaying the
        // line can only fail an otherwise-clean native compile. No other
        // replayed directive text can legally reference such a name either --
        // the host applies the same rejection there. The demand-driven
        // alternative (emit a captured define only when some other replayed
        // line references it) would be safer by construction against hidden
        // consumers, but no consumer is known to remain post-#1114 (the
        // LIMIT_EXPR-inside-#embed-limit case that motivated define replay
        // is gone), so the plain name filter matches the surrounding
        // per-line filters in both mechanism and cost. `--emit-cccc` is
        // exempted like the filters above -- dialect-fidelity output expects
        // a cccc-aware reader.
        if (!vm->compiler.emit_cccc && line_macro_name_is_non_ascii(line))
            continue;
        fprintf(f, "%s\n", line);
        // On Linux, a replayed `#include <sys/mount.h>` does NOT bring
        // `struct statfs` into scope the way it does on macOS/BSD -- real
        // glibc's own <sys/mount.h> only carries mount(2) flags; the struct
        // lives in <sys/vfs.h> instead (include/sys/mount.h's own #ifdef
        // __linux__ branch documents this same asymmetry for the
        // explicit-`-I` case, but that branch is never reached here since
        // this loop replays the bare `#include` line verbatim, resolved
        // against the host's OWN header search path, not CCCC's bundled
        // one). Without this, a folded sizeof(struct statfs)/_Alignof
        // re-materialized textually (#1031/#1095/#1098) hits "invalid
        // application of 'sizeof' to an incomplete type" on a real Linux
        // host. run_native_backend never cross-compiles -- the host cc it
        // spawns always targets the same OS this cccc binary itself runs
        // on -- so gating on __linux__ here (this translation unit's own
        // host, not some target flag) is safe.
#ifdef __linux__
        if (resolved && path_basename_is(resolved, "mount.h") &&
            strstr(resolved, "sys/mount.h"))
            fprintf(f, "#include <sys/vfs.h>\n");
#endif
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
        // #1068: unlike serialize_synth_setjmp_decls, NOT gated on
        // emit_cccc -- the ND_CAST rewrite these helpers back are emitted
        // unconditionally (see serialize_expr's ND_CAST case), so
        // --emit-cccc output needs them defined too, or it calls an
        // undefined function.
        serialize_synth_f2i_helpers(f, prog);
        // #1117: same unconditional-on-emit_cccc placement (#1068
        // reasoning) -- the bundled complex.h/tgmath.h macros expand plain
        // spelled accessors to cccc-internal names that need host-side
        // definitions no replay can supply. Sits after the f2i pass and
        // before the typedef/accessor-shim passes; nothing here references
        // anything those emit, and everything downstream of the include
        // replay above can already see it.
        serialize_synth_complex_decls(f, vm, prog);
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

    // Serialize file-scope type definitions before declarations that reference
    // them.
    serialize_type_defs_for_owner(f, &ctx, NULL);

    // #1088: real definitions for the C11 <threads.h> family -- run after
    // serialize_type_defs_for_owner just above (the shim bodies name mtx_t/
    // cnd_t/thrd_t/tss_t, re-derived there like any other cccc-only header's
    // types) and before the block/nested preambles below, which don't
    // reference threads.h's own types. !generated_only-gated like its
    // neighbours; also skips --emit-cccc internally (see its own comment).
    if (!generated_only)
        serialize_threads_shims(f, vm, prog);

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
        // #1044: deferred into its owning function's own body -- see
        // collect_deferred_static_labels()'s own comment.
        if (var_is_deferred_label_static(&ctx, obj))
            continue;
        // #1047: a header-supplied global is already forward-visible via
        // the replayed #include -- see global_is_header_supplied()'s
        // comment.
        if (global_is_header_supplied(vm, &ctx, obj))
            continue;
        fprintf(f, obj->is_static ? "static " : "extern ");
        if (obj->is_tls) // #1022: see serialize_global_var's own comment
            fprintf(f, "_Thread_local ");
        // #1136: see serialize_alignas_if_needed's own comment.
        serialize_alignas_if_needed(f, obj);
        // #1095: same rule as serialize_global_var's own -- only when no
        // byte-image initializer will follow for this object, so the
        // forward declaration and the real definition further down never
        // disagree on how the dimension is spelled.
        ctx.allow_layout_dims = !obj->init_data;
        serialize_type_decl(f, &ctx, obj->ty, obj->name);
        ctx.allow_layout_dims = false;
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

    // #1098: file-scope _Static_assert/static_assert records. Placed here
    // -- after tag forward decls, type definitions, and the #include
    // replay above, so an asserted type's own definition (or its replayed
    // #include) is already visible; before global-variable definitions,
    // which is the only real ordering constraint (no global depends on a
    // preceding assert or vice versa). Every record here came from a
    // hand-written source declaration (never macro-generated -- there is
    // no synthesis path that produces one), so -c=generated (which only
    // re-emits macro-generated additions layered onto the original
    // source) skips this pass entirely, the same reasoning the
    // global-variable loop just below applies per-Obj via is_macro_generated.
    if (!generated_only)
        for (StaticAssertRecord *sa = vm->compiler.static_asserts; sa;
             sa                     = sa->next)
            serialize_static_assert(f, vm, &ctx, sa->cond, sa->msg, sa->msg_len,
                                    sa->tok, 0);

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
                bool from_input =
                    t && t->file &&
                    (file_is_command_line_input(vm, t->file->name) ||
                     cc_file_is_cccc_only(vm, t->file->name));
                // #1096: a declaration sourced from one of CCCC's own
                // bundled headers (e.g. bundled fcntl.h's own
                // `#include "unistd.h"` declaring close()) is NOT supplied
                // by the auto-captured #include the way a real host
                // header's transitively-reached declaration is -- the
                // replayed `#include <fcntl.h>` resolves to the *host's*
                // fcntl.h under -c=native, which may not declare it (the
                // real bug this whole branch is scoped to catch: see
                // is_compiler_owned_header's own scope note and
                // test_sys_mount_statfs.c). Only applies when that bundled
                // header's own #include was never replayed
                // (path_is_captured()) -- when it was replayed, the usual
                // from_include suppression is correct and this declaration
                // really is already supplied by that replay. Also gated on
                // obj->is_used: a bundled header like unistd.h declares
                // dozens of functions the primary file never references --
                // emitting every one of them (rather than just the handful
                // the program actually calls) would needlessly bloat the
                // output and risk a real conflict for some declaration
                // whose signature the host's own header spells slightly
                // differently. is_used is set by the parser on any
                // identifier lookup (see its own doc comment on Obj), which
                // is exactly "does this TU actually reference it".
                bool cccc_bundled_uncaptured =
                    obj->is_used && t && t->file &&
                    cc_file_is_cccc_bundled(vm, t->file->name) &&
                    !path_is_captured(&ctx, t->file->name);
                if (!from_input && !cccc_bundled_uncaptured)
                    continue;
            }
        }
        serialize_function_signature(f, &ctx, obj);
        fprintf(f, ";\n\n");
    }

    // #1033: after every guest function's own prototype (just above) so the
    // harness's per-test wrapper functions -- which call test symbols
    // directly by name -- always see a prototype in scope, regardless of
    // where in `prog` the real definition sits.
    if (!generated_only && emit_test_harness)
        serialize_test_harness(f, vm, prog);

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
