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
} CheckedBase;

static CheckedBase find_checked_base(Node *n) {
    while (n) {
        switch (n->kind) {
            case ND_VAR:
                return (CheckedBase){.var = n->var};
            case ND_MEMBER:
                return (CheckedBase){.mem = n->member, .obj = n->lhs};
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

// True if `base` names a declared checked pointer (variable or member) --
// i.e. find_checked_base() found a recognised root at all. Doesn't imply a
// bounds *form* is present (CB_NONE/CB_UNKNOWN roots still count as
// "declared checked" here; compute_checked_bounds() is what decides whether
// there's anything to enforce).
static bool checked_base_is_declared(CheckedBase base) {
    if (base.var)
        return base.var->checked_kind != CHECKED_NONE;
    if (base.mem)
        return base.mem->ty->checked_kind != CHECKED_NONE;
    return false;
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
static Node *checked_base_self_expr(VirtualMachine *vm, CheckedBase base,
                                    Token *tok) {
    if (base.var) {
        Node *p = new_var_node(vm, base.var, tok);
        add_type(vm, p);
        return p;
    }
    Node *member_node =
        new_unary(vm, ND_MEMBER, clone_bounds_node(vm, base.obj), tok);
    member_node->member = base.mem;
    add_type(vm, member_node);
    return member_node;
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

static Obj *new_checked_prop_temp(VirtualMachine *vm, Obj *fn, Type *ty);

static void compute_checked_bounds(VirtualMachine *vm, CheckedBase base,
                                   Type *access_ty, Token *tok, Node **out_lo,
                                   Node **out_hi, bool *out_nt,
                                   Node **out_obj_init, Obj *hoist_fn) {
    *out_lo = NULL;
    *out_hi = NULL;
    *out_nt = false;
    if (out_obj_init)
        *out_obj_init = NULL;
    if (!base.var && !base.mem)
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
    if (base.mem && node_has_side_effects(base.obj))
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
    if (out_obj_init && base.mem && (vm->flags & CCCC_CHECKED_BOUNDS) &&
        (hoist_fn || vm->compiler.current_fn) &&
        !checked_obj_is_trivial(base.obj) &&
        checked_obj_is_addressable(base.obj)) {
        Obj  *t = hoist_fn ? new_checked_prop_temp(vm, hoist_fn,
                                                   pointer_to(vm, base.obj->ty))
                           : new_lvar(vm, "", 0, pointer_to(vm, base.obj->ty));
        Node *addr =
            new_unary(vm, ND_ADDR, clone_bounds_node(vm, base.obj), tok);
        add_type(vm, addr);
        Node *init =
            new_binary(vm, ND_ASSIGN, new_var_node(vm, t, tok), addr, tok);
        add_type(vm, init);
        *out_obj_init  = init;
        Node *obj_temp = new_unary(vm, ND_DEREF, new_var_node(vm, t, tok), tok);
        add_type(vm, obj_temp);
        base.obj = obj_temp;
    }

    CheckedKind kind =
        base.var ? base.var->checked_kind : base.mem->ty->checked_kind;
    CheckedBoundsForm form = base.var ? base.var->checked_bounds_form
                                      : base.mem->ty->checked_bounds_form;
    Node             *resolved_lo =
        base.var ? base.var->checked_bounds_lo : base.mem->checked_bounds_lo;
    Node *resolved_hi =
        base.var ? base.var->checked_bounds_hi : base.mem->checked_bounds_hi;
    Type   *base_ty   = access_ty->base;
    int64_t elem_size = base_ty ? get_vm_size(base_ty) : 1;

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
                              new_long(vm, elem_size, tok), tok);
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
            // #938: the terminator slot is the elem_size bytes beginning at the
            // declared end of the range -- for bounds(lo,hi) that end is `hi`
            // itself, so widen it by one element exactly as CB_COUNT/
            // CB_BYTE_COUNT do below.
            if (kind == CHECKED_NTARRAY) {
                hi = new_binary(vm, ND_ADD, hi, new_long(vm, elem_size, tok),
                                tok);
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
            // n[*elem_size]. #938: ntarray widens by one element for the
            // terminator slot -- the elem_size bytes beginning at the declared
            // end of the range -- under all three bounds forms (#483 originally
            // shipped this for count(n) only; man/SAFETY.md documents the
            // unified rule).
            *out_lo = checked_base_self_expr(vm, base, tok);
            Node *n = base.mem
                          ? clone_member_bounds_node(vm, resolved_hi, base.obj)
                          : clone_bounds_node(vm, resolved_hi); // already typed
            Node *count_bytes;
            if (form == CB_BYTE_COUNT) {
                count_bytes = n;
                if (kind == CHECKED_NTARRAY) {
                    count_bytes = new_binary(vm, ND_ADD, count_bytes,
                                             new_long(vm, elem_size, tok), tok);
                    add_type(vm, count_bytes);
                }
            } else if (kind == CHECKED_NTARRAY) {
                Node *n_plus_1 =
                    new_binary(vm, ND_ADD, n, new_long(vm, 1, tok), tok);
                add_type(vm, n_plus_1);
                count_bytes = new_binary(vm, ND_MUL, n_plus_1,
                                         new_long(vm, elem_size, tok), tok);
                add_type(vm, count_bytes);
            } else {
                count_bytes = new_binary(vm, ND_MUL, n,
                                         new_long(vm, elem_size, tok), tok);
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
    CheckedBase base = find_checked_base(addr);
    if (!checked_base_is_declared(base))
        return;
    Type *pty                  = base.var ? base.var->ty : base.mem->ty;
    deref->checked_access_size = pty->base ? get_vm_size(pty->base) : 1;
    compute_checked_bounds(vm, base, pty, tok, &deref->checked_bounds_lo,
                           &deref->checked_bounds_hi,
                           &deref->checked_nt_terminator,
                           &deref->checked_bounds_obj_init, NULL);
}

// #919/#941: bounds propagation across assignment for an unchecked pointer
// local whose value is snapshotted from a checked-rooted source. See the
// design note at propagate_checked_bounds() below for the whole-function
// fixpoint this implements, and man/SAFETY.md's "Bounds propagation across
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
                                       int64_t *out_nt_elem, Obj *hoist_fn,
                                       Node **out_obj_init) {
    *out_lo = NULL;
    *out_hi = NULL;
    if (out_obj_init)
        *out_obj_init = NULL;
    CheckedBase base = find_checked_base(rhs);
    if (checked_base_is_declared(base)) {
        if (base.mem && node_has_side_effects(base.obj))
            return false;
        Type *pty = base.var ? base.var->ty : base.mem->ty;
        compute_checked_bounds(vm, base, pty, tok, out_lo, out_hi, out_nt,
                               out_obj_init, hoist_fn);
        if (!(*out_lo && *out_hi))
            return false;
        *out_optional = false;
        Type *base_ty = pty->base;
        *out_nt_elem  = *out_nt ? (base_ty ? get_vm_size(base_ty) : 1) : 0;
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
                    bool    src_optional, src_nt;
                    int64_t src_nt_elem;
                    if (checked_prop_source_bounds(
                            vm, node->rhs, node->tok, &lo, &hi, &src_optional,
                            &src_nt, &src_nt_elem, NULL, NULL)) {
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
    bool    src_optional, src_nt;
    int64_t src_nt_elem;
    // NT-ness isn't needed here -- walk 3 (checked_prop_attach_scan())
    // consults the candidate's own frozen checked_prop_nt_elem directly,
    // rather than recomputing per-store like lo/hi -- so src_nt/src_nt_elem
    // are discarded.
    bool is_source = checked_prop_source_bounds(
        vm, assign_node->rhs, assign_node->tok, &lo, &hi, &src_optional,
        &src_nt, &src_nt_elem, fn, &src_obj_init);
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
static void checked_prop_attach_scan(VirtualMachine *vm, Node *node) {
    for (; node; node = node->next) {
        if (node->kind == ND_DEREF && !node->checked_bounds_lo) {
            CheckedBase base = find_checked_base(node->lhs);
            if (base.var && base.var->checked_prop_candidate &&
                !base.var->checked_prop_unsafe && base.var->checked_prop_lo) {
                Node *lo =
                    new_var_node(vm, base.var->checked_prop_lo, node->tok);
                add_type(vm, lo);
                Node *hi =
                    new_var_node(vm, base.var->checked_prop_hi, node->tok);
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
                Type *pointee_ty = access_ty ? access_ty->base : NULL;
                node->checked_access_size =
                    pointee_ty ? get_vm_size(pointee_ty) : 1;
                if (base.var->checked_prop_nt_elem != 0 &&
                    node->checked_access_size ==
                        base.var->checked_prop_nt_elem &&
                    checked_nt_pointee_supported(pointee_ty))
                    node->checked_nt_terminator = true;

                // #943: to_assign()'s RMW desugar builds a synthesized store
                // node (checked_rmw_mirror) BEFORE this pass runs, so it
                // never got a chance to see these just-attached bounds --
                // mirror them across now, the same fields #937 already
                // copies for the direct-access case.
                if (node->checked_rmw_mirror) {
                    Node *mirror = node->checked_rmw_mirror;
                    if (mirror->kind == ND_DEREF) {
                        mirror->checked_bounds_lo =
                            clone_bounds_node(vm, node->checked_bounds_lo);
                        mirror->checked_bounds_hi =
                            clone_bounds_node(vm, node->checked_bounds_hi);
                        mirror->checked_bounds_optional =
                            node->checked_bounds_optional;
                        mirror->checked_access_size = node->checked_access_size;
                        mirror->checked_nt_terminator =
                            node->checked_nt_terminator;
                    } else {
                        // ND_CAS (the `_Atomic` desugar) -- lo is deliberately
                        // omitted, matching to_assign()'s own direct-access
                        // ND_CAS copy (it has no gen_addr-driven CHKR path to
                        // feed lo into).
                        mirror->checked_bounds_hi =
                            clone_bounds_node(vm, node->checked_bounds_hi);
                        mirror->checked_access_size = node->checked_access_size;
                        mirror->checked_nt_terminator =
                            node->checked_nt_terminator;
                    }
                }
            }
        }
        checked_prop_attach_scan(vm, node->lhs);
        checked_prop_attach_scan(vm, node->rhs);
        checked_prop_attach_scan(vm, node->cond);
        checked_prop_attach_scan(vm, node->then);
        checked_prop_attach_scan(vm, node->els);
        checked_prop_attach_scan(vm, node->init);
        checked_prop_attach_scan(vm, node->inc);
        checked_prop_attach_scan(vm, node->body);
        for (Node *a = node->args; a; a = a->next)
            checked_prop_attach_scan(vm, a);
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
// a FULL candidate that's the pre-existing #919-era residual (man/
// SAFETY.md): `q` itself is equally indeterminate at that point, so a
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
// bounds (Checked C's _Assume_bounds_cast direction); it only ever widens
// trust for a previously-unchecked target.
#define CHECKED_PROP_MAX_ROUNDS 32

void propagate_checked_bounds(VirtualMachine *vm, Obj *fn) {
    if (!(vm->flags & CCCC_CHECKED_BOUNDS))
        return;

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
    int  prev_survivors = -1, prev_optional = -1, prev_nt = -1;
    bool converged = false;
    for (int round = 0; round < CHECKED_PROP_MAX_ROUNDS; round++) {
        for (Obj *v = fn->locals; v; v = v->next)
            if (v->checked_prop_candidate) {
                v->checked_prop_unsafe            = false;
                v->checked_prop_scan_saw_rooted   = false;
                v->checked_prop_scan_saw_unrooted = false;
                v->checked_prop_scan_src_optional = false;
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
        int survivors = 0, optional_survivors = 0, nt_survivors = 0;
        for (Obj *v = fn->locals; v; v = v->next)
            if (v->checked_prop_candidate) {
                // #942: a candidate with no checked-rooted store at all
                // (an escaping address-of already set checked_prop_unsafe
                // directly during the scan) is NONE, same outcome as
                // #919's original "poisoned" -- there is nothing to ever
                // snapshot. Otherwise it's a survivor: FULL if every store
                // reached was rooted, OPT if any wasn't (or if a rooted
                // store's own source was itself OPT).
                if (!v->checked_prop_scan_saw_rooted)
                    v->checked_prop_unsafe = true;
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
                } else {
                    v->checked_prop_nt_elem = 0;
                }
            }
        if (survivors == prev_survivors &&
            optional_survivors == prev_optional && nt_survivors == prev_nt) {
            converged = true;
            break;
        }
        prev_survivors = survivors;
        prev_optional  = optional_survivors;
        prev_nt        = nt_survivors;
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
    checked_prop_attach_scan(vm, fn->body);
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
                                          Node *dst_obj_init) {
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
// resolvable bounds form (checked_prop_source_bounds()'s kind 1 only -- see
// verify_checked_assign_bounds()'s v1-scope comment), and rewrites it via
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
            if (checked_base_is_declared(dst_base) &&
                !(dst_base.mem && node_has_side_effects(dst_base.obj))) {
                Type *dst_pty =
                    dst_base.var ? dst_base.var->ty : dst_base.mem->ty;
                Node *dst_lo, *dst_hi;
                bool  dst_nt;
                // #947: probe with NULL first -- a failed test below (no
                // usable dst/src bounds) must not allocate a hoist temp for
                // a rewrite that never happens. Only recomputed with the
                // hoist enabled once the rewrite is actually committed.
                compute_checked_bounds(vm, dst_base, dst_pty, node->tok,
                                       &dst_lo, &dst_hi, &dst_nt, NULL, NULL);
                if (dst_lo && dst_hi) {
                    Node       *src_lo, *src_hi;
                    bool        src_optional, src_nt;
                    int64_t     src_nt_elem;
                    CheckedBase src_base = find_checked_base(node->rhs);
                    bool        is_source =
                        checked_base_is_declared(src_base) &&
                        checked_prop_source_bounds(
                            vm, node->rhs, node->tok, &src_lo, &src_hi,
                            &src_optional, &src_nt, &src_nt_elem, NULL, NULL);
                    if (is_source && src_lo && src_hi) {
                        verify_checked_assign_scan(vm, fn, node->rhs);
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
                            &src_optional, &src_nt2, &src_nt_elem2, fn,
                            &src_obj_init);
                        verify_checked_assign_rewrite(
                            vm, fn, node, dst_lo, dst_hi, src_lo, src_hi,
                            src_obj_init, dst_obj_init);
                        continue; // node is now the wrapper; parts already
                                  // scanned
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

// #944: Checked C's `_Assume_bounds_cast` direction -- verifies, at
// assignment time, that a declared-checked TARGET's own bounds are implied
// by a declared-checked SOURCE's bounds, rather than trusting the target's
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
// v1 scope, all deliberate (see man/SAFETY.md for the user-facing writeup):
//  - Source must be a directly DECLARED-checked base (find_checked_base() +
//    checked_base_is_declared()), i.e. kind 1 of checked_prop_source_bounds()
//    -- not a #941-propagated local, which can hold the OPT sentinel and
//    would need a sentinel-aware variant of CHKAB. `q = r;` where `r` is
//    itself only a propagation candidate is silently skipped, same as an
//    unrecognised source.
//  - Target must be ND_VAR or ND_MEMBER (side-effect-free object expression
//    for the latter), declared checked, with a resolvable bounds form
//    (compute_checked_bounds() returns non-NULL lo/hi) -- CB_NONE/
//    CB_UNKNOWN targets are skipped, same rationale as propagation's
//    identically-named source exclusion.
//  - Function argument passing and return values are not covered -- only a
//    direct `q = E;` ND_ASSIGN.
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
