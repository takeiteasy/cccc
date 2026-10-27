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

// Checked-pointer (--checked-pointers) bounds subsystem: computing a
// checked pointer's declared bounds, propagating them across assignments
// (#919/#941/#942), and verifying an assignment into an already-checked
// target.

#include "./parse_internal.h"

// Deep-clones an expression subtree for a checked-pointer bounds check
// (#770/#484). Obj.checked_bounds_lo/hi (resolved once, at the pointer's
// declaration by resolve_checked_bounds()) get reused at every checked
// access the pointer participates in; cloning gives each access site its
// own Node objects rather than aliasing the same ones into multiple
// positions in the AST, which is the shape codegen otherwise always sees
// (one Node, one place in the tree). Safe as a shallow-copy-and-recurse
// over the generic child pointers because bounds expressions are
// restricted to side-effect-free assign()-grammar subtrees (checked by
// node_has_side_effects() at resolution time) with no funcall/statement-
// expression nodes, so ->args/->body are always NULL here and need no
// list-cloning.
Node *clone_bounds_node(VirtualMachine *vm, Node *n) {
    if (!n)
        return NULL;
    Node *c = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    *c      = *n;
    c->next = NULL;
    c->lhs  = clone_bounds_node(vm, n->lhs);
    c->rhs  = clone_bounds_node(vm, n->rhs);
    c->cond = clone_bounds_node(vm, n->cond);
    c->then = clone_bounds_node(vm, n->then);
    c->els  = clone_bounds_node(vm, n->els);
    return c;
}

// Like clone_bounds_node(), but for a struct/union *member's* resolved
// bounds template (Member.checked_bounds_lo/hi, #921): every ND_VAR that
// references a sibling-field placeholder (Obj.checked_self_member, set up
// by resolve_member_checked_bounds()) is rewritten into a real ND_MEMBER
// over a fresh clone of `obj` -- the direct object expression of the access
// actually being checked (`s` in `s.p[i]`, `*sp` in `sp->p[i]`). A plain
// variable-rooted bounds expression is resolved directly in a real scope
// and can never contain a placeholder, so clone_bounds_node() (no
// substitution) is correct there; this function is only ever called with a
// member-rooted template.
static Node *clone_member_bounds_node(VirtualMachine *vm, Node *n, Node *obj) {
    if (!n)
        return NULL;
    if (n->kind == ND_VAR && n->var && n->var->checked_self_member) {
        Node *member_node =
            new_unary(vm, ND_MEMBER, clone_bounds_node(vm, obj), n->tok);
        member_node->member = n->var->checked_self_member;
        add_type(vm, member_node);
        return member_node;
    }
    Node *c = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    *c      = *n;
    c->next = NULL;
    c->lhs  = clone_member_bounds_node(vm, n->lhs, obj);
    c->rhs  = clone_member_bounds_node(vm, n->rhs, obj);
    c->cond = clone_member_bounds_node(vm, n->cond, obj);
    c->then = clone_member_bounds_node(vm, n->then, obj);
    c->els  = clone_member_bounds_node(vm, n->els, obj);
    return c;
}

// Identifies the checked-pointer "root" underlying an address expression,
// looking through pointer arithmetic (#770/#484: "bounds carry within a
// single expression" -- `(p+k)[i]` checks against `p`'s declared range, not
// `p+k`'s non-existent one). new_add()/new_sub() always canonicalize a
// pointer +/- integer so the pointer operand is `lhs` (see their
// "Canonicalize `num + ptr`" step), so descending into ->lhs through
// ND_ADD/ND_SUB is sufficient. Also descends through ND_CAST: reading a
// checked-pointer variable turns out to insert an implicit cast node even
// with no source-level cast present (confirmed empirically -- `(p+k)[i]`
// for a plain checked `p` yields ND_ADD(ND_CAST(ND_VAR p), ...) already at
// the innermost `p+k`), so skipping casts is required for the common case,
// not just explicit `(int*)p + k`.
//
// Two roots are recognised (#921 added the second): a plain variable
// (`.var` set, `.mem`/`.obj` NULL), or a struct/union member access
// (`.mem`/`.obj` set to the member and its *direct* object expression --
// `s` in `s.p`, `*sp` in `sp->p` -- `.var` NULL). Anything else (a function
// call returning a pointer, a ternary, ...) is not a checked-pointer access
// in the v1 model and returns an all-NULL CheckedBase, meaning no check is
// emitted -- consistent with bounds not propagating across an assignment
// this function's caller doesn't itself resolve (see #919's
// propagate_checked_bounds() for the assignment-propagation case, which is
// a separate mechanism layered on top, not part of this lookup).
typedef struct {
    Obj    *var;
    Member *mem;
    Node   *obj; // member root's own object expression; NULL unless .mem is set
    Type   *row; // row root's checked array type; .obj is the row expression
} CheckedBase;

static bool checked_vla_row_ready(Type *ty);

static CheckedBase find_checked_base(Node *n) {
    while (n) {
        switch (n->kind) {
            case ND_VAR:
                return (CheckedBase){.var = n->var};
            case ND_MEMBER:
                return (CheckedBase){.mem = n->member, .obj = n->lhs};
            case ND_DEREF:
                // A row of a multidimensional checked array (`a[i]` in
                // `a[i][j]`).
                if (n->ty &&
                    (n->ty->kind == TY_ARRAY || n->ty->kind == TY_VLA) &&
                    n->ty->checked_kind != CHECKED_NONE)
                    return (CheckedBase){.row = n->ty, .obj = n};
                // A plain row reached through a checked or propagated pointer
                // (`p[0]` of `int (*p)[5] = a`) keeps the row's extent.
                if (n->ty &&
                    ((n->ty->kind == TY_ARRAY && n->ty->array_len > 0) ||
                     checked_vla_row_ready(n->ty)) &&
                    n->checked_bounds_lo && !n->checked_bounds_optional)
                    return (CheckedBase){.row = n->ty, .obj = n};
                return (CheckedBase){0};
            case ND_ADD:
            case ND_SUB:
            case ND_CAST:
                n = n->lhs;
                continue;
            default:
                return (CheckedBase){0};
        }
    }
    return (CheckedBase){0};
}

// A comma whose value is a checked-cast temporary (the #486 bounds-cast
// desugar) or a checked array, as a compound literal such as
// `(int _Checked[4]){...}` produces. An array's extent is part of its type,
// not a value the comma establishes, so it is safe to look through.
static bool comma_yields_checked_var(Node *n) {
    while (n && n->kind == ND_COMMA)
        n = n->rhs;
    return n && n->kind == ND_VAR &&
           (n->var->checked_cast_kind != CC_NONE ||
            n->var->checked_call_result ||
            (n->var->ty->kind == TY_ARRAY &&
             n->var->ty->checked_kind != CHECKED_NONE));
}

// #486: like find_checked_base(), but ALSO follows into a bounds cast's own
// desugared shape -- `(__cv = (T [[...]])expr, __cv)`, ND_COMMA's rhs is
// ND_VAR(__cv) -- so a dereference of the cast's result directly,
// `((T [[...]])q)[i]`, finds __cv the same way a plain declared-checked
// pointer would.
//
// Deliberately NOT folded into find_checked_base() itself: that function is
// also called by checked_prop_source_bounds() (#919/#941/#942) and #944's
// verify_checked_assign_scan(), both of which snapshot a SOURCE's bounds
// into a compiler-generated temp BEFORE the enclosing statement's own store
// executes (`(temp = source bounds), (q = E)`) -- sound when the source
// already held a valid value before that statement began, unsound here,
// since __cv's value is itself only established by a side effect of
// evaluating this very same expression (the comma's own internal
// assignment). Exposing this shape to those two passes would let them
// snapshot __cv's bounds before __cv is ever assigned. A bare dereference
// has no such hazard: by the time gen_addr's ND_DEREF case reads bounds off
// its address expression, that address (the whole comma) has already been
// fully evaluated, including its internal store -- ordinary C comma-operator
// semantics. So this variant is used only by set_checked_deref_bounds()
// below; the assignment/propagation machinery splits the cast's store out
// first (split_checked_cast()) and sees a plain read of __cv. The general
// `(a, p)[i]` carry for an arbitrary (non-cast-desugar) comma is filed as a
// follow-up.
static CheckedBase find_checked_deref_base(Node *n) {
    while (n) {
        if (n->kind == ND_COMMA && n->rhs && comma_yields_checked_var(n)) {
            n = n->rhs;
            continue;
        }
        if (n->kind == ND_ADD || n->kind == ND_SUB || n->kind == ND_CAST) {
            n = n->lhs;
            continue;
        }
        return find_checked_base(n);
    }
    return (CheckedBase){0};
}

static Type *checked_base_type(CheckedBase base) {
    return base.row ? base.row : base.var ? base.var->ty : base.mem->ty;
}

// True if `base` names a declared checked pointer (variable or member) --
// i.e. find_checked_base() found a recognised root at all. Doesn't imply a
// bounds *form* is present (CB_NONE/CB_UNKNOWN roots still count as
// "declared checked" here; compute_checked_bounds() is what decides whether
// there's anything to enforce).
static bool checked_base_is_declared(CheckedBase base) {
    if (base.row)
        return true;
    if (base.var)
        return base.var->checked_kind != CHECKED_NONE;
    if (base.mem)
        return base.mem->ty->checked_kind != CHECKED_NONE;
    return false;
}

static Node *split_checked_call_result(VirtualMachine *vm, Node *call,
                                       Obj *alloc_fn);
static bool checked_nt_pointee_supported(Type *base_ty);

static bool checked_ret_form_ok(Type *ret) {
    return ret && ret->kind == TY_PTR && ret->checked_kind != CHECKED_NONE &&
           (ret->checked_kind == CHECKED_SINGLE ||
            (ret->checked_bounds_form != CB_NONE &&
             ret->checked_bounds_form != CB_UNKNOWN));
}

// True if a value of parameter type `p` can be held in a temp and passed on:
// the types a bounds expression can read a hoisted argument through.
static bool checked_param_copyable(Type *p) {
    return is_integer(p) || is_flonum(p) || p->kind == TY_PTR ||
           p->kind == TY_STRUCT || p->kind == TY_UNION;
}

// True if every parameter the return-bounds template `n` names is a copyable
// parameter the call passes an argument for, i.e. one
// split_checked_call_result() can hoist.
static bool checked_ret_tmpl_ok(Node *n, Type *params, Node *args) {
    if (!n)
        return true;
    if (n->kind == ND_VAR && n->var && n->var->checked_self_param_idx) {
        int   k = n->var->checked_self_param_idx;
        Type *p = params;
        Node *a = args;
        for (int i = 1; i < k && p && a; i++) {
            p = p->next;
            a = a->next;
        }
        return p && a && checked_param_copyable(p);
    }
    return checked_ret_tmpl_ok(n->lhs, params, args) &&
           checked_ret_tmpl_ok(n->rhs, params, args) &&
           checked_ret_tmpl_ok(n->cond, params, args) &&
           checked_ret_tmpl_ok(n->then, params, args) &&
           checked_ret_tmpl_ok(n->els, params, args);
}

// The call `n` roots in (through the same `+`, `-` and cast descent as
// find_checked_base()) when its callee returns a checked pointer whose bounds
// split_checked_call_result() can substitute. A call wrapped in its argument
// prelude is returned as the wrapper, so the split keeps the prelude ahead of
// the call. A pure predicate: it allocates nothing and does not depend on
// --checked-pointers.
static Node *checked_call_root(Node *n) {
    while (n) {
        switch (n->kind) {
            case ND_ADD:
            case ND_SUB:
            case ND_CAST:
                n = n->lhs;
                continue;
            case ND_COMMA:
                return n->call_prelude && checked_call_root(n->rhs) == n->rhs
                           ? n
                           : NULL;
            case ND_FUNCALL: {
                Type *fty = n->func_ty;
                if (!fty || n->has_splice_arg ||
                    !checked_ret_form_ok(fty->return_ty))
                    return NULL;
                if (fty->return_ty->checked_kind != CHECKED_SINGLE &&
                    !fty->checked_ret_bounds_lo && !fty->checked_ret_bounds_hi)
                    return NULL;
                return checked_ret_tmpl_ok(fty->checked_ret_bounds_lo,
                                           fty->params, n->args) &&
                               checked_ret_tmpl_ok(fty->checked_ret_bounds_hi,
                                                   fty->params, n->args)
                           ? n
                           : NULL;
            }
            default:
                return NULL;
        }
    }
    return NULL;
}

// The bounds-cast desugar `(__cv = (T [[...]])e, __cv)` that `n` roots in
// (through the same `+`, `-` and cast descent as find_checked_base()), or NULL.
static Node *checked_cast_root(Node *n) {
    while (n) {
        switch (n->kind) {
            case ND_ADD:
            case ND_SUB:
            case ND_CAST:
                n = n->lhs;
                continue;
            case ND_COMMA:
                return n->lhs && n->lhs->kind == ND_ASSIGN && n->rhs &&
                               n->rhs->kind == ND_VAR &&
                               n->rhs->var->checked_cast_kind != CC_NONE &&
                               n->lhs->lhs->kind == ND_VAR &&
                               n->lhs->lhs->var == n->rhs->var
                           ? n
                           : NULL;
            default:
                return NULL;
        }
    }
    return NULL;
}

// Moves the `__cv = (T)e` store out of the cast desugar `comma`, leaving a
// read of `__cv` in its place, so the caller can run the store ahead of a
// source-bounds snapshot that reads `__cv`. Returns the store.
static Node *split_checked_cast(VirtualMachine *vm, Node *comma) {
    Node *store = comma->lhs;
    Node *read  = new_var_node(vm, comma->rhs->var, comma->tok);
    add_type(vm, read);
    Node *saved  = comma->next;
    *comma       = *read;
    comma->next  = saved;
    return store;
}

// Builds the self-referencing expression a checked base's own bounds
// formulas are written in terms of -- `p` itself for a variable root, or
// `obj.mem`/`(*obj).mem` for a member root (obj cloned fresh each call so
// multiple uses in the same formula, e.g. CB_COUNT's lo and the p-in-p+n*sz
// hi, don't alias the same Node into two places in the tree, matching how
// clone_bounds_node() already treats every other reused subtree). Returns
// an already-typed node.
//
// #945: for a member root with a non-trivial object expression (a runtime
// index, e.g. `k` in `arr[k].p[i]`), `base.obj` arrives here already
// rewritten by compute_checked_bounds()'s hoist into `*t` for a
// compiler-generated temp `t` -- so the "fresh clone every call" above is
// still correct (it clones the cheap `*t` dereference, not the original
// expensive expression) and `k` itself is evaluated only once per access,
// by Node.checked_bounds_obj_init. A trivial object expression (a bare
// local, `s.a.b`) is left untouched and cloned directly, since re-cloning
// it costs nothing (see checked_obj_is_trivial()).
// #487: a checked ARRAY base (Type.checked_kind set on a TY_ARRAY, see the
// checked_array_extent comment in src/cccc.h) types as TY_ARRAY, but every
// caller of checked_base_self_expr() feeds the result straight into
// new_binary(vm, ND_ADD, ...) + add_type() -- add_type(), unlike the parser's
// own new_add() helper, does NOT perform array-to-pointer decay or pointer-
// arithmetic scaling, so an un-decayed TY_ARRAY operand there would be wrong
// (either untyped or scaled by the wrong factor). Explicitly decay it here:
// `&a` deliberately does not itself decay (src/type.c, #973/#975) -- it
// types as `T (*)[N]` -- so the cast to `T *` is the actual decay step,
// exactly what an ordinary `a[i]` expression (desugared to `*(a+i)` in
// postfix()) gets for free from new_add(). Purely additive: checked_kind can
// only be set on a TY_PTR before #487, so this arm was unreachable until
// checked arrays existed.
static Node *checked_array_decay(VirtualMachine *vm, Node *e, Token *tok) {
    if (!e->ty || (e->ty->kind != TY_ARRAY && e->ty->kind != TY_VLA))
        return e;
    Node *addr = new_unary(vm, ND_ADDR, e, tok);
    add_type(vm, addr);
    return new_cast(vm, addr, pointer_to(vm, e->ty->base));
}

static Node *checked_base_self_expr(VirtualMachine *vm, CheckedBase base,
                                    Token *tok) {
    if (base.row) {
        Node *row = clone_bounds_node(vm, base.obj);
        add_type(vm, row);
        return checked_array_decay(vm, row, tok);
    }
    if (base.var) {
        Node *p = new_var_node(vm, base.var, tok);
        add_type(vm, p);
        return checked_array_decay(vm, p, tok);
    }
    Node *member_node =
        new_unary(vm, ND_MEMBER, clone_bounds_node(vm, base.obj), tok);
    member_node->member = base.mem;
    add_type(vm, member_node);
    return checked_array_decay(vm, member_node, tok);
}

// #945: true if `n` (a member access's object expression, base.obj) is
// cheap enough to re-clone as many times as compute_checked_bounds() needs
// -- a chain of plain variable reads and member selections, which folds to
// a stack-frame offset with no runtime work. Anything else (a runtime
// index buried in an ND_ADD, a function call, ...) is worth hoisting into a
// single temp instead of re-evaluating per bound. Deliberately does NOT
// special-case ND_DEREF: a checked-pointer chain like `sp->p` is a deref
// over a plain variable read (`*sp`), covered by the ND_VAR base case
// through recursion into ->lhs, so no separate arm is needed for it here.
static bool checked_obj_is_trivial(Node *n) {
    if (!n)
        return true;
    switch (n->kind) {
        case ND_VAR:
            return true;
        case ND_MEMBER:
        case ND_DEREF:
        case ND_CAST:
            return checked_obj_is_trivial(n->lhs);
        default:
            return false;
    }
}

// #945: true if `&n` is a well-formed address expression -- i.e. `n` is
// guaranteed to be an lvalue gen_addr() knows how to take the address of.
// ND_VAR/ND_MEMBER/ND_DEREF cover every shape find_checked_base()'s `.obj`
// can actually take: a plain variable, a nested member chain (`s.a.b`), and
// -- critically -- array/pointer indexing (`arr[k]` parses to
// ND_DEREF(ND_ADD(arr, k)), so this is what makes the ticket's motivating
// case, `arr[k].p[i]`, addressable). struct_ref() (the `.`/`->` parser) in
// this compiler already rejects any non-lvalue operand ("not an lvalue")
// before an ND_MEMBER can even exist over it -- e.g. `(c ? s1 : s2).p[i]`
// for two same-type struct locals is a compile error independent of
// checked-pointers -- so in practice every `base.obj` this function ever
// sees already satisfies this check. Kept anyway as a hard safety
// invariant rather than an assumption: if a future lvalue-producing node
// kind is ever added to struct_ref()'s accepted set without a matching arm
// here, this function's default (false) makes compute_checked_bounds()
// safely skip the hoist and fall back to the original re-clone-in-place
// behavior, rather than building an invalid ND_ADDR over it.
static bool checked_obj_is_addressable(Node *n) {
    return n &&
           (n->kind == ND_VAR || n->kind == ND_MEMBER || n->kind == ND_DEREF);
}

// The array-typed lvalue a plain-array source expression denotes: a variable,
// a member, or a row (`m[i]`), reached through `+`, `-`, casts and `&x[i]`.
static Node *plain_array_lvalue(Node *n) {
    while (n) {
        switch (n->kind) {
            case ND_VAR:
            case ND_MEMBER:
                return n;
            case ND_DEREF:
                return n->ty && n->ty->kind == TY_ARRAY ? n : NULL;
            case ND_ADDR:
                if (!n->lhs || n->lhs->kind != ND_DEREF)
                    return NULL;
                n = n->lhs->lhs;
                continue;
            case ND_ADD:
            case ND_SUB:
            case ND_CAST:
                n = n->lhs;
                continue;
            default:
                return NULL;
        }
    }
    return NULL;
}

// The array variable or member a chain of rows (`m[i][j]`) is part of.
static Node *plain_array_whole(Node *lv) {
    while (lv && lv->kind == ND_DEREF)
        lv = plain_array_lvalue(lv->lhs);
    return lv;
}

// True if `n` names a struct object of declared size: a struct variable, or
// members of one, with no pointer in the way.
static bool checked_obj_complete(Node *n) {
    while (n && n->kind == ND_MEMBER)
        n = n->lhs;
    return n && n->kind == ND_VAR && n->var->ty->kind == TY_STRUCT;
}

// A plain array object with a known extent as a source for a checked target:
// `q = buf`, `sink(buf, 8)`, `(T [[...]])buf`. Its bounds are the object's
// own [buf, buf + sizeof buf). Not a propagation source: an unchecked
// `int *q = buf;` stays unchecked.
//
// Accepted: a complete non-VLA array variable (a string literal is one), a
// non-bit-field array member of a struct reached through a trivial object
// expression, or a row of either (`m[i]`, `&buf[i]`). The last member of a
// struct is accepted only on a complete object (`s.data`, never `sp->data`),
// since through a pointer it may be a flexible-array stand-in. An array
// parameter is a TY_PTR and never matches.
static Type *checked_plain_array_root(Node *rhs, CheckedBase *out) {
    Node *lv = plain_array_lvalue(rhs);
    if (!lv)
        return NULL;
    CheckedBase base = {0};
    Type       *ty   = NULL;
    switch (lv->kind) {
        case ND_VAR:
            base.var = lv->var;
            ty       = lv->var->ty;
            break;
        case ND_MEMBER:
            base.mem = lv->member;
            base.obj = lv->lhs;
            ty       = lv->member->ty;
            if (lv->member->is_bitfield || !base.obj->ty ||
                base.obj->ty->kind != TY_STRUCT ||
                !checked_obj_is_trivial(base.obj) ||
                (!lv->member->next && !checked_obj_complete(base.obj)))
                return NULL;
            break;
        default: {
            ty = lv->ty;
            if (node_has_side_effects(lv) ||
                !checked_plain_array_root(lv->lhs, &(CheckedBase){0}))
                return NULL;
            base.row = ty;
            base.obj = lv;
            break;
        }
    }
    if (!ty || ty->kind != TY_ARRAY || ty->checked_kind != CHECKED_NONE ||
        ty->size <= 0)
        return NULL;
    *out = base;
    return ty;
}

static bool checked_plain_array_source(VirtualMachine *vm, Node *rhs,
                                       Token *tok, Node **out_lo,
                                       Node **out_hi) {
    CheckedBase base;
    Type       *ty = checked_plain_array_root(rhs, &base);
    *out_lo        = NULL;
    *out_hi        = NULL;
    if (!ty)
        return false;
    Node *arr;
    if (base.row) {
        arr = clone_bounds_node(vm, base.obj);
    } else if (base.var) {
        arr = new_var_node(vm, base.var, tok);
    } else {
        arr = new_unary(vm, ND_MEMBER, clone_bounds_node(vm, base.obj), tok);
        arr->member = base.mem;
    }
    add_type(vm, arr);
    Node *lo = checked_array_decay(vm, arr, tok);
    Node *hi = new_binary(vm, ND_ADD, clone_bounds_node(vm, lo),
                          new_long(vm, ty->size, tok), tok);
    add_type(vm, hi);
    if (base.row) {
        // A row outside the array it indexes (`m[i]` with `i` out of range)
        // has the empty range [lo, lo), which no non-empty target satisfies.
        CheckedBase whole_base;
        Node       *whole = plain_array_whole(base.obj);
        Type       *wty   = checked_plain_array_root(whole, &whole_base);
        Node       *warr  = whole->kind == ND_VAR
                                ? new_var_node(vm, whole->var, tok)
                                : clone_bounds_node(vm, whole);
        add_type(vm, warr);
        Node *wlo = checked_array_decay(vm, warr, tok);
        Node *whi = new_binary(vm, ND_ADD, clone_bounds_node(vm, wlo),
                               new_long(vm, wty->size, tok), tok);
        add_type(vm, whi);
        Node *lo_ok = new_binary(vm, ND_LE, wlo, clone_bounds_node(vm, lo), tok);
        Node *hi_ok = new_binary(vm, ND_LE, clone_bounds_node(vm, hi), whi, tok);
        Node *inside = new_binary(vm, ND_LOGAND, lo_ok, hi_ok, tok);
        Node *cond   = new_node(vm, ND_COND, tok);
        cond->cond   = inside;
        cond->then   = hi;
        cond->els    = clone_bounds_node(vm, lo);
        add_type(vm, cond);
        hi = cond;
    }
    *out_lo = lo;
    *out_hi = hi;
    return true;
}

// Computes the checked-pointer bounds range to enforce for an access rooted
// at `base` (#770/#484, generalised for #921's member roots and split out
// of set_checked_deref_bounds() so #919's propagate_checked_bounds() can
// call it too, to build assignment-time snapshot bounds rather than
// per-deref bounds). Leaves *out_lo/*out_hi NULL and *out_nt false for an
// unchecked base, or a checked base with no bounds to enforce (CB_NONE --
// `[[cccc::array]]` with no count/byte_count/bounds attribute at all -- or
// CB_UNKNOWN, the `bounds(unknown)` trust escape hatch).
//
// The actual CHKR emission is gated on --checked-pointers at codegen time
// (src/codegen.c); these fields are populated unconditionally so the
// compile-time type rules (arithmetic rejection etc.) and the runtime gate
// stay independent, per the plan's Decisions section.
//
// #945/#947: `out_obj_init` (may be NULL) receives a `t = &obj` hoist for a
// non-trivial member-access object expression -- see
// Node.checked_bounds_obj_init's comment (src/cccc.h) for why it must be
// re-evaluated at every codegen site that reads *out_lo/*out_hi. `hoist_fn`
// selects which temp allocator builds `t`: NULL uses new_lvar() (the #945
// original, valid only while a function body is being parsed -- see
// vm->compiler.current_fn below), non-NULL uses new_checked_prop_temp()
// (below) to link `t` straight into `hoist_fn->locals` instead --
// required by #919's propagate_checked_bounds() and #944's
// verify_checked_assign_scan(), both of which call this function AFTER
// their function's own leave_scope()/`fn->locals = vm->compiler.locals`
// snapshot, where new_lvar()'s prepend-to-vm->compiler.locals would
// silently produce a temp with no stack slot (the $with_fn-locals hazard
// class). #947 folded that per-assignment-site hoist in; the only
// remaining NULL-out_obj_init callers are phase-A-style usability probes
// that discard lo/hi and must not allocate a temp per probe.
// #939: pointee types whose terminator-slot store CHKNT/CHKNTZ can actually
// guard. Integer/pointer (the original #923 set) and now also float/double
// (via a bit-pattern transfer into an int reg -- see codegen.c's ND_ASSIGN
// case) and struct/union/wide _BitInt/_Decimal (the memcpy-lowered types,
// via CHKNTZ's source byte-scan). TY_LDOUBLE is deliberately excluded: its
// widened terminator slot is 16 bytes (get_vm_size) but the actual store is
// an 8-byte flat-double FSTR -- a guard would assert bytes it never
// inspected, a false assurance rather than an honest gap. Vectors,
// _Complex, and anything else not listed here stay excluded too.
static bool checked_nt_pointee_supported(Type *base_ty) {
    if (!base_ty)
        return false;
    return is_integer(base_ty) || base_ty->kind == TY_PTR ||
           base_ty->kind == TY_FLOAT || base_ty->kind == TY_DOUBLE ||
           base_ty->kind == TY_STRUCT || base_ty->kind == TY_UNION ||
           is_decimal(base_ty);
}

// #950: a store into a member of an ntarray element (`tbl[n].a`,
// `tbl[n].in.arr[k]`) lands in the terminator slot when the element does, but
// the NT fact lives on the element's ND_DEREF. Walk the member chain back to
// it. Bit-fields stay unguarded: the stored value is truncated to the field
// width, so a non-zero value need not leave the slot non-zero.
Node *checked_nt_store_root(Node *lhs) {
    if (!lhs)
        return NULL;
    if (lhs->kind == ND_DEREF && lhs->checked_nt_terminator)
        return lhs;
    if (lhs->kind != ND_MEMBER && lhs->kind != ND_DEREF)
        return NULL;
    if (lhs->kind == ND_MEMBER && lhs->member->is_bitfield)
        return NULL;
    if (!checked_nt_pointee_supported(lhs->ty))
        return NULL;
    for (Node *n = lhs;;) {
        if (n->kind == ND_DEREF && n->checked_nt_terminator)
            return n;
        if (n->kind == ND_MEMBER) {
            n = n->lhs;
            continue;
        }
        if (n->kind == ND_DEREF && n->rmw_lvalue) {
            // `*tmp` of an `A.x op= B` / `*tmp op= B` desugar: back to A.
            n = n->rmw_lvalue;
            continue;
        }
        if (n->kind == ND_DEREF) {
            // Subscript of an array member: DEREF(ADD(MEMBER, idx)).
            Node *a = n->lhs;
            while (a && (a->kind == ND_ADD || a->kind == ND_SUB ||
                         a->kind == ND_CAST))
                a = a->lhs;
            if (a && a->kind == ND_MEMBER && a->ty && a->ty->kind == TY_ARRAY) {
                n = a;
                continue;
            }
        }
        return NULL;
    }
}

static Obj *new_checked_prop_temp(VirtualMachine *vm, Obj *fn, Type *ty);

// An array-typed deref only computes an address; the real access carries the
// check, so the hoisted `&row` clone drops it.
static void strip_row_checks(Node *n) {
    if (!n)
        return;
    if (n->kind == ND_DEREF && n->ty && n->ty->kind == TY_ARRAY) {
        n->checked_bounds_lo       = NULL;
        n->checked_bounds_hi       = NULL;
        n->checked_bounds_obj_init = NULL;
        n->checked_nt_terminator   = false;
        n->checked_bounds_optional = false;
    }
    strip_row_checks(n->lhs);
    strip_row_checks(n->rhs);
    strip_row_checks(n->cond);
    strip_row_checks(n->then);
    strip_row_checks(n->els);
}

// A row whose length is only known at run time carries its byte size in a
// temporary (`vla_size`); element-size arithmetic reads that instead of the
// placeholder get_vm_size() reports for a TY_VLA.
static bool checked_vla_row_ready(Type *ty) {
    return ty->kind == TY_VLA && ty->vla_len && ty->vla_size;
}

static Node *checked_elem_size_node(VirtualMachine *vm, Type *ty, Token *tok) {
    if (ty && ty->kind == TY_VLA) {
        Node *n = new_cast(vm, new_var_node(vm, ty->vla_size, tok), ty_long);
        add_type(vm, n);
        return n;
    }
    return new_long(vm, ty ? get_vm_size(ty) : 1, tok);
}

// The size CHKR checks an access of `pointee` against. A variable-length row
// starts on a row boundary, so checking its first byte against the exact end
// is the same as checking the whole row.
static int64_t checked_access_bytes(Type *pointee) {
    if (!pointee)
        return 1;
    return pointee->kind == TY_VLA ? 1 : get_vm_size(pointee);
}

static void compute_checked_bounds(VirtualMachine *vm, CheckedBase base,
                                   Type *access_ty, Token *tok, Node **out_lo,
                                   Node **out_hi, bool *out_nt,
                                   Node **out_obj_init, Obj *hoist_fn) {
    *out_lo = NULL;
    *out_hi = NULL;
    *out_nt = false;
    if (out_obj_init)
        *out_obj_init = NULL;
    if (!base.var && !base.mem && !base.row)
        return;

    // #921/#948: an object expression with side effects (e.g. `f()->p[i]`)
    // is declined -- permanently, not a temporary scope limitation. #945's
    // hoist only changes how many times a side-effect-free object
    // expression is evaluated (many times down to once); it does not make
    // instrumenting a side-effecting one safe, because the hoist rewrites
    // the BOUNDS expressions, not the access itself -- `f()->p[i]`'s actual
    // access still calls `f()` once on its own, so emitting a check would
    // call it again just to build `t = &obj`, an extra evaluation of a
    // side-effecting expression that a --checked-pointers build must not
    // introduce over a default build. The multi-site re-emission contract
    // (Node.checked_bounds_obj_init, src/cccc.h) also relies on `obj` being
    // side-effect-free/idempotent by construction, not incidentally.
    if ((base.mem || base.row) && node_has_side_effects(base.obj))
        return;

    // #945/#947: hoist a non-trivial object expression into a single
    // compiler-generated temp for this access, so every downstream
    // checked_base_self_expr()/clone_member_bounds_node() call below clones
    // the cheap `*t` dereference instead of re-evaluating `obj` itself.
    // Gated on CCCC_CHECKED_BOUNDS (this function otherwise runs
    // unconditionally at parse time -- only CHKR *emission* is
    // flag-gated -- so without this a default build would grow a stack slot
    // for every checked-member access), on a live allocation context
    // (either `hoist_fn` non-NULL, or new_lvar()'s own requirement that
    // vm->compiler.current_fn be live -- see this function's doc comment),
    // and on `obj` being addressable (checked_obj_is_addressable() --
    // taking `&obj` is only well-formed for an lvalue; anything else falls
    // back to the original re-clone behavior, unchanged).
    if (out_obj_init && (base.mem || base.row) &&
        (vm->flags & CCCC_CHECKED_BOUNDS) &&
        (hoist_fn || vm->compiler.current_fn) &&
        !checked_obj_is_trivial(base.obj) &&
        checked_obj_is_addressable(base.obj)) {
        Obj  *t = hoist_fn ? new_checked_prop_temp(vm, hoist_fn,
                                                   pointer_to(vm, base.obj->ty))
                           : new_lvar(vm, "", 0, pointer_to(vm, base.obj->ty));
        Node *obj_clone = clone_bounds_node(vm, base.obj);
        strip_row_checks(obj_clone);
        Node *addr = new_unary(vm, ND_ADDR, obj_clone, tok);
        add_type(vm, addr);
        Node *init =
            new_binary(vm, ND_ASSIGN, new_var_node(vm, t, tok), addr, tok);
        add_type(vm, init);
        *out_obj_init  = init;
        Node *obj_temp = new_unary(vm, ND_DEREF, new_var_node(vm, t, tok), tok);
        add_type(vm, obj_temp);
        base.obj = obj_temp;
    }

    bool        plain_row  = base.row && base.row->checked_kind == CHECKED_NONE;
    CheckedKind kind       = plain_row  ? CHECKED_ARRAY
                             : base.row ? base.row->checked_kind
                             : base.var ? base.var->checked_kind
                                        : base.mem->ty->checked_kind;
    CheckedBoundsForm form = plain_row  ? CB_COUNT
                             : base.row ? base.row->checked_bounds_form
                             : base.var ? base.var->checked_bounds_form
                                        : base.mem->ty->checked_bounds_form;
    Node             *resolved_lo =
        base.var ? base.var->checked_bounds_lo
        : base.mem ? base.mem->checked_bounds_lo
                   : NULL;
    Node *resolved_hi = NULL;
    if (base.row && base.row->kind == TY_VLA) {
        if (!checked_vla_row_ready(base.row))
            return;
        resolved_hi = checked_vla_extent(vm, base.row, tok);
    } else if (base.row) {
        resolved_hi = new_long(vm,
                               plain_row ? base.row->array_len
                                         : base.row->checked_array_extent,
                               tok);
        add_type(vm, resolved_hi);
    } else {
        resolved_hi =
            base.var ? base.var->checked_bounds_hi : base.mem->checked_bounds_hi;
    }
    Type   *base_ty   = access_ty->base;
    if (base_ty && base_ty->kind == TY_VLA && !checked_vla_row_ready(base_ty))
        return;

    if (kind == CHECKED_SINGLE) {
        // ~ Checked C _Ptr<T>: implicit range [p, p + sizeof(T)). Built
        // directly with new_binary rather than new_add() -- new_add()
        // itself rejects arithmetic on a CHECKED_SINGLE pointer, which
        // would reject this synthesized bound too. add_type() is called on
        // each node explicitly (rather than hand-setting ->ty) so it
        // recurses into and types every child the way it would for the
        // equivalent user-written expression -- add_type() no-ops on a node
        // whose ->ty is already non-NULL without visiting its children, so
        // pre-setting ->ty on a binary node would leave its operands
        // untyped and crash codegen.
        *out_lo  = checked_base_self_expr(vm, base, tok);
        Node *hi = new_binary(vm, ND_ADD, checked_base_self_expr(vm, base, tok),
                              checked_elem_size_node(vm, base_ty, tok), tok);
        add_type(vm, hi);
        *out_hi = hi;
        return;
    }

    // CHECKED_ARRAY / CHECKED_NTARRAY
    switch (form) {
        case CB_NONE:
        case CB_UNKNOWN:
            return; // nothing declared/trusted -- no runtime check
        case CB_RANGE: {
            // Absolute [lo, hi) -- already resolved (and typed) at declaration
            // time; clone_bounds_node() copies ->ty along with everything else.
            // A checked base whose declaration path never resolved its bounds
            // (e.g. a block-scope `static` local, or any future declaration
            // shape that copies checked_kind/checked_bounds_form without also
            // being wired to resolve them) has these left NULL -- treat that
            // exactly like CB_NONE/CB_UNKNOWN rather than feeding a NULL Node*
            // into clone_bounds_node()/add_type() below and crashing the
            // compiler.
            if (!resolved_lo || !resolved_hi)
                return;
            *out_lo  = base.mem
                           ? clone_member_bounds_node(vm, resolved_lo, base.obj)
                           : clone_bounds_node(vm, resolved_lo);
            Node *hi = base.mem
                           ? clone_member_bounds_node(vm, resolved_hi, base.obj)
                           : clone_bounds_node(vm, resolved_hi);
            // #938: the terminator slot is the element-size bytes beginning at
            // the declared end of the range -- for bounds(lo,hi) that end is
            // `hi` itself, so widen it by one element exactly as CB_COUNT/
            // CB_BYTE_COUNT do below.
            if (kind == CHECKED_NTARRAY) {
                hi = new_binary(vm, ND_ADD, hi,
                                checked_elem_size_node(vm, base_ty, tok), tok);
                add_type(vm, hi);
                if (checked_nt_pointee_supported(base_ty))
                    *out_nt = true;
            }
            *out_hi = hi;
            return;
        }
        case CB_COUNT:
        case CB_BYTE_COUNT: {
            if (!resolved_hi)
                return; // see the CB_RANGE comment above
            // lo = the base's own live value at this access; hi = base +
            // n[*element size]. #938: ntarray widens by one element for the
            // terminator slot -- the element-size bytes beginning at the
            // declared end of the range -- under all three bounds forms (#483
            // originally shipped this for count(n) only; docs/safety.md
            // documents the unified rule).
            *out_lo = checked_base_self_expr(vm, base, tok);
            Node *n = base.mem
                          ? clone_member_bounds_node(vm, resolved_hi, base.obj)
                          : clone_bounds_node(vm, resolved_hi); // already typed
            Node *count_bytes;
            if (form == CB_BYTE_COUNT) {
                count_bytes = n;
                if (kind == CHECKED_NTARRAY) {
                    count_bytes = new_binary(
                        vm, ND_ADD, count_bytes,
                        checked_elem_size_node(vm, base_ty, tok), tok);
                    add_type(vm, count_bytes);
                }
            } else if (kind == CHECKED_NTARRAY) {
                Node *n_plus_1 =
                    new_binary(vm, ND_ADD, n, new_long(vm, 1, tok), tok);
                add_type(vm, n_plus_1);
                count_bytes =
                    new_binary(vm, ND_MUL, n_plus_1,
                               checked_elem_size_node(vm, base_ty, tok), tok);
                add_type(vm, count_bytes);
            } else {
                count_bytes =
                    new_binary(vm, ND_MUL, n,
                               checked_elem_size_node(vm, base_ty, tok), tok);
                add_type(vm, count_bytes);
            }
            // #923/#938/#939: this access can land on the widened terminator
            // slot -- flag it for CHKNT/CHKNTZ's store-side null-terminator
            // guard, for any pointee checked_nt_pointee_supported() can
            // actually guard (see its comment for the excluded types and why).
            if (kind == CHECKED_NTARRAY &&
                checked_nt_pointee_supported(base_ty))
                *out_nt = true;
            Node *hi =
                new_binary(vm, ND_ADD, checked_base_self_expr(vm, base, tok),
                           count_bytes, tok);
            add_type(vm, hi);
            *out_hi = hi;
            return;
        }
    }
}

// Populates `deref` (an ND_DEREF built at a `*p` or `p[i]`/`p->x` site) with
// the checked-pointer bounds range to enforce, if `addr` (the address
// expression, i.e. deref->lhs) is rooted at a checked pointer or member with
// resolvable bounds (#770/#484/#921). Thin wrapper around
// find_checked_base()/compute_checked_bounds() -- see #919's
// propagate_checked_bounds() for the other caller of compute_checked_bounds().
void set_checked_deref_bounds(VirtualMachine *vm, Node *deref, Node *addr,
                              Token *tok) {
    // A dereference through a checked-return call: evaluate the call into its
    // result local first, so the bounds read it after the call.
    if ((vm->flags & CCCC_CHECKED_BOUNDS) && vm->compiler.current_fn) {
        Node *croot = checked_call_root(addr);
        if (croot) {
            Node *store = split_checked_call_result(vm, croot, NULL);
            Node *read  = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
            *read       = *croot;
            read->next  = NULL;
            Node *comma = new_binary(vm, ND_COMMA, store, read, tok);
            add_type(vm, comma);
            Node *saved = croot->next;
            *croot      = *comma;
            croot->next = saved;
        }
    }
    CheckedBase base = find_checked_deref_base(addr);
    if (!checked_base_is_declared(base))
        return;
    Type *pty = checked_base_type(base);
    deref->checked_access_size = checked_access_bytes(pty->base);
    compute_checked_bounds(vm, base, pty, tok, &deref->checked_bounds_lo,
                           &deref->checked_bounds_hi,
                           &deref->checked_nt_terminator,
                           &deref->checked_bounds_obj_init, NULL);
}

// #919/#941: bounds propagation across assignment for an unchecked pointer
// local whose value is snapshotted from a checked-rooted source. See the
// design note at propagate_checked_bounds() below for the whole-function
// fixpoint this implements, and docs/safety.md's "Bounds propagation across
// assignment" section for the user-facing writeup.
//
// True if `base` (the result of find_checked_base() on an assignment's rhs
// or a deref's address) is a source a propagation candidate may snapshot
// from. Three kinds of source are recognised:
//
//  1. A declared checked variable or member (#921), side-effect-free if a
//     member, AND actually has an enforceable bounds form -- CB_NONE/
//     CB_UNKNOWN sources (a bare `[[cccc::array]]` with nothing declared, or
//     the bounds(unknown) trust escape hatch) are deliberately excluded here
//     even though they ARE "declared checked": propagating "no check" itself
//     would require every assignment along every path to agree on that,
//     which this per-assignment design can't guarantee, so such a source
//     poisons the candidate instead (conservative: no propagation, not a
//     false trap).
//  2. #941: a plain variable that is itself a *chained* propagation source
//     as of the previous fixpoint round (Obj.checked_prop_chain_src) --
//     i.e. `q` in `int *r = q + 1;` where `q` itself propagates from `p`.
//     The source range is then `q`'s own snapshot temps
//     (checked_prop_lo/hi), read as plain variables rather than recomputed
//     -- there is no bounds *expression* to rebuild for a propagated
//     source, only the runtime range already snapshotted into `q`'s temps.
//     Returns lo/hi as NULL (source recognised but not yet usable) if the
//     temps haven't been allocated yet -- true only during phase A's
//     probing, before phase B allocates every survivor's temps; callers
//     that need a definite yes/no from a chained source should only be
//     invoked after phase B for the walk-2/3 rewrite/attach purposes, which
//     is how propagate_checked_bounds() already sequences its phases.
//
// Building `*out_lo`/`*out_hi` as a side effect is intentional -- the caller
// either discards them (the phase-A usability probe) or keeps them (the
// phase-C rewrite); recomputing per call is cheap, compile-time-only work
// for kind 1 (a fresh expression tree) and a single Obj lookup for kind 2.
//
// #942: `*out_optional` reports whether a *recognised* source is only
// trustworthy on some paths, not every path -- always false for kind 1 (a
// declared source's bounds are a static expression, always valid whenever
// it's evaluated), and equal to the source's own frozen
// Obj.checked_prop_optional (as of the previous round) for kind 2, so
// optionality composes transitively through an arbitrarily long #941 chain:
// if `q` is OPT, then `r = q + 1;` makes `r`'s rooted store from `q` an
// optional source too, even though the store itself is unconditionally
// checked-rooted. Left unset (caller must not read it) when this returns
// false -- there is no source to be optional about.
//
// #943: `*out_nt`/`*out_nt_elem` report whether this source's `hi` is
// specifically a widened [[cccc::ntarray]] upper bound (the terminator-slot
// fact), and its pointee element size if so -- 0 means no NT fact. Kind 1
// forwards compute_checked_bounds()'s own `nt` output (previously discarded)
// and the source's own pointee size; kind 2 forwards the chained source's
// frozen Obj.checked_prop_nt_elem, composing transitively through a #941
// chain exactly like *out_optional does. Also left unset when this returns
// false.
//
// #947: `hoist_fn`/`out_obj_init` (both NULL-able, forwarded straight to
// compute_checked_bounds()) let a kind-1 (directly-declared) source hoist a
// non-trivial member object expression into a single per-assignment temp,
// same rationale as #945's per-access hoist. Pass NULL/NULL from a probing
// call (phase A's usability scan, which discards lo/hi every round and must
// not allocate a temp per probe) and a real `fn`/non-NULL out param only
// once a rewrite is actually being committed. Kind 2 (a chained source) can
// never produce an init -- it reads back already-snapshotted temp vars, not
// a member expression -- so *out_obj_init is left NULL on that path.
static bool checked_prop_source_bounds(VirtualMachine *vm, Node *rhs,
                                       Token *tok, Node **out_lo, Node **out_hi,
                                       bool *out_optional, bool *out_nt,
                                       int64_t *out_nt_elem, bool *out_plain,
                                       Obj *hoist_fn, Node **out_obj_init) {
    *out_lo    = NULL;
    *out_hi    = NULL;
    *out_plain = false;
    if (out_obj_init)
        *out_obj_init = NULL;
    // An unsplit bounds cast is a source through its `__cv` (the split, done
    // by the consumers that rewrite, leaves a plain read of it).
    Node       *cast = checked_cast_root(rhs);
    CheckedBase base =
        cast ? (CheckedBase){.var = cast->rhs->var} : find_checked_base(rhs);
    if (checked_base_is_declared(base)) {
        if ((base.mem || base.row) && node_has_side_effects(base.obj))
            return false;
        Type *pty = checked_base_type(base);
        compute_checked_bounds(vm, base, pty, tok, out_lo, out_hi, out_nt,
                               out_obj_init, hoist_fn);
        if (!(*out_lo && *out_hi))
            return false;
        *out_optional = false;
        Type *base_ty = pty->base;
        *out_nt_elem  = *out_nt ? (base_ty ? get_vm_size(base_ty) : 1) : 0;
        return true;
    }
    // A plain array's own extent: a source for checked targets only, so the
    // propagation passes keep derefs through the holder unchecked.
    if (checked_plain_array_source(vm, rhs, tok, out_lo, out_hi)) {
        *out_optional = false;
        *out_nt       = false;
        *out_nt_elem  = 0;
        *out_plain    = true;
        return true;
    }
    // A checked-return call is a source whose bounds exist only once the call
    // is split into its result local (split_checked_call_result()); the
    // classification probe needs just the fact and its terminator slot.
    Node *croot = checked_call_root(rhs);
    if (croot) {
        Type *rt =
            (croot->call_prelude ? croot->rhs : croot)->func_ty->return_ty;
        *out_optional = false;
        *out_nt       = rt->checked_kind == CHECKED_NTARRAY &&
                        checked_nt_pointee_supported(rt->base);
        *out_nt_elem  = *out_nt ? get_vm_size(rt->base) : 0;
        return true;
    }
    // #941: kind 2 -- a chained propagation source. base.var is non-NULL
    // here (find_checked_base() never sets .mem for a plain unchecked
    // variable read, and checked_base_is_declared() already ruled out the
    // "declared checked" case above, so this is the "unchecked plain
    // variable" fallthrough).
    if (base.var && base.var->checked_prop_chain_src) {
        // The source is recognised regardless of whether its temps exist
        // yet: during phase A's probing (before phase B has run),
        // checked_prop_lo is still NULL for every candidate, including a
        // chain source that will go on to survive to the final round --
        // the poison scan only consults the boolean return here, never
        // *out_lo/*out_hi, so leaving them NULL is correct, not a failure.
        // Requiring temps up front would make phase A's own fixpoint unable
        // to ever accept a second-order chain, since round 1 always sees
        // the first round's newly-chain_src sources with no temps
        // allocated yet. By phase C (after phase B has run), any base.var
        // that is itself a final survivor -- which it must be, since a
        // chain source's checked_prop_chain_src can only be true if it
        // stabilised as non-unsafe by the round the fixpoint stopped at --
        // has real temps, so *out_lo/*out_hi come back non-NULL there.
        if (base.var->checked_prop_lo) {
            *out_lo = new_var_node(vm, base.var->checked_prop_lo, tok);
            add_type(vm, *out_lo);
            *out_hi = new_var_node(vm, base.var->checked_prop_hi, tok);
            add_type(vm, *out_hi);
        }
        *out_optional = base.var->checked_prop_optional;
        *out_nt_elem  = base.var->checked_prop_nt_elem;
        *out_nt       = *out_nt_elem != 0;
        *out_plain    = base.var->checked_prop_plain;
        return true;
    }
    return false;
}

// Walk 1 of propagate_checked_bounds() (#942: now a classifier, not just a
// poisoner) -- accumulates, on every registered candidate
// (Obj.checked_prop_candidate) reached by an ND_ASSIGN whose lhs is the
// candidate, whether the store is checked-rooted
// (checked_prop_scan_saw_rooted, plus checked_prop_scan_src_optional if the
// rooted source is itself only optionally trustworthy -- #941-chain
// transitivity) or not (checked_prop_scan_saw_unrooted) -- see the Obj
// field comments in cccc.h. checked_prop_unsafe is still set directly and
// unconditionally for an escaping address-of (outside the to_assign() RMW
// desugar): that remains a hard poison regardless of what else this
// candidate's stores look like, same as before #942. Mirrors
// objsize_poison_scan()'s shape and its host-stack-safety rationale
// (iterate ->next, recurse everywhere else) but NOT its "exempt the
// recorded init assignment" step: objsize's init_assign is inherently
// trusted (it's the very alloc-family call establishing the tracked size),
// whereas checked_prop_init_assign is only a *candidate* initializer whose
// own checked-rootedness still has to be proven here like any other
// assignment -- `int *q = malloc(...);` must count as an unrooted store to
// `q` right at its declaration, not skip straight to trusting it.
//
// #941: a *self-rooted* reassignment (`q = q + 1;`, rhs resolves back to `q`
// itself via find_checked_base()) is treated as neutral -- touches none of
// the three scratch fields -- rather than requiring `q` to also be its own
// declared/chained source. Sound because the snapshotted range (real or
// sentinel) is whatever `q` already held: the same fact that already lets
// `q++`/`q += k` (the to_assign() RMW desugar, which never even reaches
// this switch since it hides behind a temp) preserve the snapshot applies
// equally to an explicit self-store. Deliberately narrow: only
// `find_checked_base(rhs).var == node->lhs->var` (strictly the *lhs's own
// descent*, e.g. `q = q + 1` or `q = (int*)q`, not `q = 1 + q` --
// find_checked_base() only descends ->lhs, so the commuted form isn't
// recognised here either, consistent with how it already isn't for a
// declared source) counts; this can never be exploited as a mutual cycle
// (`q = r + 1; r = q + 1;` with no declared root) because neutrality only
// excuses `q`'s *own* self-referencing store, not a store rooted at a
// different candidate -- that still goes through checked_prop_source_bounds()
// and is decided by the fixpoint below. The node that IS
// checked_prop_init_assign is explicitly excluded: `int *q = q;` must still
// count as an unrooted store (there is no prior value of `q` to have been
// rooted in anything).
static void checked_prop_poison_scan(VirtualMachine *vm, Node *node) {
    for (; node; node = node->next) {
        switch (node->kind) {
            case ND_ASSIGN:
                if (node->lhs && node->lhs->kind == ND_VAR && node->lhs->var &&
                    node->lhs->var->checked_prop_candidate) {
                    Obj *var = node->lhs->var;
                    if (node != var->checked_prop_init_assign &&
                        find_checked_base(node->rhs).var == var)
                        break; // self-rooted -- neutral, see comment above
                    Node   *lo, *hi;
                    bool    src_optional, src_nt, src_plain;
                    int64_t src_nt_elem;
                    if (checked_prop_source_bounds(
                            vm, node->rhs, node->tok, &lo, &hi, &src_optional,
                            &src_nt, &src_nt_elem, &src_plain, NULL, NULL)) {
                        if (src_plain) {
                            // Rooted or not is decided at the end of the
                            // round, once it is known whether any checked
                            // store shares this local.
                            var->checked_prop_scan_saw_plain     = true;
                            var->checked_prop_scan_src_optional |= src_optional;
                            break;
                        }
                        var->checked_prop_scan_saw_rooted    = true;
                        var->checked_prop_scan_src_optional |= src_optional;
                        // #943: fold this store's NT fact into the round's
                        // running element size, order-independently within the
                        // round -- see checked_prop_scan_saw_non_nt's comment
                        // in cccc.h for why both directions need their own
                        // seen-flag rather than just comparing against `elem ==
                        // 0`. A non-checked-rooted store never reaches this
                        // branch at all (see the `else` below), so it can't
                        // contradict an NT fact already seen -- it just isn't
                        // live on that path.
                        if (src_nt) {
                            if (var->checked_prop_scan_nt_elem == 0)
                                var->checked_prop_scan_nt_elem = src_nt_elem;
                            else if (var->checked_prop_scan_nt_elem !=
                                     src_nt_elem)
                                var->checked_prop_scan_nt_conflict = true;
                            if (var->checked_prop_scan_saw_non_nt)
                                var->checked_prop_scan_nt_conflict = true;
                        } else {
                            var->checked_prop_scan_saw_non_nt = true;
                            if (var->checked_prop_scan_nt_elem != 0)
                                var->checked_prop_scan_nt_conflict = true;
                        }
                    } else {
                        var->checked_prop_scan_saw_unrooted = true;
                    }
                }
                break;
            case ND_ADDR:
                if (node->lhs && node->lhs->kind == ND_VAR && node->lhs->var &&
                    node->lhs->var->checked_prop_candidate &&
                    !node->is_rmw_temp_addr)
                    node->lhs->var->checked_prop_unsafe = true;
                break;
            default:
                break;
        }
        checked_prop_poison_scan(vm, node->lhs);
        checked_prop_poison_scan(vm, node->rhs);
        checked_prop_poison_scan(vm, node->cond);
        checked_prop_poison_scan(vm, node->then);
        checked_prop_poison_scan(vm, node->els);
        checked_prop_poison_scan(vm, node->init);
        checked_prop_poison_scan(vm, node->inc);
        checked_prop_poison_scan(vm, node->body);
        for (Node *a = node->args; a; a = a->next)
            checked_prop_poison_scan(vm, a);
    }
}

// #919: allocates a compiler-generated local for a checked-pointer
// propagation snapshot temp, linking it directly into `fn->locals` rather
// than going through new_lvar(). propagate_checked_bounds() runs well after
// the function's own leave_scope()/`fn->locals = vm->compiler.locals`
// snapshot, so by then vm->compiler.locals and fn->locals are no longer
// necessarily the same list head -- new_lvar() prepends to
// vm->compiler.locals, which assign_stack_offsets() (codegen.c) never
// reads; a temp allocated that way gets no stack slot at all (offset stays
// 0), the same class of bug as the documented $with_fn-locals hazard
// elsewhere in this codebase. No scope entry needed either: these temps are
// never looked up by name, only ever referenced directly through the Obj*
// stored on Obj.checked_prop_lo/hi.
static Obj *new_checked_prop_temp(VirtualMachine *vm, Obj *fn, Type *ty) {
    Obj *var      = new_var(vm, "", 0, ty);
    var->is_local = true;
    var->next     = fn->locals;
    fn->locals    = var;
    return var;
}

// #941 phase B: allocates checked_prop_lo/hi for every candidate that
// survived the phase-A fixpoint (checked_prop_init_assign set,
// checked_prop_unsafe still false). Must run to completion for the WHOLE
// candidate set before any rewriting (phase C) starts: a chained rewrite
// (`r = q + 1;`) reads `q`'s temps via checked_prop_source_bounds(), so `q`'s
// temps have to exist before `r`'s assignment is visited, and phase C's
// rewrite order is plain AST order with no dependency sort. Replaces #919's
// original lazy per-candidate allocation inside checked_prop_rewrite_assign()
// -- no longer viable once a later candidate's rewrite can need an earlier
// candidate's temps to already be there regardless of which one phase C's
// walk reaches first.
//
// Iterates fn->locals while new_checked_prop_temp() (called from inside
// this loop) prepends freshly allocated temps onto that same list -- safe
// here only because a prepend lands BEHIND the loop's current cursor `v`,
// so the walk never revisits or sees its own new temps (which have no
// checked_prop_init_assign set anyway, so they'd be skipped even if it
// did). Would need re-deriving if this ever switched to appending instead.
static void checked_prop_alloc_temps(VirtualMachine *vm, Obj *fn) {
    for (Obj *v = fn->locals; v; v = v->next)
        if (v->checked_prop_candidate && !v->checked_prop_unsafe &&
            !v->checked_prop_lo) {
            v->checked_prop_lo =
                new_checked_prop_temp(vm, fn, pointer_to(vm, ty_char));
            v->checked_prop_hi =
                new_checked_prop_temp(vm, fn, pointer_to(vm, ty_char));
        }
}

// #942: builds the raw integer constant (0 for hi, -1 for lo) that
// checked_prop_rewrite_assign() casts to `char*` the same way it casts a
// real LO/HI expression -- the "snapshot not currently rooted in a checked
// source" sentinel CHKRO recognises as a no-op (src/ops.c's chkr_common()).
// An inverted range ([lo=-1, hi=0), i.e. lo > hi) no legitimate bound can
// ever produce, so a classification bug that lets a genuinely-invalid
// snapshot reach plain CHKR instead of CHKRO fails LOUD (a spurious trap on
// correct code) rather than silently disabling the check -- deliberate, see
// the design note at propagate_checked_bounds() below.
static Node *checked_prop_sentinel_node(VirtualMachine *vm, bool is_hi,
                                        Token *tok) {
    return new_num(vm, is_hi ? 0 : -1, tok);
}

// Phase C's per-assignment rewrite: `q = E` (an ND_ASSIGN whose lhs is a
// surviving candidate) becomes `(__q_lo = (char*)LO, __q_hi = (char*)HI),
// (q = E)` -- two compiler-generated pointer_to(char) locals snapshotting
// LO/HI BEFORE `q` itself is overwritten, so reading `p`/`n` for the
// snapshot always sees their pre-assignment values. The snapshot temps are
// allocated up front for every survivor by checked_prop_alloc_temps()
// (phase B), not lazily here.
//
// #942: when this store's rhs IS checked-rooted, LO/HI are the source's own
// absolute bounds, computed exactly as a direct access through the source
// would be -- unchanged from #919/#941. When it is NOT (only possible for
// an "OPT" survivor, Obj.checked_prop_optional -- phase A's classifier
// proved every store on a "FULL" survivor is rooted, so this can't happen
// there), LO/HI are instead the checked_prop_sentinel_node() pair: this
// store is refreshing the snapshot to "not currently valid," exactly
// mirroring what a direct, un-propagated access through `q` on this same
// path would see (nothing, since `q` isn't itself checked-rooted right
// now) -- the runtime mechanism that makes CHKRO path-sensitive without any
// CFG/join analysis.
//
// Mutates `assign_node` in place (copies its old contents into a fresh
// node, then overwrites the original with the wrapping ND_COMMA) so every
// existing parent pointer into it stays valid; ->next is preserved on the
// copy, not on the wrapper, so the statement chain is unaffected.
//
// #947: `fn` is the enclosing function, passed through to
// checked_prop_source_bounds() so a kind-1 source with a non-trivial member
// object expression (e.g. `q = arr[k].p;`) hoists `arr[k]` into a single
// new_checked_prop_temp() temp rather than re-evaluating `k` once for `lo`
// and again for `hi`. This is the actual rewrite (not a probe), so the
// hoist is requested unconditionally here.
static void checked_prop_rewrite_assign(VirtualMachine *vm, Obj *fn,
                                        Node *assign_node) {
    Obj    *var = assign_node->lhs->var;
    Node   *lo, *hi, *src_obj_init;
    bool    src_optional, src_nt, src_plain;
    int64_t src_nt_elem;
    // A checked-return call source is evaluated into its result local first;
    // the snapshot below reads that local's bounds after the call.
    Node *cr_store = NULL;
    Node *croot    = checked_call_root(assign_node->rhs);
    if (croot)
        cr_store = split_checked_call_result(vm, croot, fn);
    else if ((croot = checked_cast_root(assign_node->rhs)))
        cr_store = split_checked_cast(vm, croot);
    // NT-ness isn't needed here -- walk 3 (checked_prop_attach_scan())
    // consults the candidate's own frozen checked_prop_nt_elem directly,
    // rather than recomputing per-store like lo/hi -- so src_nt/src_nt_elem
    // are discarded.
    bool is_source = checked_prop_source_bounds(
        vm, assign_node->rhs, assign_node->tok, &lo, &hi, &src_optional,
        &src_nt, &src_nt_elem, &src_plain, fn, &src_obj_init);
    // A plain-array store into a local that also holds checked values is
    // unrooted (the classifier made that local OPT).
    if (is_source && src_plain && !var->checked_prop_plain)
        is_source = false;
    Token *tok = assign_node->tok;
    if (is_source && lo && hi) {
        // Rooted store -- unchanged #919/#941 behavior below; lo/hi are
        // already the source's own bounds, nothing more to do here.
    } else if (!is_source && var->checked_prop_optional) {
        // #942: non-rooted store into an OPT survivor -- refresh the
        // snapshot to the sentinel rather than leaving it stale.
        lo = checked_prop_sentinel_node(vm, false, tok);
        hi = checked_prop_sentinel_node(vm, true, tok);
    } else {
        // `!lo || !hi` (is_source true but bounds unusable) covers the
        // theoretical case where checked_prop_source_bounds() recognises a
        // #941 chain source (kind 2) whose own temps somehow still aren't
        // allocated by the time phase C runs; `!is_source` on a FULL
        // survivor covers phase A somehow having misclassified. Neither
        // should happen -- phase A already proved every surviving FULL
        // candidate's assignments are checked-rooted, and phase B
        // pre-allocates temps for every final survivor before phase C
        // runs -- but if it ever did, walk 3 (checked_prop_attach_scan())
        // would otherwise still attach those (never-stored) temps to every
        // deref through `var` even though this particular store didn't
        // refresh them. Poison here so that fails closed instead of
        // feeding CHKR/CHKRO a garbage range.
        var->checked_prop_unsafe = true;
        if (cr_store) {
            assign_node->rhs = new_binary(vm, ND_COMMA, cr_store,
                                          assign_node->rhs, assign_node->tok);
            add_type(vm, assign_node->rhs);
        }
        return;
    }

    Node *store_lo =
        new_binary(vm, ND_ASSIGN, new_var_node(vm, var->checked_prop_lo, tok),
                   new_cast(vm, lo, pointer_to(vm, ty_char)), tok);
    add_type(vm, store_lo);
    Node *store_hi =
        new_binary(vm, ND_ASSIGN, new_var_node(vm, var->checked_prop_hi, tok),
                   new_cast(vm, hi, pointer_to(vm, ty_char)), tok);
    add_type(vm, store_hi);
    Node *snapshot = new_binary(vm, ND_COMMA, store_lo, store_hi, tok);
    add_type(vm, snapshot);
    // #947: if the source needed a hoisted object-expression temp, its init
    // has to run before BOTH store_lo and store_hi read it -- splice it in
    // ahead of the existing pair rather than appending, same program point
    // the un-hoisted lo/hi expressions were already evaluated at.
    if (src_obj_init) {
        snapshot = new_binary(vm, ND_COMMA, src_obj_init, snapshot, tok);
        add_type(vm, snapshot);
    }
    if (cr_store) {
        snapshot = new_binary(vm, ND_COMMA, cr_store, snapshot, tok);
        add_type(vm, snapshot);
    }

    Node *orig  = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    *orig       = *assign_node;
    orig->next  = NULL;

    Node *comma = new_binary(vm, ND_COMMA, snapshot, orig, tok);
    add_type(vm, comma);
    Node *saved_next  = assign_node->next;
    *assign_node      = *comma;
    assign_node->next = saved_next;
}

// Walk 2: finds every ND_ASSIGN targeting a surviving candidate
// (checked_prop_candidate set, checked_prop_unsafe still false after the
// phase-A fixpoint) and rewrites it via checked_prop_rewrite_assign() --
// #942: this now includes a NON-rooted store into an "OPT" survivor
// (Obj.checked_prop_optional), which checked_prop_rewrite_assign() itself
// recognises and rewrites into a sentinel-store rather than a real
// snapshot. Structurally identical to checked_prop_poison_scan() -- separate
// function rather than folded into phase A because phase A must finish
// deciding EVERY candidate's fate (a later assignment in the same function
// can still affect a candidate whose earlier assignment already looked
// fine) before this phase commits to rewriting any of them.
// #947: `fn` is threaded down to checked_prop_rewrite_assign() (and from
// there to checked_prop_source_bounds()'s hoist temp allocator) -- needed
// because this whole walk runs after the function's own
// leave_scope()/`fn->locals = vm->compiler.locals` snapshot, so a hoist
// temp must be linked directly into `fn->locals` via
// new_checked_prop_temp(), same as checked_prop_lo/hi themselves.
static void checked_prop_rewrite_scan(VirtualMachine *vm, Obj *fn, Node *node) {
    for (; node; node = node->next) {
        if (node->kind == ND_ASSIGN && node->lhs && node->lhs->kind == ND_VAR &&
            node->lhs->var && node->lhs->var->checked_prop_candidate &&
            !node->lhs->var->checked_prop_unsafe) {
            // #941: mirror checked_prop_poison_scan()'s self-rooted-neutral
            // rule -- `q = q + 1;` was never poisoned nor proven by phase A,
            // so it must not be rewritten into `q_lo = q_lo, q_hi = q_hi`
            // here either (checked_prop_source_bounds() wouldn't even find a
            // source for it: `q` is unchecked and, until proven otherwise,
            // not yet a chain source of itself).
            if (node != node->lhs->var->checked_prop_init_assign &&
                find_checked_base(node->rhs).var == node->lhs->var) {
                checked_prop_rewrite_scan(vm, fn, node->rhs);
                continue;
            }
            // Recurse into the rhs FIRST (catches a nested candidate
            // assignment inside E, e.g. a comma or call argument) before
            // rewriting `node` in place -- checked_prop_rewrite_assign()
            // turns `node` into an ND_COMMA wrapper whose ->rhs is a COPY of
            // this same ND_ASSIGN (see its comment), which would otherwise
            // still match this branch on the generic descent below and
            // rewrite forever.
            checked_prop_rewrite_scan(vm, fn, node->rhs);
            checked_prop_rewrite_assign(vm, fn, node);
            continue; // node is now the wrapper; its parts are already scanned
        }

        checked_prop_rewrite_scan(vm, fn, node->lhs);
        checked_prop_rewrite_scan(vm, fn, node->rhs);
        checked_prop_rewrite_scan(vm, fn, node->cond);
        checked_prop_rewrite_scan(vm, fn, node->then);
        checked_prop_rewrite_scan(vm, fn, node->els);
        checked_prop_rewrite_scan(vm, fn, node->init);
        checked_prop_rewrite_scan(vm, fn, node->inc);
        checked_prop_rewrite_scan(vm, fn, node->body);
        for (Node *a = node->args; a; a = a->next)
            checked_prop_rewrite_scan(vm, fn, a);
    }
}

// Walk 3: attaches the snapshot temps to every still-unchecked ND_DEREF
// rooted at a surviving candidate. Runs after walk 2 so it doesn't matter
// that walk 2 already rewrote this candidate's assignments into comma
// expressions elsewhere in the same tree -- a deref's own address
// expression is untouched by that rewrite. checked_access_size/pointee type
// come from the access site's own address expression (`node->lhs->ty`), NOT
// from the propagation source -- a cast (`char *q = (char*)p;`) legitimately
// changes what a later `q[i]` accesses.
//
// #943: checked_nt_terminator IS now propagated, when the candidate's own
// frozen checked_prop_nt_elem says every rooted store this candidate ever
// saw was ntarray-rooted with the SAME pointee element size, AND this
// particular access's own checked_access_size matches that size too (a cast
// through the propagated pointer, e.g. `int *q = (int*)s;` off a `char`
// ntarray source, changes what's actually accessed at hi - elem_size, so a
// size mismatch must not attach the guard -- see compute_checked_bounds()'s
// own identical size-derived guard for the direct-access case). Also gated
// on checked_nt_pointee_supported(), matching compute_checked_bounds() (#939).
// Mirrors a deref's just-attached bounds onto its RMW store node.
static void checked_attach_mirror(VirtualMachine *vm, Node *node) {
    Node *mirror = node->checked_rmw_mirror;
    if (!mirror)
        return;
    if (mirror->kind == ND_DEREF) {
        mirror->checked_bounds_lo =
            clone_bounds_node(vm, node->checked_bounds_lo);
        mirror->checked_bounds_obj_init =
            clone_bounds_node(vm, node->checked_bounds_obj_init);
        mirror->checked_bounds_optional = node->checked_bounds_optional;
    }
    // ND_CAS (the `_Atomic` desugar) -- lo is deliberately omitted, matching
    // to_assign()'s own direct-access ND_CAS copy (it has no gen_addr-driven
    // CHKR path to feed lo into).
    mirror->checked_bounds_hi = clone_bounds_node(vm, node->checked_bounds_hi);
    mirror->checked_access_size   = node->checked_access_size;
    mirror->checked_nt_terminator = node->checked_nt_terminator;
}

static void checked_prop_attach_scan(VirtualMachine *vm, Obj *fn, Node *node) {
    for (; node; node = node->next) {
        // Children first: a row access reads the bounds its inner row deref
        // (`p[0]` in `p[0][j]`) receives here.
        checked_prop_attach_scan(vm, fn, node->lhs);
        checked_prop_attach_scan(vm, fn, node->rhs);
        checked_prop_attach_scan(vm, fn, node->cond);
        checked_prop_attach_scan(vm, fn, node->then);
        checked_prop_attach_scan(vm, fn, node->els);
        checked_prop_attach_scan(vm, fn, node->init);
        checked_prop_attach_scan(vm, fn, node->inc);
        checked_prop_attach_scan(vm, fn, node->body);
        for (Node *a = node->args; a; a = a->next)
            checked_prop_attach_scan(vm, fn, a);

        if (node->kind != ND_DEREF || node->checked_bounds_lo)
            continue;
        CheckedBase base = find_checked_base(node->lhs);
        if (base.var && base.var->checked_prop_candidate &&
            !base.var->checked_prop_unsafe && !base.var->checked_prop_plain &&
            base.var->checked_prop_lo) {
            Node *lo = new_var_node(vm, base.var->checked_prop_lo, node->tok);
            add_type(vm, lo);
            Node *hi = new_var_node(vm, base.var->checked_prop_hi, node->tok);
            add_type(vm, hi);
            node->checked_bounds_lo = lo;
            node->checked_bounds_hi = hi;
            // #942: OPT survivors' snapshot temps can legitimately hold
            // the sentinel range at runtime (any path that hasn't yet
            // executed a checked-rooted store), so this deref must go
            // through CHKRO, not plain CHKR -- see emit_chkr()'s
            // comment in codegen.c.
            node->checked_bounds_optional = base.var->checked_prop_optional;
            Type *access_ty               = node->lhs->ty;
            Type *pointee_ty              = access_ty ? access_ty->base : NULL;
            node->checked_access_size     = checked_access_bytes(pointee_ty);
            if (base.var->checked_prop_nt_elem != 0 &&
                node->checked_access_size == base.var->checked_prop_nt_elem &&
                checked_nt_pointee_supported(pointee_ty))
                node->checked_nt_terminator = true;
            // #943: to_assign()'s RMW desugar built its store node before
            // this pass ran, so mirror the attached bounds across.
            checked_attach_mirror(vm, node);
        } else if (base.row && !node_has_side_effects(base.obj)) {
            // An element of a row reached through a propagated pointer.
            Type *pty = node->lhs->ty;
            compute_checked_bounds(
                vm, base, pty, node->tok, &node->checked_bounds_lo,
                &node->checked_bounds_hi, &node->checked_nt_terminator,
                &node->checked_bounds_obj_init, fn);
            if (node->checked_bounds_lo && node->checked_bounds_hi) {
                node->checked_access_size = checked_access_bytes(pty->base);
                checked_attach_mirror(vm, node);
            }
        }
    }
}

// #942 phase B': prepends `(__q_lo = (char*)-1, __q_hi = (char*)0);` onto
// the FRONT of the function's own top-level statement list
// (fn->body->body -- the same list resolve_return_type() walks to append
// an implicit `return 0;`) for every OPT survivor. This is what makes
// CHKRO sound before ANY store to `q` has run yet on the current execution:
// an uninitialized OPT candidate (`int *q;`), a loop back-edge that hasn't
// looped yet, or a `goto` past `q`'s own declaration would otherwise leave
// checked_prop_lo/hi holding whatever garbage the stack slot last had. For
// a FULL candidate that's the pre-existing #919-era residual (docs/
// safety.md): `q` itself is equally indeterminate at that point, so a
// direct unchecked deref through `q` would already be undefined behavior,
// and this pass doesn't newly introduce a check reading garbage -- FULL
// deliberately gets no sentinel init, matching #919/#941 exactly. An OPT
// candidate is different: by definition (checked_prop_optional) it *can*
// be reached without a prior rooted store on a real, well-defined path
// (the ticket's own `if (c) q = p;` example, where `q` DOES hold a
// well-defined value either way) -- so its temps must start in a
// well-defined "invalid" state for that path's check to safely no-op
// rather than read garbage. Order among OPT survivors' inits doesn't
// matter (each writes its own disjoint pair of temps), so a simple
// prepend loop suffices; `fn->body->tok` (the function's own opening-brace
// token) is used as every synthesized node's representative token, same
// as elsewhere in this pass reaches for `assign_node->tok`/`node->tok`.
static void checked_prop_init_optional_sentinels(VirtualMachine *vm, Obj *fn) {
    Token *tok = fn->body->tok;
    for (Obj *v = fn->locals; v; v = v->next) {
        if (!(v->checked_prop_candidate && !v->checked_prop_unsafe &&
              v->checked_prop_optional))
            continue;
        Node *store_lo =
            new_binary(vm, ND_ASSIGN, new_var_node(vm, v->checked_prop_lo, tok),
                       new_cast(vm, checked_prop_sentinel_node(vm, false, tok),
                                pointer_to(vm, ty_char)),
                       tok);
        add_type(vm, store_lo);
        Node *store_hi =
            new_binary(vm, ND_ASSIGN, new_var_node(vm, v->checked_prop_hi, tok),
                       new_cast(vm, checked_prop_sentinel_node(vm, true, tok),
                                pointer_to(vm, ty_char)),
                       tok);
        add_type(vm, store_hi);
        Node *init =
            new_unary(vm, ND_EXPR_STMT,
                      new_binary(vm, ND_COMMA, store_lo, store_hi, tok), tok);
        add_type(vm, init->lhs);
        init->next     = fn->body->body;
        fn->body->body = init;
    }
}

// Entry point (#919), called from the same three function()/block-literal
// tail sites as resolve_objsize_queries()/mark_addr_escapes(), immediately
// after mark_addr_escapes(). Gated on --checked-pointers at PARSE time
// (unlike the rest of the checked-pointer machinery, which populates its
// fields unconditionally and gates only CHKR's emission at codegen) -- the
// snapshot temps cost real stack slots and stores per propagating
// assignment, which is only worth paying when something might actually
// enforce them. This gate reads `vm->flags` at every registration/pass
// call site, not a value frozen once at the start of the file: verified
// empirically that a `#pragma cccc config(checked_pointers = true)`
// anywhere in the translation unit -- even textually after every
// declaration it affects -- still takes effect, because #pragma cccc
// config() is resolved during preprocessing (src/preprocess.c), a pass
// that runs to completion before parse() ever sees a token. So, unlike a
// hypothetical token-interleaved pragma, there is no "must precede the
// declaration" ordering caveat to document here.
//
// #942: classify every candidate touched by a non-checked-rooted assignment,
// an escaping address-of, or (via Obj.is_captured, already computed by
// mark_nested_captures()) an access from a nested function's body, as
// exactly one of NONE (no checked-rooted store ever reached, or an escape
// -- unchanged #919 poisoning outcome, no temps, no checks), FULL (every
// store checked-rooted -- unchanged #919/#941 outcome, plain CHKR,
// byte-identical codegen), or OPT (a mix of rooted and non-rooted stores,
// or a rooted store whose own source is itself OPT). OPT is #942's new
// case: it materializes the "is q trustworthy right now" fact as a runtime
// value instead of a static join, so it's exact per EXECUTED path with no
// CFG/join/fixpoint-over-a-graph needed at all -- every store (rooted or
// not) refreshes the snapshot to either the real bounds or an explicit
// "invalid" sentinel, so whichever store actually ran on this execution is
// the one whose snapshot (valid or not) is live, and CHKRO (src/ops.c)
// checks only when it's valid. This is what lets
// `q = malloc(...); if (c) q = p; q[i];` -- the ticket's own motivating
// case, which a conservative static join would still leave entirely
// unchecked -- be enforced exactly on the paths where `q` really does hold
// `p`'s value.
//
// #941: a candidate that is itself only propagated (never declared checked)
// CAN be a valid propagation source for another candidate, as of a chained
// source's own most recent round (Obj.checked_prop_chain_src) -- this is the
// phase-A fixpoint loop below; #942 additionally threads OPT-ness through
// the same chain (Obj.checked_prop_optional), so a candidate chained from an
// OPT source is itself OPT even if its own single store is, in isolation,
// checked-rooted (see checked_prop_source_bounds()'s `*out_optional`).
// checked_nt_terminator is never propagated across an assignment, chained,
// OPT or not (the attach phase leaves it false unconditionally) -- CHKNT's
// own follow-up ticket, distinct from to_assign()'s own-expression RMW copy
// (#937, see checked_prop_attach_scan()'s comment above). A further kind of
// follow-up, orthogonal to all of the above: this pass never verifies an
// assignment INTO an already-declared-checked target against the source's
// bounds (that's #944's verify_checked_assign_bounds(), a sibling pass); it
// only ever widens trust for a previously-unchecked target.
#define CHECKED_PROP_MAX_ROUNDS 32

// Phase A alone: classifies every candidate (NONE / FULL / OPT) without
// allocating temps or rewriting anything, so it is safe to run with
// --checked-pointers off (resolve_pending_dynamic_casts() does).
static void checked_prop_classify(VirtualMachine *vm, Obj *fn) {
    // #941/#942 phase A: iterate checked_prop_poison_scan() to a fixpoint
    // over Obj.checked_prop_chain_src/checked_prop_optional, both seeded
    // false for every candidate -- so round 0 reproduces #919's original
    // single-pass behaviour exactly for chain-source recognition, only
    // declared-checked sources (checked_base_is_declared()) are accepted as
    // a source in round 0. Each later round additionally trusts whichever
    // candidates survived as chained sources as of the END of the PREVIOUS
    // round only -- checked_prop_chain_src/checked_prop_optional are frozen
    // snapshots, never mutated mid-round (see their comments in cccc.h), so
    // within one round's scan the accepted-source set is fixed and the
    // round's result depends only on that snapshot, not on AST visit order.
    // Seeding from below (declared sources only, then growing) rather than
    // from above (assume everything propagates, then remove failures) is
    // what makes an unrooted cycle like `q = r + 1; r = q + 1;` never
    // validate itself: neither `q` nor `r` is ever a member of any round's
    // frozen source set, so neither can ever clear the other to a survivor.
    //
    // The accepted-source set is monotone non-decreasing round over round (a
    // superset of trusted sources can only turn an equal-or-larger set of
    // candidates into survivors), and once a candidate survives it can only
    // move from FULL to OPT, never back -- both `survivors` and
    // `optional_survivors` are therefore monotone non-decreasing counts, so
    // comparing the PAIR between consecutive rounds is a valid, cheap
    // fixpoint test: equal pair with a monotone superset relation implies
    // the sets are actually equal, not just the same size. Bounded by the
    // number of candidates in the function in the worst case (one
    // additional chain link, or one additional FULL->OPT flip, closes per
    // round); CHECKED_PROP_MAX_ROUNDS is a defensive cap on top of that.
    // Stopping early on the chain-source dimension is sound the same way it
    // always was (leaves some very long chains unpropagated); stopping
    // early while a candidate's true OPT-ness hasn't yet been discovered is
    // handled explicitly below by forcing every remaining survivor to OPT
    // -- always sound (OPT only ever costs precision, never correctness),
    // and it's what keeps the round cap from ever becoming a FALSE-TRAP
    // source (a candidate wrongly left classified FULL when it should have
    // been OPT would read the sentinel through plain CHKR and trap on
    // correct code -- see checked_prop_sentinel_node()'s comment).
    //
    // Placeholder inefficiency (follow-up ticket, same class as
    // checked_base_self_expr()'s documented one): every round re-runs
    // checked_prop_poison_scan() over the WHOLE function body, so a
    // declared source's bound expression tree (compute_checked_bounds())
    // gets rebuilt and discarded from scratch each round even though a
    // declared source's own bounds never change round to round -- only
    // whether a chained candidate's source is trusted yet does. Harmless in
    // practice (arena-allocated, and the loop normally exits within a
    // couple of rounds for realistic chain depths), but a boolean-only
    // probe path (skip building *out_lo/*out_hi entirely when the caller is
    // checked_prop_poison_scan(), which only reads the return value) would
    // avoid the rebuild.
    int  prev_survivors = -1, prev_optional = -1, prev_nt = -1, prev_plain = -1;
    bool converged = false;
    for (int round = 0; round < CHECKED_PROP_MAX_ROUNDS; round++) {
        for (Obj *v = fn->locals; v; v = v->next)
            if (v->checked_prop_candidate) {
                v->checked_prop_unsafe            = false;
                v->checked_prop_scan_saw_rooted   = false;
                v->checked_prop_scan_saw_unrooted = false;
                v->checked_prop_scan_src_optional = false;
                v->checked_prop_scan_saw_plain    = false;
                // #943: reset alongside the other per-round scratch fields
                // -- see their comments in cccc.h.
                v->checked_prop_scan_nt_elem     = 0;
                v->checked_prop_scan_saw_non_nt  = false;
                v->checked_prop_scan_nt_conflict = false;
            }
        checked_prop_poison_scan(vm, fn->body);
        // is_captured is set by mark_nested_captures(), which -- for every
        // nested function textually inside this one -- always finishes
        // running before this function's own tail (and therefore this call)
        // runs; see propagate_checked_bounds()'s call sites. Reads
        // fn->locals rather than vm->compiler.locals: both hold the same
        // list at every call site today (fn->locals is snapshotted from it
        // just before leave_scope(), and nothing between there and here
        // touches vm->compiler.locals), but taking `fn` as a parameter and
        // reading its own field is not hostage to that ordering staying
        // true after some future refactor.
        int survivors = 0, optional_survivors = 0, nt_survivors = 0,
            plain_survivors = 0;
        for (Obj *v = fn->locals; v; v = v->next)
            if (v->checked_prop_candidate) {
                // #942: a candidate with no checked-rooted store at all
                // (an escaping address-of already set checked_prop_unsafe
                // directly during the scan) is NONE, same outcome as
                // #919's original "poisoned" -- there is nothing to ever
                // snapshot. Otherwise it's a survivor: FULL if every store
                // reached was rooted, OPT if any wasn't (or if a rooted
                // store's own source was itself OPT).
                // Plain-array stores root a local only when no checked store
                // shares it; otherwise they are unrooted, making it OPT.
                bool plain_only = v->checked_prop_scan_saw_plain &&
                                  !v->checked_prop_scan_saw_rooted;
                if (v->checked_prop_scan_saw_plain && !plain_only)
                    v->checked_prop_scan_saw_unrooted = true;
                if (!v->checked_prop_scan_saw_rooted && !plain_only)
                    v->checked_prop_unsafe = true;
                v->checked_prop_plain = plain_only;
                if (v->is_captured)
                    v->checked_prop_unsafe = true;
                v->checked_prop_chain_src = !v->checked_prop_unsafe;
                if (!v->checked_prop_unsafe) {
                    v->checked_prop_optional =
                        v->checked_prop_scan_saw_unrooted ||
                        v->checked_prop_scan_src_optional;
                    survivors++;
                    if (v->checked_prop_optional)
                        optional_survivors++;
                    // #943: this round's terminator-slot fact -- 0 unless
                    // every rooted store this round agreed on the same
                    // ntarray element size (checked_prop_scan_nt_conflict
                    // clear). Recomputed every round exactly like
                    // checked_prop_optional, since a chained source's own
                    // NT-ness (kind 2 of checked_prop_source_bounds()) can
                    // only be trusted once IT stabilised as of the previous
                    // round -- same reasoning as checked_prop_chain_src.
                    v->checked_prop_nt_elem =
                        v->checked_prop_scan_nt_conflict
                            ? 0
                            : v->checked_prop_scan_nt_elem;
                    if (v->checked_prop_nt_elem != 0)
                        nt_survivors++;
                    if (v->checked_prop_plain)
                        plain_survivors++;
                } else {
                    v->checked_prop_nt_elem = 0;
                    v->checked_prop_plain   = false;
                }
            }
        if (survivors == prev_survivors &&
            optional_survivors == prev_optional && nt_survivors == prev_nt &&
            plain_survivors == prev_plain) {
            converged = true;
            break;
        }
        prev_survivors = survivors;
        prev_optional  = optional_survivors;
        prev_nt        = nt_survivors;
        prev_plain     = plain_survivors;
    }
    // Defensive: if the cap was hit before convergence, force every
    // remaining survivor to OPT rather than trusting a possibly-incomplete
    // FULL classification -- see the comment above for why this direction
    // (never the reverse) is the only sound choice. #943: also clear
    // checked_prop_nt_elem for the same reason -- an incompletely-discovered
    // NT fact could otherwise attach CHKNT to a store that, with one more
    // round, would have turned out to conflict; losing the NT guard only
    // costs precision (falls back to plain CHKR/CHKRO), never soundness.
    if (!converged)
        for (Obj *v = fn->locals; v; v = v->next)
            if (v->checked_prop_candidate && !v->checked_prop_unsafe) {
                v->checked_prop_optional = true;
                v->checked_prop_nt_elem  = 0;
            }
}

void propagate_checked_bounds(VirtualMachine *vm, Obj *fn) {
    if (!(vm->flags & CCCC_CHECKED_BOUNDS))
        return;
    checked_prop_classify(vm, fn);

    // #941 phase B: allocate every survivor's snapshot temps up front (see
    // checked_prop_alloc_temps()'s comment for why this can no longer be
    // lazy once chaining exists).
    checked_prop_alloc_temps(vm, fn);
    // #942 phase B': seed every OPT survivor's temps with the sentinel at
    // function entry, before phase C's rewrite touches any of them.
    checked_prop_init_optional_sentinels(vm, fn);
    // Phase C: rewrite every surviving candidate's assignments (rooted ones
    // into a real snapshot, non-rooted OPT ones into a sentinel refresh),
    // then attach the resulting snapshots to every deref reached through a
    // survivor.
    checked_prop_rewrite_scan(vm, fn, fn->body);
    checked_prop_attach_scan(vm, fn, fn->body);
}

// #944 per-assignment rewrite: `q = E` where `q` is a declared-checked
// target and `E` is rooted at a declared-checked source becomes
// `(__slo = (char*)SRC_LO, __shi = (char*)SRC_HI), (q = E)` -- the source's
// bounds are snapshotted BEFORE the store into two fresh compiler-generated
// pointer_to(char) temps (reusing new_checked_prop_temp(), same shape as
// #919's own snapshot temps), and the ND_ASSIGN itself (the untouched `orig`
// copy, exactly like checked_prop_rewrite_assign()'s wrapping trick) is
// stamped with all four CHKAB operands: checked_assign_src_lo/hi are var
// reads of the two temps just stored, checked_assign_dst_lo/hi are the
// target's OWN bounds expressions, built fresh here and left for codegen to
// evaluate AFTER the store (see the ordering note above
// verify_checked_assign_bounds()). Mutates `assign_node` in place, same
// parent-pointer-preserving technique as checked_prop_rewrite_assign().
//
// #947: `src_obj_init`/`dst_obj_init` (both NULL-able) are the two possible
// member-object-expression hoists -- `src_obj_init` splices into the
// pre-store snapshot comma below, the same program point src_lo/src_hi are
// already evaluated at. `dst_obj_init` CANNOT go there: dst_lo/dst_hi are
// deliberately evaluated AFTER the store (see the comment above), so a
// pre-store `t = &obj` would snapshot the target's object expression before
// an rhs that might still change it (e.g. `arr[k].p = f_that_bumps_k();`).
// Instead it's carried on Node.checked_assign_dst_obj_init (src/cccc.h) for
// codegen's post-store CHKAB site to re-run immediately before dst_lo/hi.
static void verify_checked_assign_rewrite(VirtualMachine *vm, Obj *fn,
                                          Node *assign_node, Node *dst_lo,
                                          Node *dst_hi, Node *src_lo,
                                          Node *src_hi, Node *src_obj_init,
                                          Node *dst_obj_init, Node *cr_store) {
    Token *tok     = assign_node->tok;
    Obj   *slo_var = new_checked_prop_temp(vm, fn, pointer_to(vm, ty_char));
    Obj   *shi_var = new_checked_prop_temp(vm, fn, pointer_to(vm, ty_char));

    Node  *store_slo =
        new_binary(vm, ND_ASSIGN, new_var_node(vm, slo_var, tok),
                   new_cast(vm, src_lo, pointer_to(vm, ty_char)), tok);
    add_type(vm, store_slo);
    Node *store_shi =
        new_binary(vm, ND_ASSIGN, new_var_node(vm, shi_var, tok),
                   new_cast(vm, src_hi, pointer_to(vm, ty_char)), tok);
    add_type(vm, store_shi);
    Node *snapshot = new_binary(vm, ND_COMMA, store_slo, store_shi, tok);
    add_type(vm, snapshot);
    if (src_obj_init) {
        snapshot = new_binary(vm, ND_COMMA, src_obj_init, snapshot, tok);
        add_type(vm, snapshot);
    }
    if (cr_store) {
        snapshot = new_binary(vm, ND_COMMA, cr_store, snapshot, tok);
        add_type(vm, snapshot);
    }

    Node *orig = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    *orig      = *assign_node;
    orig->next = NULL;
    orig->checked_assign_dst_lo       = dst_lo;
    orig->checked_assign_dst_hi       = dst_hi;
    orig->checked_assign_dst_obj_init = dst_obj_init;
    orig->checked_assign_src_lo       = new_var_node(vm, slo_var, tok);
    add_type(vm, orig->checked_assign_src_lo);
    orig->checked_assign_src_hi = new_var_node(vm, shi_var, tok);
    add_type(vm, orig->checked_assign_src_hi);

    Node *comma = new_binary(vm, ND_COMMA, snapshot, orig, tok);
    add_type(vm, comma);
    Node *saved_next  = assign_node->next;
    *assign_node      = *comma;
    assign_node->next = saved_next;
}

// #944 tree walk: finds every ND_ASSIGN whose lhs is a declared-checked
// target (ND_VAR or a side-effect-free ND_MEMBER) with a resolvable bounds
// form, and whose rhs is rooted at a declared-checked source with a
// resolvable bounds form (a declared or FULL propagated source -- see
// verify_checked_assign_bounds()'s scope comment), and rewrites it via
// verify_checked_assign_rewrite(). Structurally the same shape as
// checked_prop_rewrite_scan() -- recurse into rhs first, then rewrite, then
// `continue` past the resulting ND_COMMA wrapper's own copy of this same
// ND_ASSIGN so the generic descent below doesn't match and rewrite it again.
static void verify_checked_assign_scan(VirtualMachine *vm, Obj *fn,
                                       Node *node) {
    for (; node; node = node->next) {
        if (node->kind == ND_ASSIGN && node->lhs &&
            (node->lhs->kind == ND_VAR || node->lhs->kind == ND_MEMBER)) {
            CheckedBase dst_base = find_checked_base(node->lhs);
            // #486: never verify the store INTO an [[cccc::assume]] cast's
            // own desugared temp -- taking the claimed bounds on trust
            // without a runtime check is the entire point of `assume`; a
            // CHKAB pair here would defeat it.
            if (checked_base_is_declared(dst_base) &&
                !(dst_base.mem && node_has_side_effects(dst_base.obj)) &&
                !(dst_base.var &&
                  dst_base.var->checked_cast_kind == CC_ASSUME)) {
                Type *dst_pty =
                    checked_base_type(dst_base);
                Node *dst_lo, *dst_hi;
                bool  dst_nt;
                // #947: probe with NULL first -- a failed test below (no
                // usable dst/src bounds) must not allocate a hoist temp for
                // a rewrite that never happens. Only recomputed with the
                // hoist enabled once the rewrite is actually committed.
                compute_checked_bounds(vm, dst_base, dst_pty, node->tok,
                                       &dst_lo, &dst_hi, &dst_nt, NULL, NULL);
                if (dst_lo && dst_hi) {
                    Node   *src_lo, *src_hi;
                    bool    src_optional = false, src_nt, src_plain;
                    int64_t src_nt_elem;
                    // A checked-return call source is evaluated into its
                    // result local first; the source bounds below are that
                    // local's, snapshotted after the call.
                    Node *cr_store = NULL;
                    Node *croot    = checked_call_root(node->rhs);
                    if (croot)
                        cr_store = split_checked_call_result(vm, croot, fn);
                    // A bounds-cast source is split the same way, its store
                    // scanned whole so a `dynamic` cast's own check still runs.
                    Node *cv_store = NULL;
                    Node *croot_cv = croot ? NULL : checked_cast_root(node->rhs);
                    if (croot_cv) {
                        cv_store = split_checked_cast(vm, croot_cv);
                        cr_store = cv_store;
                    }
                    // An OPT propagated source can hold the sentinel range,
                    // which CHKAB would read as a real (always-failing)
                    // bound, so only declared and FULL sources are verified.
                    bool is_source = checked_prop_source_bounds(
                        vm, node->rhs, node->tok, &src_lo, &src_hi,
                        &src_optional, &src_nt, &src_nt_elem, &src_plain, NULL,
                        NULL);
                    if (is_source && !src_optional && src_lo && src_hi) {
                        verify_checked_assign_scan(vm, fn, node->rhs);
                        // The split moved the call, with its argument
                        // stores, out of the rhs.
                        if (cv_store)
                            verify_checked_assign_scan(vm, fn, cv_store);
                        else if (cr_store)
                            verify_checked_assign_scan(vm, fn, cr_store->rhs);
                        // #947: recompute both sides now with the hoist
                        // allocator live -- the probe above only proved
                        // this rewrite is going to happen.
                        Node   *dst_obj_init, *src_obj_init;
                        bool    dst_nt2, src_nt2;
                        int64_t src_nt_elem2;
                        compute_checked_bounds(vm, dst_base, dst_pty, node->tok,
                                               &dst_lo, &dst_hi, &dst_nt2,
                                               &dst_obj_init, fn);
                        checked_prop_source_bounds(
                            vm, node->rhs, node->tok, &src_lo, &src_hi,
                            &src_optional, &src_nt2, &src_nt_elem2, &src_plain,
                            fn, &src_obj_init);
                        verify_checked_assign_rewrite(
                            vm, fn, node, dst_lo, dst_hi, src_lo, src_hi,
                            src_obj_init, dst_obj_init, cr_store);
                        continue; // node is now the wrapper; parts already
                                  // scanned
                    }
                    if (cr_store) {
                        node->rhs = new_binary(vm, ND_COMMA, cr_store,
                                               node->rhs, node->tok);
                        add_type(vm, node->rhs);
                    }
                }
            }
        }

        verify_checked_assign_scan(vm, fn, node->lhs);
        verify_checked_assign_scan(vm, fn, node->rhs);
        verify_checked_assign_scan(vm, fn, node->cond);
        verify_checked_assign_scan(vm, fn, node->then);
        verify_checked_assign_scan(vm, fn, node->els);
        verify_checked_assign_scan(vm, fn, node->init);
        verify_checked_assign_scan(vm, fn, node->inc);
        verify_checked_assign_scan(vm, fn, node->body);
        for (Node *a = node->args; a; a = a->next)
            verify_checked_assign_scan(vm, fn, a);
    }
}

// #944: assignment-time bounds implication -- verifies, at assignment time,
// that a declared-checked TARGET's own bounds are implied by a
// declared-checked SOURCE's bounds, rather than trusting the target's
// declared bounds unconditionally the way every access through it otherwise
// does. A sibling pass to propagate_checked_bounds(), not folded into it:
// candidacy there requires `checked_kind == CHECKED_NONE` (an
// already-declared-checked target is never a propagation candidate), so the
// two passes have entirely disjoint target sets and don't interact. Called
// from the same three function()/block-literal tail sites as
// propagate_checked_bounds(), immediately after it, and gated the same way
// on `vm->flags & CCCC_CHECKED_BOUNDS` at parse time -- the snapshot temps
// this emits cost real stack slots and stores per checked assignment, only
// worth paying when something might enforce them.
//
// Scope, all deliberate (see docs/safety-checked-interfaces.md for the
// user-facing writeup):
//  - Source must be a declared-checked base (find_checked_base() +
//    checked_base_is_declared(), kind 1 of checked_prop_source_bounds()) or a
//    #919-propagated local classified FULL (kind 2: its snapshot temps are
//    the source range). An OPT local can hold the #942 sentinel range, which
//    CHKAB would misread as a real bound, so `q = r;` with `r` OPT (or never
//    rooted) is silently skipped, same as an unrecognised source.
//  - Target must be ND_VAR or ND_MEMBER (side-effect-free object expression
//    for the latter), declared checked, with a resolvable bounds form
//    (compute_checked_bounds() returns non-NULL lo/hi) -- CB_NONE/
//    CB_UNKNOWN targets are skipped, same rationale as propagation's
//    identically-named source exclusion.
//  - Call arguments and return values reach this pass as `__ca = arg`
//    (rewrite_checked_call_args()) and `__cr = E` (rewrite_checked_return()),
//    ordinary assignments to a declared-checked local.
//
// Ordering is the INVERSE of propagate_checked_bounds()'s snapshot: the
// target's own declared bounds are self-referencing (`[q, q + m*sizeof(T))`
// for a count(m) target), so they must be evaluated AFTER the store, off
// q's just-written value -- checked_assign_dst_lo/hi on the ND_ASSIGN node
// are left as bare (unsnapshotted) expressions for codegen to evaluate
// post-store for exactly this reason. The SOURCE's bounds, by contrast, are
// snapshotted into compiler-generated temps BEFORE the store (the rewrite
// below is `(temp = src bounds), (q = E)`, mirroring #919's own
// before-the-store snapshot) since the source expression may itself be
// overwritten or aliased by the assignment (`q = q;` self-store, or `q = r;`
// where `r` and `q` alias the same storage).
void verify_checked_assign_bounds(VirtualMachine *vm, Obj *fn) {
    if (!(vm->flags & CCCC_CHECKED_BOUNDS))
        return;
    verify_checked_assign_scan(vm, fn, fn->body);
}

// ---------------------------------------------------------------------
// #488: bounds-safe interfaces -- caller-side verification of a checked
// pointer PARAMETER's declared bounds against the actual argument
// expressions, at each call site. Unlike #919/#944 above, this is not a
// whole-function tail pass: a call site is one local, syntactic spot, so
// rewrite_checked_call_args() runs inline from funcall() (src/parse_
// postfix.c), once per call, right before the ND_FUNCALL node is built.
//
// The mechanism deliberately reuses #944's own machinery wholesale rather
// than duplicating it: for each qualifying argument this rewrites
// `arg` into `(__ca = arg, __ca)`, where `__ca` is a fresh, ordinary
// declared-checked local carrying the PARAMETER's bounds template with
// every sibling-parameter reference substituted by a clone of that call's
// actual argument expression (clone_param_bounds_node() below). No CHKAB
// stamping happens here at all -- verify_checked_assign_bounds() (just
// above), which already runs after this function's parent function
// finishes parsing, discovers the resulting `__ca = arg` ND_ASSIGN on its
// own ordinary whole-body walk (verify_checked_assign_scan() already
// recurses into node->args, and from there into an ND_COMMA's ->lhs/->rhs
// like any other node) and instruments it exactly as if the programmer had
// written `__ca = arg;` by hand -- same CHKAB pair, same dst-after-store/
// src-before-store ordering, zero new codegen. Verified empirically before
// committing to this design: an ND_ASSIGN nested in an ND_COMMA that is
// itself a call argument is reached by verify_checked_assign_scan()'s
// existing descent.
//
// A sibling argument a bounds template names is substituted as a clone, so
// one with side effects is first hoisted into a temp. The stores form a
// prelude `(__t = f(), call)` around the whole call, evaluated once and
// before any argument: arguments are unsequenced, so a read of the temp in one
// argument could not be ordered after its store in another.
// ---------------------------------------------------------------------

// Walks a resolved parameter-bounds TEMPLATE (Type.checked_param_bounds_lo/
// hi, resolved once by resolve_param_checked_bounds(), src/parse_core.c)
// checking that every OTHER parameter it references (every ND_VAR whose
// var->checked_self_param_idx names a different parameter than `self_idx`)
// is fed, at THIS call site, by a side-effect-free argument expression --
// the substitutability gate. `arg_by_idx[k-1]` is that call's actual
// argument for parameter k (1-based), captured once before any rewriting
// begins; node_has_side_effects() is the same predicate
// resolve_bounds_tokens() already uses for the identical "this expression
// is re-evaluated, so it must have no side effects" reasoning -- NOT
// checked_obj_is_trivial(), which has no ND_NUM arm and would wrongly
// decline a plain integer-literal argument (the ticket's own headline
// case, `sink(small, 8)`). A reference to `self_idx` itself needs no
// argument at all -- it substitutes to `__ca`'s own value, not to any
// argument expression -- so it is always accepted here.
static bool checked_param_bounds_args_ok(Node *n, Node **arg_by_idx, int nargs,
                                         int self_idx) {
    if (!n)
        return true;
    if (n->kind == ND_VAR && n->var && n->var->checked_self_param_idx) {
        int k = n->var->checked_self_param_idx;
        if (k != self_idx) {
            if (k < 1 || k > nargs || !arg_by_idx[k - 1])
                return false;
            Node *a = arg_by_idx[k - 1];
            if (node_has_side_effects(a))
                return false;
        }
        return true;
    }
    return checked_param_bounds_args_ok(n->lhs, arg_by_idx, nargs, self_idx) &&
           checked_param_bounds_args_ok(n->rhs, arg_by_idx, nargs, self_idx) &&
           checked_param_bounds_args_ok(n->cond, arg_by_idx, nargs, self_idx) &&
           checked_param_bounds_args_ok(n->then, arg_by_idx, nargs, self_idx) &&
           checked_param_bounds_args_ok(n->els, arg_by_idx, nargs, self_idx);
}

// Like clone_bounds_node()/clone_member_bounds_node(), but for a
// PARAMETER's resolved bounds template (#488): every ND_VAR that
// references the SAME parameter this template belongs to
// (var->checked_self_param_idx == self_idx) is rewritten to a read of
// `ca` (the temp standing in for that parameter's value at this call --
// e.g. `bounds(p, p+n)` on parameter `p` itself must read `ca`, not the
// raw argument expression, since `ca` is what CHKAB will actually verify
// against). A reference to any OTHER parameter k substitutes a deep clone
// of that call's actual argument expression, arg_by_idx[k-1] -- safe to
// clone with the restricted, args/body-free clone_bounds_node() only
// because checked_param_bounds_args_ok() (above) has already required
// every such reference to be side-effect-free, which rules out an
// ND_FUNCALL/ND_STMT_EXPR/etc. actually needing its ->args/->body cloned.
static Node *clone_param_bounds_node(VirtualMachine *vm, Node *n,
                                     Node **arg_by_idx, int nargs, int self_idx,
                                     Obj *ca) {
    if (!n)
        return NULL;
    if (n->kind == ND_VAR && n->var && n->var->checked_self_param_idx) {
        int k = n->var->checked_self_param_idx;
        if (k == self_idx) {
            Node *v = new_var_node(vm, ca, n->tok);
            add_type(vm, v);
            return v;
        }
        Node *v = clone_bounds_node(vm, arg_by_idx[k - 1]);
        return v;
    }
    Node *c = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    *c      = *n;
    c->next = NULL;
    c->lhs =
        clone_param_bounds_node(vm, n->lhs, arg_by_idx, nargs, self_idx, ca);
    c->rhs =
        clone_param_bounds_node(vm, n->rhs, arg_by_idx, nargs, self_idx, ca);
    c->cond =
        clone_param_bounds_node(vm, n->cond, arg_by_idx, nargs, self_idx, ca);
    c->then =
        clone_param_bounds_node(vm, n->then, arg_by_idx, nargs, self_idx, ca);
    c->els =
        clone_param_bounds_node(vm, n->els, arg_by_idx, nargs, self_idx, ca);
    return c;
}

// Instantiates a parameter-bounds template inside the definition's body:
// placeholder k becomes the definition's k-th parameter, so a definition that
// renames its parameters still binds by position. `self_idx` is the 1-based
// parameter the template belongs to, or 0 for a return value.
void resolve_checked_template_bounds(VirtualMachine *vm, Obj **params,
                                     int nparams, Node *lo_tmpl, Node *hi_tmpl,
                                     int self_idx, Node **out_lo,
                                     Node **out_hi) {
    Node **arg_by_idx = arena_alloc(&vm->compiler.parser_arena,
                                    sizeof(Node *) * (nparams + 1));
    for (int i = 0; i < nparams; i++) {
        arg_by_idx[i] = new_var_node(vm, params[i], params[i]->ty->name);
        add_type(vm, arg_by_idx[i]);
    }
    Obj *self = self_idx ? params[self_idx - 1] : NULL;
    *out_lo   = clone_param_bounds_node(vm, lo_tmpl, arg_by_idx, nparams,
                                        self_idx, self);
    *out_hi   = clone_param_bounds_node(vm, hi_tmpl, arg_by_idx, nparams,
                                        self_idx, self);
}

// Allocates the hidden `__ca` local this rewrite assigns each qualifying
// argument through. Deliberately bypasses new_lvar() -- new_lvar() calls
// warn_if_shadowing(vm, ty->name), and `ty` here is the CALLEE's parameter
// Type, whose ->name is the callee's own parameter identifier (e.g. `n`);
// checking that name for shadowing against the CALLER's scope would be
// meaningless at best and a spurious "shadows a variable" warning at
// worst. Same "" name / direct vm->compiler.locals prepend pattern as
// checked_cast_desugar()'s `__cv` (#486) and new_checked_prop_temp()'s
// phase-B temps (#941) -- new_var() alone still copies checked_kind/
// checked_bounds_form off `ty`, which is what makes the result an ordinary
// declared-checked Obj every existing pass already knows how to handle.
static Obj *new_checked_call_temp(VirtualMachine *vm, Type *ty) {
    Obj *var            = new_var(vm, "", 0, ty);
    var->is_local       = true;
    var->next           = vm->compiler.locals;
    vm->compiler.locals = var;
    return var;
}

// The declared-checked temp standing for parameter `idx`'s argument: its
// bounds are the parameter's template with the other arguments substituted.
static Obj *new_checked_arg_temp(VirtualMachine *vm, Type *p, Node **arg_by_idx,
                                 int nargs, int idx) {
    Obj *ca               = new_checked_call_temp(vm, p);
    ca->checked_bounds_lo = clone_param_bounds_node(
        vm, p->checked_param_bounds_lo, arg_by_idx, nargs, idx, ca);
    ca->checked_bounds_hi = clone_param_bounds_node(
        vm, p->checked_param_bounds_hi, arg_by_idx, nargs, idx, ca);
    return ca;
}

static Node *copy_arg_node(VirtualMachine *vm, Node *a) {
    Node *copy = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    *copy      = *a;
    copy->next = NULL;
    return copy;
}

// Overwrites argument `a` with `repl` in place, keeping its place in the list.
static void overwrite_arg(Node *a, Node *repl) {
    Node *saved = a->next;
    *a          = *repl;
    a->next     = saved;
}

// `tmp = value` as a prelude statement.
static Node *new_temp_store(VirtualMachine *vm, Obj *tmp, Node *value,
                            Token *tok) {
    Node *lhs = new_var_node(vm, tmp, tok);
    add_type(vm, lhs);
    Node *store = new_binary(vm, ND_ASSIGN, lhs, value, tok);
    add_type(vm, store);
    return store;
}

static Node *append_prelude(VirtualMachine *vm, Node *prelude, Node *store,
                            Token *tok) {
    if (!prelude)
        return store;
    Node *comma = new_binary(vm, ND_COMMA, prelude, store, tok);
    add_type(vm, comma);
    return comma;
}

// A scalar parameter's value as a plain local: a checked-annotated pointer
// parameter is held as an ordinary pointer, so the copy carries no bounds of
// its own.
static Type *plain_value_type(VirtualMachine *vm, Type *ty) {
    if (ty->kind == TY_PTR && ty->checked_kind != CHECKED_NONE)
        return pointer_to(vm, ty->base);
    return unqualified(vm, ty);
}

// Hoists, once each, the arguments the return-bounds template `n` names into
// temps, rewriting the argument in place to `(__ak = ak)`. `reads[k-1]`
// becomes a read of the temp.
static void hoist_ret_bounds_args(VirtualMachine *vm, Node *n, Type *params,
                                  Node *args, Node **reads, Obj *alloc_fn,
                                  Token *tok) {
    if (!n)
        return;
    if (n->kind == ND_VAR && n->var && n->var->checked_self_param_idx) {
        int k = n->var->checked_self_param_idx;
        if (reads[k - 1])
            return;
        Type *p = params;
        Node *a = args;
        for (int i = 1; i < k; i++) {
            p = p->next;
            a = a->next;
        }
        Type *ty   = plain_value_type(vm, p);
        Obj  *tmp  = alloc_fn ? new_checked_prop_temp(vm, alloc_fn, ty)
                              : new_checked_call_temp(vm, ty);
        Node *orig = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
        *orig      = *a;
        orig->next = NULL;
        Node *lhs  = new_var_node(vm, tmp, tok);
        add_type(vm, lhs);
        Node *store = new_binary(vm, ND_ASSIGN, lhs, orig, tok);
        add_type(vm, store);
        Node *saved = a->next;
        *a          = *store;
        a->next     = saved;
        Node *read  = new_var_node(vm, tmp, tok);
        add_type(vm, read);
        reads[k - 1] = read;
        return;
    }
    hoist_ret_bounds_args(vm, n->lhs, params, args, reads, alloc_fn, tok);
    hoist_ret_bounds_args(vm, n->rhs, params, args, reads, alloc_fn, tok);
    hoist_ret_bounds_args(vm, n->cond, params, args, reads, alloc_fn, tok);
    hoist_ret_bounds_args(vm, n->then, params, args, reads, alloc_fn, tok);
    hoist_ret_bounds_args(vm, n->els, params, args, reads, alloc_fn, tok);
}

// Splits a call to a checked-return function (one checked_call_root()
// accepted) into `__cr = call`: the call node becomes a read of the fresh
// declared-checked local `__cr`, whose bounds are the callee's return-bounds
// template with the arguments it names hoisted into temps (each evaluated once,
// so the bounds hold the values the callee received). Returns the store, which
// the caller must evaluate before anything that reads `__cr`. `alloc_fn` is
// the enclosing function once its body is parsed (the tail passes), NULL while
// it is being parsed.
static Node *split_checked_call_result(VirtualMachine *vm, Node *call,
                                       Obj *alloc_fn) {
    Node  *callee = call->call_prelude ? call->rhs : call;
    Type  *fty    = callee->func_ty;
    Type  *ret    = unqualified(vm, fty->return_ty);
    Token *tok    = call->tok;

    int    nargs  = 0;
    for (Node *a = callee->args; a; a = a->next)
        nargs++;
    Node **reads =
        arena_alloc(&vm->compiler.parser_arena, sizeof(Node *) * (nargs + 1));
    for (int i = 0; i <= nargs; i++)
        reads[i] = NULL;
    hoist_ret_bounds_args(vm, fty->checked_ret_bounds_lo, fty->params,
                          callee->args, reads, alloc_fn, tok);
    hoist_ret_bounds_args(vm, fty->checked_ret_bounds_hi, fty->params,
                          callee->args, reads, alloc_fn, tok);

    Obj *cr = alloc_fn ? new_checked_prop_temp(vm, alloc_fn, ret)
                       : new_checked_call_temp(vm, ret);
    cr->checked_call_result = true;
    cr->checked_bounds_lo   = clone_param_bounds_node(
        vm, fty->checked_ret_bounds_lo, reads, nargs, 0, cr);
    cr->checked_bounds_hi = clone_param_bounds_node(
        vm, fty->checked_ret_bounds_hi, reads, nargs, 0, cr);

    Node *copy = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    *copy      = *call;
    copy->next = NULL;
    Node *lhs  = new_var_node(vm, cr, tok);
    add_type(vm, lhs);
    Node *store = new_binary(vm, ND_ASSIGN, lhs, copy, tok);
    add_type(vm, store);

    Node *read = new_var_node(vm, cr, tok);
    add_type(vm, read);
    Node *saved = call->next;
    *call       = *read;
    call->next  = saved;
    return store;
}

// True when parameter `p`, fed by argument `a`, is verified at the call: a
// checked pointer parameter with a bounds form, whose argument is rooted at a
// declared-checked var/member, a propagation candidate, or a call returning a
// checked pointer. Whether a candidate is FULL (verified) or NONE/OPT (the
// store stays unverified, as an unchecked argument would) is only known once
// the function body is parsed; verify_checked_assign_scan() decides then.
static bool checked_param_qualifies(Type *p, Node *a) {
    if (p->kind != TY_PTR || p->checked_kind == CHECKED_NONE)
        return false;
    bool has_form = p->checked_kind == CHECKED_SINGLE ||
                    (p->checked_bounds_form != CB_NONE &&
                     p->checked_bounds_form != CB_UNKNOWN);
    if (!has_form)
        return false;
    CheckedBase src_base = find_checked_base(a);
    CheckedBase plain;
    return checked_base_is_declared(src_base) ||
           (src_base.var && src_base.var->checked_prop_candidate) ||
           checked_call_root(a) != NULL || checked_cast_root(a) != NULL ||
           checked_plain_array_root(a, &plain) != NULL;
}

// Marks every side-effecting sibling argument the bounds template `n` names so
// it can be hoisted: in `need` when it is a plain value, in `vneed` when it is
// itself verified at this call (its store into a declared-checked temp keeps
// its own check). False when one cannot be: its parameter cannot be held in a
// temp.
static bool checked_param_hoist_siblings(Node *n, Type *params,
                                         Node **arg_by_idx, int nargs,
                                         int self_idx, bool *need,
                                         bool *vneed) {
    if (!n)
        return true;
    if (n->kind == ND_VAR && n->var && n->var->checked_self_param_idx) {
        int k = n->var->checked_self_param_idx;
        if (k == self_idx)
            return true;
        if (k < 1 || k > nargs || !arg_by_idx[k - 1])
            return false;
        Node *a = arg_by_idx[k - 1];
        if (!node_has_side_effects(a))
            return true;
        Type *pk = params;
        for (int i = 1; i < k && pk; i++)
            pk = pk->next;
        if (!pk || !checked_param_copyable(pk))
            return false;
        if (checked_param_qualifies(pk, a))
            vneed[k - 1] = true;
        else
            need[k - 1] = true;
        return true;
    }
    return checked_param_hoist_siblings(n->lhs, params, arg_by_idx, nargs,
                                        self_idx, need, vneed) &&
           checked_param_hoist_siblings(n->rhs, params, arg_by_idx, nargs,
                                        self_idx, need, vneed) &&
           checked_param_hoist_siblings(n->cond, params, arg_by_idx, nargs,
                                        self_idx, need, vneed) &&
           checked_param_hoist_siblings(n->then, params, arg_by_idx, nargs,
                                        self_idx, need, vneed) &&
           checked_param_hoist_siblings(n->els, params, arg_by_idx, nargs,
                                        self_idx, need, vneed);
}

// Hoists the side-effecting siblings that verified parameters name into temps,
// rewriting each argument in place into a read of its temp. Plain values come
// first, in argument order. A verified sibling follows as `ca = arg` once every
// argument its own bounds name is side-effect-free, so a chain is stored in
// dependency order; a cycle stays as it is and the calls that name it are left
// unchecked. `done[k-1]` is set for a verified sibling whose store is in the
// prelude. Returns the chain of stores, or NULL if nothing was hoisted.
static Node *hoist_checked_call_siblings(VirtualMachine *vm, Type *fn_ty,
                                         Node **arg_by_idx, int nargs,
                                         bool *done, Token *tok) {
    bool *need   = arena_alloc(&vm->compiler.parser_arena, nargs);
    bool *vneed  = arena_alloc(&vm->compiler.parser_arena, nargs);
    bool *trial  = arena_alloc(&vm->compiler.parser_arena, nargs);
    bool *vtrial = arena_alloc(&vm->compiler.parser_arena, nargs);
    int   idx    = 0;
    for (Type *p = fn_ty->params; p && idx < nargs; p = p->next) {
        Node *a = arg_by_idx[idx++];
        if (!checked_param_qualifies(p, a))
            continue;
        for (int i = 0; i < nargs; i++)
            trial[i] = vtrial[i] = false;
        if (checked_param_hoist_siblings(p->checked_param_bounds_lo,
                                         fn_ty->params, arg_by_idx, nargs, idx,
                                         trial, vtrial) &&
            checked_param_hoist_siblings(p->checked_param_bounds_hi,
                                         fn_ty->params, arg_by_idx, nargs, idx,
                                         trial, vtrial))
            for (int i = 0; i < nargs; i++) {
                need[i] |= trial[i];
                vneed[i] |= vtrial[i];
            }
    }

    Node *prelude = NULL;
    Type *pk      = fn_ty->params;
    for (int k = 1; k <= nargs && pk; k++, pk = pk->next) {
        if (!need[k - 1])
            continue;
        Node *a    = arg_by_idx[k - 1];
        Obj  *tmp  = new_checked_call_temp(vm, plain_value_type(vm, pk));
        Node *orig = copy_arg_node(vm, a);
        prelude    = append_prelude(vm, prelude,
                                    new_temp_store(vm, tmp, orig, tok), tok);
        Node *read = new_var_node(vm, tmp, tok);
        add_type(vm, read);
        overwrite_arg(a, read);
    }

    for (bool progress = true; progress;) {
        progress = false;
        pk       = fn_ty->params;
        for (int k = 1; k <= nargs && pk; k++, pk = pk->next) {
            if (!vneed[k - 1] || done[k - 1] ||
                !checked_param_bounds_args_ok(pk->checked_param_bounds_lo,
                                              arg_by_idx, nargs, k) ||
                !checked_param_bounds_args_ok(pk->checked_param_bounds_hi,
                                              arg_by_idx, nargs, k))
                continue;
            Node *a    = arg_by_idx[k - 1];
            Obj  *ca   = new_checked_arg_temp(vm, pk, arg_by_idx, nargs, k);
            Node *orig = copy_arg_node(vm, a);
            prelude    = append_prelude(vm, prelude,
                                        new_temp_store(vm, ca, orig, tok), tok);
            Node *read = new_var_node(vm, ca, tok);
            add_type(vm, read);
            overwrite_arg(a, read);
            done[k - 1] = true;
            progress    = true;
        }
    }
    return prelude;
}

// #488 entry point, called once per call site from funcall() (src/parse_
// postfix.c), after every other per-argument validation there and before
// the ND_FUNCALL node is built. Returns the prelude of hoisted sibling
// arguments the caller must evaluate before the call, or NULL. `fn_ty` is the
// callee's function type (already unwrapped from a function-pointer type by
// funcall()'s own `ty` computation); `args` is the just-built, already
// param-type-cast argument list (funcall()'s `head.next`); `tok` is the call's
// own token, used for every node built here.
//
// No-op (silent) unless CCCC_CHECKED_BOUNDS is set -- ABI transparency:
// with the flag off, this never runs, so -m/-c=native/-c=generated output
// is unaffected. Also a no-op with no vm->compiler.current_fn live (a call
// in a context with no enclosing function body, e.g. a global
// initializer) -- new_checked_call_temp() needs a live locals list to
// attach to, the same precondition checked_cast_desugar() enforces (by
// erroring) for its own temp; here it is simply skipped, since a call in
// such a context is either already invalid C or a constant-folding path
// this ticket has no reason to newly reject.
Node *rewrite_checked_call_args(VirtualMachine *vm, Type *fn_ty, Node *args,
                                Token *tok) {
    if (!(vm->flags & CCCC_CHECKED_BOUNDS) || !vm->compiler.current_fn)
        return NULL;

    int nargs = 0;
    for (Node *a = args; a; a = a->next)
        nargs++;
    if (nargs == 0)
        return NULL;

    // Snapshot positional argument pointers. Hoisting rewrites an argument in
    // place into a side-effect-free temp read, so the pointer stays valid; a
    // verified argument rewritten to `(ca = arg, ca)` is replaced here by its
    // original expression.
    Node **arg_by_idx =
        arena_alloc(&vm->compiler.parser_arena, sizeof(Node *) * nargs);
    {
        int i = 0;
        for (Node *a = args; a; a = a->next)
            arg_by_idx[i++] = a;
    }

    resolve_param_checked_bounds(vm, fn_ty);

    bool *done = arena_alloc(&vm->compiler.parser_arena, nargs);
    Node *prelude =
        hoist_checked_call_siblings(vm, fn_ty, arg_by_idx, nargs, done, tok);

    int   idx = 0;
    Node *a   = args;
    for (Type *p = fn_ty->params; p && a; p = p->next, a = a->next) {
        idx++;
        if (done[idx - 1] || !checked_param_qualifies(p, a))
            continue;

        // Gate: every OTHER parameter this parameter's bounds template
        // references must be fed, at this call, by a side-effect-free
        // argument (a hoisted one is).
        if (!checked_param_bounds_args_ok(p->checked_param_bounds_lo,
                                          arg_by_idx, nargs, idx) ||
            !checked_param_bounds_args_ok(p->checked_param_bounds_hi,
                                          arg_by_idx, nargs, idx))
            continue;

        Obj *ca = new_checked_arg_temp(vm, p, arg_by_idx, nargs, idx);

        // `(ca = arg, ca)`, same shape and in-place-overwrite trick as
        // checked_cast_desugar()'s `(__cv = (T[[...]])e, __cv)` (#486).
        Node *arg_copy = copy_arg_node(vm, a);
        Node *store    = new_temp_store(vm, ca, arg_copy, tok);

        Node *result = new_var_node(vm, ca, tok);
        add_type(vm, result);
        Node *comma = new_binary(vm, ND_COMMA, store, result, tok);
        add_type(vm, comma);

        overwrite_arg(a, comma);
        // Later bounds name the argument as written: the rewritten shape's
        // store would read as a side effect and decline them.
        arg_by_idx[idx - 1] = arg_copy;
    }
    return prelude;
}

bool is_attr_name(Token *tok, char *name) {
    if (equal(tok, name))
        return true;
    int len = strlen(name);
    return tok->len == len + 4 && !memcmp(tok->loc, "__", 2) &&
           !memcmp(tok->loc + 2, name, len) &&
           !memcmp(tok->loc + 2 + len, "__", 2);
}

// Captures the raw token span(s) inside a checked-pointer bounds attribute's
// argument list -- count(n), byte_count(n), bounds(lo, hi), bounds(unknown)
// -- WITHOUT evaluating them. A bounds expression may reference a sibling
// parameter not yet in scope at attribute-parse time (e.g. `count(n)` where
// `n` is a later parameter of the same function), so resolution is deferred
// to resolve_checked_bounds() (#483) once the right scope exists; see
// Type.checked_bounds_arg1/arg2's comment in cccc.h. `tok` must point at the
// opening '('. *arg1 is always set to the first argument's start token;
// *arg2 is set to the second argument's start token only if a top-level
// comma was found (bounds(lo, hi)), else left NULL. Returns the token past
// the matching ')'.
static Token *capture_checked_bounds_args(VirtualMachine *vm, Token *tok,
                                          Token **arg1, Token **arg2) {
    Token *paren_tok = tok;
    tok              = skip(vm, tok, "(");
    *arg1            = tok;
    *arg2            = NULL;
    int depth        = 0;
    while (tok && tok->kind != TK_EOF) {
        if (equal(tok, "(")) {
            depth++;
        } else if (equal(tok, ")")) {
            if (depth == 0)
                return tok->next;
            depth--;
        } else if (equal(tok, ",") && depth == 0 && !*arg2) {
            *arg2 = tok->next;
        }
        tok = tok->next;
    }
    error_tok(vm, paren_tok, "unterminated argument list");
    return tok; // unreachable
}

// Shared body for the six checked-pointer attributes (#770/#482-484):
// [[cccc::single]] / [[cccc::array]] / [[cccc::ntarray]] and their bounds
// forms [[cccc::count(n)]] / [[cccc::byte_count(n)]] / [[cccc::bounds(lo,
// hi)]] / [[cccc::bounds(unknown)]]. Used by both the GNU __attribute__ and
// C23 [[...]] parsers, and only meaningful in one grammar position: attached
// to `pointers()`'s just-built TY_PTR, right after the '*' it qualifies --
// the same position const/volatile/restrict attach in today (see the
// CheckedKind comment in cccc.h for why this position was chosen over the
// ticket's original declspec-position sketch, which appertains to the
// pointee, not the pointer). `name_tok` is the attribute name token (for
// diagnostics); `tok` points just past the name. Returns the token past any
// argument list. Consistency between the kind attributes and the bounds
// attributes (e.g. a bounds form without array/ntarray, or on a `single`
// pointer) is checked once per '*', after all of this level's attributes
// have been parsed -- see the checked-pointer post-check in pointers().
Token *apply_checked_ptr_attr(VirtualMachine *vm, Token *name_tok, Token *tok,
                              Type *ty, const char *name) {
    if (!ty || ty->kind != TY_PTR)
        error_tok(vm, name_tok,
                  "'%s' must be written immediately after '*' "
                  "(e.g. int * [[cccc::%s]] p;), not in this position",
                  name, name);

    if (!strcmp(name, "single") || !strcmp(name, "array") ||
        !strcmp(name, "ntarray")) {
        if (equal(tok, "("))
            error_tok(vm, name_tok, "'%s' takes no arguments", name);
        ty->checked_kind = !strcmp(name, "single")  ? CHECKED_SINGLE
                           : !strcmp(name, "array") ? CHECKED_ARRAY
                                                    : CHECKED_NTARRAY;
        return tok;
    }

    // #486: assume / dynamic -- the two sanctioned bounds-cast forms.
    // Consistency (mutual exclusion, cast-only position, kind/bounds-form
    // requirements) is checked once per '*' in pointers()'s post-check,
    // same as the bounds-form-requires-a-kind rule for count()/byte_count()/
    // bounds() above.
    if (!strcmp(name, "assume") || !strcmp(name, "dynamic")) {
        if (equal(tok, "("))
            error_tok(vm, name_tok, "'%s' takes no arguments", name);
        CheckedCastKind kind = !strcmp(name, "assume") ? CC_ASSUME : CC_DYNAMIC;
        if (ty->checked_cast_kind != CC_NONE && ty->checked_cast_kind != kind)
            error_tok(vm, name_tok,
                      "'assume' and 'dynamic' cannot both be used on the "
                      "same cast");
        ty->checked_cast_kind = kind;
        return tok;
    }

    // count / byte_count / bounds
    if (!equal(tok, "("))
        error_tok(vm, name_tok, "'%s' requires an argument list", name);
    Token *arg1, *arg2;
    Token *after = capture_checked_bounds_args(vm, tok, &arg1, &arg2);

    if (!strcmp(name, "count")) {
        ty->checked_bounds_form = CB_COUNT;
        ty->checked_bounds_arg1 = arg1;
    } else if (!strcmp(name, "byte_count")) {
        ty->checked_bounds_form = CB_BYTE_COUNT;
        ty->checked_bounds_arg1 = arg1;
    } else { // bounds
        if (arg2) {
            ty->checked_bounds_form = CB_RANGE;
            ty->checked_bounds_arg1 = arg1;
            ty->checked_bounds_arg2 = arg2;
        } else if (arg1 && arg1->kind == TK_IDENT && arg1->len == 7 &&
                   !memcmp(arg1->loc, "unknown", 7)) {
            ty->checked_bounds_form = CB_UNKNOWN;
        } else {
            error_tok(vm, name_tok,
                      "'bounds' requires two arguments (lo, hi), or the "
                      "single argument 'unknown'");
        }
    }
    return after;
}

// `[[cccc::count(n)]]` after a checked flexible array member's `[]`. `tok`
// points at the opening '(' and `arr` is a private copy of the array type.
// Returns the token past the matching ')'.
Token *cc_apply_checked_array_count(VirtualMachine *vm, Token *name_tok,
                                    Token *tok, Type *arr) {
    if (arr->array_len >= 0)
        error_tok(vm, name_tok,
                  "'count' on a checked array is only for a flexible array "
                  "member (e.g. int data _Checked[] [[cccc::count(n)]];)");
    Token *arg1, *arg2;
    Token *after = capture_checked_bounds_args(vm, tok, &arg1, &arg2);
    if (arg2)
        error_tok(vm, name_tok, "'count' takes one argument");
    arr->checked_bounds_form = CB_COUNT;
    arr->checked_bounds_arg1 = arg1;
    return after;
}

typedef struct CheckedDynPending CheckedDynPending;
struct CheckedDynPending {
    CheckedDynPending *next;
    Obj               *src;
    Token             *tok;
};

static void dynamic_source_no_bounds_error(VirtualMachine *vm, Token *tok) {
    error_tok(vm, tok,
              "'dynamic' cast source has no known bounds to verify "
              "against -- use 'assume' to take responsibility for this "
              "conversion, or give the source a bounds declaration");
}

// #486: checks that a [[cccc::dynamic]] cast's SOURCE (`cast_node->lhs`,
// the pre-cast expression) has bounds to verify the claim against: either a
// declared-checked base with a resolvable bounds form (kind 1 of
// checked_prop_source_bounds()), or an unchecked pointer local whose bounds
// are propagated from one (#919). Without that there is nothing for #944's
// CHKAB to verify the claim against, and falling back to CHKD/CHKB's
// allocation-derived bound would silently do nothing for a stack or global
// source -- #770 documented they have no upper bound at all for a non-heap
// base, the headline Checked C case.
//
// A propagated source is only accepted if propagation classifies it FULL
// (bounds known on every path), which isn't known until the function body is
// parsed; it is queued on the function and settled by
// resolve_pending_dynamic_casts().
//
// Deliberately independent of --checked-pointers/CCCC_CHECKED_BOUNDS (a
// no-op for CC_ASSUME): called from cast() unconditionally, same footing as
// the mutual-exclusion/cast-only/kind-requires-bounds-form rules in
// pointers()'s post-check, so a `dynamic` cast with no verifiable claim is
// caught even in a build that never enables runtime enforcement.
void check_checked_cast_dynamic_source(VirtualMachine *vm, Node *cast_node,
                                       Type *ty, Token *tok) {
    if (ty->checked_cast_kind != CC_DYNAMIC)
        return;
    add_type(vm, cast_node->lhs);
    // The inner bounds cast's claim is the source's bounds; with
    // --checked-pointers off it is still a plain ND_CAST.
    if (checked_cast_root(cast_node->lhs) ||
        (cast_node->lhs->kind == ND_CAST && cast_node->lhs->ty &&
         cast_node->lhs->ty->checked_cast_kind != CC_NONE))
        return;
    CheckedBase src_base = find_checked_base(cast_node->lhs);
    Node       *src_lo = NULL, *src_hi = NULL;
    bool        src_nt = false;
    if (checked_base_is_declared(src_base)) {
        Type *src_pty = checked_base_type(src_base);
        compute_checked_bounds(vm, src_base, src_pty, tok, &src_lo, &src_hi,
                               &src_nt, NULL, NULL);
    } else if (src_base.var && src_base.var->checked_prop_candidate &&
               vm->compiler.current_fn) {
        CheckedDynPending *p =
            arena_alloc(&vm->compiler.parser_arena, sizeof(*p));
        p->src  = src_base.var;
        p->tok  = tok;
        p->next = vm->compiler.current_fn->checked_dynamic_pending;
        vm->compiler.current_fn->checked_dynamic_pending = p;
        return;
    } else if (checked_call_root(cast_node->lhs)) {
        return; // bounds come from the callee's return type
    } else if (checked_plain_array_root(cast_node->lhs, &(CheckedBase){0})) {
        return; // bounds are the array's own extent
    }
    (void)src_nt;
    if (!src_lo || !src_hi)
        dynamic_source_no_bounds_error(vm, tok);
}

// Settles the `dynamic` casts check_checked_cast_dynamic_source() queued on
// `fn`, once its candidates are classified: only a FULL local of `fn` is a
// source CHKAB can verify against. With --checked-pointers off nothing else
// has classified them, so the classifier runs here, for functions that
// need it only.
static void resolve_pending_dynamic_casts(VirtualMachine *vm, Obj *fn) {
    if (!fn->checked_dynamic_pending)
        return;
    if (!(vm->flags & CCCC_CHECKED_BOUNDS))
        checked_prop_classify(vm, fn);
    for (CheckedDynPending *p = fn->checked_dynamic_pending; p; p = p->next) {
        bool in_fn = false;
        for (Obj *v = fn->locals; v && !in_fn; v = v->next)
            in_fn = v == p->src;
        if (!in_fn || !p->src->checked_prop_candidate ||
            p->src->checked_prop_unsafe)
            dynamic_source_no_bounds_error(vm, p->tok);
        if (p->src->checked_prop_optional)
            error_tok(vm, p->tok,
                      "'dynamic' cast source has known bounds on only some "
                      "paths -- use 'assume' to take responsibility for this "
                      "conversion, or give the source a bounds declaration");
    }
    fn->checked_dynamic_pending = NULL;
}

// ---------------------------------------------------------------------------
// Deferred sibling-write group check.
//
// `s.p = small; s.n = 8;` stores a pointer whose bounds name `s.n`, then
// widens the count. The assignment check above ran right after the pointer
// store, against the old count. This pass moves that check to the end of a
// run of directly following writes to the pointer's bound-naming siblings, so
// it sees the final count.
//
// A group is a stamped pointer assignment (the "anchor") plus the leaves that
// follow it in evaluation order: stores to compiler temps, writes to the
// anchor's siblings on the same object (plain, compound and ++/--), and other
// anchors on that object. Anything else ends it. The anchor's CHKAB stamp
// moves onto a carrier `__cg = 0` placed after the last sibling write; the
// source-bounds snapshot stays before the pointer store.
//
// A sibling write whose value has side effects or reads the pointer ends the
// group before it, so the pointer is never read through the stale check.

typedef struct {
    Node *a;
    Node *b;
} GroupTemp; // `tmp = &obj`: the object a compound assignment writes through

typedef struct {
    Obj    *var;
    Member *mem;
    Node   *obj;
    Obj    *vars[8];
    Member *mems[8];
    int     nvars, nmems;
} GroupTarget;

static void group_collect_siblings(Node *n, Obj *self, GroupTarget *t) {
    if (!n)
        return;
    if (n->kind == ND_VAR && n->var) {
        if (n->var->checked_self_member) {
            if (t->nmems < 8)
                t->mems[t->nmems++] = n->var->checked_self_member;
        } else if (n->var != self && n->var->name && n->var->name[0] &&
                   !n->var->checked_self_param_idx) {
            if (t->nvars < 8)
                t->vars[t->nvars++] = n->var;
        }
        return;
    }
    group_collect_siblings(n->lhs, self, t);
    group_collect_siblings(n->rhs, self, t);
    group_collect_siblings(n->cond, self, t);
    group_collect_siblings(n->then, self, t);
    group_collect_siblings(n->els, self, t);
}

// The pointer a stamped assignment stores into, with the variables and
// members its bounds name. False when it names none, or is a compiler temp.
static bool group_anchor_target(Node *leaf, GroupTarget *t) {
    memset(t, 0, sizeof(*t));
    if (leaf->kind != ND_ASSIGN || !leaf->checked_assign_dst_lo || !leaf->lhs)
        return false;
    Node *l = leaf->lhs;
    if (l->kind == ND_VAR && l->var && l->var->name && l->var->name[0]) {
        t->var = l->var;
        group_collect_siblings(l->var->checked_bounds_lo, l->var, t);
        group_collect_siblings(l->var->checked_bounds_hi, l->var, t);
    } else if (l->kind == ND_MEMBER && l->member && l->lhs &&
               !node_has_side_effects(l->lhs)) {
        t->mem = l->member;
        t->obj = l->lhs;
        group_collect_siblings(l->member->checked_bounds_lo, NULL, t);
        group_collect_siblings(l->member->checked_bounds_hi, NULL, t);
    } else {
        return false;
    }
    return t->nvars || t->nmems;
}

static Node *group_peel_casts(Node *n) {
    while (n && n->kind == ND_CAST)
        n = n->lhs;
    return n;
}

static bool group_reads_target(Node *n, const GroupTarget *t) {
    if (!n)
        return false;
    if (n->kind == ND_VAR && t->var && n->var == t->var)
        return true;
    if (n->kind == ND_MEMBER && t->mem && n->member == t->mem)
        return true;
    if (group_reads_target(n->lhs, t) || group_reads_target(n->rhs, t) ||
        group_reads_target(n->cond, t) || group_reads_target(n->then, t) ||
        group_reads_target(n->els, t))
        return true;
    for (Node *a = n->args; a; a = a->next)
        if (group_reads_target(a, t))
            return true;
    return false;
}

static bool group_is_temp(Node *n) {
    return n && n->kind == ND_VAR && n->var &&
           (!n->var->name || !n->var->name[0]);
}

static Node *group_temp_object(GroupTemp *temps, int ntemps, Node *tmp) {
    for (int i = ntemps - 1; i >= 0; i--)
        if (temps[i].a->var == tmp->var)
            return temps[i].b;
    return NULL;
}

// What a leaf writes: a plain variable, or a member of an object. Writes
// through a `tmp = &x` compound-assignment temp resolve to `x`.
static bool group_write_target(Node *leaf, GroupTemp *temps, int ntemps,
                               Obj **out_var, Member **out_mem,
                               Node **out_obj) {
    *out_var = NULL;
    *out_mem = NULL;
    *out_obj = NULL;
    Node *l  = leaf->lhs;
    if (!l)
        return false;
    if (l->kind == ND_VAR && !group_is_temp(l)) {
        *out_var = l->var;
        return true;
    }
    if (l->kind == ND_MEMBER && l->lhs) {
        Node *o = l->lhs;
        if (o->kind == ND_DEREF && group_is_temp(o->lhs)) {
            Node *x = group_temp_object(temps, ntemps, o->lhs);
            if (!x)
                return false;
            o = x;
        }
        *out_mem = l->member;
        *out_obj = o;
        return true;
    }
    if (l->kind == ND_DEREF && group_is_temp(l->lhs)) {
        Node *x = group_temp_object(temps, ntemps, l->lhs);
        if (!x)
            return false;
        if (x->kind == ND_VAR && !group_is_temp(x)) {
            *out_var = x->var;
            return true;
        }
        if (x->kind == ND_MEMBER && x->lhs) {
            *out_mem = x->member;
            *out_obj = x->lhs;
            return true;
        }
    }
    return false;
}

static bool group_target_has(const GroupTarget *t, Obj *var, Member *mem) {
    for (int i = 0; var && i < t->nvars; i++)
        if (t->vars[i] == var)
            return true;
    for (int i = 0; mem && i < t->nmems; i++)
        if (t->mems[i] == mem)
            return true;
    return false;
}

// True when `b`'s pointer is bound by a sibling `a` also names, on the same
// object.
static bool group_anchors_related(const GroupTarget *a, const GroupTarget *b) {
    if ((a->var != NULL) != (b->var != NULL))
        return false;
    if (a->mem && !nodes_structurally_equal(a->obj, b->obj))
        return false;
    for (int i = 0; i < b->nvars; i++)
        if (group_target_has(a, b->vars[i], NULL))
            return true;
    for (int i = 0; i < b->nmems; i++)
        if (group_target_has(a, NULL, b->mems[i]))
            return true;
    return false;
}

static void group_wrap_leaf(VirtualMachine *vm, Node *leaf, Node *extra) {
    Node *copy  = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    *copy       = *leaf;
    copy->next  = NULL;
    Node *comma = new_binary(vm, ND_COMMA, copy, extra, leaf->tok);
    add_type(vm, comma);
    Node *saved = leaf->next;
    *leaf       = *comma;
    leaf->next  = saved;
}

static void group_move_stamp(VirtualMachine *vm, Obj *fn, Node *from,
                             Node *after) {
    Obj  *cg  = new_checked_prop_temp(vm, fn, ty_int);
    Node *lhs = new_var_node(vm, cg, from->tok);
    add_type(vm, lhs);
    Node *carrier =
        new_binary(vm, ND_ASSIGN, lhs,
                   new_cast(vm, new_long(vm, 0, from->tok), ty_int), from->tok);
    add_type(vm, carrier);
    carrier->checked_assign_dst_lo       = from->checked_assign_dst_lo;
    carrier->checked_assign_dst_hi       = from->checked_assign_dst_hi;
    carrier->checked_assign_dst_obj_init = from->checked_assign_dst_obj_init;
    carrier->checked_assign_src_lo       = from->checked_assign_src_lo;
    carrier->checked_assign_src_hi       = from->checked_assign_src_hi;
    from->checked_assign_dst_lo          = NULL;
    from->checked_assign_dst_hi          = NULL;
    from->checked_assign_dst_obj_init    = NULL;
    from->checked_assign_src_lo          = NULL;
    from->checked_assign_src_hi          = NULL;
    group_wrap_leaf(vm, after, carrier);
}

// An anchor kept for tracking after its group run: the stamp's expressions are
// remembered before group_move_stamp() takes them off the leaf.
typedef struct {
    GroupTarget at;
    Node       *leaf;
    Node       *dst_lo, *dst_hi, *dst_obj_init, *src_lo, *src_hi;
    int         resume; // first leaf the group run does not cover
} TrackAnchor;

typedef struct {
    TrackAnchor *v;
    int          n, cap;
} TrackAnchors;

static void track_note(TrackAnchors *t, int idx, Node *leaf,
                       const GroupTarget *at) {
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 4;
        t->v   = realloc(t->v, (size_t)t->cap * sizeof(TrackAnchor));
    }
    t->v[t->n++] =
        (TrackAnchor){.at           = *at,
                      .leaf         = leaf,
                      .dst_lo       = leaf->checked_assign_dst_lo,
                      .dst_hi       = leaf->checked_assign_dst_hi,
                      .dst_obj_init = leaf->checked_assign_dst_obj_init,
                      .src_lo       = leaf->checked_assign_src_lo,
                      .src_hi       = leaf->checked_assign_src_hi,
                      .resume       = idx + 1};
}

static void track_set_resume(TrackAnchors *t, Node *leaf, int resume) {
    for (int k = 0; k < t->n; k++)
        if (t->v[k].leaf == leaf)
            t->v[k].resume = resume;
}

// A `__cg = 0` carrying a checked-assignment stamp, from a group or a recheck.
static bool track_is_carrier(Node *n) {
    return n && n->kind == ND_ASSIGN && group_is_temp(n->lhs) &&
           n->checked_assign_dst_lo;
}

// `n` leaves in evaluation order; the last `protect_last` ones never take a
// carrier (the value of an expression-level comma chain). `tracks`, when
// given, receives every anchor of the stream.
static void group_process(VirtualMachine *vm, Obj *fn, Node **leaves, int n,
                          int protect_last, TrackAnchors *tracks) {
    int limit = n - protect_last;
    for (int i = 0; tracks && i < limit; i++) {
        GroupTarget t;
        if (group_anchor_target(leaves[i], &t))
            track_note(tracks, i, leaves[i], &t);
    }
    for (int i = 0; i < limit; i++) {
        GroupTarget at;
        if (!group_anchor_target(leaves[i], &at))
            continue;
        GroupTemp temps[8];
        int       ntemps = 0;
        Node     *joined[8];
        int       njoined = 0, last = -1, last_joined = 0;
        for (int j = i + 1; j < limit; j++) {
            Node *l = leaves[j];
            if (l->kind == ND_NULL_EXPR)
                continue;
            if (l->kind != ND_ASSIGN)
                break;
            GroupTarget bt;
            if (group_anchor_target(l, &bt)) {
                if (njoined == 8 || !group_anchors_related(&at, &bt))
                    break;
                joined[njoined++] = l;
                continue;
            }
            if (track_is_carrier(l))
                continue;
            if (l->checked_assign_dst_lo)
                break;
            if (group_is_temp(l->lhs)) {
                if (node_has_side_effects(l->rhs))
                    break;
                Node *r = group_peel_casts(l->rhs);
                if (r && r->kind == ND_ADDR && ntemps < 8)
                    temps[ntemps++] = (GroupTemp){l->lhs, r->lhs};
                continue;
            }
            Obj    *wv;
            Member *wm;
            Node   *wo;
            if (!group_write_target(l, temps, ntemps, &wv, &wm, &wo))
                break;
            bool sibling = wv ? group_target_has(&at, wv, NULL)
                              : (group_target_has(&at, NULL, wm) && at.mem &&
                                 !node_has_side_effects(wo) &&
                                 nodes_structurally_equal(at.obj, wo));
            if (!sibling || node_has_side_effects(l->rhs) ||
                group_reads_target(l->rhs, &at))
                break;
            last        = j;
            last_joined = njoined;
        }
        if (tracks) {
            int resume = last >= 0 ? last + 1 : i + 1;
            track_set_resume(tracks, leaves[i], resume);
            for (int k = 0; k < last_joined; k++)
                track_set_resume(tracks, joined[k], resume);
        }
        if (last < 0)
            continue;
        group_move_stamp(vm, fn, leaves[i], leaves[last]);
        for (int k = 0; k < last_joined; k++)
            group_move_stamp(vm, fn, joined[k], leaves[last]);
    }
}

typedef struct {
    Node **v;
    int    n, cap;
} GroupLeaves;

static void group_push(GroupLeaves *g, Node *leaf) {
    if (g->n == g->cap) {
        g->cap = g->cap ? g->cap * 2 : 16;
        g->v   = realloc(g->v, (size_t)g->cap * sizeof(Node *));
    }
    g->v[g->n++] = leaf;
}

// The spine of a comma chain, in evaluation order.
static void group_flatten(Node *e, GroupLeaves *g) {
    if (e && e->kind == ND_COMMA) {
        group_flatten(e->lhs, g);
        group_flatten(e->rhs, g);
        return;
    }
    if (e)
        group_push(g, e);
}

// A discarded statement's value wrappers: the `(x, ...) + c` a postfix ++/--
// builds.
static Node *group_peel_discarded(VirtualMachine *vm, Node *e) {
    for (;;) {
        if (e->kind == ND_CAST) {
            e = e->lhs;
        } else if ((e->kind == ND_ADD || e->kind == ND_SUB) && e->rhs &&
                   is_const_expr(vm, e->rhs)) {
            e = e->lhs;
        } else {
            return e;
        }
    }
}

// ---------------------------------------------------------------------------
// Tracked rechecks.
//
// The group run ends at the first statement that is not a sibling write. The
// pointer keeps the value the anchor stored, though, so each later write to a
// sibling of the same object is checked against the anchor's source snapshot
// (`__slo`/`__shi`, read before the pointer store) with a recheck carrier
// stamped like the anchor's own.
//
// Tracking runs forward through the anchor's block and ends at anything that
// could change the pointer, or which object it lives in: a write to the
// pointer or to a variable its object expression reads, a label, and (for an
// object that is not a private local) any call or indirect store. A nested
// statement is entered only when none of it ends tracking. A write whose value
// is used (`if ((s.n = f()))`) is not rechecked.

typedef enum { TV_SKIP, TV_RECHECK, TV_STOP } TrackVerdict;

// A variable whose address is taken. `via` is the local pointer the address
// initialises when the form is `via = &target`.
typedef struct {
    Obj  *root;
    Obj  *via;
    Node *target;
} AddrRoot;

// A local pointer's stores, and whether it is used other than as `*q`.
typedef struct {
    Obj *ptr;
    int  stores;
    bool leaked;
} PtrUse;

// What one function's body says about its locals' addresses.
typedef struct {
    AddrRoot *addrs;
    int       naddrs, caddrs;
    PtrUse   *ptrs;
    int       nptrs, cptrs;
    bool      scanned;
    bool      disabled; // block literals and indirect gotos: nothing is tracked
} TrackFn;

typedef struct {
    TrackAnchor *a;
    TrackFn     *tf;
    Node        *obj;       // the anchor's object, seen through a must-alias
    bool         priv;      // nothing but the function's own code reaches it
    bool         immediate; // wrap rechecks as found, not after the statement
    Obj         *objvars[8];
    int          nobjvars;
    GroupTemp    temps[8];
    int          ntemps;
    Obj         *cg;
} TrackCtx;

static void track_addr_add(TrackFn *tf, Obj *root, Obj *via, Node *target) {
    if (tf->naddrs == tf->caddrs) {
        tf->caddrs = tf->caddrs ? tf->caddrs * 2 : 8;
        tf->addrs  = realloc(tf->addrs, (size_t)tf->caddrs * sizeof(AddrRoot));
    }
    tf->addrs[tf->naddrs++] = (AddrRoot){root, via, target};
}

static bool track_tracked_ptr(Obj *v) {
    return v && v->is_local && !v->is_static && !v->is_param && v->name &&
           v->name[0] && v->ty && v->ty->kind == TY_PTR;
}

static PtrUse *track_ptr(TrackFn *tf, Obj *v) {
    for (int i = 0; i < tf->nptrs; i++)
        if (tf->ptrs[i].ptr == v)
            return &tf->ptrs[i];
    if (tf->nptrs == tf->cptrs) {
        tf->cptrs = tf->cptrs ? tf->cptrs * 2 : 8;
        tf->ptrs  = realloc(tf->ptrs, (size_t)tf->cptrs * sizeof(PtrUse));
    }
    tf->ptrs[tf->nptrs] = (PtrUse){.ptr = v};
    return &tf->ptrs[tf->nptrs++];
}

// The variable an lvalue or address expression is rooted at.
static Obj *track_root_var(Node *n) {
    while (n) {
        switch (n->kind) {
            case ND_VAR:
                return n->var;
            case ND_MEMBER:
            case ND_DEREF:
            case ND_CAST:
            case ND_ADD:
            case ND_SUB:
                n = n->lhs;
                break;
            default:
                return NULL;
        }
    }
    return NULL;
}

static void track_scan_one(Node *n, TrackFn *tf);

static void track_scan_list(Node *n, TrackFn *tf) {
    for (; n; n = n->next)
        track_scan_one(n, tf);
}

// Every `&x` and every array that decays outside `a[i]` exposes its root to
// writes the tracker cannot see. The `tmp = &x` a compound assignment hoists
// is not one: the tracker resolves it. A local pointer whose only store is
// `q = &x` and which is used only as `*q` / `q->f` is a must-alias of `x`.
static void track_scan_one(Node *n, TrackFn *tf) {
    if (!n)
        return;
    switch (n->kind) {
        case ND_BLOCK_LITERAL:
        case ND_BLOCK_CALL:
        case ND_GOTO_EXPR:
        case ND_LABEL_VAL:
            tf->disabled = true;
            break;
        case ND_ADDR: {
            Obj *r = track_root_var(n->lhs);
            if (r)
                track_addr_add(tf, r, NULL, NULL);
            if (track_tracked_ptr(r))
                track_ptr(tf, r)->leaked = true; // `&q->f` exposes the pointee
            break;
        }
        case ND_VAR:
            if (n->var && n->var->ty && n->var->ty->kind == TY_ARRAY)
                track_addr_add(tf, n->var, NULL, NULL);
            else if (track_tracked_ptr(n->var))
                track_ptr(tf, n->var)->leaked = true;
            return;
        case ND_DEREF: {
            Node *b = n->lhs;
            if (b && b->kind == ND_VAR && track_tracked_ptr(b->var))
                return;
            while (b && (b->kind == ND_ADD || b->kind == ND_SUB ||
                         b->kind == ND_CAST)) {
                track_scan_one(b->rhs, tf);
                b = b->lhs;
            }
            if (!(b && b->kind == ND_VAR && b->var && b->var->ty &&
                  b->var->ty->kind == TY_ARRAY))
                track_scan_one(b, tf);
            return;
        }
        case ND_ASSIGN: {
            Node *r    = group_peel_casts(n->rhs);
            bool  addr = r && r->kind == ND_ADDR;
            if (addr && group_is_temp(n->lhs)) {
                track_scan_one(r->lhs, tf);
                return;
            }
            if (n->lhs && n->lhs->kind == ND_VAR &&
                track_tracked_ptr(n->lhs->var)) {
                track_ptr(tf, n->lhs->var)->stores++;
                if (addr) {
                    Obj *root = track_root_var(r->lhs);
                    if (root)
                        track_addr_add(tf, root, n->lhs->var, r->lhs);
                    if (track_tracked_ptr(root))
                        track_ptr(tf, root)->leaked = true;
                    track_scan_one(r->lhs, tf);
                } else {
                    track_scan_one(n->rhs, tf);
                }
                return;
            }
            break;
        }
        default:
            break;
    }
    track_scan_one(n->lhs, tf);
    track_scan_one(n->rhs, tf);
    track_scan_one(n->cond, tf);
    track_scan_one(n->then, tf);
    track_scan_one(n->els, tf);
    track_scan_one(n->init, tf);
    track_scan_one(n->inc, tf);
    track_scan_list(n->body, tf);
    track_scan_list(n->args, tf);
}

// What `*q` is, when q is a must-alias; NULL otherwise.
static Node *track_alias_target(TrackFn *tf, Obj *q) {
    if (!track_tracked_ptr(q))
        return NULL;
    PtrUse *u = track_ptr(tf, q);
    if (u->stores != 1 || u->leaked)
        return NULL;
    Node *target = NULL;
    for (int i = 0; i < tf->naddrs; i++) {
        if (tf->addrs[i].root == q)
            return NULL;
        if (tf->addrs[i].via == q)
            target = tf->addrs[i].target;
    }
    return target && !node_has_side_effects(target) ? target : NULL;
}

// `o` with `*q` replaced by what the must-alias q points at.
static Node *track_alias_object(TrackFn *tf, Node *o) {
    if (o && o->kind == ND_DEREF && o->lhs && o->lhs->kind == ND_VAR) {
        Node *x = track_alias_target(tf, o->lhs->var);
        if (x)
            return x;
    }
    return o;
}

static bool track_collect_vars(Node *n, TrackCtx *cx) {
    if (!n)
        return true;
    if (n->kind == ND_VAR && n->var) {
        if (cx->nobjvars == 8)
            return false;
        cx->objvars[cx->nobjvars++] = n->var;
        return true;
    }
    return track_collect_vars(n->lhs, cx) && track_collect_vars(n->rhs, cx) &&
           track_collect_vars(n->cond, cx) && track_collect_vars(n->then, cx) &&
           track_collect_vars(n->els, cx);
}

// Reached only through its own function: no address is taken, except to
// initialise a must-alias.
static bool track_var_private(Obj *v, TrackFn *tf) {
    if (!(v && v->is_local && !v->is_static))
        return false;
    for (int i = 0; i < tf->naddrs; i++)
        if (tf->addrs[i].root == v &&
            !(tf->addrs[i].via && track_alias_target(tf, tf->addrs[i].via)))
            return false;
    return true;
}

static bool track_ctx_init(TrackCtx *cx, TrackAnchor *a, TrackFn *tf) {
    memset(cx, 0, sizeof(*cx));
    cx->a  = a;
    cx->tf = tf;
    if (a->at.var) {
        cx->priv = track_var_private(a->at.var, tf);
        return true;
    }
    cx->obj = track_alias_object(tf, a->at.obj);
    if (!track_collect_vars(a->at.obj, cx) ||
        (cx->obj != a->at.obj && !track_collect_vars(cx->obj, cx)))
        return false;
    Obj *root = track_root_var(cx->obj);
    cx->priv  = root && root->ty &&
                (root->ty->kind == TY_STRUCT || root->ty->kind == TY_UNION ||
                 root->ty->kind == TY_ARRAY) &&
                track_var_private(root, tf);
    return true;
}

static Node *track_unwrap(Node *n) {
    while (n && n->kind == ND_COMMA && track_is_carrier(n->rhs))
        n = n->lhs;
    return n;
}

// A write that replaces the pointer, or moves the object it lives in.
static bool track_write_hits(const TrackCtx *cx, Obj *wv, Member *wm) {
    if (wv) {
        if (wv == cx->a->at.var)
            return true;
        for (int i = 0; i < cx->nobjvars; i++)
            if (cx->objvars[i] == wv)
                return true;
    }
    return wm && wm == cx->a->at.mem;
}

static TrackVerdict track_node(Node *n, TrackCtx *cx, bool top);

// A store through `*q` (or the `*tmp` of a compound assignment on it) for a
// must-alias q: the write to the variable or member q points at.
static bool track_alias_write(TrackCtx *cx, Node *l, Obj **wv, Member **wm,
                              Node **wo) {
    if (l && l->kind == ND_DEREF && group_is_temp(l->lhs))
        l = group_temp_object(cx->temps, cx->ntemps, l->lhs);
    Node *x = track_alias_object(cx->tf, l);
    if (!x || x == l)
        return false;
    *wv = NULL;
    *wm = NULL;
    *wo = NULL;
    if (x->kind == ND_VAR) {
        *wv = x->var;
        return true;
    }
    if (x->kind == ND_MEMBER && x->lhs) {
        *wm = x->member;
        *wo = x->lhs;
        return true;
    }
    return false;
}

static TrackVerdict track_assign(Node *n, TrackCtx *cx, bool top) {
    if (track_node(n->rhs, cx, false) == TV_STOP ||
        track_node(n->lhs, cx, false) == TV_STOP)
        return TV_STOP;
    if (group_is_temp(n->lhs)) {
        Node *r = group_peel_casts(n->rhs);
        if (r && r->kind == ND_ADDR && cx->ntemps < 8)
            cx->temps[cx->ntemps++] = (GroupTemp){n->lhs, r->lhs};
        return TV_SKIP;
    }
    Obj    *wv;
    Member *wm;
    Node   *wo;
    if (group_write_target(n, cx->temps, cx->ntemps, &wv, &wm, &wo)) {
        if (wm)
            wo = track_alias_object(cx->tf, wo);
    } else if (!track_alias_write(cx, n->lhs, &wv, &wm, &wo)) {
        return cx->priv ? TV_SKIP : TV_STOP;
    }
    if (track_write_hits(cx, wv, wm))
        return TV_STOP;
    const GroupTarget *at = &cx->a->at;
    bool sibling          = wv ? group_target_has(at, wv, NULL)
                               : (group_target_has(at, NULL, wm) && at->mem &&
                                  !node_has_side_effects(wo) &&
                                  nodes_structurally_equal(cx->obj, wo));
    return sibling && top ? TV_RECHECK : TV_SKIP;
}

// What evaluating `n` does to the anchor. `top` is a discarded leaf, the only
// place a sibling write can take a recheck.
static TrackVerdict track_node(Node *n, TrackCtx *cx, bool top) {
    if (!n)
        return TV_SKIP;
    n              = track_unwrap(n);
    bool call_like = false;
    switch (n->kind) {
        case ND_ASSIGN:
            return track_assign(n, cx, top);
        case ND_MEMZERO:
            return track_write_hits(cx, n->var, NULL) ? TV_STOP : TV_SKIP;
        case ND_LABEL:
        case ND_CASE:
        case ND_LABEL_VAL:
        case ND_GOTO_EXPR:
        case ND_STMT_EXPR:
        case ND_BLOCK_LITERAL:
        case ND_BLOCK_CALL:
            return TV_STOP;
        case ND_FUNCALL:
        case ND_ASM:
        case ND_CAS:
        case ND_EXCH:
            call_like = true;
            break;
        default:
            break;
    }
    if (track_node(n->lhs, cx, false) == TV_STOP ||
        track_node(n->rhs, cx, false) == TV_STOP ||
        track_node(n->cond, cx, false) == TV_STOP ||
        track_node(n->then, cx, false) == TV_STOP ||
        track_node(n->els, cx, false) == TV_STOP)
        return TV_STOP;
    for (Node *a = n->args; a; a = a->next)
        if (track_node(a, cx, false) == TV_STOP)
            return TV_STOP;
    return call_like && !cx->priv ? TV_STOP : TV_SKIP;
}

static void track_wrap(VirtualMachine *vm, Obj *fn, Node *leaf, TrackCtx *cx) {
    TrackAnchor *a = cx->a;
    if (!cx->cg)
        cx->cg = new_checked_prop_temp(vm, fn, ty_int);
    Node *lhs = new_var_node(vm, cx->cg, leaf->tok);
    add_type(vm, lhs);
    Node *carrier =
        new_binary(vm, ND_ASSIGN, lhs,
                   new_cast(vm, new_long(vm, 0, leaf->tok), ty_int), leaf->tok);
    add_type(vm, carrier);
    carrier->checked_assign_dst_lo = clone_bounds_node(vm, a->dst_lo);
    carrier->checked_assign_dst_hi = clone_bounds_node(vm, a->dst_hi);
    carrier->checked_assign_dst_obj_init =
        clone_bounds_node(vm, a->dst_obj_init);
    carrier->checked_assign_src_lo = clone_bounds_node(vm, a->src_lo);
    carrier->checked_assign_src_hi = clone_bounds_node(vm, a->src_hi);
    group_wrap_leaf(vm, leaf, carrier);
}

// An expression statement's leaves, or a condition/value as one unit.
static bool track_expr(VirtualMachine *vm, Obj *fn, Node *e, bool discarded,
                       TrackCtx *cx, GroupLeaves *wraps) {
    if (!e)
        return true;
    if (!discarded)
        return track_node(e, cx, false) != TV_STOP;
    GroupLeaves g = {0};
    group_flatten(group_peel_discarded(vm, e), &g);
    bool ok = true;
    for (int i = 0; ok && i < g.n; i++) {
        TrackVerdict v = track_node(g.v[i], cx, true);
        if (v == TV_STOP)
            ok = false;
        else if (v == TV_RECHECK && cx->immediate)
            track_wrap(vm, fn, g.v[i], cx);
        else if (v == TV_RECHECK)
            group_push(wraps, g.v[i]);
    }
    free(g.v);
    return ok;
}

static bool track_stmt(VirtualMachine *vm, Obj *fn, Node *s, TrackCtx *cx,
                       GroupLeaves *wraps) {
    if (!s)
        return true;
    switch (s->kind) {
        case ND_NULL_EXPR:
        case ND_GOTO:
            return true;
        case ND_EXPR_STMT:
            return track_expr(vm, fn, s->lhs, true, cx, wraps);
        case ND_BLOCK:
            for (Node *t = s->body; t; t = t->next)
                if (!track_stmt(vm, fn, t, cx, wraps))
                    return false;
            return true;
        case ND_IF:
            return track_expr(vm, fn, s->cond, false, cx, wraps) &&
                   track_stmt(vm, fn, s->then, cx, wraps) &&
                   track_stmt(vm, fn, s->els, cx, wraps);
        case ND_FOR:
            return track_stmt(vm, fn, s->init, cx, wraps) &&
                   track_expr(vm, fn, s->cond, false, cx, wraps) &&
                   track_stmt(vm, fn, s->then, cx, wraps) &&
                   track_expr(vm, fn, s->inc, true, cx, wraps);
        case ND_DO:
            return track_stmt(vm, fn, s->then, cx, wraps) &&
                   track_expr(vm, fn, s->cond, false, cx, wraps);
        case ND_SWITCH:
            return track_expr(vm, fn, s->cond, false, cx, wraps) &&
                   track_stmt(vm, fn, s->then, cx, wraps);
        case ND_RETURN:
            return track_expr(vm, fn, s->lhs, false, cx, wraps);
        case ND_LABEL:
        case ND_CASE:
            return false;
        default:
            return track_node(s, cx, false) != TV_STOP;
    }
}

// Tracks each anchor of a stream through the rest of its leaves, then through
// the statements from `next_stmt` on.
// TODO: a count written where no pointer store is visible stays unchecked;
// a run-time extent check on the write would cover it (#1823).
static void track_anchors(VirtualMachine *vm, Obj *fn, TrackFn *tf,
                          Node **leaves, int n, TrackAnchors *as,
                          Node *next_stmt) {
    if (!as->n)
        return;
    if (!tf->scanned) {
        tf->scanned = true;
        track_scan_one(fn->body, tf);
    }
    if (tf->disabled)
        return;
    GroupLeaves wraps = {0};
    for (int k = 0; k < as->n; k++) {
        TrackCtx cx;
        if (!track_ctx_init(&cx, &as->v[k], tf))
            continue;
        bool live = true;
        for (int j = as->v[k].resume; live && j < n; j++) {
            TrackVerdict v = track_node(leaves[j], &cx, true);
            if (v == TV_STOP)
                live = false;
            else if (v == TV_RECHECK)
                track_wrap(vm, fn, leaves[j], &cx);
        }
        for (Node *s = next_stmt; live && s; s = s->next) {
            wraps.n      = 0;
            cx.immediate = s->kind == ND_EXPR_STMT ||
                           (s->kind == ND_BLOCK && s->is_decl_group);
            live         = track_stmt(vm, fn, s, &cx, &wraps);
            for (int w = 0; live && w < wraps.n; w++)
                track_wrap(vm, fn, wraps.v[w], &cx);
        }
    }
    free(wraps.v);
}

static void group_flush(VirtualMachine *vm, Obj *fn, TrackFn *tf,
                        GroupLeaves *g, Node *next_stmt) {
    TrackAnchors tracks = {0};
    group_process(vm, fn, g->v, g->n, 0, &tracks);
    track_anchors(vm, fn, tf, g->v, g->n, &tracks, next_stmt);
    free(tracks.v);
    g->n = 0;
}

static void group_stream_add(VirtualMachine *vm, Obj *fn, TrackFn *tf, Node *s,
                             GroupLeaves *g) {
    if (s->kind == ND_EXPR_STMT && s->lhs) {
        group_flatten(group_peel_discarded(vm, s->lhs), g);
    } else if (s->kind == ND_BLOCK && s->is_decl_group) {
        for (Node *t = s->body; t; t = t->next)
            group_stream_add(vm, fn, tf, t, g);
    } else {
        group_flush(vm, fn, tf, g, s);
    }
}

static void group_walk_one(VirtualMachine *vm, Obj *fn, TrackFn *tf,
                           Node *node);

static void group_walk(VirtualMachine *vm, Obj *fn, TrackFn *tf, Node *list) {
    for (Node *n = list; n; n = n->next)
        group_walk_one(vm, fn, tf, n);
}

static void group_walk_children(VirtualMachine *vm, Obj *fn, TrackFn *tf,
                                Node *node) {
    group_walk_one(vm, fn, tf, node->lhs);
    group_walk_one(vm, fn, tf, node->rhs);
    group_walk_one(vm, fn, tf, node->cond);
    group_walk_one(vm, fn, tf, node->then);
    group_walk_one(vm, fn, tf, node->els);
    group_walk_one(vm, fn, tf, node->init);
    group_walk_one(vm, fn, tf, node->inc);
    group_walk(vm, fn, tf, node->body);
    group_walk(vm, fn, tf, node->args);
}

// Groups the statement lists of every block and the comma chains inside
// expressions (a compound literal's initializer).
static void group_walk_one(VirtualMachine *vm, Obj *fn, TrackFn *tf,
                           Node *node) {
    if (!node)
        return;
    if (node->kind == ND_BLOCK) {
        GroupLeaves g = {0};
        for (Node *s = node->body; s; s = s->next)
            group_stream_add(vm, fn, tf, s, &g);
        group_flush(vm, fn, tf, &g, NULL);
        free(g.v);
    } else if (node->kind == ND_COMMA) {
        GroupLeaves g = {0};
        group_flatten(node, &g);
        group_process(vm, fn, g.v, g.n, 1, NULL);
        for (int i = 0; i < g.n; i++) {
            if (g.v[i]->kind == ND_COMMA) // a leaf wrapped with its carrier
                group_walk_children(vm, fn, tf, g.v[i]->lhs);
            else
                group_walk_one(vm, fn, tf, g.v[i]);
        }
        free(g.v);
        return;
    }
    group_walk_children(vm, fn, tf, node);
}

void group_checked_sibling_writes(VirtualMachine *vm, Obj *fn) {
    if (!(vm->flags & CCCC_CHECKED_BOUNDS) || !fn->body)
        return;
    TrackFn tf = {0};
    group_walk_one(vm, fn, &tf, fn->body);
    free(tf.addrs);
    free(tf.ptrs);
}

// The tail passes of every function body, in order: propagation, the
// pending `dynamic` casts it decides, then #944's assignment verification.
void checked_tail_passes(VirtualMachine *vm, Obj *fn) {
    propagate_checked_bounds(vm, fn);
    resolve_pending_dynamic_casts(vm, fn);
    verify_checked_assign_bounds(vm, fn);
    group_checked_sibling_writes(vm, fn);
    if (fn->checked_ret_entry && fn->body) {
        Node *init =
            new_unary(vm, ND_EXPR_STMT, fn->checked_ret_entry, fn->body->tok);
        add_type(vm, init->lhs);
        init->next            = fn->body->body;
        fn->body->body        = init;
        fn->checked_ret_entry = NULL;
    }
}

typedef struct {
    Obj *param, *copy;
} RetEntryCopy;

// Points every parameter reference in the return-bounds tree `n` at a local
// holding its entry value, creating the local on first use.
static void retarget_ret_params(VirtualMachine *vm, Obj *fn, Node *n,
                                RetEntryCopy *map, int *count) {
    if (!n)
        return;
    if (n->kind == ND_VAR && n->var) {
        for (Obj *p = fn->params; p; p = p->next) {
            if (p != n->var)
                continue;
            Obj *copy = NULL;
            for (int i = 0; i < *count; i++)
                if (map[i].param == p)
                    copy = map[i].copy;
            if (!copy) {
                copy = new_lvar(vm, "", 0, plain_value_type(vm, p->ty));
                map[(*count)++] = (RetEntryCopy){p, copy};
            }
            n->var = copy;
            n->ty  = copy->ty;
            break;
        }
        return;
    }
    retarget_ret_params(vm, fn, n->lhs, map, count);
    retarget_ret_params(vm, fn, n->rhs, map, count);
    retarget_ret_params(vm, fn, n->cond, map, count);
    retarget_ret_params(vm, fn, n->then, map, count);
    retarget_ret_params(vm, fn, n->els, map, count);
}

// Declares the local a checked return value is assigned through; see
// rewrite_checked_return(). Called from function() once all parameters are
// in scope, so a bound naming one (`count(n)`) resolves there and a body
// local can't shadow it. Block literals infer their return type and are
// not covered.
void declare_checked_return_var(VirtualMachine *vm, Obj *fn) {
    Type *rty = fn->ty->return_ty;
    if (!(vm->flags & CCCC_CHECKED_BOUNDS) || !rty || rty->kind != TY_PTR ||
        rty->checked_kind == CHECKED_NONE)
        return;
    bool has_form = rty->checked_kind == CHECKED_SINGLE ||
                    (rty->checked_bounds_form != CB_NONE &&
                     rty->checked_bounds_form != CB_UNKNOWN);
    if (!has_form)
        return;
    Obj *var = new_lvar(vm, "", 0, rty);
    int  nparams = 0;
    for (Obj *p = fn->params; p; p = p->next)
        nparams++;
    resolve_param_checked_bounds(vm, fn->ty);
    Obj **params = arena_alloc(&vm->compiler.parser_arena,
                               sizeof(Obj *) * (nparams + 1));
    int   i      = 0;
    for (Obj *p = fn->params; p; p = p->next)
        params[i++] = p;
    resolve_checked_template_bounds(
        vm, params, nparams, fn->ty->checked_ret_bounds_lo,
        fn->ty->checked_ret_bounds_hi, 0, &var->checked_bounds_lo,
        &var->checked_bounds_hi);
    fn->checked_ret_var = var;

    // Callers evaluate the return bounds with the argument values they
    // passed, so `return` is verified against entry copies of the parameters
    // the bounds name rather than their possibly-reassigned values.
    RetEntryCopy *map =
        arena_alloc(&vm->compiler.parser_arena, sizeof(*map) * (nparams + 1));
    int count = 0;
    retarget_ret_params(vm, fn, var->checked_bounds_lo, map, &count);
    retarget_ret_params(vm, fn, var->checked_bounds_hi, map, &count);
    Token *tok   = fn->ty->name;
    Node  *chain = NULL;
    for (int i = 0; i < count; i++) {
        Node *dst = new_var_node(vm, map[i].copy, tok);
        add_type(vm, dst);
        Node *src = new_var_node(vm, map[i].param, tok);
        add_type(vm, src);
        Node *store = new_binary(vm, ND_ASSIGN, dst, src, tok);
        add_type(vm, store);
        chain = chain ? new_binary(vm, ND_COMMA, chain, store, tok) : store;
        add_type(vm, chain);
    }
    fn->checked_ret_entry = chain;
}

// `return E` in a function with a checked return type becomes
// `return (__cr = E, __cr)` when E is rooted at a checked pointer or a
// propagation candidate, the shape #944 already verifies for `q = E`: the
// bounds are checked against the return type's, evaluated with the
// parameters' values at the return.
Node *rewrite_checked_return(VirtualMachine *vm, Obj *fn, Node *exp,
                             Token *tok) {
    if (!fn || !fn->checked_ret_var)
        return exp;
    CheckedBase base = find_checked_base(exp);
    if (!checked_base_is_declared(base) &&
        !(base.var && base.var->checked_prop_candidate) &&
        !checked_call_root(exp) && !checked_cast_root(exp) &&
        !checked_plain_array_root(exp, &(CheckedBase){0}))
        return exp;

    Node *lhs = new_var_node(vm, fn->checked_ret_var, tok);
    add_type(vm, lhs);
    Node *store = new_binary(vm, ND_ASSIGN, lhs, exp, tok);
    add_type(vm, store);
    Node *result = new_var_node(vm, fn->checked_ret_var, tok);
    add_type(vm, result);
    Node *comma = new_binary(vm, ND_COMMA, store, result, tok);
    add_type(vm, comma);
    return comma;
}

// #486: desugars a checked-annotated cast -- `(T [[cccc::array,
// cccc::count(n), cccc::assume]])expr` -- into
// `(__cv = (T [[...]])expr, __cv)`, where `__cv` is a fresh,
// compiler-generated local declared with the cast's own checked kind and
// bounds form. This is deliberately NOT a special ND_CAST-rooted case in
// find_checked_base(): making `__cv` a REAL declared-checked local means
// every existing pass -- CHKR emission (set_checked_deref_bounds()),
// #919/#941/#942 propagation, #944's verify_checked_assign_scan() -- sees
// exactly the shape it already knows how to handle, with no new resolution
// path needed. `cast_node` is the already-built ND_CAST; `ty` is its
// checked-annotated destination type (== cast_node->ty, passed separately
// so the caller doesn't have to re-read a field it just set); `tok` is the
// cast's start token, used for every diagnostic and node built here.
//
// Only called once check_checked_cast_dynamic_source() has already passed
// and CCCC_CHECKED_BOUNDS is set (see the call site in cast(),
// src/parse_expr.c) -- this function's own precondition:
//  - Must be inside a function body. vm->compiler.current_fn is NULL at
//    file scope (e.g. a global initializer), where new_lvar() has no
//    enclosing local-variable list to attach to and a comma-with-assignment
//    isn't a constant expression anyway.
Node *checked_cast_desugar(VirtualMachine *vm, Node *cast_node, Type *ty,
                           Token *tok) {
    if (!vm->compiler.current_fn)
        error_tok(vm, tok,
                  "a checked bounds cast ('assume'/'dynamic') is only valid "
                  "inside a function body");

    // Anonymous temp: same "" name / new_lvar prepend pattern as every
    // other compiler-generated checked-pointer temp (new_checked_prop_temp
    // above), so it never collides with a real identifier and needs no
    // uniqueness counter of its own.
    Obj *var               = new_lvar(vm, "", 0, ty);
    var->checked_cast_kind = ty->checked_cast_kind;
    resolve_checked_bounds(vm, var);

    Node *lhs = new_var_node(vm, var, tok);
    add_type(vm, lhs);
    Node *store = new_binary(vm, ND_ASSIGN, lhs, cast_node, tok);
    add_type(vm, store);

    Node *result = new_var_node(vm, var, tok);
    add_type(vm, result);
    Node *comma = new_binary(vm, ND_COMMA, store, result, tok);
    add_type(vm, comma);
    return comma;
}

// ---------------------------------------------------------------------
// Checked regions (#485): [[cccc::checked]]/[[cccc::unchecked]] (function
// definitions and compound statements) and #pragma cccc checked/unchecked
// begin/end (a positional, file-scope-restricted TU default). Unlike the
// checked-*pointer* attributes above, a region carries no Type/Obj/Node
// state of its own -- it is purely diagnostics (an unchecked pointer
// declaration, an array pointer with no bounds, and casts or implicit
// conversions that drop or fabricate a checked kind) that fire while a
// region is in effect. Always on, independent of
// --checked-pointers -- same footing as the single-pointer-arithmetic ban
// in parse_expr.c and the bounds side-effect rejection in parse_core.c.
// ---------------------------------------------------------------------

// The only reader of Token.checked_scope (the #pragma-introduced region,
// stamped by the preprocessor onto every command-line-input token) and
// Compiler.checked_scope_attr (an enclosing [[cccc::checked]]/
// [[cccc::unchecked]] attribute, saved/restored by the parser around each
// function body and attributed compound statement -- see function()
// (src/parse_decl.c) and stmt() (src/parse_stmt.c)). An attribute region
// always overrides the pragma stamp for its lexical extent; there is no
// interleaving case to adjudicate because a pragma region can only ever be
// opened/closed at file scope (outside any function body), so the stamp on
// every token inside one attribute frame's extent is constant. Nothing
// outside this function should read either field directly -- that is what
// keeps the two sources from drifting apart.
CheckedScope cc_checked_scope_at(VirtualMachine *vm, Token *tok) {
    if (vm->compiler.checked_scope_attr != CHECKED_SCOPE_UNSET)
        return vm->compiler.checked_scope_attr;
    return tok ? tok->checked_scope : CHECKED_SCOPE_UNSET;
}

CheckedScope cc_checked_scope_keyword(Token *tok) {
    if (!tok || tok->kind != TK_IDENT)
        return CHECKED_SCOPE_UNSET;
    if (equal(tok, "_Checked"))
        return CHECKED_SCOPE_ON;
    if (equal(tok, "_Unchecked"))
        return CHECKED_SCOPE_OFF;
    return CHECKED_SCOPE_UNSET;
}

void cc_set_checked_scope(VirtualMachine *vm, VarAttr *attr, CheckedScope want,
                          Token *tok) {
    if (attr->checked_scope != CHECKED_SCOPE_UNSET &&
        attr->checked_scope != want)
        error_tok(vm, tok,
                  "'checked' and 'unchecked' cannot both apply to the same "
                  "declaration");
    attr->checked_scope = want;
}

// `_Checked int f(void) { ... }`: the keyword counts as a specifier only when
// a type or attribute follows, so `int _Checked = 3;` and `T _Checked[4]`
// keep `_Checked` as an ordinary name.
bool cc_checked_spec_start(VirtualMachine *vm, Token *tok) {
    if (cc_checked_scope_keyword(tok) == CHECKED_SCOPE_UNSET ||
        find_typedef(vm, tok))
        return false;
    Token *next = tok->next;
    return is_typename_ex(vm, next, false) || equal(next, "__attribute__") ||
           (equal(next, "[") && equal(next->next, "["));
}

// A region written before the declarators applies to a function definition
// only.
void cc_reject_checked_specifier(VirtualMachine *vm, Token *tok, Type *basety,
                                 VarAttr *attr) {
    if (attr->is_typedef || !is_function(vm, tok, basety))
        cc_reject_checked_scope_here(vm, attr);
}

void cc_reject_checked_scope_here(VirtualMachine *vm, VarAttr *attr) {
    if (attr->checked_spec_tok)
        error_tok(vm, attr->checked_spec_tok,
                  "'%.*s' as a declaration specifier applies only to a "
                  "function definition",
                  attr->checked_spec_tok->len, attr->checked_spec_tok->loc);
    if (attr->checked_attr_tok)
        error_tok(vm, attr->checked_attr_tok,
                  "'%.*s' applies to a function definition or a compound "
                  "statement, not to this declaration",
                  attr->checked_attr_tok->len, attr->checked_attr_tok->loc);
}

static bool is_null_pointer_constant(Node *e) {
    if (e && e->ty && e->ty->kind == TY_NULLPTR_T)
        return true;
    while (e && e->kind == ND_CAST)
        e = e->lhs;
    return e && e->kind == ND_NUM && e->ty && is_integer(e->ty) && e->val == 0;
}

// Whether `e` may flow into a checked pointer without fabricating bounds: a
// null constant, an already-checked pointer, or (implicit conversions only,
// `visible_ok`) an object whose extent the compiler can see.
static bool checked_source_ok(Node *e, bool visible_ok) {
    if (!e)
        return true;
    if (is_null_pointer_constant(e))
        return true;
    if (e->ty && e->ty->kind == TY_PTR && e->ty->checked_kind != CHECKED_NONE)
        return true;
    if (checked_base_is_declared(find_checked_deref_base(e)))
        return true;
    if (visible_ok && e->ty && e->ty->kind == TY_ARRAY)
        return true;
    switch (e->kind) {
        case ND_ADDR:
            return visible_ok;
        case ND_COND:
            return checked_source_ok(e->then, visible_ok) &&
                   checked_source_ok(e->els, visible_ok);
        case ND_COMMA:
            return checked_source_ok(e->rhs, visible_ok);
        case ND_ADD:
        case ND_SUB:
        case ND_CAST:
            return checked_source_ok(e->lhs, visible_ok);
        default:
            return false;
    }
}

static bool is_checked_ptr_target(Type *ty) {
    return ty && ty->kind == TY_PTR && ty->checked_kind != CHECKED_NONE &&
           !(ty->base && ty->base->kind == TY_FUNC);
}

// A region violation is a warning only when it has been explicitly demoted;
// -Wno-checked-regions, or any pass that clears the masks, leaves it an error.
static bool checked_region_demoted(VirtualMachine *vm, Token *tok) {
    uint64_t mask     = CCCC_WARN_CHECKED_REGIONS;
    uint64_t warnings = tok && tok->diag_warnings
                            ? tok->diag_warnings & ~(1ULL << 63)
                            : vm->compiler.warnings;
    uint64_t werror = tok && tok->diag_werror ? tok->diag_werror & ~(1ULL << 63)
                                              : vm->compiler.warning_errors;
    if (!(warnings & mask) || (werror & mask))
        return false;
    return !(vm->warnings_as_errors &&
             !(vm->compiler.warning_no_errors & mask));
}

static void checked_region_diag(VirtualMachine *vm, Token *tok, const char *fmt,
                                ...) {
    char    msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (checked_region_demoted(vm, tok))
        warn_tok(vm, tok, CCCC_WARN_CHECKED_REGIONS, "%s", msg);
    else
        error_tok(vm, tok, "%s", msg);
}

// Explicit-cast ban list inside a checked region. Called from cast() for a
// cast with no assume/dynamic annotation.
void cc_check_checked_scope_cast(VirtualMachine *vm, Node *expr, Type *ty,
                                 Token *tok) {
    if (ty && ty->kind == TY_PTR && ty->checked_kind == CHECKED_NONE &&
        !(ty->base && ty->base->kind == TY_FUNC)) {
        if (is_null_pointer_constant(expr))
            return;
        checked_region_diag(vm, tok,
                            "cast to an unchecked pointer type is not allowed "
                            "in a checked region -- wrap this code in "
                            "[[cccc::unchecked]] { ... }");
        return;
    }
    if (!is_checked_ptr_target(ty))
        return;
    if (expr->ty && expr->ty->kind == TY_PTR &&
        expr->ty->checked_kind != CHECKED_NONE &&
        ty->checked_kind != expr->ty->checked_kind) {
        checked_region_diag(vm, tok,
                            "cast changes a pointer's checked kind in a "
                            "checked region -- wrap this code in "
                            "[[cccc::unchecked]] { ... }");
        return;
    }
    if (ty->checked_kind != CHECKED_SINGLE &&
        ty->checked_bounds_form == CB_NONE) {
        checked_region_diag(vm, tok,
                            "cast to an array pointer with no bounds in a "
                            "checked region -- add count(), byte_count() or "
                            "bounds(), or bounds(unknown) to opt out");
        return;
    }
    if (ty->checked_bounds_form != CB_UNKNOWN &&
        !checked_source_ok(expr, /*visible_ok=*/false))
        checked_region_diag(
            vm, tok,
            "cast from an unchecked pointer to a checked pointer "
            "type is not allowed in a checked region -- use "
            "[[cccc::dynamic]] or [[cccc::assume]], or wrap this code "
            "in [[cccc::unchecked]] { ... }");
}

// Implicit-conversion ban inside a checked region: init, assignment, call
// argument and return into a checked pointer.
void cc_check_checked_scope_conv(VirtualMachine *vm, Node *src, Type *to,
                                 Token *tok) {
    if (vm->compiler.in_type_lookahead || !src || !is_checked_ptr_target(to) ||
        to->checked_bounds_form == CB_UNKNOWN)
        return;
    if (cc_checked_scope_at(vm, tok) != CHECKED_SCOPE_ON)
        return;
    add_type(vm, src);
    if (checked_source_ok(src, /*visible_ok=*/true))
        return;
    checked_region_diag(
        vm, tok,
        "implicit conversion from an unchecked pointer to a checked "
        "pointer is not allowed in a checked region -- cast with "
        "[[cccc::assume]] or [[cccc::dynamic]], or move it into an "
        "[[cccc::unchecked]] block");
}

// Declaration-ban diagnostic (#485 v1 ban list): a local, parameter, global
// or struct/union member whose type is an unchecked pointer or a plain array,
// declared while a checked region is in effect. Called once per
// declarator from declaration()/global_variable() (src/parse_init.c,
// src/parse_decl.c), create_param_lvars() (src/parse_decl.c) and the
// struct/union member loop (src/parse_types.c) -- never from new_lvar()/
// new_gvar() directly, which would also fire on compiler-synthesized
// pointer locals (__alloca_size__, __static_link, propagation temps) that
// carry no user-visible checked-kind annotation at all.
//
// A function-pointer type is deliberately exempt: there is no bounds form
// that applies to it, so banning it would make a checked region unable to
// hold a callback local at all -- a hole in the ban, not a gap in
// enforcement. Guarded on !in_type_lookahead like every other checked-
// pointer frontend check (parse_expr.c's single-arithmetic ban,
// resolve_bounds_tokens()'s side-effect rejection): is_typename_ex()'s
// speculative probes must never fire a fatal error_tok() for a declaration
// that is ultimately discarded.
void cc_check_checked_scope_decl(VirtualMachine *vm, Type *ty, Token *name_tok,
                                 const char *what) {
    if (vm->compiler.in_type_lookahead || !ty || !name_tok)
        return;
    if ((ty->kind == TY_ARRAY || ty->kind == TY_VLA) &&
        ty->checked_kind == CHECKED_NONE && !ty->header_array_typedef &&
        cc_checked_scope_at(vm, name_tok) == CHECKED_SCOPE_ON)
        checked_region_diag(vm, name_tok,
                            "unchecked array %s '%.*s' is not allowed in a "
                            "checked region -- declare it _Checked[N] or "
                            "_Nt_checked[N], or move it into an "
                            "[[cccc::unchecked]] block",
                            what, name_tok->len, name_tok->loc);
    while (ty->kind == TY_ARRAY || ty->kind == TY_VLA)
        ty = ty->base;
    if (ty->kind != TY_PTR)
        return;
    if (ty->base && ty->base->kind == TY_FUNC)
        return;
    if (cc_checked_scope_at(vm, name_tok) != CHECKED_SCOPE_ON)
        return;
    if (ty->checked_kind != CHECKED_NONE) {
        if (ty->checked_kind != CHECKED_SINGLE &&
            ty->checked_bounds_form == CB_NONE)
            checked_region_diag(
                vm, name_tok,
                "%s '%.*s' has no bounds in a checked region -- add "
                "count(), byte_count() or bounds(), or bounds(unknown) "
                "to opt out",
                what, name_tok->len, name_tok->loc);
        return;
    }
    checked_region_diag(vm, name_tok,
                        "unchecked pointer %s '%.*s' is not allowed in a "
                        "checked region -- declare it [[cccc::single]], "
                        "[[cccc::array]] or [[cccc::ntarray]], or move it "
                        "into an [[cccc::unchecked]] block",
                        what, name_tok->len, name_tok->loc);
}

// A compound literal of plain array type creates an unchecked array object.
void cc_check_checked_scope_compound_literal(VirtualMachine *vm, Type *ty,
                                             Token *tok) {
    if (vm->compiler.in_type_lookahead || !ty ||
        (ty->kind != TY_ARRAY && ty->kind != TY_VLA) ||
        ty->checked_kind != CHECKED_NONE || ty->header_array_typedef ||
        cc_checked_scope_at(vm, tok) != CHECKED_SCOPE_ON)
        return;
    checked_region_diag(vm, tok,
                        "compound literal of an unchecked array type is not "
                        "allowed in a checked region -- write "
                        "(T _Checked[N]){...}, or move it into an "
                        "[[cccc::unchecked]] block");
}

static bool is_unchecked_data_ptr(Type *ty) {
    return ty && ty->kind == TY_PTR && ty->checked_kind == CHECKED_NONE &&
           !(ty->base && ty->base->kind == TY_FUNC);
}

// A named function declared in a command-line input and not marked
// [[cccc::trusted]]. Header declarations (all of libc) are never watched.
// TODO(#1789): calls through a function pointer are not watched either.
CheckedCallWatch cc_checked_call_watch(VirtualMachine *vm, Node *fn,
                                       Token *tok) {
    CheckedCallWatch w = {0};
    if (vm->compiler.in_type_lookahead ||
        cc_checked_scope_at(vm, tok) != CHECKED_SCOPE_ON)
        return w;
    if (fn->kind != ND_VAR || !fn->var || !fn->var->is_function ||
        fn->var->is_trusted || !fn->var->tok || !fn->var->tok->file ||
        !cc_file_is_command_line_input(vm, fn->var->tok->file->name))
        return w;
    w.on     = true;
    w.callee = fn->var->name;
    return w;
}

// A checked pointer argument reaching an unchecked pointer parameter.
void cc_check_checked_call_arg(VirtualMachine *vm, CheckedCallWatch *w,
                               Node *arg, Type *param_ty, int index,
                               Token *tok) {
    if (!w->on || !is_unchecked_data_ptr(param_ty))
        return;
    add_type(vm, arg);
    bool checked = (arg->ty && arg->ty->kind == TY_PTR &&
                    arg->ty->checked_kind != CHECKED_NONE) ||
                   checked_base_is_declared(find_checked_deref_base(arg));
    if (!checked)
        return;
    w->arg_warned = true;
    warn_tok(vm, arg->tok ? arg->tok : tok, CCCC_WARN_CHECKED_CALLS,
             "checked pointer passed to unchecked parameter %d of '%s' -- its "
             "bounds are not carried across the call",
             index, w->callee);
}

// A call into a prototype with an unchecked pointer parameter or return type.
void cc_check_checked_call(VirtualMachine *vm, CheckedCallWatch *w, Type *fn_ty,
                           Token *tok) {
    if (!w->on || w->arg_warned)
        return;
    int index = 1;
    for (Type *p = fn_ty->params; p; p = p->next, index++) {
        if (!is_unchecked_data_ptr(p))
            continue;
        warn_tok(vm, tok, CCCC_WARN_CHECKED_CALLS,
                 "call to '%s' with an unchecked pointer parameter %d -- "
                 "mark the prototype [[cccc::trusted]] or give it checked "
                 "pointer types",
                 w->callee, index);
        return;
    }
    if (is_unchecked_data_ptr(fn_ty->return_ty))
        warn_tok(vm, tok, CCCC_WARN_CHECKED_CALLS,
                 "call to '%s' returns an unchecked pointer -- mark the "
                 "prototype [[cccc::trusted]] or give it a checked return "
                 "type",
                 w->callee);
}

// A constant-initialized checked pointer whose source is a static array the
// declared bounds overrun, e.g. `struct S g = {8, buf}` with `int buf[2]`.
// The bound is evaluated with each sibling member replaced by its own
// initializer; one that is not a compile-time constant is skipped.
static Node *static_init_bounds(VirtualMachine *vm, Node *n,
                                Initializer *siblings) {
    if (!n)
        return NULL;
    if (n->kind == ND_VAR && n->var && n->var->checked_self_member) {
        if (!siblings)
            return NULL;
        Initializer *si = siblings->children[n->var->checked_self_member->idx];
        if (si && si->expr)
            return si->expr;
        return new_long(vm, 0, n->tok);
    }
    Node *c       = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    *c            = *n;
    c->next       = NULL;
    Node **kids[] = {&c->lhs, &c->rhs, &c->cond, &c->then, &c->els};
    for (int i = 0; i < 5; i++) {
        Node *k = *kids[i];
        if (!k)
            continue;
        *kids[i] = static_init_bounds(vm, k, siblings);
        if (!*kids[i])
            return NULL;
    }
    return c;
}

static void check_static_checked_slot(VirtualMachine *vm, Node *expr, Type *pty,
                                      CheckedKind kind, CheckedBoundsForm form,
                                      Node *bound, Initializer *siblings) {
    Obj *src;
    int  off;
    if (!expr || kind == CHECKED_NONE || pty->kind != TY_PTR ||
        !objsize_peel_offset_chain(vm, expr, &src, &off))
        return;
    Type *aty = src->ty;
    if (!aty || aty->kind != TY_ARRAY || aty->size <= 0 || off < 0)
        return;

    int64_t elem = pty->base ? get_vm_size(pty->base) : 1;
    int64_t need;
    if (kind == CHECKED_SINGLE) {
        need = elem;
    } else if (form == CB_COUNT || form == CB_BYTE_COUNT) {
        Node *n = static_init_bounds(vm, bound, siblings);
        if (!n || !is_const_expr(vm, n))
            return;
        int64_t v = eval(vm, n);
        if (v < 0)
            return;
        need = form == CB_COUNT ? v * elem : v;
        if (kind == CHECKED_NTARRAY)
            need += elem;
    } else {
        return;
    }
    if (off + need > aty->size)
        error_tok(vm, expr->tok,
                  "checked initializer needs %lld bytes but the source array "
                  "has %lld from this offset",
                  (long long)need, (long long)(aty->size - off));
}

static void check_static_checked_walk(VirtualMachine *vm, Initializer *init,
                                      Type *ty) {
    if (!init)
        return;
    switch (ty->kind) {
        case TY_ARRAY:
            if (ty->base->kind != TY_PTR && ty->base->kind != TY_STRUCT &&
                ty->base->kind != TY_UNION && ty->base->kind != TY_ARRAY)
                return;
            if (!init->children)
                return;
            for (int i = 0; i < ty->array_len; i++)
                check_static_checked_walk(vm, init->children[i], ty->base);
            return;
        case TY_STRUCT:
            if (!init->children)
                return;
            for (Member *m = ty->members; m; m = m->next) {
                if (m->is_bitfield)
                    continue;
                Initializer *child = init->children[m->idx];
                if (!child)
                    continue;
                if (m->ty->kind == TY_PTR)
                    check_static_checked_slot(
                        vm, child->expr, m->ty, m->ty->checked_kind,
                        m->ty->checked_bounds_form, m->checked_bounds_hi, init);
                else
                    check_static_checked_walk(vm, child, m->ty);
            }
            return;
        case TY_UNION:
            if (!init->mem || !init->children)
                return;
            if (init->mem->ty->kind == TY_PTR)
                check_static_checked_slot(
                    vm, init->children[init->mem->idx]->expr, init->mem->ty,
                    init->mem->ty->checked_kind,
                    init->mem->ty->checked_bounds_form,
                    init->mem->checked_bounds_hi, NULL);
            else
                check_static_checked_walk(vm, init->children[init->mem->idx],
                                          init->mem->ty);
            return;
        default:
            return;
    }
}

void check_static_checked_init(VirtualMachine *vm, Obj *var,
                               Initializer *init) {
    if (!(vm->flags & CCCC_CHECKED_BOUNDS) || !init)
        return;
    if (var->ty->kind == TY_PTR)
        check_static_checked_slot(vm, init->expr, var->ty, var->checked_kind,
                                  var->checked_bounds_form,
                                  var->checked_bounds_hi, NULL);
    else
        check_static_checked_walk(vm, init, var->ty);
}
