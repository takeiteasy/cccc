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

// Function overloading by parameter types (clang's `overloadable`): the
// Itanium mangler that names each member, the per-name overload sets, and the
// call-site resolver.

#include "./parse_internal.h"

// ---------------------------------------------------------------------------
// Itanium mangling
// ---------------------------------------------------------------------------

typedef struct {
    char *p;
    int   len, cap;
} Buf;

static void buf_putn(Buf *b, const char *s, int n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->p   = realloc(b->p, (size_t)b->cap);
    }
    memcpy(b->p + b->len, s, (size_t)n);
    b->len       += n;
    b->p[b->len]  = '\0';
}

static void buf_puts(Buf *b, const char *s) {
    buf_putn(b, s, (int)strlen(s));
}

typedef struct {
    VirtualMachine *vm;
    Token          *site;
    const char     *fn_name;
    bool            soft; // report failure by flag instead of an error
    bool            failed;
    char          **subs;
    int             nsubs, cap_subs;
} Mangler;

enum { ENC_STRIP_TOP = 1, ENC_NOSUBS = 2 };

static void mangle_fail(Mangler *m, const char *why) {
    if (m->soft) {
        m->failed = true;
        return;
    }
    error_tok(m->vm, m->site,
              "cannot mangle a parameter type of overloadable '%s': %s",
              m->fn_name, why);
}

static const char *builtin_code(Mangler *m, Type *t, char *scratch) {
    switch (t->kind) {
        case TY_VOID:
            return "v";
        case TY_BOOL:
            return "b";
        case TY_CHAR:
            return t->is_unsigned ? "h" : "c";
        case TY_SHORT:
            return t->is_unsigned ? "t" : "s";
        case TY_INT:
            return t->is_unsigned ? "j" : "i";
        case TY_LONG:
            if (t->is_long_long)
                return t->is_unsigned ? "y" : "x";
            return t->is_unsigned ? "m" : "l";
        case TY_FLOAT:
            return "f";
        case TY_DOUBLE:
            return "d";
        case TY_LDOUBLE:
            return "e";
        case TY_FLOAT16:
            return "DF16_";
        case TY_NULLPTR_T:
            return "Dn";
        case TY_DECIMAL32:
            return "Df";
        case TY_DECIMAL64:
            return "Dd";
        case TY_DECIMAL128:
            return "De";
        case TY_BITINT:
            // __int128 is stored as _BitInt(128), so the two spellings cannot
            // be told apart.
            if (t->bit_width == 128) {
                mangle_fail(m, "_BitInt(128) and __int128 mangle differently");
                return "";
            }
            snprintf(scratch, 24, "D%c%d_", t->is_unsigned ? 'U' : 'B',
                     t->bit_width);
            return scratch;
        default:
            return NULL;
    }
}

static bool is_builtin_kind(Type *t) {
    switch (t->kind) {
        case TY_PTR:
        case TY_FUNC:
        case TY_ARRAY:
        case TY_COMPLEX:
        case TY_STRUCT:
        case TY_UNION:
        case TY_ENUM:
            return false;
        default:
            return true;
    }
}

static void enc(Mangler *m, Buf *out, Type *t, int flags);

static void put_sub(Buf *out, int idx) {
    if (idx == 0) {
        buf_puts(out, "S_");
        return;
    }
    char tmp[16];
    int  n = idx - 1, i = 0;
    do {
        int d     = n % 36;
        tmp[i++]  = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        n        /= 36;
    } while (n);
    buf_puts(out, "S");
    while (i)
        buf_putn(out, &tmp[--i], 1);
    buf_puts(out, "_");
}

static int find_sub(Mangler *m, const char *key) {
    for (int i = 0; i < m->nsubs; i++)
        if (!strcmp(m->subs[i], key))
            return i;
    return -1;
}

static void add_sub(Mangler *m, const char *key) {
    if (m->nsubs == m->cap_subs) {
        m->cap_subs = m->cap_subs ? m->cap_subs * 2 : 16;
        m->subs     = realloc(m->subs, sizeof(char *) * (size_t)m->cap_subs);
    }
    m->subs[m->nsubs++] = strdup(key);
}

static void enc_params(Mangler *m, Buf *out, Type *fn, int flags) {
    if (!fn->params && (!fn->is_variadic || fn->is_unprototyped)) {
        buf_puts(out, "v");
        return;
    }
    for (Type *p = fn->params; p; p = p->next)
        enc(m, out, p, ENC_STRIP_TOP | (flags & ENC_NOSUBS));
    if (fn->is_variadic && !fn->is_unprototyped)
        buf_puts(out, "z");
}

static Token *aggregate_tag(Type *t) {
    for (Type *x = t; x; x = x->origin) {
        const TypeAgg *a   = type_agg(x);
        Token         *tag = t->kind == TY_ENUM ? a->enum_tag : a->struct_tag;
        if (tag)
            return tag;
    }
    for (Type *x = t; x; x = x->origin)
        if (type_agg(x)->typedef_name)
            return type_agg(x)->typedef_name;
    return NULL;
}

// The type's own shape, without its qualifiers. `flags` carries ENC_NOSUBS.
static void enc_body(Mangler *m, Buf *out, Type *t, int flags) {
    char scratch[24];
    int  sub = flags & ENC_NOSUBS;
    switch (t->kind) {
        case TY_PTR:
            buf_puts(out, "P");
            enc(m, out, t->base, sub);
            return;
        case TY_FUNC:
            buf_puts(out, "F");
            enc(m, out, t->return_ty, sub);
            enc_params(m, out, t, sub);
            buf_puts(out, "E");
            return;
        case TY_ARRAY: {
            char n[32];
            snprintf(n, sizeof n, "A%d_", t->array_len);
            buf_puts(out, n);
            enc(m, out, t->base, sub);
            return;
        }
        case TY_COMPLEX:
            buf_puts(out, "C");
            enc(m, out, t->base, sub);
            return;
        case TY_STRUCT:
        case TY_UNION:
        case TY_ENUM: {
            Token *tag = aggregate_tag(t);
            if (!tag) {
                mangle_fail(m, "an anonymous struct, union or enum has no "
                               "name; give it a tag or a typedef name");
                return;
            }
            char n[16];
            snprintf(n, sizeof n, "%d", tag->len);
            buf_puts(out, n);
            buf_putn(out, tag->loc, tag->len);
            return;
        }
        default: {
            const char *code = builtin_code(m, t, scratch);
            if (!code) {
                mangle_fail(m, "unsupported type");
                return;
            }
            buf_puts(out, code);
        }
    }
}

// Appends `t`: a substitution reference when an equal component was already
// written, else the component, which then becomes substitutable. Unqualified
// builtin types are never substituted.
static void enc(Mangler *m, Buf *out, Type *t, int flags) {
    if (m->failed)
        return;
    if (t->kind == TY_VLA || t->kind == TY_BLOCK || t->kind == TY_VECTOR ||
        t->kind == TY_AUTO || t->kind == TY_ERROR) {
        mangle_fail(m, "unsupported type");
        return;
    }
    if (t->is_atomic) {
        mangle_fail(m, "_Atomic is not supported");
        return;
    }

    char quals[4] = "";
    if (!(flags & ENC_STRIP_TOP)) {
        int n = 0;
        if (t->is_restrict)
            quals[n++] = 'r';
        if (t->is_volatile)
            quals[n++] = 'V';
        if (t->is_const)
            quals[n++] = 'K';
        quals[n] = '\0';
    }

    if (flags & ENC_NOSUBS) {
        buf_puts(out, quals);
        enc_body(m, out, t, ENC_NOSUBS);
        return;
    }
    bool builtin = is_builtin_kind(t);
    if (builtin && !quals[0]) {
        enc_body(m, out, t, 0);
        return;
    }

    Buf key = {0};
    buf_puts(&key, quals);
    enc_body(m, &key, t, ENC_NOSUBS);
    int hit = m->failed ? -1 : find_sub(m, key.p);
    if (hit >= 0) {
        put_sub(out, hit);
        free(key.p);
        return;
    }

    buf_puts(out, quals);
    if (quals[0] && !builtin)
        enc(m, out, t, ENC_STRIP_TOP);
    else
        enc_body(m, out, t, 0);
    if (!m->failed)
        add_sub(m, key.p);
    free(key.p);
}

static char *mangle_function(VirtualMachine *vm, Token *site, const char *name,
                             int name_len, Type *fn, bool soft) {
    Mangler m   = {.vm = vm, .site = site, .fn_name = name, .soft = soft};
    Buf     out = {0};
    char    len[16];
    buf_puts(&out, "_Z");
    snprintf(len, sizeof len, "%d", name_len);
    buf_puts(&out, len);
    buf_putn(&out, name, name_len);
    enc_params(&m, &out, fn, 0);
    char *r = m.failed ? NULL : arena_strndup(vm, out.p, out.len);
    free(out.p);
    for (int i = 0; i < m.nsubs; i++)
        free(m.subs[i]);
    free(m.subs);
    return r;
}

// The Itanium name of overloadable function `name` of type `fn`.
char *cc_mangle_overload(VirtualMachine *vm, Token *site, const char *name,
                         int name_len, Type *fn) {
    return mangle_function(vm, site, name, name_len, fn, false);
}

// ---------------------------------------------------------------------------
// Overload sets
// ---------------------------------------------------------------------------

typedef struct {
    Obj  *fn;
    char *sig; // Itanium name, or NULL when the type is not mangleable
    bool  marked;
} OverloadMember;

struct OverloadSet {
    char           *name;
    OverloadMember *m;
    int             n, cap;
};

static OverloadSet *find_set(VirtualMachine *vm, const char *name, int len) {
    return hashmap_get2(overload_sets(vm), name, len);
}

OverloadSet *cc_overload_find(VirtualMachine *vm, Token *name_tok) {
    if (!*saw_overloads(vm))
        return NULL;
    return find_set(vm, name_tok->loc, name_tok->len);
}

bool cc_overload_has(OverloadSet *set, Obj *fn) {
    for (int i = 0; i < set->n; i++)
        if (set->m[i].fn == fn)
            return true;
    return false;
}

int cc_overload_count(OverloadSet *set) {
    return set->n;
}

Obj *cc_overload_first(OverloadSet *set) {
    return set->m[0].fn;
}

static void add_member(VirtualMachine *vm, OverloadSet *set, Obj *fn, char *sig,
                       bool marked) {
    if (set->n == set->cap) {
        int             cap = set->cap ? set->cap * 2 : 4;
        OverloadMember *m   = arena_alloc(&vm->compiler.parser_arena,
                                          sizeof(OverloadMember) * (size_t)cap);
        if (set->n)
            memcpy(m, set->m, sizeof(OverloadMember) * (size_t)set->n);
        set->m   = m;
        set->cap = cap;
    }
    set->m[set->n++] = (OverloadMember){fn, sig, marked};
}

// The plain-named function `name` that is in scope, if any.
static Obj *find_plain_function(VirtualMachine *vm, const char *name, int len) {
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        VarScope *vs = scope_find_var(sc, (char *)name, len);
        if (vs)
            return vs->var && vs->var->is_function ? vs->var : NULL;
    }
    return NULL;
}

static void mismatch(VirtualMachine *vm, Token *tok, const char *name,
                     bool marked, bool redecl);

// A second unmarked function of a name that has an overload set.
static void reject_second_unmarked(VirtualMachine *vm, Type *ty, Token *tok,
                                   char *name) {
    OverloadSet *set = find_set(vm, tok->loc, tok->len);
    if (!set)
        return;
    char *sig = mangle_function(vm, tok, name, tok->len, ty, true);
    for (int i = 0; i < set->n; i++)
        if (!set->m[i].marked && set->m[i].sig && sig &&
            strcmp(set->m[i].sig, sig))
            mismatch(vm, tok, name, false, false);
}

// The name a declaration of `ty` goes by: the Itanium name for an overloadable
// function, otherwise its own.
OverloadName cc_overload_decl_name(VirtualMachine *vm, Type *ty, Token *tok) {
    OverloadName r = {get_ident(vm, tok), tok->len, tok};
    if (ty && ty->kind == TY_FUNC && !ty->is_overloadable)
        reject_second_unmarked(vm, ty, tok, r.name);
    if (!ty || ty->kind != TY_FUNC || !ty->is_overloadable)
        return r;
    if (vm->compiler.current_fn)
        error_tok(vm, tok,
                  "overloadable function '%s' must be declared at file scope",
                  r.name);
    if (ty->is_unprototyped)
        error_tok(vm, tok, "overloadable function '%s' must have a prototype",
                  r.name);
    r.name = cc_mangle_overload(vm, tok, r.name, tok->len, ty);
    r.len  = (int)strlen(r.name);
    r.tok  = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    token_assign(vm, r.tok, tok);
    r.tok->loc = r.name;
    r.tok->len = r.len;
    return r;
}

static void mismatch(VirtualMachine *vm, Token *tok, const char *name,
                     bool marked, bool redecl) {
    if (marked)
        error_tok(vm, tok,
                  "declaration of '%s' must not have the 'overloadable' "
                  "attribute",
                  name);
    error_tok(vm, tok,
              redecl ? "redeclaration of '%s' must also have the "
                       "'overloadable' attribute"
                     : "declaration of '%s' must have the 'overloadable' "
                       "attribute",
              name);
}

// Features an overload set does not combine with yet.
static void reject_unsupported_combination(VirtualMachine *vm, Token *src,
                                           const char *name) {
    if (cc_find_template(vm, src))
        error_tok(vm, src,
                  "overloadable function '%s' shares its name with a "
                  "template",
                  name);
}

// Records declaration `fn` of type `ty`, and checks the marking rules: every
// function of a name carries the attribute but for at most one, and a
// redeclaration agrees with what it redeclares.
void cc_overload_declared(VirtualMachine *vm, Obj *fn, Type *ty, Token *src) {
    bool         marked = ty->kind == TY_FUNC && ty->is_overloadable;
    const char  *name   = src->loc;
    int          len    = src->len;
    OverloadSet *set    = find_set(vm, name, len);
    if (!marked && !set)
        return;

    char *src_name = get_ident(vm, src);
    if (marked)
        reject_unsupported_combination(vm, src, src_name);
    if (!set) {
        set = arena_alloc(&vm->compiler.parser_arena, sizeof *set);
        memset(set, 0, sizeof *set);
        set->name = src_name;
        hashmap_put2(overload_sets(vm), name, len, set);
        *saw_overloads(vm)         = true;
        Obj *plain                 = find_plain_function(vm, name, len);
        if (plain && plain != fn)
            add_member(vm, set, plain,
                       mangle_function(vm, src, src_name, len, plain->ty, true),
                       false);
    }
    if (cc_overload_has(set, fn)) {
        if (marked) {
            fn->display_name = src_name;
            fn->tok          = src;
        }
        return;
    }

    char *sig = mangle_function(vm, src, src_name, len, fn->ty, !marked);
    for (int i = 0; i < set->n; i++) {
        OverloadMember *o = &set->m[i];
        if (o->sig && sig && !strcmp(o->sig, sig)) {
            // A redefinition that replaces its predecessor (hot reload).
            if (o->marked == marked && o->fn != fn && o->fn->is_definition &&
                vm->compiler.allow_fn_redefinition) {
                o->fn = fn;
                goto named;
            }
            mismatch(vm, src, src_name, marked, true);
        }
        if (!marked && !o->marked)
            mismatch(vm, src, src_name, false, false);
    }
    add_member(vm, set, fn, sig, marked);
named:
    if (marked) {
        fn->display_name = src_name;
        fn->tok          = src;
    }
}

// ---------------------------------------------------------------------------
// Call resolution
// ---------------------------------------------------------------------------

typedef enum {
    R_EXACT,
    R_QUAL,             // adds qualifiers to the pointee
    R_PROMOTION,
    R_CONVERSION,
    R_PTR_INCOMPATIBLE, // clang's last resort
    R_ELLIPSIS,
    R_NONE,
} Rank;

static bool is_arith(Type *t) {
    return is_numeric(t) || is_float16(t);
}

static Type *decayed(VirtualMachine *vm, Type *t) {
    if (t->kind == TY_ARRAY || t->kind == TY_VLA)
        return pointer_to(vm, t->base);
    if (t->kind == TY_FUNC)
        return pointer_to(vm, t);
    return t;
}

static bool same_arith(Type *a, Type *b) {
    if (a->kind != b->kind)
        return false;
    switch (a->kind) {
        case TY_ENUM:
            return is_compatible(a, b);
        case TY_BITINT:
            return a->is_unsigned == b->is_unsigned &&
                   a->bit_width == b->bit_width;
        case TY_COMPLEX:
            return same_arith(a->base, b->base);
        default:
            return a->is_unsigned == b->is_unsigned &&
                   a->is_long_long == b->is_long_long;
    }
}

static Rank arith_rank(Type *from, Type *to) {
    if (same_arith(from, to))
        return R_EXACT;
    TypeKind f         = from->kind;
    bool     small_int = f == TY_BOOL || f == TY_CHAR || f == TY_SHORT;
    if (small_int && to->kind == TY_INT && !to->is_unsigned)
        return R_PROMOTION;
    // An enum promotes to the int type its own underlying type is.
    if (f == TY_ENUM && to->kind == TY_INT &&
        to->is_unsigned == from->is_unsigned)
        return R_PROMOTION;
    if (f == TY_FLOAT && to->kind == TY_DOUBLE)
        return R_PROMOTION;
    // C: float and double widen to long double by promotion.
    if ((f == TY_FLOAT || f == TY_DOUBLE) && to->kind == TY_LDOUBLE)
        return R_PROMOTION;
    return R_CONVERSION;
}

static bool drops_qualifiers(Type *from_pointee, Type *to_pointee) {
    return (from_pointee->is_const && !to_pointee->is_const) ||
           (from_pointee->is_volatile && !to_pointee->is_volatile);
}

// How many members of `set` have the function type `target` points to; the
// last one found is stored in `*found`. No conversion applies, as in Clang.
static int count_matches(OverloadSet *set, Type *target, Obj **found) {
    if (!target || target->kind != TY_PTR || target->base->kind != TY_FUNC)
        return 0;
    int n = 0;
    for (int i = 0; i < set->n; i++)
        if (is_compatible(set->m[i].fn->ty, target->base)) {
            if (found)
                *found = set->m[i].fn;
            n++;
        }
    return n;
}

static Rank conversion_rank(VirtualMachine *vm, Node *arg, Type *from,
                            Type *to) {
    if (from->kind == TY_ERROR || to->kind == TY_ERROR)
        return R_EXACT;
    if (from->kind == TY_OVERLOAD)
        return count_matches(node_misc(arg)->overload_set, to, NULL) == 1
                   ? R_EXACT
                   : R_NONE;
    from = decayed(vm, from);
    // A string literal is `char[N]` in C; cccc types its elements const.
    if (arg && arg->kind == ND_VAR && arg->var && arg->var->is_string_literal &&
        from->kind == TY_PTR && from->base->is_const) {
        Type *elem     = copy_type(vm, from->base);
        elem->is_const = false;
        from           = pointer_to(vm, elem);
    }

    if (is_arith(from) && is_arith(to))
        return arith_rank(from, to);

    if (to->kind == TY_BOOL && from->kind == TY_PTR)
        return R_CONVERSION;

    if (to->kind == TY_PTR) {
        if (from->kind == TY_NULLPTR_T)
            return R_CONVERSION;
        if (is_integer(from))
            return arg && arg->kind == ND_NUM && arg->val == 0 ? R_CONVERSION
                                                               : R_NONE;
        if (from->kind != TY_PTR)
            return R_NONE;
        Type *fb = from->base, *tb = to->base;
        if (is_compatible_qualified(from, to, vm->compiler.compiler_family))
            return R_EXACT;
        if (is_compatible(from, to))
            return drops_qualifiers(fb, tb) ? R_PTR_INCOMPATIBLE : R_QUAL;
        bool void_involved = fb->kind == TY_VOID || tb->kind == TY_VOID;
        if (void_involved && fb->kind != TY_FUNC && tb->kind != TY_FUNC)
            return drops_qualifiers(fb, tb) ? R_PTR_INCOMPATIBLE : R_CONVERSION;
        return R_PTR_INCOMPATIBLE;
    }

    if (from->kind == TY_PTR || is_arith(from) || is_arith(to))
        return R_NONE;
    return is_compatible_qualified(from, to, vm->compiler.compiler_family)
               ? R_EXACT
               : R_NONE;
}

enum { MAX_RANKED_ARGS = 64 };

typedef struct {
    Obj *fn;
    Rank r[MAX_RANKED_ARGS];
    bool viable;
} Candidate;

static Type *param_named(Type *params, Token *key) {
    for (Type *p = params; p; p = p->next)
        if (token_is_name(key, p->param_name))
            return p;
    return NULL;
}

// Whether `fty` can take the arguments: the positional ones, then the keyed
// ones by name, with every other parameter defaulted.
static bool arguments_fit(Type *params, bool variadic, int npos,
                          KeyedArg *keyed) {
    int np = 0;
    for (Type *p = params; p; p = p->next)
        np++;
    if (npos > np && !variadic)
        return false;
    for (KeyedArg *k = keyed; k; k = k->next) {
        Type *p = param_named(params, k->key);
        if (!p)
            return false;
        int index = 0;
        for (Type *q = params; q != p; q = q->next)
            index++;
        if (index < npos)
            return false;
    }
    int index = 0;
    for (Type *p = params; p; p = p->next, index++) {
        if (index < npos)
            continue;
        bool keyed_here = false;
        for (KeyedArg *k = keyed; k && !keyed_here; k = k->next)
            keyed_here = token_is_name(k->key, p->param_name);
        if (!keyed_here && !param_default_use(p))
            return false;
    }
    return true;
}

// Ranks are indexed by argument: the positional ones, then the keyed ones in
// the order written. An omitted defaulted parameter has no rank.
static bool rank_candidate(VirtualMachine *vm, Candidate *c, Type *params,
                           Node **args, int npos, KeyedArg *keyed,
                           bool allow_incompatible) {
    c->viable = false;
    if (!arguments_fit(params, c->fn->ty->is_variadic, npos, keyed))
        return false;
    Type *p = params;
    for (int i = 0; i < npos; i++) {
        Node *a = args[i];
        Rank  r;
        if (i >= MAX_RANKED_ARGS)
            r = R_ELLIPSIS;
        else if (p) {
            r = conversion_rank(vm, a, a->ty, p);
            if (r == R_PTR_INCOMPATIBLE && !allow_incompatible)
                return false;
        } else
            r = R_ELLIPSIS;
        if (r == R_NONE)
            return false;
        if (i < MAX_RANKED_ARGS)
            c->r[i] = r;
        if (p)
            p = p->next;
    }
    int i = npos;
    for (KeyedArg *k = keyed; k; k = k->next, i++) {
        Rank r = conversion_rank(vm, k->value, k->value->ty,
                                 param_named(params, k->key));
        if (r == R_PTR_INCOMPATIBLE && !allow_incompatible)
            return false;
        if (r == R_NONE)
            return false;
        if (i < MAX_RANKED_ARGS)
            c->r[i] = r;
    }
    c->viable = true;
    return true;
}

// 1 when `a` is better than `b`, -1 when worse, 0 when neither dominates.
static int compare_candidates(const Candidate *a, const Candidate *b,
                              int nargs) {
    bool a_better = false, b_better = false;
    for (int i = 0; i < nargs && i < MAX_RANKED_ARGS; i++) {
        if (a->r[i] < b->r[i])
            a_better = true;
        else if (a->r[i] > b->r[i])
            b_better = true;
    }
    if (a_better && !b_better)
        return 1;
    if (b_better && !a_better)
        return -1;
    return 0;
}

static void describe(Buf *out, Type *t) {
    if (t->is_const)
        buf_puts(out, "const ");
    if (t->is_volatile)
        buf_puts(out, "volatile ");
    switch (t->kind) {
        case TY_VOID:
            buf_puts(out, "void");
            break;
        case TY_BOOL:
            buf_puts(out, "_Bool");
            break;
        case TY_CHAR:
            buf_puts(out, t->is_unsigned ? "unsigned char" : "char");
            break;
        case TY_SHORT:
            buf_puts(out, t->is_unsigned ? "unsigned short" : "short");
            break;
        case TY_INT:
            buf_puts(out, t->is_unsigned ? "unsigned int" : "int");
            break;
        case TY_LONG:
            buf_puts(out, t->is_unsigned
                              ? (t->is_long_long ? "unsigned long long"
                                                 : "unsigned long")
                              : (t->is_long_long ? "long long" : "long"));
            break;
        case TY_FLOAT:
            buf_puts(out, "float");
            break;
        case TY_DOUBLE:
            buf_puts(out, "double");
            break;
        case TY_LDOUBLE:
            buf_puts(out, "long double");
            break;
        case TY_PTR:
            describe(out, t->base);
            buf_puts(out, " *");
            break;
        case TY_ARRAY:
            describe(out, t->base);
            buf_puts(out, "[]");
            break;
        case TY_STRUCT:
        case TY_UNION:
        case TY_ENUM: {
            buf_puts(out, t->kind == TY_STRUCT  ? "struct "
                          : t->kind == TY_UNION ? "union "
                                                : "enum ");
            Token *tag = aggregate_tag(t);
            if (tag)
                buf_putn(out, tag->loc, tag->len);
            else
                buf_puts(out, "<anonymous>");
            break;
        }
        case TY_OVERLOAD:
            buf_puts(out, "<overloaded function type>");
            break;
        default:
            buf_puts(out, "...");
    }
}

// The source text of a parameter's @default value, or NULL.
static const char *default_text(VirtualMachine *vm, Type *param, int *len) {
    CustomAttrUse *use = param_default_use(param);
    if (!use)
        return NULL;
    Token *first = use->tok->next->next, *last = first;
    int    depth = 0;
    for (Token *t = first; t->kind != TK_EOF; t = t->next) {
        if (equal(t, "("))
            depth++;
        else if (equal(t, ")") && depth-- == 0)
            break;
        last = t;
    }
    const char *end = last->loc + last->len;
    if (end < first->loc || end - first->loc > 64) {
        *len = 3;
        return "...";
    }
    *len = (int)(end - first->loc);
    return first->loc;
}

static char *signature_text(VirtualMachine *vm, Obj *fn) {
    Buf out = {0};
    buf_puts(&out, obj_display_name(fn));
    buf_puts(&out, "(");
    for (Type *p = fn->ty->params; p; p = p->next) {
        describe(&out, p);
        if (p->param_name) {
            if (out.p[out.len - 1] != '*')
                buf_puts(&out, " ");
            buf_putn(&out, p->param_name->loc, p->param_name->len);
        }
        int         len;
        const char *def = default_text(vm, p, &len);
        if (def) {
            buf_puts(&out, " = ");
            buf_putn(&out, def, len);
        }
        if (p->next || fn->ty->is_variadic)
            buf_puts(&out, ", ");
    }
    if (fn->ty->is_variadic)
        buf_puts(&out, "...");
    buf_puts(&out, ")");
    char *s = arena_strndup(vm, out.p, out.len);
    free(out.p);
    return s;
}

static char *candidate_list(VirtualMachine *vm, const OverloadCand *cands,
                            int n) {
    Buf out = {0};
    for (int i = 0; i < n; i++) {
        buf_puts(&out, "\n    candidate: ");
        buf_puts(&out, signature_text(vm, cands[i].fn));
    }
    char *s = arena_strndup(vm, out.p ? out.p : "", out.len);
    free(out.p);
    return s;
}

static char *set_candidate_list(VirtualMachine *vm, OverloadSet *set) {
    OverloadCand *cands = calloc((size_t)set->n, sizeof *cands);
    for (int i = 0; i < set->n; i++)
        cands[i].fn = set->m[i].fn;
    char *s = candidate_list(vm, cands, set->n);
    free(cands);
    return s;
}

// The candidate a call with `npos` positional and the keyed arguments selects.
// NULL when none fits and `required` is false; an ambiguous call is an error.
Obj *cc_overload_pick(VirtualMachine *vm, Token *site, const char *name,
                      const OverloadCand *cands, int n, Node **args, int npos,
                      KeyedArg *keyed, bool required) {
    int nargs = npos;
    for (KeyedArg *k = keyed; k; k = k->next)
        nargs++;
    Candidate *ranked    = calloc((size_t)n, sizeof *ranked);
    int        best      = -1;
    bool       ambiguous = false;
    int        viable    = 0;

    for (int pass = 0; pass < 2 && !viable; pass++) {
        for (int i = 0; i < n; i++) {
            ranked[i].fn = cands[i].fn;
            if (rank_candidate(vm, &ranked[i], cands[i].params, args, npos,
                               keyed, pass == 1))
                viable++;
        }
    }

    for (int i = 0; i < n && viable; i++) {
        if (!ranked[i].viable)
            continue;
        bool beats_all = true;
        for (int j = 0; j < n; j++)
            if (j != i && ranked[j].viable &&
                compare_candidates(&ranked[i], &ranked[j], nargs) <= 0)
                beats_all = false;
        if (beats_all) {
            best = i;
            break;
        }
    }
    if (best < 0 && viable)
        ambiguous = true;
    Obj *chosen = best >= 0 ? ranked[best].fn : NULL;
    free(ranked);

    if (!viable && !required)
        return NULL;
    if (!viable)
        error_tok(vm, site, "no matching function for call to '%s'%s", name,
                  candidate_list(vm, cands, n));
    if (ambiguous)
        error_tok(vm, site, "call to '%s' is ambiguous%s", name,
                  candidate_list(vm, cands, n));
    return chosen;
}

// The member of `set` a call with `args` (typed) selects.
Obj *cc_overload_resolve(VirtualMachine *vm, Token *site, OverloadSet *set,
                         Node *args, KeyedArg *keyed) {
    int npos = 0;
    for (Node *a = args; a; a = a->next)
        npos++;
    Node **array = calloc((size_t)npos + 1, sizeof *array);
    int    i     = 0;
    for (Node *a = args; a; a = a->next)
        array[i++] = a;
    OverloadCand *cands = calloc((size_t)set->n, sizeof *cands);
    for (i = 0; i < set->n; i++)
        cands[i] = (OverloadCand){set->m[i].fn, set->m[i].fn->ty->params};
    Obj *fn = cc_overload_pick(vm, site, set->name, cands, set->n, array, npos,
                               keyed, true);
    free(cands);
    free(array);
    return fn;
}

// ---------------------------------------------------------------------------
// The address of a set
// ---------------------------------------------------------------------------

// The member of the set named by placeholder `ph` that function pointer type
// `target` selects, as a function name.
Node *cc_overload_take_address(VirtualMachine *vm, Node *ph, Type *target) {
    OverloadSet *set   = node_misc(ph)->overload_set;
    Obj         *fn    = NULL;
    int          count = count_matches(set, target, &fn);
    if (count == 0)
        error_tok(vm, ph->tok,
                  "no overloaded function '%s' matches the target type%s",
                  set->name, set_candidate_list(vm, set));
    if (count > 1)
        error_tok(vm, ph->tok,
                  "address of overloaded function '%s' is ambiguous%s",
                  set->name, set_candidate_list(vm, set));

    mark_function_referenced(vm, fn);
    fn->is_used = true;
    if (type_attrs(fn->ty)->attr_error_msg && vm->compiler.dead_code_depth == 0)
        error_tok(vm, ph->tok, "%s", type_attrs(fn->ty)->attr_error_msg);
    if (fn->is_deprecated)
        warn_deprecated_use(vm, ph->tok, obj_display_name(fn),
                            obj_attr(fn)->deprecated_msg);
    return new_var_node(vm, fn, ph->tok);
}

// `expr` converted to `target`: a bare overload-set name becomes the member
// `target` selects, anything else is returned as it is.
Node *cc_overload_convert(VirtualMachine *vm, Node *expr, Type *target) {
    if (expr && expr->kind == ND_OVERLOAD)
        return cc_overload_take_address(vm, expr, target);
    return expr;
}

// A bare overload-set name used where no target type selects a member.
void cc_overload_reject(VirtualMachine *vm, Node *expr) {
    if (!expr || expr->kind != ND_OVERLOAD || vm->compiler.in_type_lookahead)
        return;
    OverloadSet *set = node_misc(expr)->overload_set;
    error_tok(vm, expr->tok,
              "reference to overloaded function '%s' could not be resolved; "
              "did you mean to call it?%s",
              set->name, set_candidate_list(vm, set));
}
