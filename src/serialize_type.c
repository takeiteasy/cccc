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

// Serialization: type-name provenance/dedup machinery, type
// collection + definition ordering, struct/enum definitions,
// typedef aliases, tag forward declarations (#1150).
#include "./serialize_internal.h"

// Forward declaration
// #1124: serialize_expr's actual switch-on-node->kind body, renamed so the
// public serialize_expr can wrap it with the _BitInt width-mask check below
// without recursing into itself.
// #1062/#1085: matches CCCC's own struct va_list structurally (defined
// further down, near its own long comment) -- forward-declared here so
// ND_FUNCALL (inside serialize_expr's own switch, above the definition
// textually) can call it too.
// #1074/#1080: does `var` belong to `fn`'s own locals list? Defined near
// serialize_nested_preamble() (with the rest of the nested-function-upvar
// machinery); forward-declared here so ND_BLOCK_LITERAL's capture-copy
// (serialize_expr, above that machinery in file order) can reuse it.
// #1136: defined near serialize_global_var (with the rest of the
// declaration-printing machinery); forward-declared here so the hoisted-local
// declarator (serialize_function, above that machinery in file order) can
// reuse it too.
// #1103: defined near the other include-provenance predicates (with
// global_is_header_supplied); forward-declared here so
// rename_colliding_static_names() (above that machinery in file order) can
// share the exact same "does this definition actually reach the output"
// test the emitter itself uses, instead of a hand-rolled paraphrase that can
// drift out of sync with it.
// #964: mutually recursive with serialize_stmt -- see the comment on its
// definition, near ND_BLOCK below.

// #1233 / #1283: equirecursive-comparison support for same_type_or_origin().
//
// Two things live here:
//
//  1. An in-progress (a, b) pair stack. Comparing two independently-parsed
//     occurrences of a recursive type (`struct S { struct S *next; };`, or
//     any mutually-referential group like cccc's own Type/Node/Obj) walks a
//     member's type back to a pair already being compared higher up. The
//     stack breaks that the standard co-inductive way: a pair already on it
//     is assumed equal rather than re-derived. #1233 guarded only the
//     TY_PTR arm; at whole-program self-hosting scale the struct-member,
//     function and array arms recurse through freshly-copied Type nodes the
//     TY_PTR-only check never matched, so the guard is now applied uniformly
//     in same_type_or_origin() itself, ahead of every arm.
//
//  2. An (a, b) -> result memo (16-byte exact key, no hash collisions).
//     Without it, comparing two copies of a heavily-shared *acyclic*
//     subgraph re-walks it once per path that reaches it -- exponential on
//     cccc's real type graph, and the actual self-hosting-spike wall in
//     rename_colliding_type_tags(). A result is cached only when its
//     computation consumed no co-inductive assumption from an ancestor
//     frame (tracked via g_stm_min_assume): a result derived under "assume
//     (x, y) equal" is not sound to reuse in a context where that
//     assumption does not hold. The memo is explicitly begin/end-scoped
//     (same_type_memo_begin/end) around the one hot consumer; every other
//     caller uses the un-memoized but still guard-protected path.
//
// Not thread-safe -- same_type_or_origin() and its callers only ever run
// during the single-threaded `-c=native`/`-c=generated`/`-m` serialize pass.
#define SAME_TYPE_PAIR_STACK_CEILING 200000
static Type  **g_stp_a   = NULL;
static Type  **g_stp_b   = NULL;
static int     g_stp_len = 0;
static int     g_stp_cap = 0;

static HashMap g_same_type_memo; // key: 2 Type* ; val: 1 NEQ / 2 EQ
static bool    g_same_type_memo_on = false;
// Shallowest ancestor-frame stack index whose co-inductive "assume equal"
// was used by the subtree currently being evaluated; INT_MAX == none. A
// result is memoizable only if this stays >= the frame's own depth.
static int g_stm_min_assume = INT_MAX;

static void same_type_pair_push(Type *a, Type *b) {
    if (g_stp_len >= g_stp_cap) {
        int newcap = g_stp_cap ? g_stp_cap * 2 : 64;
        if (newcap > SAME_TYPE_PAIR_STACK_CEILING)
            error("cccc: internal error: same_type_or_origin() structural "
                  "recursion exceeded %d levels -- non-terminating comparison",
                  SAME_TYPE_PAIR_STACK_CEILING);
        g_stp_a   = realloc(g_stp_a, sizeof(Type *) * newcap);
        g_stp_b   = realloc(g_stp_b, sizeof(Type *) * newcap);
        g_stp_cap = newcap;
    }
    g_stp_a[g_stp_len] = a;
    g_stp_b[g_stp_len] = b;
    g_stp_len++;
}

// The memo is live for the whole serialize pass. same_type_or_origin() is
// the bottleneck in *two* phases -- collect_scope_names()'s
// type_vec_find_nominal() dedup scans and rename_colliding_type_tags()'s
// pairwise same_type_strong() -- so a memo scoped to just one of them leaves
// the other exponential.
//
//  begin()  start of cc_serialize_program(), before collect_scope_names().
//           Self-healing: deinits first, so an error() that unwound past a
//           previous end() cannot leak a stale enabled memo into the next
//           program.
//  clear()  after the rename passes: they mutate struct_tag (which the
//           tag-compare arms read), so every pre-rename entry must go.
//  end()    the two cc_serialize_program() cleanup paths.
//
// Only complete<->complete results are ever stored (see the guarded layer):
// an incomplete tagged aggregate is completed in place by the #1010
// swap during the same collect walk, which would otherwise stale a cached
// comparison against it.
void same_type_memo_begin(void) {
    hashmap_deinit(&g_same_type_memo);
    g_same_type_memo    = (HashMap){0};
    g_same_type_memo_on = !getenv("CCCC_TYPE_SAME_MEMO_DISABLE");
}

void same_type_memo_clear(void) {
    bool was_on = g_same_type_memo_on;
    hashmap_deinit(&g_same_type_memo);
    g_same_type_memo    = (HashMap){0};
    g_same_type_memo_on = was_on;
}

void same_type_memo_end(void) {
    hashmap_deinit(&g_same_type_memo);
    g_same_type_memo    = (HashMap){0};
    g_same_type_memo_on = false;
}

// A tagged aggregate / enum with no body yet -- may be completed in place
// later in the same pass, so a comparison touching one is not memoizable.
static bool type_is_incomplete_tagged(Type *t) {
    if (!t)
        return false;
    if (t->kind == TY_STRUCT || t->kind == TY_UNION)
        return t->size < 0;
    if (t->kind == TY_ENUM)
        return t->enum_constants == NULL;
    return false;
}

// #1283: env-gated cost instrumentation for same_type_or_origin(). This
// predicate became a whole-program-scale performance wall in the
// self-hosting spike; `sample` could not distinguish "too many top-level
// registry scans" (n^2) from "the recursion re-walks the same subgraphs"
// (needs memoization). These counters, dumped by serialize_type_stats_report()
// when CCCC_TYPE_STATS is set, make the distinction measurable.
static long long g_sto_calls_total     = 0; // every entry, including recursion
static long long g_sto_calls_toplevel  = 0; // entries with recursion depth 0
static long long g_sto_cycle_hits      = 0; // in-progress-pair guard hits
static long long g_sto_memo_hits       = 0; // resolved straight from the memo
static int       g_sto_recursion_depth = 0;
static int       g_sto_max_depth       = 0;

static bool same_type_or_origin_impl(Type *a, Type *b);

// The guard + memo layer described at the top of this file. same_type_or_
// origin_impl() holds only the structural per-kind logic and recurses back
// through same_type_or_origin() (below) so every sub-pair is guarded and
// memoized too.
static bool same_type_or_origin_guarded(Type *a, Type *b) {
    if (!a || !b)
        return same_type_or_origin_impl(a, b); // NULL edges cannot recurse

    Type *k1 = a < b ? a : b; // symmetric relation -> order-normalized key
    Type *k2 = a < b ? b : a;

    for (int i = 0; i < g_stp_len; i++)
        if (g_stp_a[i] == k1 && g_stp_b[i] == k2) {
            g_sto_cycle_hits++;
            if (i < g_stm_min_assume)
                g_stm_min_assume = i; // record which ancestor we leaned on
            return true;              // co-inductive: assumed equal higher up
        }

    unsigned char key[2 * sizeof(Type *)];
    memcpy(key, &k1, sizeof k1);
    memcpy(key + sizeof k1, &k2, sizeof k2);
    if (g_same_type_memo_on) {
        void *m = hashmap_get2(&g_same_type_memo, (char *)key, (int)sizeof key);
        if (m) {
            g_sto_memo_hits++;
            return m == (void *)2;
        }
    }

    int my_depth = g_stp_len;
    same_type_pair_push(k1, k2);
    int saved_min    = g_stm_min_assume;
    g_stm_min_assume = INT_MAX; // fresh accounting for this subtree
    bool r           = same_type_or_origin_impl(a, b);
    int  subtree_min = g_stm_min_assume;
    g_stp_len--;

    if (subtree_min >= my_depth) {
        // Self-contained: the only assumption in play was (k1, k2) itself,
        // now fully resolved -> the result is unconditionally valid. Skip
        // caching if either side is an incomplete tagged type: it can be
        // completed in place later this pass, staling the entry.
        if (g_same_type_memo_on && !type_is_incomplete_tagged(a) &&
            !type_is_incomplete_tagged(b))
            hashmap_put2(&g_same_type_memo, (char *)key, (int)sizeof key,
                         r ? (void *)2 : (void *)1);
        g_stm_min_assume = saved_min;
    } else {
        // An ancestor's assumption is baked into r; propagate the debt so
        // that ancestor's frame decides, and do not memoize here.
        g_stm_min_assume = saved_min < subtree_min ? saved_min : subtree_min;
    }
    return r;
}

static bool same_type_or_origin(Type *a, Type *b) {
    g_sto_calls_total++;
    if (g_sto_recursion_depth == 0)
        g_sto_calls_toplevel++;
    if (g_sto_recursion_depth > g_sto_max_depth)
        g_sto_max_depth = g_sto_recursion_depth;
    g_sto_recursion_depth++;
    bool r = same_type_or_origin_guarded(a, b);
    g_sto_recursion_depth--;
    return r;
}

// #1283: reports the same_type_or_origin() call-cost counters (see their
// declaration above) to stderr at the end of the serialize pass when
// CCCC_TYPE_STATS is set in the environment. Registry sizes come from the
// caller since they live on SerializeContext.
void serialize_type_stats_report(SerializeContext *ctx) {
    if (!getenv("CCCC_TYPE_STATS"))
        return;
    fprintf(stderr,
            "[cccc type-stats #1283] same_type_or_origin: total=%lld "
            "toplevel=%lld ratio=%.1f max_depth=%d cycle_hits=%lld "
            "memo_hits=%lld | tags=%d typedefs=%d defs=%d\n",
            g_sto_calls_total, g_sto_calls_toplevel,
            g_sto_calls_toplevel
                ? (double)g_sto_calls_total / (double)g_sto_calls_toplevel
                : 0.0,
            g_sto_max_depth, g_sto_cycle_hits, g_sto_memo_hits, ctx->tags_len,
            ctx->typedefs_len, ctx->defs.len);
}

static bool same_type_or_origin_impl(Type *a, Type *b) {
    for (Type *pa = a; pa; pa = pa->origin)
        for (Type *pb = b; pb; pb = pb->origin)
            if (pa == pb)
                return true;

    // #1233: two independently-parsed (e.g. per-TU) pointer types can never
    // share an origin-chain node, so without a structural fallback here, any
    // aggregate with a pointer member -- true of nearly every non-trivial
    // runtime/ABI struct -- can never be proven structurally identical
    // across TUs even when byte-for-byte the same layout. This is what let
    // rename_colliding_type_tags() (src/serialize_program.c) treat a single
    // struct, merely completed independently in two TUs, as two distinct
    // colliding groups and rename one of them -- see that ticket for the
    // full symptom (a renamed spelling with no body, since the real body is
    // supplied by a verbatim-replayed header under the original spelling).
    // The in-progress-pair cycle break that used to live here is now applied
    // uniformly by same_type_or_origin() ahead of every arm (see the top of
    // this file), so this is just the structural recursion.
    if (a && b && a->kind == TY_PTR && b->kind == TY_PTR)
        return same_type_or_origin(a->base, b->base);

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

    // #1233: same gap as TY_PTR above, one level removed -- a struct member
    // whose type is a function pointer (e.g. a callback/closure slot, `LObj
    // *(*fn)(LObj *args, LObj *env)`) never structurally deduped across TUs
    // either, since nothing recursed into TY_FUNC's own return type/param
    // chain. `params` is itself a Type linked list (one node per parameter,
    // walked via `->next`); no name to compare, only each parameter's type.
    if (a && b && a->kind == TY_FUNC && b->kind == TY_FUNC) {
        if (a->is_variadic != b->is_variadic)
            return false;
        if (!same_type_or_origin(a->return_ty, b->return_ty))
            return false;
        Type *pa = a->params;
        Type *pb = b->params;
        for (; pa && pb; pa = pa->next, pb = pb->next)
            if (!same_type_or_origin(pa, pb))
                return false;
        return pa == NULL && pb == NULL;
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

bool type_vec_contains(TypeVec *vec, Type *ty) {
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

void type_vec_push(TypeVec *vec, Type *ty) {
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

void collect_scope_names(SerializeContext *ctx, VirtualMachine *vm) {
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

// ===========================================================================
// #1283: type-name registry candidate index.
//
// `ctx->tags` / `ctx->typedefs` are whole-program (not per-TU): at
// self-hosting scale they hold thousands of entries, mostly the same
// bundled-header tags/typedefs re-parsed once per input file under #1001's
// per-TU preprocessor isolation. Every provenance/dedup lookup below used to
// linear-scan the whole array calling same_type_or_origin() per entry -- an
// O(n) probe run O(n) times, the measured performance wall.
//
// Each probe is now narrowed to a small candidate set via three hash
// indices; same_type_or_origin() (plus each caller's own extra predicates)
// stays the final arbiter on that set. Correctness reduces to one property:
// the candidate set is a SUPERSET of every entry E for which
// same_type_or_origin(E.ty, ty) is true. CCCC_TYPE_INDEX_VERIFY=1 checks it
// against a full linear scan on every probe; CCCC_TYPE_INDEX_DISABLE=1 forces
// the old path.
//
//   by_root : origin_root(E.ty) -> entries. same_type_or_origin()'s first arm
//             is true iff the two ->origin chains intersect, i.e. iff they
//             share the same root (single-successor chain). Covers every
//             origin-identity match, every scalar (no structural arm at all),
//             and -- since a chain node shares its chain's root -- is a
//             superset of the 8-hop identity pre-scans.
//   by_tag  : hash(E.ty->struct_tag bytes) -> entries. A tag mismatch is
//             conclusive for TY_STRUCT/TY_UNION, and an incomplete tagged
//             aggregate matches only same-tag entries. (Enums keep their tag
//             in ->enum_tag, which that arm never reads -- not indexed here.)
//   by_fp   : depth-0 structural fingerprint -> entries, for every
//             struct/union/enum/func/array/pointer entry. A NECESSARY
//             condition for a same-kind structural match; member/param types
//             are NOT folded in (the recursion re-enters same_type_or_origin
//             with its full origin arm).
//
// The arrays are append-only; the later mutating passes
// (rename_colliding_type_tags, hoist_local_type_to_file_scope) rewrite only
// name / name_len / owner_fn -- read fresh at match time, never index keys.
// So the index is built incrementally on demand. One wrinkle:
// serialize_typedef_alias() (#999) transiently blanks a registered
// TypeName.ty to NULL while it prints that alias's own RHS; an ensure() that
// runs inside that window records the entry as a "hole" and retries it on the
// next ensure rather than skipping it forever.
// ===========================================================================

#define TYPE_CAND_CAP 1024

typedef struct {
    int *v;
    int  len, cap;
} IdxList;

typedef struct {
    HashMap by_root;
    HashMap by_tag;
    HashMap by_fp;
    int     indexed; // # of entries visited at least once
    IdxList holes;   // visited indices whose .ty was NULL then -- retried
} CandIndex;

static CandIndex g_tag_ix;
static CandIndex g_td_ix;
static int       g_type_index_disabled = -1; // -1 = env not yet read
static int       g_type_index_verify   = -1;

static void idxlist_add(IdxList *l, int x) {
    if (l->len >= l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->v   = realloc(l->v, sizeof(int) * l->cap);
    }
    l->v[l->len++] = x;
}

static void cand_bucket_add(HashMap *m, long long key, int idx) {
    IdxList *l = hashmap_get_int(m, key);
    if (!l) {
        l = calloc(1, sizeof(IdxList));
        hashmap_put_int(m, key, l);
    }
    idxlist_add(l, idx);
}

static Type *type_origin_root(Type *t) {
    if (!t)
        return NULL;
    int hop = 0;
    while (t->origin) {
        // copy_type() is the only writer of Type.origin and never closes a
        // loop, so a cycle here is a bug -- and a soft "return whatever we're
        // at" would give two entries in the same cycle different roots
        // depending on start phase, silently breaking the by_root superset
        // property. Fail loud instead.
        if (++hop > 1000000)
            error("cccc: internal error (#1283): Type.origin chain does not "
                  "terminate");
        t = t->origin;
    }
    return t;
}

static unsigned long long fp_step(unsigned long long h, unsigned long long x) {
    h ^= x;
    h *= 1099511628211ULL;
    return h;
}

static bool type_kind_has_fp(TypeKind k) {
    return k == TY_PTR || k == TY_ARRAY || k == TY_FUNC || k == TY_STRUCT ||
           k == TY_UNION || k == TY_ENUM;
}

// hash of a struct/union tag's spelling; 0 when untagged (never indexed).
static long long type_tag_key(Type *t) {
    Token *tg = t ? t->struct_tag : NULL;
    if (!tg)
        return 0;
    unsigned long long h = 1469598103934665603ULL;
    for (int i = 0; i < tg->len; i++)
        h = fp_step(h, (unsigned char)tg->loc[i]);
    return (long long)(h ? h : 1);
}

// depth-0 structural fingerprint -- a necessary condition for a same-kind
// structural match in same_type_or_origin(). Deliberately does NOT recurse
// into member/param/element types.
static long long type_fp_key(Type *t) {
    unsigned long long h = 1469598103934665603ULL;
    h                    = fp_step(h, (unsigned)t->kind);
    switch (t->kind) {
        case TY_PTR:
            h = fp_step(h, t->base ? (unsigned)t->base->kind + 1u : 0u);
            break;
        case TY_ARRAY:
            h = fp_step(h, (unsigned long long)(unsigned)t->array_len);
            break;
        case TY_FUNC: {
            int np = 0;
            for (Type *p = t->params; p; p = p->next)
                np++;
            h = fp_step(h, t->is_variadic ? 1u : 0u);
            h = fp_step(h, (unsigned)np);
            break;
        }
        case TY_STRUCT:
        case TY_UNION: {
            int mc = 0;
            for (Member *m = t->members; m; m = m->next) {
                mc++;
                if (m->name)
                    for (int i = 0; i < m->name->len; i++)
                        h = fp_step(h, (unsigned char)m->name->loc[i]);
                h = fp_step(h, 0x100u | (m->name ? 1u : 0u));
            }
            h = fp_step(h, (unsigned)mc);
            break;
        }
        case TY_ENUM: {
            int ec = 0;
            for (EnumConstant *e = t->enum_constants; e; e = e->next) {
                ec++;
                if (e->name)
                    for (const char *s = e->name; *s; s++)
                        h = fp_step(h, (unsigned char)*s);
                h = fp_step(h, 0x100u | (e->name ? 1u : 0u));
                h = fp_step(h, (unsigned long long)e->value);
            }
            h = fp_step(h, (unsigned)ec);
            break;
        }
        default:
            break;
    }
    return (long long)(h ? h : 1);
}

// Returns false when `t` is NULL (caller records `i` as a hole to retry).
static bool cand_add_one(CandIndex *ix, Type *t, int i) {
    if (!t)
        return false;
    cand_bucket_add(&ix->by_root, (long long)(intptr_t)type_origin_root(t), i);
    if (t->struct_tag)
        cand_bucket_add(&ix->by_tag, type_tag_key(t), i);
    if (type_kind_has_fp(t->kind))
        cand_bucket_add(&ix->by_fp, type_fp_key(t), i);
    return true;
}

static int free_idxlist_cb(char *k, int kl, void *v, void *ud) {
    (void)k;
    (void)kl;
    (void)ud;
    IdxList *l = v;
    free(l->v);
    free(l);
    return 0;
}

static void cand_index_clear(CandIndex *ix) {
    hashmap_foreach(&ix->by_root, free_idxlist_cb, NULL);
    hashmap_foreach(&ix->by_tag, free_idxlist_cb, NULL);
    hashmap_foreach(&ix->by_fp, free_idxlist_cb, NULL);
    hashmap_deinit(&ix->by_root);
    hashmap_deinit(&ix->by_tag);
    hashmap_deinit(&ix->by_fp);
    free(ix->holes.v);
    *ix = (CandIndex){0};
}

// #1283: called at the start (after the rename passes) and end of
// cc_serialize_program() so a stale index from a prior program is never
// carried over.
void serialize_type_index_reset(void) {
    cand_index_clear(&g_tag_ix);
    cand_index_clear(&g_td_ix);
    g_type_index_disabled = -1;
    g_type_index_verify   = -1;
}

// Fold every entry appended since the last call into the index; retry entries
// that were NULL last time (holes). O(holes + new entries) per call.
static void cand_index_ensure(CandIndex *ix, TypeName *arr, int len) {
    int w = 0;
    for (int h = 0; h < ix->holes.len; h++) {
        int i = ix->holes.v[h];
        if (!cand_add_one(ix, arr[i].ty, i))
            ix->holes.v[w++] = i;
    }
    ix->holes.len = w;
    for (int i = ix->indexed; i < len; i++)
        if (!cand_add_one(ix, arr[i].ty, i))
            idxlist_add(&ix->holes, i);
    ix->indexed = len;
}

static bool type_index_off(void) {
    if (g_type_index_disabled < 0)
        g_type_index_disabled = getenv("CCCC_TYPE_INDEX_DISABLE") ? 1 : 0;
    return g_type_index_disabled != 0;
}

// Fill `buf` (capacity TYPE_CAND_CAP) with the ascending, de-duplicated
// candidate index set for `ty`. Returns the count, or -1 for "no narrowing --
// scan all `len`" (index disabled, ty NULL, or the set overflowed the buffer).
static int cand_index_probe(CandIndex *ix, TypeName *arr, int len, Type *ty,
                            int *buf) {
    if (type_index_off() || !ty)
        return -1;
    cand_index_ensure(ix, arr, len);

    int n = 0;
    for (int pass = 0; pass < 3; pass++) {
        long long key;
        HashMap  *m;
        if (pass == 0) {
            m   = &ix->by_root;
            key = (long long)(intptr_t)type_origin_root(ty);
        } else if (pass == 1) {
            if (!ty->struct_tag)
                continue;
            m   = &ix->by_tag;
            key = type_tag_key(ty);
        } else {
            if (!type_kind_has_fp(ty->kind))
                continue;
            m   = &ix->by_fp;
            key = type_fp_key(ty);
        }
        IdxList *l = hashmap_get_int(m, key);
        if (!l)
            continue;
        for (int j = 0; j < l->len; j++) {
            if (n >= TYPE_CAND_CAP)
                return -1;
            buf[n++] = l->v[j];
        }
    }
    // insertion sort + dedup (n is small: entries per bucket ~= number of
    // input files declaring one shared type)
    for (int i = 1; i < n; i++) {
        int v = buf[i], j = i - 1;
        while (j >= 0 && buf[j] > v) {
            buf[j + 1] = buf[j];
            j--;
        }
        buf[j + 1] = v;
    }
    int w = 0;
    for (int i = 0; i < n; i++)
        if (w == 0 || buf[w - 1] != buf[i])
            buf[w++] = buf[i];

    if (g_type_index_verify < 0)
        g_type_index_verify = getenv("CCCC_TYPE_INDEX_VERIFY") ? 1 : 0;
    if (g_type_index_verify) {
        for (int i = 0; i < len; i++) {
            if (!arr[i].ty || !same_type_or_origin(arr[i].ty, ty))
                continue;
            bool found = false;
            for (int k = 0; k < w && !found; k++)
                found = (buf[k] == i);
            if (!found)
                error("cccc: internal error (#1283): type-name index missed a "
                      "match -- entry %d (kind=%d) matches same_type_or_origin "
                      "but is not a candidate",
                      i, (int)arr[i].ty->kind);
        }
    }
    return w;
}

static int type_cand_tags(SerializeContext *ctx, Type *ty, int *buf) {
    return cand_index_probe(&g_tag_ix, ctx->tags, ctx->tags_len, ty, buf);
}
static int type_cand_typedefs(SerializeContext *ctx, Type *ty, int *buf) {
    return cand_index_probe(&g_td_ix, ctx->typedefs, ctx->typedefs_len, ty,
                            buf);
}

// Candidate-index scratch pool: each TYPE_CAND_FOR loop takes one slot for
// its lifetime, rotating so nested uses don't clobber. The 8-hop identity
// scans deliberately probe once into a caller-local buffer instead, so live
// nesting never approaches 16.
static int          g_tc_pool[16][TYPE_CAND_CAP];
static unsigned int g_tc_pool_next;

// Iterate the candidate entry indices for `ty` against a registry, binding
// `ivar` (ascending array order); `cntexpr` is the fallback length when the
// index yields no narrowing. A single `for` -- safe to use several times in
// one scope and to nest.
#define TYPE_CAND_FOR(ivar, cndfn, ctxp, typ, cntexpr)                         \
    for (int *_tcb = g_tc_pool[g_tc_pool_next++ & 15u],                        \
             _tcn  = cndfn((ctxp), (typ), _tcb),                               \
             _tct = _tcn < 0 ? (cntexpr) : _tcn, _tck = 0, ivar = 0;           \
         _tck < _tct && (ivar = (_tcn < 0 ? _tck : _tcb[_tck]), 1); _tck++)

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
bool type_is_complete_tagged(Type *ty) {
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
bool same_type_strong(Type *a, Type *b) {
    return type_is_complete_tagged(a) == type_is_complete_tagged(b) &&
           same_type_or_origin(a, b);
}

// #1015: forward-declared here since serialize_enum_def() (below) needs it
// but its definition, next to rename_colliding_enum_constants(), comes
// much later in this file.

// #1047: forward-declared here since serialize_global_var() (below) needs
// it but its definition, next to function_is_header_supplied() (the
// function-side counterpart it mirrors), comes much later in this file.

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
        TYPE_CAND_FOR(i, type_cand_tags, ctx, ty, ctx->tags_len)
    if (name_visible(&ctx->tags[i], ctx->current_fn) &&
        !tag_spelling_mismatch(ty, ctx->tags[i].ty) &&
        same_type_strong(ctx->tags[i].ty, ty))
        return &ctx->tags[i];

    TYPE_CAND_FOR(i, type_cand_tags, ctx, ty, ctx->tags_len)
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
    TYPE_CAND_FOR(i, type_cand_tags, ctx, ty, ctx->tags_len) {
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
TypeName *find_typedef_name(SerializeContext *ctx, Type *ty) {
    if (!ctx || !ty)
        return NULL;

    TypeName *exact = find_typedef_name_exact(ctx, ty);
    if (exact)
        return exact;

    TYPE_CAND_FOR(i, type_cand_typedefs, ctx, ty, ctx->typedefs_len)
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
    // #1283: probe once for `ty` (every origin-chain hop's candidate set is a
    // subset -- all chain nodes share an origin root), then iterate hop-major
    // over that fixed set to preserve "nearest hop wins" ordering.
    int cb[TYPE_CAND_CAP];
    int cn    = type_cand_typedefs(ctx, ty, cb);
    int total = cn < 0 ? ctx->typedefs_len : cn;
    int hop   = 0;
    for (Type *cur = ty; cur && hop < 8; cur = cur->origin, hop++)
        for (int k = 0; k < total; k++) {
            int i = cn < 0 ? k : cb[k];
            if (ctx->typedefs[i].ty == cur &&
                (!require_visible ||
                 name_visible(&ctx->typedefs[i], ctx->current_fn)))
                return &ctx->typedefs[i];
        }
    return NULL;
}

TypeName *find_typedef_name_exact(SerializeContext *ctx, Type *ty) {
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
    int cb[TYPE_CAND_CAP];
    int cn    = type_cand_tags(ctx, ty, cb);
    int total = cn < 0 ? ctx->tags_len : cn;
    int hop   = 0;
    for (Type *cur = ty; cur && hop < 8; cur = cur->origin, hop++)
        for (int k = 0; k < total; k++) {
            int i = cn < 0 ? k : cb[k];
            if (ctx->tags[i].ty == cur) {
                if (owner)
                    *owner = ctx->tags[i].owner_fn;
                return true;
            }
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

    TYPE_CAND_FOR(i, type_cand_typedefs, ctx, ty, ctx->typedefs_len) {
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
    // #1283: probe both registries once for `ty`; every origin-chain hop's
    // candidate set is a subset. Iterate hop-major to keep "nearest hop wins".
    int tcb[TYPE_CAND_CAP], ycb[TYPE_CAND_CAP];
    int tcn  = type_cand_tags(ctx, ty, tcb);
    int ycn  = type_cand_typedefs(ctx, ty, ycb);
    int ttot = tcn < 0 ? ctx->tags_len : tcn;
    int ytot = ycn < 0 ? ctx->typedefs_len : ycn;
    int hop  = 0;
    for (Type *cur = ty; cur && hop < 8; cur = cur->origin, hop++) {
        for (int k = 0; k < ttot; k++) {
            int i = tcn < 0 ? k : tcb[k];
            if (ctx->tags[i].ty == cur)
                return ctx->tags[i].owner_fn;
        }
        for (int k = 0; k < ytot; k++) {
            int i = ycn < 0 ? k : ycb[k];
            if (ctx->typedefs[i].ty == cur)
                return ctx->typedefs[i].owner_fn;
        }
    }
    for (int k = 0; k < ttot; k++) {
        int i = tcn < 0 ? k : tcb[k];
        if (same_type_or_origin(ctx->tags[i].ty, ty))
            return ctx->tags[i].owner_fn;
    }
    for (int k = 0; k < ytot; k++) {
        int i = ycn < 0 ? k : ycb[k];
        if (same_type_or_origin(ctx->typedefs[i].ty, ty))
            return ctx->typedefs[i].owner_fn;
    }
    return NULL;
}

static void collect_type(SerializeContext *ctx, Type *ty);
// #1167: forward-declared so collect_node_types()/collect_type() (both
// defined here, well above their own definitions further down this file)
// can share the exact same "will this layout constant actually be
// re-materialized" gate serialize_layout_const() itself uses, rather than
// a parallel copy that could drift out of sync with it.
static bool type_has_printable_name(SerializeContext *ctx, Type *ty);

// #1167: true when a folded sizeof/_Alignof of `ty` is a candidate for
// collect_node_types()/collect_type() to feed into ctx->defs -- mirrors
// serialize_layout_const()'s own gate exactly (type_layout_is_host_owned()
// + type_has_printable_name()), not a broader "collect it just in case"
// test. Collecting unconditionally regressed test_suite_structs.c's own
// `_Static_assert(sizeof(struct tc_bi1135_wide) == 32, ...)`: that struct
// has a `_BitInt(129)` member, which back when serialize_type.c's TY_BITINT
// case still hard-errored on N>128 (before #1123's __cccc_biK lowering) blew
// up here even though it compiled fine pre-#1167 -- the struct was never
// collected (the assert stays folded; type_layout_is_host_owned() is false
// for a plain non-from_include struct), so its body was never emitted.
// Collecting it anyway, only to have serialize_layout_const() decline to
// re-materialize the operator for the exact same reason, forced that
// otherwise-never-emitted body into the output and hit the hard error. #1123
// removed that hard error (a wide-_BitInt member now prints its __cccc_biK
// container like any other member type), so this is no longer a correctness
// hazard -- kept as-is regardless, since the gate this function applies
// (type_layout_is_host_owned() + type_has_printable_name()) is still the
// correct, narrower test for whether re-materialization is even possible: a
// type ctx->defs contains gets its FULL body printed by
// serialize_type_defs_for_owner() (unless from_include-suppressed), a much
// stronger action than "available for re-materialization".
static bool layout_type_needs_collecting(SerializeContext *ctx, Type *ty) {
    return ty && type_layout_is_host_owned(ctx, ty, 0) &&
           type_has_printable_name(ctx, ty);
}

// #1167: a struct/union/enum referenced ONLY inside sizeof/_Alignof/a
// case label/an enum value/a _Static_assert is const-folded to a plain
// integer literal at parse time, but the operand Type IS retained on the
// fold site (Node.layout_ty/case_begin_layout_ty/case_end_layout_ty,
// EnumConstant.layout_ty, Type.array_len_layout_ty -- see #1031/#1095/
// #1098's own comments) so serialize_layout_const() can re-materialize the
// operator textually when the type is host-owned (#1031). Without walking
// those stashes here too, that re-materialized `sizeof(T)`/`_Alignof(T)`
// text is the only reference to T anywhere in the AST -- collect_type()
// never visits it, no definition (not even a forward declaration) is
// emitted, and the host compiler rejects the emitted C ("invalid
// application of 'sizeof' to an incomplete type"). Minimal repro: `union
// W { char a; int b; }; ... printf("%zu", sizeof(union W));`. Declaring an
// actual object of the type elsewhere in the same TU worked around it by
// giving collect_node_types() a real node->ty/var->ty reference to walk;
// the fix below gives it the layout-provenance references directly,
// gated by layout_type_needs_collecting() so a type that stays folded
// (the common case) is never force-emitted.
static void collect_node_types(SerializeContext *ctx, Node *node) {
    if (!node)
        return;

    collect_type(ctx, node->ty);
    // #1205: an enumerator reference's own node->ty is ty_int (C17/C23
    // 6.7.2.2p3), not the enum type -- without this, an enum referenced
    // only via its enumerators (never sizeof'd, never used as an object's
    // type) is never collected and its tag definition is silently dropped.
    if (node->enum_source_ty)
        collect_type(ctx, node->enum_source_ty);
    if (node->var)
        collect_type(ctx, node->var->ty);
    if (node->member)
        collect_type(ctx, node->member->ty);
    if (node->func_ty)
        collect_type(ctx, node->func_ty);
    // #1167: see this function's own comment above -- these are the
    // layout-provenance stashes a folded sizeof/_Alignof/case-label/
    // _Static_assert leaves behind; without collecting them, a
    // re-materialized `sizeof(T)`/`_Alignof(T)` (serialize_layout_const())
    // can be the only surviving reference to T in the whole AST.
    if (layout_type_needs_collecting(ctx, node->layout_ty))
        collect_type(ctx, node->layout_ty);
    if (layout_type_needs_collecting(ctx, node->case_begin_layout_ty))
        collect_type(ctx, node->case_begin_layout_ty);
    if (layout_type_needs_collecting(ctx, node->case_end_layout_ty))
        collect_type(ctx, node->case_end_layout_ty);
    // #1098: a block-scope _Static_assert's condition tree (stashed on an
    // otherwise-empty ND_BLOCK, see Node.static_assert_cond's own comment)
    // isn't reached by the generic ->lhs/->rhs/... walk below at all --
    // recurse into it explicitly so any layout_ty leaves inside it (an
    // arbitrarily nested sizeof/_Alignof, not just a bare top-level one)
    // are collected too.
    collect_node_types(ctx, node->static_assert_cond);

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

    // #1113(a): every type reachable from the program funnels through here
    // before any real output is written (collect_obj_types()'s obj->ty/
    // param->ty/local->ty walk, collect_node_types()'s node->ty walk for
    // every literal/expression, and this function's own member recursion
    // for a decimal struct member) -- the single choke point to notice a
    // _Decimal32/64/128 use anywhere in the program ahead of time, so the
    // guard preamble (serialize_decimal_native_guard(), serialize_
    // program.c) can be written before the definitions that use it.
    if (is_decimal(ty))
        ctx->saw_decimal = true;

    if (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_VLA) {
        // #1167: an array dimension folded from a bare sizeof/_Alignof
        // (Type.array_len_layout_ty, #1095) can be re-materialized by
        // serialize_type() the same way node->layout_ty is -- collect its
        // operand type too (gated identically to serialize_layout_const()'s
        // own check, see layout_type_needs_collecting()'s comment), or that
        // re-materialized text can be the only reference to it left in the
        // AST. Must run before this arm's own `return` below (TY_PTR has no
        // array_len_layout_ty, but sharing the arm costs nothing and keeps
        // this next to ty->base).
        if (layout_type_needs_collecting(ctx, ty->array_len_layout_ty))
            collect_type(ctx, ty->array_len_layout_ty);
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
    // #1167: TY_ENUM has no ty->members (the loop above is a no-op for it),
    // so an enumerator's own folded sizeof/_Alignof (EnumConstant.layout_ty,
    // #1095), gated identically to serialize_layout_const()'s own check
    // (layout_type_needs_collecting()), needs its own walk here -- otherwise
    // a re-materialized `sizeof(T)`/`_Alignof(T)` enum value
    // (serialize_enum_def()) can be the only reference to T left in the AST.
    if (ty->kind == TY_ENUM)
        for (EnumConstant *ec = ty->enum_constants; ec; ec = ec->next)
            if (layout_type_needs_collecting(ctx, ec->layout_ty))
                collect_type(ctx, ec->layout_ty);

    type_vec_push_nominal(ctx, &ctx->defs, ty);
}

void collect_obj_types(SerializeContext *ctx, Obj *obj) {
    collect_type(ctx, obj->ty);
    collect_node_types(ctx, obj->init_expr);

    for (Obj *param = obj->params; param; param = param->next)
        collect_type(ctx, param->ty);
    for (Obj *local = obj->locals; local; local = local->next)
        collect_type(ctx, local->ty);
    collect_node_types(ctx, obj->body);
}

// #1167: a file-scope `_Static_assert`/`static_assert` (StaticAssertRecord,
// #1098) has no Node of its own attached to any Obj -- static_asserts is a
// standalone list on the compiler (cccc.h) -- so collect_obj_types()'s walk
// never reaches its condition tree. Exposed so serialize_program.c's own
// cc_serialize_program() can collect it explicitly alongside the
// collect_obj_types() loop, or a re-materialized `sizeof(T)`/`_Alignof(T)`
// inside the assert (serialize_static_assert()) can be the only reference
// to T left in the AST. Collected unconditionally, regardless of whether
// the emitter will actually re-emit this particular assert -- under-
// collecting is exactly the bug this ticket fixes, and
// type_def_is_from_include_suppressed() already drops any definition the
// emitter itself won't print.
void collect_static_assert_types(SerializeContext *ctx, Node *cond) {
    collect_node_types(ctx, cond);
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

void reorder_defs_by_byval_deps(SerializeContext *ctx) {
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
bool obj_vec_contains(ObjVec *vec, Obj *obj) {
    for (int i = 0; i < vec->len; i++)
        if (vec->data[i] == obj)
            return true;
    return false;
}

void obj_vec_push(ObjVec *vec, Obj *obj) {
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
void collect_generated_call_targets(Node *node, ObjVec *out) {
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

// #1031: defined near serialize_type_defs_for_owner (which shares
// type_layout_is_host_owned's own from_include-suppression logic via
// type_def_is_from_include_suppressed), forward-declared here so
// serialize_expr's ND_NUM case -- much earlier in this file -- can call
// them.
static bool type_has_printable_name(SerializeContext *ctx, Type *ty);
// #1095: factored out of serialize_expr's own ND_NUM arm below so array
// dimensions/case labels/enum values (none of which have a Node to walk by
// the time serialization runs -- see const_expr_layout(), parse_analysis.c)
// can share the exact same host-owned/printable-name gate rather than a
// parallel copy. Prints "sizeof(T)"/"_Alignof(T)" and returns true when
// `layout_ty` qualifies; otherwise prints nothing and returns false, so
// every caller's own fallback (the plain folded literal) still applies.
// #1098: forward-declared for the same reason as the two above -- defined
// near type_layout_is_host_owned (which expr_has_host_owned_layout calls),
// but serialize_stmt's ND_BLOCK case, much earlier in this file, needs to
// call serialize_static_assert.
static bool expr_has_host_owned_layout(SerializeContext *ctx, Node *node,
                                       int depth);

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
bool serialize_flonum_special(FILE *f, long double v, const char *suf) {
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

void format_float_literal(char *buf, size_t cap, double v) {
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
void format_ldouble_literal(char *buf, size_t cap, long double v) {
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
unsigned __int128 decode_wide_digits(const char *digits, int base) {
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

// Forward declaration -- defined below (near type_layout_is_host_owned,
// which shares its logic); serialize_type_decl's TY_PTR case (#1145 fix
// just below) needs it ahead of that definition.
static bool type_def_is_from_include_suppressed(SerializeContext *ctx,
                                                Type             *ty);

void serialize_type_decl(FILE *f, SerializeContext *ctx, Type *ty,
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
        // #1295: pointer-level const/volatile/restrict/_Atomic (`int
        // *const p`, `int *restrict p`, as opposed to `const int *p`, whose
        // qualifier lives on the pointee's base type instead) live on this
        // TY_PTR Type itself and were never printed anywhere -- this is the
        // declarator-position home for them (serialize_type()'s
        // suppress_ptr_qual gate deliberately suppresses the leading-keyword
        // spelling for a bare, non-typedef'd pointer so it doesn't land on
        // the pointee instead; see that gate's own comment). `restrict` is
        // only ever legal on a pointer-to-object, so this is also the only
        // place it is ever emitted. Order follows the canonical C spelling.
        bool   has_name      = name && *name;
        char   qualwords[48] = "";
        size_t qwlen         = 0;
#define APPEND_QUALWORD(word)                                                  \
    qwlen += (size_t)snprintf(qualwords + qwlen, sizeof(qualwords) - qwlen,    \
                              "%s" word, qwlen ? " " : "")
        if (ty->is_const)
            APPEND_QUALWORD("const");
        if (ty->is_volatile)
            APPEND_QUALWORD("volatile");
        if (ty->is_restrict)
            APPEND_QUALWORD("restrict");
        // #1295 follow-up: unlike const/volatile/restrict, `_Atomic` is
        // ONLY printed here when a real name follows (a genuine
        // declaration) -- never in the abstract-declarator form an explicit
        // cast uses (`(int *_Atomic)expr`). A verified Apple clang 17
        // (Xcode 17.0, the native host compiler CCCC_NATIVE_CC resolves to
        // by default on macOS) Sema/constant-folding bug segfaults the
        // FRONTEND outright compiling exactly that cast spelling applied to
        // a non-null pointer value (`(int *_Atomic)&x` crashes; `int
        // *_Atomic p;`, `void f(int *_Atomic);`, and `(int *_Atomic)0` all
        // compile fine -- isolated by hand, not from any upstream bug
        // report). CCCC's own serializer inserts exactly such a cast on
        // every assignment to an atomic-pointer-typed variable (matching
        // #1045's `*const`/#1291's `*volatile` cast-insertion behaviour),
        // so emitting the qualifier there would crash a real, currently
        // shipping host toolchain on ordinary code. Dropping it from the
        // cast is safe: the cast's only job is re-asserting the pointer's
        // bit pattern, and an unqualified-to-atomic-qualified pointer
        // conversion needs no cast in real C anyway.
        if (ty->is_atomic && has_name)
            APPEND_QUALWORD("_Atomic");
#undef APPEND_QUALWORD
        char qual[256] = "";
        if (ctx->emit_cccc)
            format_checked_ptr_qualifier(qual, sizeof(qual), ty);
        const char *sep = (has_name && (qualwords[0] || qual[0])) ? " " : "";
        // #971: TY_VLA is an array type for declarator-parenthesization
        // purposes, same as TY_ARRAY -- pointer-to-VLA (the row type of a
        // multi-dimensional VLA, `int (*)[m]`) needs the same `(*name)`
        // grouping a fixed-size array pointer gets, or the `*` binds to the
        // element type and mis-spells it as `int *[m]` (array of pointers).
        if (ty->base && (ty->base->kind == TY_ARRAY ||
                         ty->base->kind == TY_VLA || ty->base->kind == TY_FUNC))
            snprintf(buf, sizeof(buf), "(*%s%s%s%s)", qualwords, qual, sep,
                     has_name ? name : "");
        else
            snprintf(buf, sizeof(buf), "*%s%s%s%s", qualwords, qual, sep,
                     has_name ? name : "");
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

// #1145: local-variable-declaration counterpart to serialize_type_decl,
// used only at the handful of call sites that declare a function-local
// variable (serialize_stmt.c/serialize_decl.c) -- NOT a general
// replacement for serialize_type_decl itself, which is also used to print
// typedef right-hand sides and function-parameter prototypes/definitions,
// where the exact same alias-preserving check regressed
// test_threads_basic.c and friends: `typedef pthread_t thrd_t;` printed
// correctly by this check, while a *separate* prototype's `thrd_t thr`
// parameter (a distinct Type copy without the same resolvable identity)
// kept decomposing to `void *thr`, producing two disagreeing declarations
// for the same function ("conflicting types for 'thrd_join'"). Confining
// the check to local declarations avoids that inconsistency: a local
// variable is declared exactly once, so there is no second, independently
// serialized declaration site for it to disagree with.
//
// The bug this exists for: a from_include *pointer* typedef (e.g. glibc's
// real posix_spawnattr_t/posix_spawn_file_actions_t, structs CCCC's own
// bundled spawn.h models as an opaque `typedef void *` handle for
// portability -- see that header's own comment) must be spelled by its
// alias for a LOCAL variable, not decomposed structurally into "void
// *name" like an ordinary pointer declarator -- decomposing throws the
// alias away and always declares the variable at CCCC's own (possibly
// undersized) pointee type instead of deferring to whichever real type the
// replayed #include supplies. Confirmed as silent stack corruption: `void
// *attr;` handed to glibc's posix_spawnattr_init(), which writes a
// 336-byte struct, corrupts the 8-byte stack slot next to it. An ordinary
// in-house pointer typedef (`typedef int *IntPtr;`) has no such divergence
// and is unaffected -- decomposition already produces identical,
// correctly-sized C for those, so this only changes behavior for exactly
// the divergent, from_include case.
// #1296: factored out of serialize_local_var_type_decl (below) so
// serialize_function_signature (src/serialize_decl.c) can apply the exact
// same alias-preservation rule to a bodiless declaration's parameter/return
// types -- a from_include *pointer* typedef (e.g. nl_types.h's real
// `nl_catd`, an opaque `struct __nl_cat_d *` on macOS) must be spelled by
// its alias there too, not decomposed to `void *`: CCCC's own #1096
// fallback prototype for `catclose`/`catgets`/`catopen` used to always
// decompose, colliding with the real SDK's `nl_catd`-typed declaration
// ("conflicting types for 'catclose'"). Returns true (and has already
// printed `name`) when the alias-preserving spelling applied; false when
// the caller should fall back to serialize_type_decl itself.
bool serialize_aliased_ptr_type_decl(FILE *f, SerializeContext *ctx, Type *ty,
                                     const char *name) {
    if (ty && ty->kind == TY_PTR) {
        TypeName *ptr_alias = find_typedef_name_exact(ctx, ty);
        if (ptr_alias && type_def_is_from_include_suppressed(ctx, ty)) {
            serialize_type(f, ctx, ty);
            if (name && *name)
                fprintf(f, " %s", name);
            return true;
        }
    }
    return false;
}

void serialize_local_var_type_decl(FILE *f, SerializeContext *ctx, Type *ty,
                                   const char *name) {
    if (serialize_aliased_ptr_type_decl(f, ctx, ty, name))
        return;
    serialize_type_decl(f, ctx, ty, name);
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
bool type_needs_anon_aggregate(SerializeContext *ctx, Type *ty) {
    while (ty &&
           (ty->kind == TY_ARRAY || ty->kind == TY_PTR || ty->kind == TY_VLA))
        ty = ty->base;
    if (!ty || (ty->kind != TY_STRUCT && ty->kind != TY_UNION))
        return false;
    return !find_tag_name(ctx, ty) && !find_typedef_name(ctx, ty) &&
           !find_anonymous_typedef_name(ctx, ty);
}

// #1129/#1163: packed/aligned(N) were retained on Type (is_packed, align)
// but never re-emitted, so a struct's native layout silently diverged from
// the VM's -- see the admissibility-rule discussion in NATIVE.md. Shared
// by both aggregate-body emitters below (tagged/typedef'd and anonymous)
// since the two loops were otherwise identical.
//
// Member _Alignas(N): outside a packed aggregate, mem->align is set
// (parse_types.c) to either an explicit _Alignas/aligned request or,
// absent one, the member type's own natural alignment -- so
// mem->align > mem->ty->align isolates exactly an explicit request.
// Inside a packed aggregate that heuristic breaks (an explicit request
// equal to the member's natural alignment, e.g. aligned(4) on an int,
// looks identical to no request at all), so mem->explicit_align --
// 0 unless the declarator carried its own alignment attribute -- is used
// instead; struct_decl/union_decl (parse_types.c) apply exactly this same
// value under packed, so re-emitting it here keeps native layout in sync
// with the VM's. `> 1` rather than `!= 0`: an explicit request of 1 under
// packed is already the default, so it's both unnecessary and (per
// explicit_decl_align's own C17 6.7.5p4 check) the only value guaranteed
// never to lower alignment below the packed default, so it's always safe
// to omit.
//
// #1165: a bit-field member never re-emits via the _Alignas(N) prefix
// above, packed or not -- gcc rejects _Alignas on a bit-field outright, and
// cccc's own parser (struct_members(), parse_types.c) already rejects it
// at parse time too, so a bit-field's m->explicit_align (when set) can only
// have come from a legal suffix __attribute__((aligned(N))), which is
// re-emitted the same way, after the `: width` instead of before the type.
static void serialize_aggregate_members(FILE *f, SerializeContext *ctx,
                                        Type *ty) {
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
        int emit_align = ty->is_packed
                             ? (m->explicit_align > 1 ? m->explicit_align : 0)
                             : (m->align > m->ty->align ? m->align : 0);
        // #1165: gcc rejects _Alignas(N)/alignas(N) on a bit-field outright
        // ("alignment specified for bit-field") -- struct_members()
        // (parse_types.c) already enforces that at parse time, so a
        // bit-field here only ever carries alignment via m->explicit_align
        // (a suffix __attribute__((aligned(N))), which *is* legal on a
        // bit-field and applies packed or not, verified against gcc-16).
        // Re-emit it the same way, as a trailing GNU attribute after the
        // width instead of a declspec prefix.
        if (m->is_bitfield) {
            // #1123: a bitfield whose *declared* type is a wide (>128-bit)
            // _BitInt has no legal C spelling at all -- a bit-field's type
            // must be an integer type, and the __cccc_biK container
            // serialize_type() now emits for a bare wide _BitInt object is a
            // struct, so `__cccc_bi4 f : 193;` is exactly as illegal as
            // `struct S f : 193;` would be (confirmed: gcc/clang both reject
            // it, "bit-field 'f' has invalid type"). #1123's own value-level
            // lowering (a statement-expression per operation, an emitted
            // __cccc_biK container) does not by itself cover this -- it
            // would need every member access on the *whole enclosing
            // aggregate* rewritten to opaque byte storage (m->offset-based
            // extract/insert for every member, not just this one), a
            // separate, larger piece of work than the value-level case this
            // ticket closes. Refuse loudly here rather than let this fall
            // through to the __cccc_biK spelling above, which a host
            // compiler rejects with a confusing diagnostic that doesn't
            // name CCCC or this member at all.
            //
            // Plain error(), not error_tok(ctx->vm, m->tok, ...): m->tok
            // does exist here, but routing through error_tok crashed
            // (SIGSEGV) reached from an ND_ASSIGN into this exact member --
            // this file's aggregate-body printing is apparently not a safe
            // place to re-enter error_tok's longjmp path. serialize_decl.c's
            // own bitfield-initializer refusal (the sibling #1123 note this
            // mirrors, "bitfield wider than 128 bits") uses plain error()
            // too. Neither goes through the batched cc_print_all_errors()
            // summary path this way, so the trailer below is appended by
            // hand to match the "N error(s) generated." phrasing the test
            // harness's compile-error heuristic (tools/testing/runner.py)
            // already scans for -- same reason serialize_type()'s own
            // TY_BITINT case (just above) needed one.
            if (m->ty && m->ty->kind == TY_BITINT && m->ty->size > 16)
                error("cccc: cannot serialize a bitfield whose declared "
                      "type is _BitInt(%d) in native mode: no bitfield wider "
                      "than 128 bits has a native/-m lowering yet\n\n1 error "
                      "generated.",
                      m->ty->bit_width);
            serialize_type_decl(f, ctx, m->ty, name);
            fprintf(f, " : %d", m->bit_width);
            if (m->explicit_align > 1)
                fprintf(f, " __attribute__((aligned(%d)))", m->explicit_align);
        } else {
            if (emit_align)
                fprintf(f, "_Alignas(%d) ", emit_align);
            serialize_type_decl(f, ctx, m->ty, name);
        }
        fprintf(f, ";\n");
    }
}

// #1129/#1163: the type-level counterpart of serialize_aggregate_members --
// __attribute__((packed)) and __attribute__((aligned(N))) applied to the
// aggregate itself rather than to one member.
//
// ty->align starts at 1 (new_type's default for TY_STRUCT/TY_UNION,
// type.c) and is set directly by an aligned(N) attribute (parse_types.c);
// struct_decl/union_decl then raise it to max(mem_align) as members are
// laid out, where mem_align is mem->align normally but mem->explicit_align
// (or 1) under packed -- see those functions' own comments. `natural`
// below recomputes that same max() so this stays a precise "did the
// aggregate-level attribute have any effect beyond what its members
// already contributed" test, without duplicating struct_decl's offset
// bookkeeping: emit aligned(N) iff ty->align exceeds it, packed or not.
static void serialize_aggregate_attrs(FILE *f, Type *ty) {
    int natural = 1;
    for (Member *m = ty->members; m; m = m->next) {
        int contributed = ty->is_packed ? m->explicit_align : m->align;
        if (contributed > natural)
            natural = contributed;
    }

    bool has_align = ty->align > natural;

    if (!ty->is_packed && !has_align)
        return;

    fprintf(f, " __attribute__((");
    if (ty->is_packed)
        fprintf(f, "packed");
    if (has_align)
        fprintf(f, "%saligned(%d)", ty->is_packed ? ", " : "", ty->align);
    fprintf(f, "))");
}

// Serialize the body of a struct/union with no tag and no typedef alias
// (e.g. `struct { int x; int y; } pt;`) inline at its point of use, since
// there is no name to refer back to it by elsewhere.
static void serialize_anon_aggregate(FILE *f, SerializeContext *ctx, Type *ty) {
    // #1173: unlike __attribute__((packed))/aligned(N) (serialize_aggregate_
    // attrs, embeddable anywhere a type can appear), #pragma pack(push, N)/
    // pop is a preprocessing directive -- it must sit on its own line, so it
    // cannot wrap an anonymous aggregate serialized inline in the middle of
    // some other declaration (a variable, parameter, or member type). Refuse
    // rather than silently drop the pack requirement -- exactly the
    // "accepted, not honoured" bug class this ticket exists to close.
    // serialize_struct_def (the tagged/typedef'd path) does not have this
    // problem: it always emits its own top-level, one-definition-per-line
    // statement.
    if (ty->pack_align)
        error("cccc: an anonymous struct/union under #pragma pack(%d) has "
              "no tag or typedef name -- give it one so its definition can "
              "be emitted at file scope, wrapped in "
              "#pragma pack(push, %d)/pop",
              ty->pack_align, ty->pack_align);
    fprintf(f, "%s {\n", ty->kind == TY_UNION ? "union" : "struct");
    serialize_aggregate_members(f, ctx, ty);
    fprintf(f, "}");
    serialize_aggregate_attrs(f, ty);
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
void serialize_type(FILE *f, SerializeContext *ctx, Type *ty) {
    if (!ty) {
        fprintf(f, "void");
        return;
    }

    // #1045: a const-qualified *pointer* (`int *const p`, is_const lives on
    // the TY_PTR Type itself, not its base) used to print this leading
    // `const ` unconditionally, then fall through to the switch's TY_PTR
    // case below, which recurses into serialize_type_decl() -- whose own
    // TY_PTR branch never emitted pointer-level const at all (it only ever
    // printed `*name`, never `*const name`). The leading `const ` above
    // therefore ended up qualifying the *pointee* instead: `const int *`
    // (pointer to const int) rather than `int *const` (const pointer to
    // int) -- a genuinely incompatible type the host compiler rejects
    // ("incompatible function pointer types") wherever a const-pointer
    // value is cast to or declared alongside its non-const-pointer
    // counterpart. #1291 hit the identical hazard for is_volatile. #1295:
    // rather than keep dropping the qualifier, serialize_type_decl()'s
    // TY_PTR branch now prints pointer-level const/volatile/restrict/
    // _Atomic in the correct declarator position -- so a bare
    // (non-typedef'd) pointer must never *also* print the leading keyword
    // here, or the qualifier would land on both the pointer and its
    // pointee. A *typedef'd* pointer (`const MyPtrT`, where MyPtrT's
    // underlying type is itself a pointer) is a different case and
    // untouched: `const MyPtrT` correctly spells "const-qualify the whole
    // aliased type", i.e. exactly `T *const`, so that alias arm below still
    // needs the leading keyword -- only the un-aliased, structurally-printed
    // TY_PTR fallthrough suppresses it (in favour of declarator position).
    bool suppress_ptr_qual =
        ty->kind == TY_PTR && !find_typedef_name_exact(ctx, ty);
    if (ty->is_const && !suppress_ptr_qual)
        fprintf(f, "const ");
    // #1291: was previously never emitted at all -- a header-declared
    // `extern volatile sig_atomic_t g;` re-declared by -c=native/-m as plain
    // `sig_atomic_t g;` collides with the replayed header's own `volatile`
    // declaration ("redeclaration... with a different type") the moment
    // that global is re-spelled anywhere (the #918 forward-declare-every-
    // global pass, and the definition site).
    if (ty->is_volatile && !suppress_ptr_qual)
        fprintf(f, "volatile ");

    // Deliberately no output for ty->checked_kind (#770/#482-484): a
    // checked pointer's [[cccc::single/array/ntarray]] qualifier is a
    // cccc-internal VM-side check, not a real C construct -- gcc/clang would
    // reject the attribute names outright, and #488 requires -E/-c=generated
    // native output to be unchanged for a checked declaration ("no change to
    // ABI or to unchecked callers"). This isn't affected by #1295's
    // const/volatile/restrict/_Atomic generalization below/above -- the
    // checked-pointer attribute is printed by
    // format_checked_ptr_qualifier(), a separate --emit-cccc-gated function,
    // never by the plain-qualifier logic here.

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
    bool printed_atomic_via_alias = false;
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
            printed_atomic_via_alias = true;
        }
    }
    // #1295: a bare (non-typedef'd) _Atomic-qualified scalar/aggregate --
    // e.g. `_Atomic int`, previously spelled as plain `int` -- only survived
    // by accident when it happened to also have a typedef alias printed
    // above (`atomic_int` etc). Emitted structurally here, after the alias
    // arm (which already returns for every alias except the atomic_flag
    // divergence, itself handled by printed_atomic_via_alias), so a typedef
    // alias still prints as its own name rather than `_Atomic <alias>`.
    // Suppressed on a bare pointer for the same reason is_const/is_volatile
    // are above: an atomic *pointer* (`int *_Atomic p`) is spelled in
    // declarator position by serialize_type_decl()'s TY_PTR branch instead.
    if (ty->is_atomic && !suppress_ptr_qual && !printed_atomic_via_alias)
        fprintf(f, "_Atomic ");

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
            // #1234: CCCC has no distinct `long long` kind -- it shares
            // TY_LONG with `long` (same representation on every LP64 target).
            // is_long_long is a spelling-only bit recording that a declaration
            // wrote `long long`, so a real header the output also #includes
            // that spells the type `long long` doesn't collide with a
            // re-emitted `long` prototype ("conflicting types").
            fprintf(f, "%s%s", ty->is_unsigned ? "unsigned " : "",
                    ty->is_long_long ? "long long" : "long");
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
            // #1186: an opaque handle typedef -- `typedef struct __cccc_FTS
            // FTS;` (include/fts.h), and the same shape for DIR (dirent.h)
            // and DBM (ndbm.h) -- deliberately never completes its tag
            // (ty->members stays NULL forever; the guest only ever passes
            // the pointer back to host-backed functions, never dereferences
            // it). Under -c=native the replayed #include hands the real
            // host header's own FTS/DIR/DBM to the compiled output (the
            // #1143 -idirafter demotion lets the host header win the
            // search), so `struct __cccc_FTS` and the host's `FTS` are two
            // distinct, incompatible types by the time gcc/clang see them --
            // a declaration spelled by the tag (`struct __cccc_FTS *fts;`)
            // disagrees with a cast spelled by the alias (`(FTS *)fts_open
            // (...)`, #1107's from_include cast re-spelling below) at every
            // assignment and call site, which newer GCC promotes from a
            // warning to a hard error. Prefer the alias here so every site
            // -- declaration, argument, assignment -- agrees and binds to
            // the same (host-supplied) type throughout.
            if (tag && alias && !ty->members &&
                type_def_is_from_include_suppressed(ctx, ty))
                fprintf(f, "%.*s", alias->name_len, alias->name);
            else if (tag)
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
            // (DK_INT128, parse_types.c).
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
                // #1123: multi-word lowering. ty->size is always a multiple
                // of 8 above 16 bytes (bitint_type(), src/type.c), so
                // ty->size/8 names one of the __cccc_biK containers
                // serialize_wide_bitint_preamble() has already emitted into
                // this TU's preamble (serialize_program.c) -- signedness
                // makes no difference to the container's own spelling, only
                // to which runtime helper a use of this type calls
                // (serialize_expr.c). #1121 previously hard-errored here
                // ("exceeds 128 bits, which has no native/-m lowering"); see
                // man/NATIVE.md for the divergence from clang's/gcc's own
                // native _BitInt(N>128), which is unsupported at all widths
                // this large by clang and layout-incompatible (align 16 vs
                // this project's align 8, #1135) even where gcc accepts it.
                fprintf(f, "__cccc_bi%d", ty->size / 8);
            break;
        case TY_BLOCK:
            // #965: on the default (non `-fblocks`) lowering path a block value
            // is always a pointer to the common-initial-sequence descriptor
            // struct emitted by serialize_block_preamble() -- see the "Blocks"
            // entry in NATIVE.md's serialized-output-divergences section.
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

// Serialize struct/union type definition
static const char *aggregate_keyword(Type *ty) {
    return ty->kind == TY_UNION ? "union" : "struct";
}

// #1172: defined below (near type_layout_is_host_owned, which it calls) --
// forward-declared here so serialize_struct_def/serialize_enum_def can call
// it right after printing each definition's closing `;`.
static void serialize_layout_guards(FILE *f, SerializeContext *ctx, Type *ty,
                                    TypeName *tag, TypeName *alias, int indent);

static void serialize_struct_def(FILE *f, SerializeContext *ctx, Type *ty) {
    if (!ty)
        return;

    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
        return;

    TypeName *tag   = find_tag_name(ctx, ty);
    TypeName *alias = find_typedef_name(ctx, ty);

    if (!ty->members) {
        if (!tag && alias)
            fprintf(f, "typedef %s", aggregate_keyword(ty));
        else
            fprintf(f, "%s", aggregate_keyword(ty));
        if (tag)
            fprintf(f, " %.*s", tag->name_len, tag->name);
        if (tag)
            fprintf(f, ";\n\n");
        return;
    }

    // #1173: re-wrap the definition in the same #pragma pack(push, N)/pop
    // CCCC's own parser honoured (src/parse_types.c) so the host compiler
    // reproduces CCCC's layout exactly, rather than the struct's own
    // members/attrs alone -- push/pop is supported by gcc, clang, and MSVC
    // alike, so this is always the same directive CCCC itself parsed, not a
    // reconstruction. Must come before the "struct"/"typedef struct" keyword
    // line entirely -- a #pragma directive splitting the tag from its own
    // opening brace is a syntax error on both gcc and clang (confirmed:
    // "expected identifier or '(' before '#pragma'"), so it cannot be
    // interleaved with the declaration the way an ordinary attribute can.
    // Members may still carry a per-member _Alignas(N)/aligned(N) reflecting
    // an uncapped source request (serialize_aggregate_members, unchanged) --
    // confirmed directly against gcc-16/clang that #pragma pack(N) caps that
    // too, exactly like every other implicit or explicit contribution, so it
    // re-derives to the same capped layout either way.
    if (ty->pack_align)
        fprintf(f, "#pragma pack(push, %d)\n", ty->pack_align);

    if (!tag && alias)
        fprintf(f, "typedef %s", aggregate_keyword(ty));
    else
        fprintf(f, "%s", aggregate_keyword(ty));

    if (tag)
        fprintf(f, " %.*s", tag->name_len, tag->name);

    fprintf(f, " {\n");
    serialize_aggregate_members(f, ctx, ty);
    fprintf(f, "}");
    serialize_aggregate_attrs(f, ty);

    if (!tag && alias)
        fprintf(f, " %.*s", alias->name_len, alias->name);
    fprintf(f, ";\n");

    if (ty->pack_align)
        fprintf(f, "#pragma pack(pop)\n");

    serialize_layout_guards(f, ctx, ty, tag, alias, 0);
    fprintf(f, "\n");
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
    fprintf(f, ";\n");

    serialize_layout_guards(f, ctx, ty, tag, alias, 0);
    fprintf(f, "\n");
}

bool type_has_tag_for_owner(SerializeContext *ctx, Type *ty, Obj *owner_fn) {
    TYPE_CAND_FOR(i, type_cand_tags, ctx, ty, ctx->tags_len)
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
    TYPE_CAND_FOR(i, type_cand_tags, ctx, ty, ctx->tags_len)
    if (ctx->tags[i].owner_fn == NULL &&
        same_type_or_origin(ctx->tags[i].ty, ty))
        return true;
    TYPE_CAND_FOR(i, type_cand_typedefs, ctx, ty, ctx->typedefs_len)
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
void hoist_local_type_to_file_scope(FILE *f, VirtualMachine *vm,
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
    TYPE_CAND_FOR(i, type_cand_tags, ctx, ty, ctx->tags_len)
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
    TYPE_CAND_FOR(i, type_cand_tags, ctx, ty, ctx->tags_len)
    if (same_type_or_origin(ctx->tags[i].ty, ty)) {
        ctx->tags[i].owner_fn = NULL;
        ctx->tags[i].name     = chosen_name;
        ctx->tags[i].name_len = chosen_len;
    }
    TYPE_CAND_FOR(i, type_cand_typedefs, ctx, ty, ctx->typedefs_len)
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
bool path_basename_is(const char *path, const char *name) {
    if (!path)
        return false;
    size_t plen = strlen(path), nlen = strlen(name);
    if (plen == nlen)
        return strcmp(path, name) == 0;
    return plen > nlen && path[plen - nlen - 1] == '/' &&
           strcmp(path + plen - nlen, name) == 0;
}

bool path_is_captured(SerializeContext *ctx, const char *path) {
    if (!path)
        return false;
    // #1292: canonicalize the query path the same way collect_captured_path()
    // canonicalized ctx->captured_paths' values -- otherwise two textual
    // spellings of one on-disk header (e.g. "./cccc.h" vs "cccc.h") compare
    // unequal here even though the #include really was replayed, and this
    // type's definition is wrongly re-derived from scratch alongside it
    // ("typedef redefinition"/"redefinition of ..."). Memoized: this runs
    // once per bodiless declaration/typedef lookup across the whole
    // program, and realpath() is a syscall.
    const char *key = hashmap_get(&ctx->path_key_memo, path);
    if (!key) {
        key = cc_canonical_path_key(ctx->vm, path);
        hashmap_put(&ctx->path_key_memo, path, (void *)key);
    }
    for (int i = 0; i < ctx->captured_paths_len; i++)
        if (ctx->captured_paths[i] && strcmp(ctx->captured_paths[i], key) == 0)
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
// #1290: does some OTHER typedef record spelling the exact same name say
// this name is already supplied by a replayed #include? ctx->typedefs is a
// whole-program registry (vm->compiler.type_names is one VM-wide list,
// never reset between the per-TU preprocessor state #1001 resets --
// record_type_name() just keeps prepending), so a header's own from_include
// record and an unrelated command-line-input TU's independently-declared,
// identically-shaped local typedef of the same name (e.g. two unconnected
// `typedef struct {...} TypeVec;` -- src/serialize_internal.h's and
// src/json.c's own, #1290's repro) both land in this one list. Matched by
// NAME, deliberately not by structure: a structural match here would
// reintroduce the exact trap find_generated_uncaptured_typedef()'s own
// #1168/#1169 comment documents -- a scalar typedef structurally matches
// any same-representation type, so a plain `int`/`long` alias would falsely
// collide with the first same-width header typedef anywhere in the program.
// The *name* is what actually collides in the emitted C text, so the name
// is the right key. Shared by type_def_is_from_include_suppressed() below
// (the ctx->defs standalone-definition loop) and typedef_alias_header_
// suppressed() (the ctx->typedefs loop, serialize_type.c further down) --
// #1290's repro hits whichever of the two actually reaches this type first,
// so both must apply the same rule or the same collision reappears through
// the other loop.
// #1293: name match alone is not enough to justify SUPPRESSING td's body --
// it only tells us some from_include record spells the identical name, not
// that it spells the identical *type*. Two unconnected tagless aggregate
// typedefs sharing a name but disagreeing on members (e.g. one TU's
// header-supplied `typedef struct { int a, b; } Same;` next to another
// TU's own `typedef struct { double x, y, z; } Same;`) used to both match
// here on name alone, so the non-header TU's body was dropped outright --
// not a redefinition, a silently WRONG program (every member access on the
// non-header TU's own value resolved against the header's unrelated
// shape, "no member named 'x'"). same_type_strong() (#1014's own tag-
// collision grouping key) gates the two outcomes #1290 and #1293 both
// need: identical shape -> still suppress (the replayed #include really
// does supply this exact body, #1290's original case); different shape ->
// return false here, so the caller falls through and emits td's own body,
// which rename_colliding_typedef_names() (serialize_program.c) then gives
// a distinct name so it doesn't collide with the header's copy under the
// plain spelling.
static bool typedef_name_is_header_supplied(SerializeContext *ctx,
                                            TypeName         *td) {
    for (int i = 0; i < ctx->typedefs_len; i++) {
        TypeName *other = &ctx->typedefs[i];
        if (other == td || !other->from_include || other->always_emit)
            continue;
        if (other->name_len != td->name_len ||
            strncmp(other->name, td->name, (size_t)td->name_len) != 0)
            continue;
        // #1293: `other->ty` can be transiently NULL here -- emit_typedef_
        // and_deps()'s own self-hide trick (blank td->ty before chasing
        // this record's dependencies, restore after) blanks whichever
        // record is currently mid-emission, and with TWO OR MORE
        // structurally-identical from_include records for one name (one
        // per TU that #includes the shared header -- exactly this
        // function's own #1290 scenario), find_anonymous_typedef_name()'s
        // structural match can chain through several of them before ever
        // reaching `td`, hiding more than one at once. A hidden record's
        // shape is unknowable here, but it was only ever reached BY a
        // structural match in the first place, so treat it the same way
        // #1290's original (pre-#1293, name-only) check did: matching by
        // name is enough to suppress. Only a record whose shape is
        // actually VISIBLE gets the stricter same_type_strong() gate
        // #1293 added.
        if (!other->ty || same_type_strong(other->ty, td->ty))
            return true;
    }
    return false;
}

static bool type_def_is_from_include_suppressed(SerializeContext *ctx,
                                                Type             *ty) {
    if (!ty || ctx->emit_strict)
        return false;
    TypeName *tag   = find_tag_name(ctx, ty);
    TypeName *alias = find_typedef_name(ctx, ty);
    TypeName *provenance_source =
        tag ? find_tag_name_for_provenance(ctx, ty) : alias;
    if (!provenance_source || provenance_source->always_emit)
        return false;
    // #1290: a tagless aggregate's resolved provenance record may itself be
    // a non-header TU's own independently-declared typedef of the same name
    // (find_typedef_name()'s exact-identity match resolves to whichever
    // record's own Type this exact pointer is, which may not be the header's
    // copy at all) -- fall back to the name-matched check above in that
    // case. Restricted to a TAGLESS AGGREGATE (tag == NULL but ty->kind IS
    // struct/union/enum) -- deliberately excludes two other shapes this
    // function is also called with: tag-based provenance (a tag collision
    // is a different hazard, two distinct types sharing a spelling, that
    // rename_colliding_type_tags() already renames apart rather than
    // suppresses), and a NAMED SCALAR from_include typedef (size_t et al,
    // reached via find_typedef_name_exact()'s identity match at the callers
    // above) -- a user program's own same-named scalar typedef colliding
    // with a comptime-synthesized one (e.g. #1057's own `typedef unsigned
    // long size_t;` test) is a real, deliberately-supported redeclaration
    // has_colliding_user_typedef() already defers to; folding the name
    // check in here would suppress the user's own declaration and strand
    // every reference to it on an #include that serialize_synth_typedef_
    // includes() correctly decided never to print.
    bool is_tagless_aggregate =
        !tag &&
        (ty->kind == TY_STRUCT || ty->kind == TY_UNION || ty->kind == TY_ENUM);
    bool from_include =
        provenance_source->from_include ||
        (is_tagless_aggregate && !ctx->generated_only &&
         typedef_name_is_header_supplied(ctx, provenance_source));
    return from_include &&
           (!ctx->generated_only ||
            path_is_captured(ctx, provenance_source->file_path));
}

// #1241: `-c=generated` skips header typedefs unconditionally under
// generated_only ("output is consumed alongside normal headers",
// SerializeContext.generated_only's own comment) -- fine when the header
// really is replayed into the output (an @shared include reaches
// cc_serialize_program's leading directive replay, see the #1241 comment
// there), but silent when it isn't: a typedef reached only via a
// never-captured route (`#include @comptime`) has nothing anywhere in the
// generated file that declares it, yet nothing previously noticed. Tags are
// deliberately excluded here -- a struct/union/enum from a non-captured
// header is re-derived (its full definition serialized, with layout
// guards), so it's never left dangling the way a typedef *alias* is;
// type_def_is_from_include_suppressed()'s own tag-priority ordering just
// above is the reason a tagged type doesn't hit this at all. Only checked
// under generated_only: the !generated_only path (-c=native, plain -m) has
// no equivalent gap -- #993 already guarantees any header a type is
// actually reachable through is replayed ahead of everything that could
// use it.
//
// #1168/#1169: deliberately uses find_typedef_name_exact()'s pointer-
// identity walk, NOT find_typedef_name()'s structural fallback -- the same
// trap type_layout_is_host_owned()/type_contains_compiler_owned_layout()
// already document and guard against. A bare scalar global's element type
// (e.g. plain `int`) structurally matches ANY from_include typedef sharing
// its representation, which would misreport an ordinary `int`/`long` array
// as "uses an uncaptured header typedef" the moment some unrelated header
// typedefs the same builtin. The identity walk only ever resolves a Type
// that really is a copy_type() of the named typedef (parse_typedef()'s own
// discipline for a non-aggregate alias), so a plain builtin never matches.
TypeName *find_generated_uncaptured_typedef(SerializeContext *ctx, Type *ty) {
    if (!ty || !ctx->generated_only || ctx->emit_strict)
        return NULL;
    // Peel down to the named type a published global's own declaration
    // would reference -- an array-of-typedef or pointer-to-typedef global
    // hits this exactly like a plain one.
    while (ty &&
           (ty->kind == TY_ARRAY || ty->kind == TY_PTR || ty->kind == TY_VLA))
        ty = ty->base;
    if (!ty || find_tag_name(ctx, ty))
        return NULL;
    TypeName *alias = find_typedef_name_exact(ctx, ty);
    if (!alias || !alias->from_include || alias->always_emit)
        return NULL;
    if (path_is_captured(ctx, alias->file_path))
        return NULL;
    return alias;
}

// #1031: true when `ty`'s from_include-suppressed definition is owned by
// one of is_compiler_owned_header()'s fixed list (stdarg.h/setjmp.h/etc,
// src/preprocess.c) -- excluded from type_layout_is_host_owned() below
// even though its body IS suppressed the same way an ordinary from_include
// type's is (confirmed: `struct va_list`'s body does not appear in -m
// output either). stdarg.h's va_list and setjmp.h's jmp_buf specifically
// use the *opposite* strategy from an ordinary from_include type: CCCC's
// own layout is deliberately widened to cover every supported host's real
// one (see their own man/NATIVE.md entries), so the *guest-folded*
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
// union/enum whose body serialize_type_defs_for_owner() suppresses (member
// access re-resolves correctly against the replayed #include's real host
// layout, but a value guest-side parsing already folded into a plain
// integer literal does not), or `ty` transitively contains such a type
// (an array of it, or a struct/union with it as a direct or nested
// member) -- recursion stops at TY_PTR: a pointer's own size is uniform
// across every supported platform x arch combination regardless of what
// it points to, and following pointee types would risk a cycle through a
// self-referential struct. depth guards against a pathological type
// graph; the real-world nesting this addresses is at most a few levels.
//
// #1168: a bare scalar member/operand, e.g. plain `long`, can spuriously
// match an unrelated from_include *typedef* of the same builtin (e.g.
// sys/types.h's __int32_t, reached merely by including <stdio.h>) via
// type_def_is_from_include_suppressed()'s find_typedef_name() fallback,
// which is ultimately same_type_or_origin()'s pointer-identity walk up the
// `origin` chain (serialize_type.c's own same_type_or_origin(), fed by
// copy_type()'s `ret->origin = ty`, src/type.c) -- not a structural match,
// same_type_or_origin() has no structural arm for scalars at all. This made
// an ordinary, entirely user-defined aggregate with a scalar member (or a
// bare `sizeof(int)`) get judged host-owned, re-materializing
// `sizeof(struct Loc)` textually under -c=native/-m instead of folding to a
// plain literal -- cosmetic once #1167 landed (the definition is emitted
// either way), but needlessly verbose/fragile-looking output, and the one
// *unguarded* consumer of this function (serialize_expr.c's ND_COMMA #1103
// zero-init-chain arm) could hard-error on a store it can't classify as
// redundant once an ordinary struct was wrongly judged host-owned. #1168's
// own fix restricted this whole function to TY_STRUCT/TY_UNION/TY_ENUM,
// which sidestepped the spurious match but also stopped a *genuinely*
// from_include scalar typedef -- e.g. sigset_t, `unsigned int`/4 bytes in
// include/signal.h but 128 bytes in glibc -- from ever re-materializing,
// reintroducing the #1031 hazard for it (filed as #1169).
//
// #1169: the structural fallback was never actually necessary for a
// scalar. parse_typedef() (parse_decl.c) copy_type()s every non-aggregate
// typedef specifically so it gets a Type object of its own -- `typedef
// unsigned int sigset_t;` does NOT share the plain `unsigned int`
// singleton's Type, it holds a copy whose ->origin points back to it.
// find_typedef_name_exact() (this file) walks ->origin upward FROM `ty`,
// so it resolves sigset_t's own copy to sigset_t's TypeName record, while
// the bare `unsigned int` Type (origin == NULL) resolves to nothing --
// the walk is directional and cannot go the other way. serialize_type()'s
// own scalar-alias arm already relies on exactly this identity to spell
// e.g. `uint64_t` rather than decomposing it. So a scalar arm keyed on
// find_typedef_name_exact() (below) distinguishes "this operand really is
// the sigset_t typedef" from "this operand merely has the same underlying
// representation" without reinstating same_type_or_origin()'s structural
// fallback at all -- TY_ENUM was never affected by any of this (kept
// throughout): same_type_or_origin()'s TY_ENUM arm matches by tag +
// enumerator list, not by a pointer-identity walk through some unrelated
// scalar's origin chain.
bool type_layout_is_host_owned(SerializeContext *ctx, Type *ty, int depth) {
    if (!ty || depth > 32)
        return false;
    if (ty->kind == TY_ARRAY || ty->kind == TY_VLA)
        return type_layout_is_host_owned(ctx, ty->base, depth + 1);
    if (ty->kind == TY_PTR)
        return false;
    bool is_aggregate =
        ty->kind == TY_STRUCT || ty->kind == TY_UNION || ty->kind == TY_ENUM;
    // #1169: a non-aggregate only ever gets here as a *named* from_include
    // typedef -- find_typedef_name_exact()'s identity walk (see the block
    // comment above) can't match a bare builtin, only the typedef that
    // really was declared from_include, so this can't reopen #1168's
    // spurious-scalar-member bug.
    if (!is_aggregate && !find_typedef_name_exact(ctx, ty))
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

// #1172: true when `ty` is, or transitively contains, a from_include type
// whose body is suppressed *because* it's compiler-owned (stdarg.h's
// va_list, setjmp.h's jmp_buf -- type_header_is_compiler_owned() above).
// This is deliberately the complement of type_layout_is_host_owned(), which
// returns false for exactly these types: per that function's own doc
// comment (block above type_header_is_compiler_owned), CCCC's va_list/
// jmp_buf layout is *deliberately widened* to a safe upper bound covering
// every supported host (#1054/#1059), so the folded sizeof/_Alignof CCCC
// emits for one is correct-by-construction and intentionally NOT equal to
// the real host's own (possibly smaller) size. A layout guard is a
// cross-check against the real host size, so emitting one for a struct
// containing a va_list/jmp_buf member would assert CCCC's padded literal
// against the host's real, smaller size and fire on every host, gcc and
// clang alike -- a guard on correct code. serialize_layout_guards() below
// must OR this in alongside type_layout_is_host_owned() as a second,
// independent exclusion; reusing only type_layout_is_host_owned() (as the
// ticket's own text suggests in isolation) is not sufficient. Recursion
// mirrors type_layout_is_host_owned() exactly (TY_ARRAY/TY_VLA descend,
// TY_PTR stops, same depth cap) so the two predicates can't disagree by
// parallel edit on a shape neither was written to handle.
static bool type_contains_compiler_owned_layout(SerializeContext *ctx, Type *ty,
                                                int depth) {
    if (!ty || depth > 32)
        return false;
    // #1172: checked BEFORE descending into an array's element type (unlike
    // type_layout_is_host_owned()'s own TY_ARRAY arm, which recurses first)
    // -- jmp_buf is itself `typedef long long jmp_buf[40]` (include/
    // setjmp.h), an array typedef, not a struct. Recursing to the element
    // type first would check plain `long long` instead, which
    // find_typedef_name() can never resolve back to jmp_buf, silently
    // reopening the exact hazard this function exists to close. Checking
    // the array type itself first catches jmp_buf; checking a struct/union
    // (e.g. va_list) at this same point is equally correct since the guard
    // this function feeds is per-aggregate-type, not per-array-element.
    //
    // #1168/#1169: a bare scalar member (e.g. plain `int`) must NOT be
    // handed to type_def_is_from_include_suppressed() unless
    // find_typedef_name_exact()'s pointer-identity walk actually resolves
    // it to a named from_include typedef -- that function's own
    // find_typedef_name() fallback matches STRUCTURALLY (same_type_or_
    // origin) for a scalar, which spuriously matches ANY from_include
    // typedef sharing the same underlying representation (e.g. a plain
    // `int` member matching stdint.h's `int32_t`, merely because some
    // other #include reached stdint.h) -- exactly #1168's own bug, in the
    // sibling predicate type_layout_is_host_owned(), whose fix
    // (restricting the non-aggregate arm to find_typedef_name_exact()) is
    // mirrored here verbatim. Without this guard, an entirely ordinary
    // struct with a plain `int`/`char`/`long` member could be wrongly
    // judged "contains a compiler-owned type" the moment the TU includes
    // ANY header on is_compiler_owned_header()'s list transitively (e.g.
    // <time.h> pulling in stdint.h-shaped typedefs) -- confirmed via a
    // direct repro: struct B_plain { int a; char b; long c; } lost its
    // guard entirely as soon as the TU also #include <time.h>, with no
    // jmp_buf/va_list anywhere in sight.
    bool ty_is_aggregate =
        ty->kind == TY_STRUCT || ty->kind == TY_UNION || ty->kind == TY_ENUM;
    if ((ty_is_aggregate || find_typedef_name_exact(ctx, ty)) &&
        type_def_is_from_include_suppressed(ctx, ty) &&
        type_header_is_compiler_owned(ctx, ty))
        return true;
    if (ty->kind == TY_ARRAY || ty->kind == TY_VLA)
        return type_contains_compiler_owned_layout(ctx, ty->base, depth + 1);
    if (ty->kind == TY_PTR)
        return false;
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
        for (Member *m = ty->members; m; m = m->next)
            if (type_contains_compiler_owned_layout(ctx, m->ty, depth + 1))
                return true;
    return false;
}

// #1172: emits `_Static_assert(sizeof(<spelling>) == N, ...)`,
// `_Static_assert(_Alignof(<spelling>) == N, ...)`, and (for a struct/union,
// one per named non-bitfield member) `_Static_assert(__builtin_offsetof(
// <spelling>, <member>) == N, ...)` immediately after `ty`'s own definition
// -- the systematic detector #1170 asked for: any layout disagreement
// between CCCC's own const-folding and the host compiler that will actually
// consume this output becomes a host compile error naming the type, instead
// of a silent out-of-bounds write (#1170's own evidence table). `tag`/
// `alias` are the exact TypeName pointers the caller already resolved
// (serialize_struct_def/serialize_enum_def) -- reused rather than
// re-derived so the guard's spelling can never diverge from what was
// actually just printed for the definition itself (see
// type_needs_anon_aggregate()'s own doc comment for why re-deriving
// nameability independently is the wrong shape here: it does not tell you
// *which* name was chosen when both a tag and an alias exist).
//
// Exclusions, in order:
//   - !ctx->emit_layout_guards: --no-layout-guards.
//   - ctx->emit_cccc: this output is CCCC dialect fed back into CCCC's own
//     front end, not a real host compiler -- the guard would be tautological.
//   - ctx->emit_strict (--emit-only): type_def_is_from_include_suppressed()
//     returns false unconditionally under emit_strict (see its own comment),
//     which collapses both host-owned exclusions below to false too. A
//     from_include struct's body IS emitted under emit_strict (no auto-
//     captured #include to defer to), but CCCC's bundled headers already
//     carry their own per-platform _Static_asserts for exactly these types
//     (include/sys/stat.h, signal.h, fts.h, aio.h, mqueue.h, ndbm.h --
//     same trap serialize_static_assert()'s own doc comment warns about);
//     re-deriving a second, unconditional guard here would duplicate or
//     conflict with those. Documented residual, man/NATIVE.md.
//   - No name to write the assert with at all (neither tag nor alias).
//   - type_layout_is_host_owned(): `ty` defers to the host's own real
//     layout already (an ordinary from_include struct/union/enum) -- no
//     literal of CCCC's own to check.
//   - type_contains_compiler_owned_layout(): `ty` contains a va_list/
//     jmp_buf-shaped member -- see that function's own comment.
static void serialize_layout_guards(FILE *f, SerializeContext *ctx, Type *ty,
                                    TypeName *tag, TypeName *alias,
                                    int indent) {
    if (!ctx->emit_layout_guards || ctx->emit_cccc || ctx->emit_strict)
        return;
    if (!tag && !alias)
        return;
    if (type_layout_is_host_owned(ctx, ty, 0) ||
        type_contains_compiler_owned_layout(ctx, ty, 0))
        return;

    char spelling[320];
    if (tag)
        // #1172: aggregate_keyword() only distinguishes struct/union -- it
        // returns "struct" for a TY_ENUM (there is no third branch), which
        // silently spelled every enum guard as "struct <tag>" instead of
        // "enum <tag>". Harmless when the name is unique, but a real bug
        // once two same-named-but-distinct enum tags exist in one TU (the
        // #1015 dup-enum-tag-rename corpus case): both got spelled as the
        // same bogus "struct <tag>", which the host cc then rejected as a
        // tag-kind mismatch against the real (correctly spelled) enum
        // definition, an outright compile failure -- not merely a
        // cosmetic wrong keyword.
        snprintf(spelling, sizeof(spelling), "%s %.*s",
                 ty->kind == TY_ENUM ? "enum" : aggregate_keyword(ty),
                 tag->name_len, tag->name);
    else
        snprintf(spelling, sizeof(spelling), "%.*s", alias->name_len,
                 alias->name);

    char msg[384];
    print_indent_level(f, indent);
    snprintf(msg, sizeof(msg), "cccc/host layout disagreement: %s", spelling);
    fprintf(f, "_Static_assert(sizeof(%s) == %lld, ", spelling,
            (long long)ty->size);
    serialize_string_n(f, msg, (int)strlen(msg));
    fprintf(f, ");\n");

    print_indent_level(f, indent);
    fprintf(f, "_Static_assert(_Alignof(%s) == %lld, ", spelling,
            (long long)ty->align);
    serialize_string_n(f, msg, (int)strlen(msg));
    fprintf(f, ");\n");

    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
        return;
    for (Member *m = ty->members; m; m = m->next) {
        if (m->is_bitfield || !m->name)
            continue; // no __builtin_offsetof for a bit-field or an
                      // anonymous struct/union member
        int  len = m->name->len;
        char mname[256];
        if (len >= (int)sizeof(mname))
            len = sizeof(mname) - 1;
        memcpy(mname, m->name->loc, len);
        mname[len] = '\0';

        char mmsg[420];
        snprintf(mmsg, sizeof(mmsg), "cccc/host layout disagreement: %s.%s",
                 spelling, mname);
        print_indent_level(f, indent);
        fprintf(f, "_Static_assert(__builtin_offsetof(%s, %s) == %lld, ",
                spelling, mname, (long long)m->offset);
        serialize_string_n(f, mmsg, (int)strlen(mmsg));
        fprintf(f, ");\n");
    }
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
bool serialize_layout_const(FILE *f, SerializeContext *ctx, Type *layout_ty,
                            bool is_align) {
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
// re-check. This function's own TY_STRUCT/TY_UNION check below (base_ty's
// kind, just above the type_layout_is_host_owned() call) is narrower than
// that shared helper -- it excludes both TY_ENUM and any from_include
// *scalar* typedef type_layout_is_host_owned() recognizes as of #1169 (see
// its own comment) -- kept for #1098's original reason, matching every
// real-world case in that batch's own tickets (e.g. struct statfs); an
// enum-typed or scalar-typedef-typed _Static_assert condition gets no
// redundant host-side re-check either, since #1098's own tickets never
// needed one. Plain recursion over lhs/rhs/cond/then/els covers every
// shape a constant-expression tree can take (unary: lhs only; binary: lhs+rhs;
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
// provenance gate (serialize_program.c's function-prototype loop) so this can
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
void serialize_static_assert(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                             Node *cond, const char *msg, int msg_len,
                             Token *tok, int indent) {
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
bool typedef_alias_header_suppressed(SerializeContext *ctx, TypeName *alias) {
    if (ctx->generated_only || ctx->emit_strict || alias->always_emit)
        return false;
    if (alias->from_include)
        return true;
    // #1290: restricted to a tagless AGGREGATE alias -- see
    // type_def_is_from_include_suppressed()'s matching comment for why a
    // scalar typedef (size_t et al) must not take this fallback: a user
    // program's own same-named scalar typedef colliding with a comptime-
    // synthesized one is a real, deliberately-supported redeclaration
    // (#1057's own collision test), not a #1290-shaped hazard.
    Type *ty = alias->ty;
    if (ty &&
        (ty->kind == TY_STRUCT || ty->kind == TY_UNION || ty->kind == TY_ENUM))
        return typedef_name_is_header_supplied(ctx, alias);
    return false;
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

void serialize_type_defs_for_owner(FILE *f, SerializeContext *ctx,
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
