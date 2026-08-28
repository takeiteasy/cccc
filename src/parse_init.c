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

// Declarations and initializers: declaration(), the array/struct/union
// initializer grammar, VLA-init lowering, and local/global initializer
// codegen-facing helpers (lvar_initializer/gvar_initializer).

#include "./parse_internal.h"

// Generate code for computing a VLA size.
Node *compute_vla_size(VirtualMachine *vm, Type *ty, Token *tok) {
    Node *node = new_node(vm, ND_NULL_EXPR, tok);
    if (ty->base)
        node = new_binary(vm, ND_COMMA, node,
                          compute_vla_size(vm, ty->base, tok), tok);

    if (ty->kind != TY_VLA)
        return node;

    Node *base_sz;
    if (ty->base->kind == TY_VLA)
        base_sz = new_var_node(vm, ty->base->vla_size, tok);
    else
        base_sz =
            new_num(vm, get_vm_size(ty->base), tok); // Use VM-adjusted size

    ty->vla_size = new_lvar(vm, "", 0, ty_ulong);
    Node *expr =
        new_binary(vm, ND_ASSIGN, new_var_node(vm, ty->vla_size, tok),
                   new_binary(vm, ND_MUL, ty->vla_len, base_sz, tok), tok);
    return new_binary(vm, ND_COMMA, node, expr, tok);
}

static Node *new_alloca(VirtualMachine *vm, Node *sz) {
    Node *node = new_unary(
        vm, ND_FUNCALL, new_var_node(vm, vm->compiler.builtin_alloca, sz->tok),
        sz->tok);
    node->func_ty = vm->compiler.builtin_alloca->ty;
    node->ty      = vm->compiler.builtin_alloca->ty->return_ty;
    node->args    = sz;
    // #981: this helper is only ever called to back a VLA declaration's
    // lowering (see its one call site below), never for an explicit
    // `__builtin_alloca`/`__builtin_alloca_with_align` call -- those build
    // their own ND_FUNCALL directly in primary()/unary() and never go
    // through here. Tags the node so codegen.c's ND_FUNCALL case can emit
    // ALCV (ALLOC_KIND_FRAME, block-scoped) instead of ALCA (ALLOC_KIND_
    // ALLOCA, frame-scoped) for it.
    node->is_vla_alloca_call = true;
    add_type(vm, sz);
    return node;
}

static Node *create_vla_init(VirtualMachine *vm, Initializer *init, Type *ty,
                             Obj *var, Token *tok);
static Initializer *initializer(VirtualMachine *vm, Token **rest, Token *tok,
                                Type *ty, Type **new_ty);

// declaration = declspec (declarator ("=" expr)? ("," declarator ("="
// expr)?)*)? ";"
Node *declaration(VirtualMachine *vm, Token **rest, Token *tok, Type *basety,
                  VarAttr *attr) {
    Node  head = {};
    Node *cur  = &head;
    int   i    = 0;

    while (!equal(tok, ";")) {
        if (i++ > 0)
            tok = skip(vm, tok, ",");

        Type *ty = declarator(vm, &tok, tok, basety);

        if (has_custom_attrs(ty, attr) || (basety && basety->custom_attrs))
            error_tok(vm, ty->name ? ty->name : tok,
                      "custom attributes are only supported on file-scope "
                      "declarations");

        if (ty->kind == TY_VOID) {
            if (vm->collect_errors &&
                error_tok_recover(vm, tok, "variable declared void")) {
                // Skip to next declarator or end of declaration
                tok = skip_to_decl_boundary(vm, tok);
                if (equal(tok, ";"))
                    break;
                if (equal(tok, ","))
                    continue;
                break;
            }
            error_tok(vm, tok, "variable declared void");
        }

        if (!ty->name) {
            if (vm->collect_errors &&
                error_tok_recover(vm, ty->name_pos, "variable name omitted")) {
                // Skip to next declarator or end of declaration
                tok = skip_to_decl_boundary(vm, tok);
                if (equal(tok, ";"))
                    break;
                if (equal(tok, ","))
                    continue;
                break;
            }
            error_tok(vm, ty->name_pos, "variable name omitted");
        }

        if (attr && attr->is_constexpr && ty->kind == TY_VLA)
            error_tok(
                vm, ty->name,
                "constexpr object may not have variable length array type");

        // C23 auto type inference
        if (attr && attr->is_auto) {
            Token *name_tok   = ty->name;
            int    decl_depth = count_auto_ptr_depth(ty);
            if (decl_depth < 0)
                error_tok(
                    vm, name_tok,
                    "cannot use 'auto' with array or function declarator");
            if (!equal(tok, "="))
                error_tok(vm, name_tok,
                          "declaration of variable '%.*s' with deduced type "
                          "'auto' requires an initializer",
                          (int)name_tok->len, name_tok->loc);
            if (equal(tok->next, "{"))
                error_tok(vm, tok->next, "cannot use 'auto' with array in C");

            // Parse initializer expression to infer type
            Token *eq_tok    = tok;
            Node  *init_expr = assign(vm, &tok, tok->next);
            add_type(vm, init_expr);
            Type *deduced = auto_deduced_type(vm, init_expr->ty);

            // Validate pointer depth: declarator stars must not exceed inferred
            // type depth
            if (count_ptr_depth(deduced) < decl_depth) {
                char stars[16] = "";
                for (int i = 0; i < decl_depth && i < 15; i++)
                    stars[i] = '*';
                error_tok(vm, name_tok,
                          "variable '%.*s' with type 'auto%s%s' has "
                          "incompatible initializer",
                          (int)name_tok->len, name_tok->loc,
                          decl_depth > 0 ? " " : "", stars);
            }

            if (attr->is_static) {
                warn_if_shadowing(vm, name_tok);
                Obj *var             = new_anon_gvar(vm, deduced);
                var->tok             = name_tok;
                var->display_name    = get_ident(vm, name_tok);
                var->is_local_symbol = true;
                var->is_maybe_unused = ty->is_maybe_unused;
                var->is_deprecated   = ty->is_deprecated;
                var->deprecated_msg  = ty->deprecated_msg;
                push_scope(vm, get_ident(vm, name_tok), name_tok->len)->var =
                    var;
                Token *tmp = eq_tok;
                gvar_initializer(vm, &tmp, eq_tok->next, var);
                continue;
            }

            Obj *var =
                new_lvar(vm, get_ident(vm, name_tok), name_tok->len, deduced);
            // #1160: also honor GNU aligned(N), not just _Alignas.
            var->align = effective_decl_align(vm, name_tok, deduced, attr);
            if (attr->is_block_var)
                var->is_block_var = true;

            vm->compiler.initializing_var = var;
            Node *lhs                     = new_var_node(vm, var, name_tok);
            add_type(vm, lhs);
            Node *asgn = new_binary(vm, ND_ASSIGN, lhs, init_expr, name_tok);
            add_type(vm, asgn);
            cur = cur->next = new_unary(vm, ND_EXPR_STMT, asgn, name_tok);
            continue;
        }

        if (attr && attr->is_static) {
            // static local variable
            warn_if_shadowing(vm, ty->name);
            Obj *var             = new_anon_gvar(vm, ty);
            var->tok             = ty->name;
            var->display_name    = get_ident(vm, ty->name);
            var->is_local_symbol = true;
            var->is_constexpr    = attr->is_constexpr;
            var->is_maybe_unused = ty->is_maybe_unused;
            var->is_deprecated   = ty->is_deprecated;
            var->deprecated_msg  = ty->deprecated_msg;
            push_scope(vm, get_ident(vm, ty->name), ty->name->len)->var = var;
            if (var->checked_kind != CHECKED_NONE)
                resolve_checked_bounds(vm, var);
            if (equal(tok, "="))
                gvar_initializer(vm, &tok, tok->next, var);
            else if (attr->is_constexpr)
                error_tok(vm, ty->name,
                          "constexpr object requires an initializer");
            continue;
        }

        // Generate code for computing a VLA size. We need to do this
        // even if ty is not VLA because ty may be a pointer to VLA
        // (e.g. int (*foo)[n][m] where n and m are variables.)
        cur = cur->next =
            new_unary(vm, ND_EXPR_STMT, compute_vla_size(vm, ty, tok), tok);

        if (ty->kind == TY_VLA) {
            // Variable length arrays (VLAs) are translated to alloca() calls.
            // For example, `int x[n+2]` is translated to `tmp = n + 2,
            // x = alloca(tmp)`.
            Obj *var = new_lvar(vm, get_ident(vm, ty->name), ty->name->len, ty);
            Token *tok_local = ty->name;
            Node  *expr      = new_binary(
                vm, ND_ASSIGN, new_vla_ptr(vm, var, tok_local),
                new_alloca(vm, new_var_node(vm, ty->vla_size, tok_local)),
                tok_local);

            cur = cur->next = new_unary(vm, ND_EXPR_STMT, expr, tok_local);

            // Handle VLA initialization if present
            if (equal(tok, "=")) {
                tok = tok->next;
                Type        *new_ty;
                Initializer *init = initializer(vm, &tok, tok, ty, &new_ty);
                Node *init_node = create_vla_init(vm, init, ty, var, tok_local);
                if (init_node) {
                    // #982 (defect D): a partial VLA brace initializer
                    // (fewer elements/rows than the array's own dimensions)
                    // leaves the unspecified elements relying on the fresh
                    // alloca block already being zero -- true by
                    // construction at the default safety level, but not
                    // under -2/-3's CCCC_MEMORY_POISONING, which fills
                    // every fresh heap block with 0xCD before use
                    // (vm_heap_bump_alloc_ex, src/ops.c). Every other
                    // partial-initializer path pre-zeroes the object first
                    // for exactly this reason (lvar_initializer, below in
                    // this file); the VLA path bypasses lvar_initializer
                    // entirely (create_vla_init can't use ty->array_len --
                    // see its own comment) and never got the same
                    // treatment. Mirror it here: the ND_ASSIGN(ND_VLA_PTR,
                    // alloca(...)) statement just above already guarantees
                    // ty->vla_size is live for the ND_MEMZERO codegen.
                    Node *zero = new_node(vm, ND_MEMZERO, tok_local);
                    zero->var  = var;
                    // A VLA's byte count isn't the constant ty->size codegen
                    // defaults to (that's TY_VLA's placeholder size, see
                    // vla_of()) -- it's the runtime value in ty->vla_size,
                    // the same Obj the alloca() call above was sized from.
                    // Stash it in ->rhs (unused by every other ND_MEMZERO
                    // producer) so codegen can gen_expr it into REG_A2
                    // instead of using the constant path.
                    zero->rhs = new_var_node(vm, ty->vla_size, tok_local);
                    Node *comma =
                        new_binary(vm, ND_COMMA, zero, init_node, tok_local);
                    cur = cur->next =
                        new_unary(vm, ND_EXPR_STMT, comma, tok_local);
                }
            }

            continue;
        }

        Obj *var = new_lvar(vm, get_ident(vm, ty->name), ty->name->len, ty);
        if (attr && attr->is_constexpr) {
            var->is_constexpr = true;
        }
        // #1160: also honor GNU aligned(N), not just _Alignas. attr can be
        // NULL at this call site; effective_decl_align() tolerates that.
        var->align = effective_decl_align(vm, ty->name, ty, attr);
        if (attr && attr->is_block_var)
            var->is_block_var = true;
        // Note: cleanup_fn is transferred from attr → Type → Obj via
        // apply_var_attrs_to_type() + new_var(), no manual copy needed here.
        // Resolve checked-pointer bounds (#770/#483) now that this local is
        // itself in scope, so a bound may reference any prior sibling local.
        if (var->checked_kind != CHECKED_NONE)
            resolve_checked_bounds(vm, var);

        // #919/#942: candidate registration for checked-pointer bounds
        // propagation across assignment. An unchecked pointer local (a
        // checked_kind != CHECKED_NONE pointer already has its own declared
        // bounds and never needs propagation) is a candidate whether or not
        // it has an initializer -- #942 extended this from #919's
        // initializer-only registration so `int *q; if (c) q = p;` can be
        // tracked too, with the phase-B' entry sentinel init (see
        // propagate_checked_bounds()) covering the "no rooted value yet"
        // state for the no-initializer case. Whether any particular store
        // (including the initializer, if present) is actually checked-rooted
        // is decided later by propagate_checked_bounds()'s classifier scan,
        // which is also what actually reads checked_prop_candidate. Gating
        // registration itself on CCCC_CHECKED_BOUNDS (rather than
        // registering unconditionally and gating only the pass) keeps the
        // cost symmetric with the rest of this parse-time-gated mechanism --
        // see propagate_checked_bounds()'s comment for why this has no
        // pragma-ordering caveat despite being a parse-time check.
        if ((vm->flags & CCCC_CHECKED_BOUNDS) && var->ty->kind == TY_PTR &&
            var->checked_kind == CHECKED_NONE)
            var->checked_prop_candidate = true;

        if (equal(tok, "=")) {
            // Mark this variable as being initialized (allows const
            // initialization) NOTE: Don't clear this until after add_type is
            // called on the function body For now, just set it and it will be
            // cleared when next variable is initialized This works because
            // initializations happen sequentially
            vm->compiler.initializing_var = var;
            Node *expr = lvar_initializer(vm, &tok, tok->next, var);
            // #973 follow-up: a pointer-to-VLA local's declarator reads a
            // runtime variable (`int (*p)[n]`), so it can't be hoisted to
            // the top of the function -- record this initializer as the
            // in-place declaration site (serialize_decl.c looks for it by
            // pointer identity). `expr` is exactly the scalar
            // ND_ASSIGN(ND_VAR(var), ...) create_lvar_init/lvar_initializer
            // build for a non-aggregate initializer; var->ty is never itself
            // TY_VLA here (that's a separate declaration() branch above,
            // using ND_VLA_PTR + alloca()), so type_contains_vla only
            // matches a pointer/array chain whose base is a VLA.
            if (type_contains_vla(var->ty))
                var->deferred_vla_ptr_init = expr;
            cur = cur->next = new_unary(vm, ND_EXPR_STMT, expr, tok);
            // Don't clear here - will be cleared by next init or at end of
            // parsing

            // #642: track `ptr = malloc-family(const size)` initializers so
            // __builtin_object_size can resolve the allocation size, provided
            // the pointer is never reassigned or address-taken (checked by
            // resolve_objsize_queries once the whole function is parsed).
            // Only the plain scalar-assignment shape qualifies — is_aggregate
            // in lvar_initializer wraps aggregates in an ND_COMMA, which we
            // don't try to unwrap.
            if (var->ty->kind == TY_PTR && expr->kind == ND_ASSIGN &&
                expr->lhs->kind == ND_VAR && expr->lhs->var == var) {
                int  alloc_size;
                Obj *base;
                int  base_offset;
                if (objsize_alloc_from_call(vm, expr->rhs, &alloc_size)) {
                    var->objsize_has_alloc   = true;
                    var->objsize_alloc       = alloc_size;
                    var->objsize_init_assign = expr;
                    var->objsize_decl_fn     = vm->compiler.current_fn;
                } else if (objsize_peel_offset_chain(vm, expr->rhs, &base,
                                                     &base_offset) &&
                           ((base->objsize_has_alloc &&
                             base->objsize_decl_fn ==
                                 vm->compiler.current_fn) ||
                            (base->ty && base->ty->kind == TY_ARRAY &&
                             base->ty->size > 0))) {
                    // #700: `q = p + const`, where p is itself alloc-tracked
                    // (directly or transitively) and declared in the same
                    // function. q's effective size is resolved at query time
                    // by following objsize_derived_from -- see
                    // objsize_effective_remaining -- so this only records the
                    // link, not a concrete size. Restricting to p being
                    // declared in the *same* function sidesteps the same
                    // nested-fn/block timing hazard documented on
                    // objsize_decl_fn: a cross-function derivation could
                    // resolve (and freeze) before a later reassignment in the
                    // enclosing scope is even parsed.
                    //
                    // #701: a base that is a statically-sized array object
                    // (not itself alloc-tracked) is also accepted, without the
                    // same-function restriction -- an array's size is fixed at
                    // declaration and its name is never reassignable, so there
                    // is no reassignment hazard to sidestep (this is also why
                    // a global/static array base is fine here). A TY_VLA base
                    // has no compile-time size and is excluded by `size > 0`.
                    // An array-typed *parameter* can't reach this arm: cccc
                    // decays array parameters to TY_PTR at declaration, so
                    // `base->ty->kind` is never TY_ARRAY for one.
                    var->objsize_has_alloc      = true;
                    var->objsize_derived_from   = base;
                    var->objsize_derived_offset = base_offset;
                    var->objsize_init_assign    = expr;
                    var->objsize_decl_fn        = vm->compiler.current_fn;
                }
            }

            // #919: records the ND_ASSIGN this candidate's declaration
            // initializer built, so checked_prop_poison_scan() can find and
            // classify it exactly like any other store to the candidate (the
            // registration itself, checked_prop_candidate, already happened
            // above regardless of whether an initializer is even present --
            // see #942's comment there). Same shape-check as the objsize
            // tracking just above: only the plain `var = init_expr` shape
            // (not the ND_COMMA-wrapped aggregate-initializer shape) counts.
            if (var->checked_prop_candidate && expr->kind == ND_ASSIGN &&
                expr->lhs->kind == ND_VAR && expr->lhs->var == var)
                var->checked_prop_init_assign = expr;
        } else if (var->is_constexpr) {
            error_tok(vm, ty->name, "constexpr object requires an initializer");
        }

        if (var->ty->size < 0) {
            if (vm->collect_errors &&
                error_tok_recover(vm, ty->name,
                                  "variable has incomplete type")) {
                // Set a default size to allow parsing to continue
                var->ty->size = 1;
                continue;
            }
            error_tok(vm, ty->name, "variable has incomplete type");
        }

        if (var->ty->kind == TY_VOID) {
            if (vm->collect_errors &&
                error_tok_recover(vm, ty->name, "variable declared void")) {
                // Already reported earlier, just continue
                continue;
            }
            error_tok(vm, ty->name, "variable declared void");
        }
    }

    Node *node          = new_node(vm, ND_BLOCK, tok);
    node->body          = head.next;
    node->is_decl_group = true; // #981: not a real C block scope, see the
                                // field's own comment (cccc.h)
    *rest = tok->next;
    return node;
}

static Token *skip_excess_element(VirtualMachine *vm, Token *tok) {
    if (equal(tok, "{")) {
        tok = skip_excess_element(vm, tok->next);
        return skip(vm, tok, "}");
    }

    assign(vm, &tok, tok);
    return tok;
}

// string-initializer = string-literal
static void string_initializer(VirtualMachine *vm, Token **rest, Token *tok,
                               Initializer *init) {
    if (init->is_flexible)
        *init = *new_initializer(
            vm, array_of(vm, init->ty->base, tok->ty->array_len), false);

    // Excess check after the is_flexible rewrite above: a flexible array
    // (`char c[] = "abcd"`) has just been retyped to the string's own
    // length, so it can never be excess; checking before the rewrite would
    // read array_len off the still-incomplete array type instead.
    // tok->ty->array_len includes the trailing NUL, which C drops silently
    // when the destination has exactly enough room for the string sans NUL
    // (`char a[4] = "abcd"` is legal, matching gcc/clang) -- hence the -1.
    if (!init->is_flexible && tok->ty->array_len - 1 > init->ty->array_len)
        warn_tok(vm, tok, CCCC_WARN_EXCESS_INIT,
                 "initializer-string for array is too long (%d chars into "
                 "%d available)",
                 tok->ty->array_len - 1, init->ty->array_len);

    int len = MIN(init->ty->array_len, tok->ty->array_len);

    switch (init->ty->base->size) {
        case 1: {
            char *str = tok->str;
            for (int i = 0; i < len; i++)
                init->children[i]->expr = new_num(vm, str[i], tok);
            break;
        }
        case 2: {
            uint16_t *str = (uint16_t *)tok->str;
            for (int i = 0; i < len; i++)
                init->children[i]->expr = new_num(vm, str[i], tok);
            break;
        }
        case 4: {
            uint32_t *str = (uint32_t *)tok->str;
            for (int i = 0; i < len; i++)
                init->children[i]->expr = new_num(vm, str[i], tok);
            break;
        }
        default:
            unreachable();
    }

    *rest = tok->next;
}

// array-designator = "[" const-expr "]"
//
// C99 added the designated initializer to the language, which allows
// programmers to move the "cursor" of an initializer to any element.
// The syntax looks like this:
//
//   int x[10] = { 1, 2, [5]=3, 4, 5, 6, 7 };
//
// `[5]` moves the cursor to the 5th element, so the 5th element of x
// is set to 3. Initialization then continues forward in order, so
// 6th, 7th, 8th and 9th elements are initialized with 4, 5, 6 and 7,
// respectively. Unspecified elements (in this case, 3rd and 4th
// elements) are initialized with zero.
//
// Nesting is allowed, so the following initializer is valid:
//
//   int x[5][10] = { [5][8]=1, 2, 3 };
//
// It sets x[5][8], x[5][9] and x[6][0] to 1, 2 and 3, respectively.
//
// Use `.fieldname` to move the cursor for a struct initializer. E.g.
//
//   struct { int a, b, c; } x = { .c=5 };
//
// The above initializer sets x.c to 5.
static void array_designator(VirtualMachine *vm, Token **rest, Token *tok,
                             Type *ty, int *begin, int *end) {
    *begin = const_expr(vm, &tok, tok->next);
    if (*begin >= ty->array_len)
        error_tok(vm, tok, "array designator index exceeds array bounds");

    if (equal(tok, "...")) {
        *end = const_expr(vm, &tok, tok->next);
        if (*end >= ty->array_len)
            error_tok(vm, tok, "array designator index exceeds array bounds");
        if (*end < *begin)
            error_tok(vm, tok, "array designator range [%d, %d] is empty",
                      *begin, *end);
    } else {
        *end = *begin;
    }

    *rest = skip(vm, tok, "]");
}

// struct-designator = "." ident
static Member *struct_designator(VirtualMachine *vm, Token **rest, Token *tok,
                                 Type *ty) {
    Token *start = tok;
    tok          = skip(vm, tok, ".");
    if (tok->kind != TK_IDENT)
        error_tok(vm, tok, "expected a field designator");

    for (Member *mem = ty->members; mem; mem = mem->next) {
        // Anonymous struct/union member (its fields are reachable through
        // the parent's own designators, per C's anonymous-member rules).
        if ((mem->ty->kind == TY_STRUCT || mem->ty->kind == TY_UNION) &&
            !mem->name) {
            if (get_struct_member(mem->ty, tok)) {
                *rest = start;
                return mem;
            }
            continue;
        }

        // Any other nameless member (e.g. an unnamed bitfield) can't be
        // targeted by a designator; skip rather than deref a NULL name.
        if (!mem->name)
            continue;

        // Regular struct member
        if (mem->name->len == tok->len &&
            !strncmp(mem->name->loc, tok->loc, tok->len)) {
            *rest = tok->next;
            return mem;
        }
    }

    error_tok(vm, tok, "struct has no such member");
    return NULL;
}

// True when `mem` is the anonymous struct/union wrapper struct_designator()
// returns for a designator that actually targets one of the wrapper's own
// fields (per C's anonymous-member rules) -- in that case the designator
// token was never consumed (struct_designator() rewound `*rest` to `start`),
// so a -Woverride-init check belongs to the recursive designation() call
// that re-parses the same token against the wrapper's own type, not to this
// level (#961: the wrapper's own `is_set`/`mem` never reflects which leaf
// field was actually targeted).
static bool is_anon_aggregate_member(Member *mem) {
    return !mem->name &&
           (mem->ty->kind == TY_STRUCT || mem->ty->kind == TY_UNION);
}

// Shared -Woverride-init message for a designator that resolved to a named
// member (struct_designator() never returns an unnamed, non-aggregate
// member, so `mem->name` is always non-NULL here).
static void warn_override_init_member(VirtualMachine *vm, Token *tok,
                                      Member *mem) {
    warn_tok(vm, tok, CCCC_WARN_OVERRIDE_INIT,
             "initializer overrides prior initialization of '%.*s'",
             (int)mem->name->len, mem->name->loc);
}

static void array_initializer2(VirtualMachine *vm, Token **rest, Token *tok,
                               Initializer *init, int i);
static void initializer2(VirtualMachine *vm, Token **rest, Token *tok,
                         Initializer *init);
static void struct_initializer2(VirtualMachine *vm, Token **rest, Token *tok,
                                Initializer *init, Member *mem);

// designation = ("[" const-expr "]" | "." ident)* "="? initializer
static void designation(VirtualMachine *vm, Token **rest, Token *tok,
                        Initializer *init) {
    if (equal(tok, "[")) {
        if (vm->compiler.c_std < CCCC_STD_C99)
            warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                     "designated initializers are a C99 extension");
        if (init->ty->kind != TY_ARRAY)
            error_tok(vm, tok, "array index in non-array initializer");

        int begin, end;
        array_designator(vm, &tok, tok, init->ty, &begin, &end);

        Token *tok2;
        for (int i = begin; i <= end; i++) {
            if ((vm->compiler.warnings & CCCC_WARN_OVERRIDE_INIT) &&
                init->children[i]->is_set)
                warn_tok(vm, tok, CCCC_WARN_OVERRIDE_INIT,
                         "initializer overrides prior initialization of "
                         "element [%d]",
                         i);
            designation(vm, &tok2, tok, init->children[i]);
        }
        array_initializer2(vm, rest, tok2, init, begin + 1);
        return;
    }

    if (equal(tok, ".") && init->ty->kind == TY_STRUCT) {
        if (vm->compiler.c_std < CCCC_STD_C99)
            warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                     "designated initializers are a C99 extension");
        Member *mem = struct_designator(vm, &tok, tok, init->ty);
        if ((vm->compiler.warnings & CCCC_WARN_OVERRIDE_INIT) &&
            !is_anon_aggregate_member(mem) && init->children[mem->idx]->is_set)
            warn_override_init_member(vm, tok, mem);
        designation(vm, &tok, tok, init->children[mem->idx]);
        init->expr = NULL;

        // Only continue with struct_initializer2 if we're not immediately
        // followed by another designator (which might re-designate the same
        // nested struct) This allows {.tl.x = 10, .tl.y = 20} to work correctly
        if (!equal(tok, ",") || !equal(tok->next, ".")) {
            struct_initializer2(vm, rest, tok, init, mem->next);
        } else {
            *rest = tok;
        }
        return;
    }

    if (equal(tok, ".") && init->ty->kind == TY_UNION) {
        if (vm->compiler.c_std < CCCC_STD_C99)
            warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                     "designated initializers are a C99 extension");
        Member *mem = struct_designator(vm, &tok, tok, init->ty);
        // Union members alias, so ANY prior designator into this union --
        // same member re-designated, or a different member that already
        // took the union's "active" slot -- overrides it (#961; unlike
        // struct/array, there's no is_set-per-member state to compare,
        // just whether this union has already been actively initialized).
        if ((vm->compiler.warnings & CCCC_WARN_OVERRIDE_INIT) &&
            !is_anon_aggregate_member(mem) && init->mem &&
            init->children[init->mem->idx]->is_set)
            // Name whichever member was previously live, not the incoming
            // one -- for a different-member override that's the field
            // actually being overridden (e.g. `.i=1, .j=2` overrides 'i',
            // not 'j', which was never set).
            warn_override_init_member(vm, tok, init->mem);
        init->mem = mem;
        designation(vm, rest, tok, init->children[mem->idx]);
        return;
    }

    if (equal(tok, "."))
        error_tok(vm, tok, "field name not in struct or union initializer");

    if (equal(tok, "="))
        tok = tok->next;
    initializer2(vm, rest, tok, init);
}

// An array length can be omitted if an array has an initializer
// (e.g. `int x[] = {1,2,3}`). If it's omitted, count the number
// of initializer elements.
static int count_array_init_elements(VirtualMachine *vm, Token *tok, Type *ty) {
    bool         first = true;
    Initializer *dummy = new_initializer(vm, ty->base, true);

    int          i = 0, max = 0;

    while (!consume_end(&tok, tok)) {
        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        if (equal(tok, "[")) {
            i = const_expr(vm, &tok, tok->next);
            if (equal(tok, "..."))
                i = const_expr(vm, &tok, tok->next);
            tok = skip(vm, tok, "]");
            designation(vm, &tok, tok, dummy);
        } else {
            initializer2(vm, &tok, tok, dummy);
        }

        i++;
        max = MAX(max, i);
    }
    return max;
}

// array-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void array_initializer1(VirtualMachine *vm, Token **rest, Token *tok,
                               Initializer *init) {
    tok = skip(vm, tok, "{");

    if (init->is_flexible) {
        int len = count_array_init_elements(vm, tok, init->ty);
        // For VLA, keep the VLA type but allocate children for initializer
        // elements
        if (init->ty->kind == TY_VLA) {
            init->is_flexible = false;
            // len + 1: create_vla_init/create_lvar_init's TY_VLA branch
            // counts elements by scanning for a NULL terminator (#977), so
            // this array must be genuinely NULL-terminated, not just len
            // long with a NULL landing past the end by arena luck.
            init->children = arena_alloc(&vm->compiler.parser_arena,
                                         (len + 1) * sizeof(Initializer *));
            memset(init->children, 0, (len + 1) * sizeof(Initializer *));
            for (int i = 0; i < len; i++)
                init->children[i] = new_initializer(vm, init->ty->base, false);
        } else {
            // For flexible arrays, create a fixed-size array type
            *init =
                *new_initializer(vm, array_of(vm, init->ty->base, len), false);
        }
    }

    bool first         = true;
    bool warned_excess = false;

    for (int i = 0; !consume_end(rest, tok); i++) {
        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        if (equal(tok, "[")) {
            if (vm->compiler.c_std < CCCC_STD_C99)
                warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                         "designated initializers are a C99 extension");
            int begin, end;
            array_designator(vm, &tok, tok, init->ty, &begin, &end);

            Token *tok2;
            for (int j = begin; j <= end; j++)
                designation(vm, &tok2, tok, init->children[j]);
            tok = tok2;
            i   = end;
            continue;
        }

        // For VLA, check if children[i] exists; for regular arrays, check
        // array_len. The VLA arm is deliberately left unwarned -- a VLA's
        // bound isn't known until runtime, so excess elements can't be
        // diagnosed statically even in principle (if the bound were an
        // integer constant expression it wouldn't be a VLA); the resulting
        // out-of-bounds store is instead caught by the generic runtime
        // bounds machinery at -2/-3 (#1179).
        if (init->ty->kind == TY_VLA) {
            if (init->children && init->children[i])
                initializer2(vm, &tok, tok, init->children[i]);
            else
                tok = skip_excess_element(vm, tok);
        } else {
            if (i < init->ty->array_len)
                initializer2(vm, &tok, tok, init->children[i]);
            else {
                if (!warned_excess) {
                    warned_excess = true;
                    warn_tok(vm, tok, CCCC_WARN_EXCESS_INIT,
                             "excess elements in array initializer");
                }
                tok = skip_excess_element(vm, tok);
            }
        }
    }
}

// vector-initializer = "{" initializer ("," initializer)* ","? "}"
//
// GNU vector_size brace-initializer (tracker #713). Positional only.
// Designated initializers (`{[2] = 3.0f}`) are deliberately rejected with a
// clear diagnostic rather than supported: verified directly against real
// GCC and clang (both reject the identical syntax, in both direct and
// compound-literal form, with "initialization of non-aggregate type ...
// with a designated initializer list") -- vector_size vectors are
// non-aggregate types in GCC's own model, and C's designated-initializer
// grammar only applies to aggregates. Adding `[idx]=` support here would
// make CCCC accept syntax neither reference compiler does (tracker #719).
static void vector_initializer(VirtualMachine *vm, Token **rest, Token *tok,
                               Initializer *init) {
    tok                = skip(vm, tok, "{");

    bool first         = true;
    bool warned_excess = false;
    for (int i = 0; !consume_end(rest, tok); i++) {
        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        if (equal(tok, "["))
            error_tok(vm, tok,
                      "initialization of non-aggregate vector type with a "
                      "designated initializer list");

        if (i < init->ty->vec_len)
            initializer2(vm, &tok, tok, init->children[i]);
        else {
            if (!warned_excess) {
                warned_excess = true;
                warn_tok(vm, tok, CCCC_WARN_EXCESS_INIT,
                         "excess elements in vector initializer");
            }
            tok = skip_excess_element(vm, tok);
        }
    }
}

// array-initializer2 = initializer ("," initializer)*
static void array_initializer2(VirtualMachine *vm, Token **rest, Token *tok,
                               Initializer *init, int i) {
    if (init->is_flexible) {
        int len = count_array_init_elements(vm, tok, init->ty);
        // For VLA, keep the VLA type but allocate children for initializer
        // elements
        if (init->ty->kind == TY_VLA) {
            init->is_flexible = false;
            // len + 1: see the matching comment in array_initializer1 (#977).
            init->children = arena_alloc(&vm->compiler.parser_arena,
                                         (len + 1) * sizeof(Initializer *));
            memset(init->children, 0, (len + 1) * sizeof(Initializer *));
            for (int j = 0; j < len; j++)
                init->children[j] = new_initializer(vm, init->ty->base, false);
        } else {
            // For flexible arrays, create a fixed-size array type
            *init =
                *new_initializer(vm, array_of(vm, init->ty->base, len), false);
        }
    }

    // For VLA, loop until children run out; for regular arrays, use array_len
    int limit = (init->ty->kind == TY_VLA) ? INT_MAX : init->ty->array_len;
    for (; i < limit && !is_end(tok); i++) {
        Token *start = tok;
        if (i > 0)
            tok = skip(vm, tok, ",");

        if (equal(tok, "[") || equal(tok, ".")) {
            *rest = start;
            return;
        }

        initializer2(vm, &tok, tok, init->children[i]);
    }
    *rest = tok;
}

// struct-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void struct_initializer1(VirtualMachine *vm, Token **rest, Token *tok,
                                Initializer *init) {
    tok                   = skip(vm, tok, "{");

    Member *mem           = init->ty->members;
    bool    first         = true;
    bool    warned_excess = false;

    while (!consume_end(rest, tok)) {
        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        if (equal(tok, ".")) {
            if (vm->compiler.c_std < CCCC_STD_C99)
                warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                         "designated initializers are a C99 extension");
            mem = struct_designator(vm, &tok, tok, init->ty);
            if ((vm->compiler.warnings & CCCC_WARN_OVERRIDE_INIT) &&
                !is_anon_aggregate_member(mem) &&
                init->children[mem->idx]->is_set)
                warn_override_init_member(vm, tok, mem);
            designation(vm, &tok, tok, init->children[mem->idx]);
            mem = mem->next;
            continue;
        }

        if (mem) {
            if ((vm->compiler.warnings & CCCC_WARN_DESIGNATED_INIT) &&
                init->ty->designated_init)
                warn_tok(vm, tok, CCCC_WARN_DESIGNATED_INIT,
                         "positional initialization of field in struct "
                         "declared with 'designated_init' attribute");
            initializer2(vm, &tok, tok, init->children[mem->idx]);
            mem = mem->next;
        } else {
            if (!warned_excess) {
                warned_excess = true;
                warn_tok(vm, tok, CCCC_WARN_EXCESS_INIT,
                         "excess elements in struct initializer");
            }
            tok = skip_excess_element(vm, tok);
        }
    }
}

// struct-initializer2 = initializer ("," initializer)*
static void struct_initializer2(VirtualMachine *vm, Token **rest, Token *tok,
                                Initializer *init, Member *mem) {
    bool first = true;

    for (; mem && !is_end(tok); mem = mem->next) {
        Token *start = tok;

        if (!first)
            tok = skip(vm, tok, ",");
        first = false;

        if (equal(tok, "[") || equal(tok, ".")) {
            *rest = start;
            return;
        }

        if ((vm->compiler.warnings & CCCC_WARN_DESIGNATED_INIT) &&
            init->ty->designated_init)
            warn_tok(vm, tok, CCCC_WARN_DESIGNATED_INIT,
                     "positional initialization of field in struct "
                     "declared with 'designated_init' attribute");
        initializer2(vm, &tok, tok, init->children[mem->idx]);
    }
    *rest = tok;
}

static void union_initializer(VirtualMachine *vm, Token **rest, Token *tok,
                              Initializer *init) {
    // Unlike structs, union initializers take only one initializer,
    // and that initializes the first union member by default.
    // You can initialize other member using a designated initializer.
    //
    // #962: a union initializer can still have *multiple* comma-separated
    // designators (e.g. `{.i = 1, .i = 2}` or `{.i = 1, .f = 2}`) -- valid
    // C, accepted by gcc/clang, with the last designator's member winning
    // (union members alias, so this is exactly the same "last write wins"
    // rule an already-working named/anonymous nested union reaches via
    // designation()'s TY_UNION branch above). Loop rather than parsing a
    // single designator and demanding '}' immediately after.
    if (equal(tok, "{") && equal(tok->next, ".")) {
        tok = tok->next;
        for (;;) {
            Member *mem = struct_designator(vm, &tok, tok, init->ty);
            if ((vm->compiler.warnings & CCCC_WARN_OVERRIDE_INIT) &&
                !is_anon_aggregate_member(mem) && init->mem &&
                init->children[init->mem->idx]->is_set)
                // See the analogous comment in designation()'s TY_UNION
                // branch: name the previously-live member, not the one
                // currently being designated.
                warn_override_init_member(vm, tok, init->mem);
            init->mem = mem;
            designation(vm, &tok, tok, init->children[mem->idx]);
            if (equal(tok, ",") && equal(tok->next, ".")) {
                tok = tok->next;
                continue;
            }
            break;
        }
        consume(vm, &tok, tok, ",");
        *rest = skip(vm, tok, "}");
        return;
    }

    init->mem = init->ty->members;
    if (!init->mem) {
        if (equal(tok, "{")) {
            tok   = skip(vm, tok->next, "}");
            *rest = tok;
            return;
        }
        error_tok(vm, tok, "empty union initializer must be empty");
    }

    if (equal(tok, "{")) {
        initializer2(vm, &tok, tok->next, init->children[0]);
        consume(vm, &tok, tok, ",");
        *rest = skip(vm, tok, "}");
    } else {
        initializer2(vm, rest, tok, init->children[0]);
    }
}

// initializer = string-initializer | array-initializer
//             | struct-initializer | union-initializer
//             | assign
static void initializer2(VirtualMachine *vm, Token **rest, Token *tok,
                         Initializer *init) {
    init->is_set = true;

    if (init->ty->kind == TY_ARRAY && tok->kind == TK_STR) {
        string_initializer(vm, rest, tok, init);
        return;
    }

    if (init->ty->kind == TY_ARRAY) {
        if (equal(tok, "{"))
            array_initializer1(vm, rest, tok, init);
        else
            array_initializer2(vm, rest, tok, init, 0);
        return;
    }

    // GNU vector_size brace-initializer (tracker #713): only the braced form
    // is intercepted here. Non-brace forms (whole-vector copy `v4sf b = a;`,
    // or a bare scalar which is a deliberate compile error) must fall through
    // to the scalar `init->expr = assign(...)` path below unchanged.
    if (init->ty->kind == TY_VECTOR && equal(tok, "{")) {
        vector_initializer(vm, rest, tok, init);
        return;
    }

    // VLA initialization uses same syntax as array initialization
    if (init->ty->kind == TY_VLA) {
        if (equal(tok, "{"))
            array_initializer1(vm, rest, tok, init);
        else
            array_initializer2(vm, rest, tok, init, 0);
        return;
    }

    if (init->ty->kind == TY_STRUCT) {
        if (equal(tok, "{")) {
            struct_initializer1(vm, rest, tok, init);
            return;
        }

        // A struct can be initialized with another struct. E.g.
        // `struct T x = y;` where y is a variable of type `struct T`.
        // Handle that case first.
        Node *expr = assign(vm, rest, tok);
        add_type(vm, expr);
        if (expr->ty->kind == TY_STRUCT || expr->kind == ND_MACRO_CALL) {
            init->expr = expr;
            return;
        }

        struct_initializer2(vm, rest, tok, init, init->ty->members);
        return;
    }

    if (init->ty->kind == TY_UNION) {
        if (equal(tok, "{")) {
            union_initializer(vm, rest, tok, init);
            return;
        }

        // A union can be initialized with another union. E.g.
        // `union T x = y;` where y is a variable or expression of type `union
        // T`. Handle that case first.
        Node *expr = assign(vm, rest, tok);
        add_type(vm, expr);
        if (expr->ty->kind == TY_UNION) {
            init->expr = expr;
            return;
        }

        // Otherwise, initialize the first member
        union_initializer(vm, rest, tok, init);
        return;
    }

    if (equal(tok, "{")) {
        // An initializer for a scalar variable can be surrounded by
        // braces. E.g. `int x = {3};`. Handle that case.
        initializer2(vm, &tok, tok->next, init);
        *rest = skip(vm, tok, "}");
        return;
    }

    init->expr = assign(vm, rest, tok);
}

static Type *copy_struct_type(VirtualMachine *vm, Type *ty) {
    ty           = copy_type(vm, ty);

    Member  head = {};
    Member *cur  = &head;
    for (Member *mem = ty->members; mem; mem = mem->next) {
        Member *m = arena_alloc(&vm->compiler.parser_arena, sizeof(Member));
        memset(m, 0, sizeof(Member));
        *m  = *mem;
        cur = cur->next = m;
    }

    ty->members = head.next;
    return ty;
}

static Initializer *initializer(VirtualMachine *vm, Token **rest, Token *tok,
                                Type *ty, Type **new_ty) {
    Initializer *init = new_initializer(vm, ty, true);
    initializer2(vm, rest, tok, init);

    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->is_flexible) {
        ty          = copy_struct_type(vm, ty);

        Member *mem = ty->members;
        while (mem->next)
            mem = mem->next;
        mem->ty   = init->children[mem->idx]->ty;
        ty->size += mem->ty->size;

        *new_ty   = ty;
        return init;
    }

    *new_ty = init->ty;
    return init;
}

static bool is_constexpr_object_type(Type *ty) {
    if (!ty)
        return false;
    if (ty->kind == TY_FUNC || ty->kind == TY_VLA || ty->is_atomic ||
        ty->is_volatile || ty->is_restrict)
        return false;
    if (ty->kind == TY_ARRAY)
        return ty->size >= 0 && is_constexpr_object_type(ty->base);
    if (ty->kind == TY_PTR)
        return !type_has_restrict(ty);
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        for (Member *mem = ty->members; mem; mem = mem->next)
            if (!is_constexpr_object_type(mem->ty))
                return false;
    }
    return true;
}

static void validate_constexpr_object_type(VirtualMachine *vm, Token *tok,
                                           Type *ty) {
    if (!is_constexpr_object_type(ty))
        error_tok(vm, tok,
                  "constexpr object has unsupported type or qualifiers");
}

static bool initializer_is_constexpr(VirtualMachine *vm, Initializer *init,
                                     Type *ty) {
    if (!init)
        return true;
    if (init->expr)
        return is_const_expr(vm, init->expr);
    if (ty->kind == TY_ARRAY) {
        for (int i = 0; i < ty->array_len; i++)
            if (!initializer_is_constexpr(vm, init->children[i], ty->base))
                return false;
        return true;
    }
    if (ty->kind == TY_STRUCT) {
        for (Member *mem = ty->members; mem; mem = mem->next)
            if (!initializer_is_constexpr(vm, init->children[mem->idx],
                                          mem->ty))
                return false;
        return true;
    }
    if (ty->kind == TY_UNION)
        return !init->mem ||
               initializer_is_constexpr(vm, init->children[init->mem->idx],
                                        init->mem->ty);
    return true;
}

static void validate_constexpr_initializer(VirtualMachine *vm, Obj *var,
                                           Initializer *init, Token *tok) {
    validate_constexpr_object_type(vm, var->tok ? var->tok : tok, var->ty);
    if (!initializer_is_constexpr(vm, init, var->ty))
        error_tok(vm, tok,
                  "constexpr initializer is not a constant expression");
    var->constexpr_init = init;
    if (init && init->expr)
        var->init_expr = init->expr;
}

static Node *init_desg_expr(VirtualMachine *vm, InitDesg *desg, Token *tok) {
    if (desg->var)
        return new_var_node(vm, desg->var, tok);

    if (desg->member) {
        Node *node =
            new_unary(vm, ND_MEMBER, init_desg_expr(vm, desg->next, tok), tok);
        node->member = desg->member;
        return node;
    }

    Node *lhs = init_desg_expr(vm, desg->next, tok);
    Node *rhs = new_num(vm, desg->idx, tok);

    // Lane lvalue for a vector_size vector (tracker #713). new_add(vec, int)
    // means element-wise vector arithmetic (parse.c ~5852), not pointer
    // arithmetic, so a plain new_add() here would misinterpret the index.
    // Mirror the postfix `v[i]` lowering: &v cast to element-pointer, then
    // ordinary pointer-offset + deref. This generalizes to nesting for free
    // (array-of-vector, struct-of-vector) since `lhs` is retyped at each
    // recursion level.
    add_type(vm, lhs);
    if (lhs->ty && is_vector(lhs->ty)) {
        Type *elem_ty = lhs->ty->base;
        Node *addr    = new_unary(vm, ND_ADDR, lhs, tok);
        addr          = new_cast(vm, addr, pointer_to(vm, elem_ty));
        return new_unary(vm, ND_DEREF, new_add(vm, addr, rhs, tok), tok);
    }

    return new_unary(vm, ND_DEREF, new_add(vm, lhs, rhs, tok), tok);
}

// Combine items[lo..hi) into a *balanced* ND_COMMA tree (height O(log n))
// rather than a left-leaning spine (height n). The comma operator is fully
// associative — `(a,b),c` and `a,(b,c)` both evaluate a,b,c left-to-right and
// yield the last value — so the shape is free to choose. A balanced shape keeps
// every recursive AST pass (add_type, gen_expr, free, ...) at O(log n) stack
// depth, so a large brace-initializer no longer overflows the C stack (#576).
// Requires hi > lo.
static Node *balanced_comma(VirtualMachine *vm, Node **items, int lo, int hi,
                            Token *tok) {
    if (hi - lo == 1)
        return items[lo];
    int   mid = lo + (hi - lo) / 2;
    Node *l   = balanced_comma(vm, items, lo, mid, tok);
    Node *r   = balanced_comma(vm, items, mid, hi, tok);
    return new_binary(vm, ND_COMMA, l, r, tok);
}

static Node *create_lvar_init(VirtualMachine *vm, Initializer *init, Type *ty,
                              InitDesg *desg, Token *tok) {
    // Multi-dimensional VLA (#977): a nested row's own type is TY_VLA (e.g.
    // int v[n][m]'s outer dimension has a TY_VLA base), so a plain TY_ARRAY
    // recursion never reaches it -- this branch is what create_vla_init
    // (below) delegates to for both the outer VLA and any TY_VLA row nested
    // inside it. init->children is NULL-terminated (array_initializer1/2,
    // #977) rather than sized by a compile-time array_len, since a VLA's
    // element count is only known at parse time from the initializer itself.
    if (ty->kind == TY_VLA && init->children) {
        int cnt_children = 0;
        while (init->children[cnt_children])
            cnt_children++;
        if (cnt_children == 0)
            return new_node(vm, ND_NULL_EXPR, tok);
        Node **items = malloc((size_t)cnt_children * sizeof(Node *));
        if (!items)
            error_tok(vm, tok, "out of memory building VLA initializer");
        int cnt = 0;
        for (int i = 0; i < cnt_children; i++) {
            InitDesg desg2 = {desg, i};
            Node    *rhs =
                create_lvar_init(vm, init->children[i], ty->base, &desg2, tok);
            if (rhs->kind != ND_NULL_EXPR)
                items[cnt++] = rhs;
        }
        Node *node = cnt ? balanced_comma(vm, items, 0, cnt, tok)
                         : new_node(vm, ND_NULL_EXPR, tok);
        free(items);
        return node;
    }

    if (ty->kind == TY_ARRAY) {
        // Collect the per-element assignments, then fold them into a balanced
        // comma tree. Implicitly-zero elements lower to ND_NULL_EXPR, which
        // emits no code (lvar_initializer already pre-zeroes the whole object
        // with a single ND_MEMZERO), so we drop them — both to avoid waste and
        // because they are exactly what makes a partial `= {0}` initialiser
        // huge. See balanced_comma / #576.
        if (ty->array_len <= 0)
            return new_node(vm, ND_NULL_EXPR, tok);
        Node **items = malloc((size_t)ty->array_len * sizeof(Node *));
        if (!items)
            error_tok(vm, tok, "out of memory building array initializer");
        int cnt = 0;
        for (int i = 0; i < ty->array_len; i++) {
            InitDesg desg2 = {desg, i};
            Node    *rhs =
                create_lvar_init(vm, init->children[i], ty->base, &desg2, tok);
            if (rhs->kind != ND_NULL_EXPR)
                items[cnt++] = rhs;
        }
        Node *node = cnt ? balanced_comma(vm, items, 0, cnt, tok)
                         : new_node(vm, ND_NULL_EXPR, tok);
        free(items);
        return node;
    }

    if (ty->kind == TY_STRUCT && !init->expr) {
        int nmem = 0;
        for (Member *mem = ty->members; mem; mem = mem->next)
            nmem++;
        if (nmem == 0)
            return new_node(vm, ND_NULL_EXPR, tok);
        Node **items = malloc((size_t)nmem * sizeof(Node *));
        if (!items)
            error_tok(vm, tok, "out of memory building struct initializer");
        int cnt = 0;
        for (Member *mem = ty->members; mem; mem = mem->next) {
            InitDesg desg2 = {desg, 0, mem};
            Node *rhs = create_lvar_init(vm, init->children[mem->idx], mem->ty,
                                         &desg2, tok);
            if (rhs->kind != ND_NULL_EXPR) // no-op; pre-zeroed by ND_MEMZERO
                items[cnt++] = rhs;
        }
        Node *node = cnt ? balanced_comma(vm, items, 0, cnt, tok)
                         : new_node(vm, ND_NULL_EXPR, tok);
        free(items);
        return node;
    }

    if (ty->kind == TY_UNION && !init->expr) {
        Member *mem = init->mem ? init->mem : ty->members;
        if (!mem)
            return new_node(vm, ND_NULL_EXPR, tok);
        InitDesg desg2 = {desg, 0, mem};
        return create_lvar_init(vm, init->children[mem->idx], mem->ty, &desg2,
                                tok);
    }

    // GNU vector_size brace-initializer (tracker #713). Gated on !init->expr
    // so a whole-vector copy (`v4sf b = a;`) still routes through the scalar
    // ND_ASSIGN path below (init->children is unused/unset in that case).
    if (ty->kind == TY_VECTOR && !init->expr) {
        Node **items = malloc((size_t)ty->vec_len * sizeof(Node *));
        if (!items)
            error_tok(vm, tok, "out of memory building vector initializer");
        int cnt = 0;
        for (int i = 0; i < ty->vec_len; i++) {
            InitDesg desg2 = {desg, i};
            Node    *rhs =
                create_lvar_init(vm, init->children[i], ty->base, &desg2, tok);
            if (rhs->kind != ND_NULL_EXPR)
                items[cnt++] = rhs;
        }
        Node *node = cnt ? balanced_comma(vm, items, 0, cnt, tok)
                         : new_node(vm, ND_NULL_EXPR, tok);
        free(items);
        return node;
    }

    if (!init->expr)
        return new_node(vm, ND_NULL_EXPR, tok);

    Node *lhs = init_desg_expr(vm, desg, tok);
    return new_binary(vm, ND_ASSIGN, lhs, init->expr, tok);
}

static bool is_init_splice_expr(Node *expr) {
    return expr && expr->kind == ND_VAR && expr->var &&
           expr->var->is_splice_placeholder;
}

static int init_tail_count(Node *tail) {
    int n = 0;
    for (; tail; tail = tail->next)
        n++;
    return n;
}

static Member *member_at_index(Type *ty, int idx) {
    for (Member *mem = ty->members; mem; mem = mem->next)
        if (mem->idx == idx)
            return mem;
    return NULL;
}

static Node *init_lhs_at(VirtualMachine *vm, Obj *var, Type *ty, int idx,
                         Token *tok) {
    InitDesg base_desg = {NULL, 0, NULL, var};
    if (ty->kind == TY_STRUCT) {
        Member *mem = member_at_index(ty, idx);
        if (!mem)
            return NULL;
        InitDesg desg = {&base_desg, 0, mem};
        return init_desg_expr(vm, &desg, tok);
    }

    if (ty->kind == TY_ARRAY) {
        InitDesg desg = {&base_desg, idx};
        return init_desg_expr(vm, &desg, tok);
    }

    return NULL;
}

static Node *append_init_assignment(VirtualMachine *vm, Node *result, Obj *var,
                                    Type *ty, int idx, Node *rhs, Token *tok) {
    Node *lhs = init_lhs_at(vm, var, ty, idx, tok);
    if (!lhs)
        error_tok(vm, tok, "$@k initializer splice target is out of range");
    rhs->next = NULL;
    return new_binary(vm, ND_COMMA, result,
                      new_binary(vm, ND_ASSIGN, lhs, rhs, tok), tok);
}

// Called from reflection.c quote_substitute to expand ND_INIT_SPLICE nodes.
// Builds positional ND_ASSIGN chains for a splice chain plus any ordinary
// initializer tail that followed the $@k placeholder in the source template.
Node *node_expand_init_splice(VirtualMachine *vm, Node *splice, Node *chain) {
    Obj   *var          = splice->var;
    Type  *ty           = var->ty;
    Token *tok          = splice->tok;
    Node  *result       = new_node(vm, ND_NULL_EXPR, tok);
    Node  *elem         = chain;
    int    start        = splice->init_start_index;
    int    splice_count = 0;
    int    tail_count   = init_tail_count(splice->init_tail);

    for (Node *n = chain; n; n = n->next)
        splice_count++;

    int capacity = 0;
    if (ty->kind == TY_STRUCT) {
        for (Member *mem = ty->members; mem; mem = mem->next)
            capacity++;
    } else if (ty->kind == TY_ARRAY) {
        if (splice->init_inferred_array) {
            int len = start + splice_count + tail_count;
            var->ty = ty = array_of(vm, ty->base, len);
        }
        capacity = ty->array_len;
    } else {
        return result;
    }

    int total = splice_count + tail_count;
    if (total < capacity - start)
        error_tok(vm, tok,
                  "$@k initializer splice: too few elements (got %d, need %d)",
                  total, capacity - start);
    if (total > capacity - start)
        error_tok(vm, tok,
                  "$@k initializer splice: too many elements (got %d, need %d)",
                  total, capacity - start);

    int idx = start;
    while (elem) {
        Node *next = elem->next;
        elem->next = NULL;
        result = append_init_assignment(vm, result, var, ty, idx++, elem, tok);
        elem   = next;
    }

    for (Node *tail = splice->init_tail; tail;) {
        Node *next = tail->next;
        tail->next = NULL;
        result = append_init_assignment(vm, result, var, ty, idx++, tail, tok);
        tail   = next;
    }

    return result;
}

// Generate initialization for VLA
// Unlike create_lvar_init which uses ty->array_len, VLAs have
// runtime-determined size, so element count comes from the NULL-terminated
// init->children array built at parse time (array_initializer1/2). The
// element-count guards are duplicated ahead of the delegate call rather than
// left to create_lvar_init's own TY_VLA branch to keep create_vla_init's
// "no VLA initializer present" contract (returning NULL) unchanged for
// var_definition()'s NULL check (parse.c ~4066); create_lvar_init itself
// returns ND_NULL_EXPR, not NULL, for an empty initializer. #977 folded the
// former hand-rolled left-leaning comma spine (which built one ND_COMMA per
// element and kept ND_NULL_EXPR children) into create_lvar_init's own
// TY_VLA branch -- this now also correctly handles a multi-dimensional VLA
// (int v[n][m] = {{1,2},{3,4}}), where ty->base is itself TY_VLA.
static Node *create_vla_init(VirtualMachine *vm, Initializer *init, Type *ty,
                             Obj *var, Token *tok) {
    if (!init || ty->kind != TY_VLA || !init->children)
        return NULL;

    int init_count = 0;
    while (init->children[init_count])
        init_count++;
    if (init_count == 0)
        return NULL;

    InitDesg desg = {NULL, 0, NULL, var};
    return create_lvar_init(vm, init, ty, &desg, tok);
}

// A variable definition with an initializer is a shorthand notation
// for a variable definition followed by assignments. This function
// generates assignment expressions for an initializer. For example,
// `int x[2][2] = {{6, 7}, {8, 9}}` is converted to the following
// expressions:
//
//   x[0][0] = 6;
//   x[0][1] = 7;
//   x[1][0] = 8;
//   x[1][1] = 9;
static Node *append_init_tail(Node *tail, Node *expr) {
    if (!expr)
        return tail;
    expr->next = NULL;
    if (!tail)
        return expr;
    Node *cur = tail;
    while (cur->next)
        cur = cur->next;
    cur->next = expr;
    return tail;
}

static bool build_deferred_init_splice(VirtualMachine *vm, Initializer *init,
                                       Obj *var, bool inferred_array,
                                       Token *tok, Node **out) {
    Type *ty = var->ty;
    if ((ty->kind != TY_STRUCT && ty->kind != TY_ARRAY) || !init->children)
        return false;

    int len = 0;
    if (ty->kind == TY_ARRAY) {
        len = ty->array_len;
    } else {
        for (Member *mem = ty->members; mem; mem = mem->next)
            len++;
    }

    int   splice_idx  = -1;
    Node *placeholder = NULL;
    for (int i = 0; i < len; i++) {
        Initializer *child = init->children[i];
        if (!child || !is_init_splice_expr(child->expr))
            continue;
        if (splice_idx >= 0)
            error_tok(vm, tok, "only one $@k initializer splice is supported");
        splice_idx  = i;
        placeholder = child->expr;
    }
    if (splice_idx < 0)
        return false;

    Node *node         = new_node(vm, ND_MEMZERO, tok);
    node->var          = var;

    InitDesg base_desg = {NULL, 0, NULL, var};
    for (int i = 0; i < splice_idx; i++) {
        Initializer *child = init->children[i];
        if (!child)
            continue;
        InitDesg desg = {&base_desg, i};
        if (ty->kind == TY_STRUCT)
            desg.member = member_at_index(ty, i);
        Node *rhs = create_lvar_init(vm, child, child->ty, &desg, tok);
        node      = new_binary(vm, ND_COMMA, node, rhs, tok);
    }

    Node *tail = NULL;
    for (int i = splice_idx + 1; i < len; i++) {
        Initializer *child = init->children[i];
        if (child && child->expr)
            tail = append_init_tail(tail, child->expr);
    }

    Node *splice                = new_node(vm, ND_INIT_SPLICE, tok);
    splice->var                 = var;
    splice->lhs                 = placeholder;
    splice->init_tail           = tail;
    splice->init_start_index    = splice_idx;
    splice->init_inferred_array = inferred_array;

    *out                        = new_binary(vm, ND_COMMA, node, splice, tok);
    return true;
}

Node *lvar_initializer(VirtualMachine *vm, Token **rest, Token *tok, Obj *var) {
    bool inferred_array = var->ty->kind == TY_ARRAY && var->ty->size < 0;
    Initializer *init   = initializer(vm, rest, tok, var->ty, &var->ty);

    if (var->is_constexpr)
        validate_constexpr_initializer(vm, var, init, tok);

    // $@k splice in compound-literal context: defer final positional lowering
    // until quote_substitute knows the caller-provided chain length.
    Node *deferred = NULL;
    if (build_deferred_init_splice(vm, init, var, inferred_array, tok,
                                   &deferred))
        return deferred;

    InitDesg desg = {NULL, 0, NULL, var};

    // If a partial initializer list is given, the standard requires that
    // unspecified elements/members are set to 0. We zero-initialize the entire
    // memory region of the variable before applying the user-supplied values.
    //
    // Only aggregates (struct/union/array) can be partially initialized, so the
    // pre-zero is needed for them alone. A scalar's single initializer always
    // fully defines its value, so create_lvar_init's assignment overwrites the
    // whole object — a leading ND_MEMZERO would be dead. Emitting it anyway is
    // not just wasteful: ND_MEMZERO lowers to MSET, which clobbers REG_A0/A2,
    // so every redundant memzero widens the call-argument clobber surface (see
    // contains_funcall in codegen.c). Restrict it to the types that need it.
    Node *rhs          = create_lvar_init(vm, init, var->ty, &desg, tok);
    Type *t            = var->ty;
    bool  is_aggregate = t->kind == TY_STRUCT || t->kind == TY_UNION ||
                         t->kind == TY_ARRAY || t->kind == TY_VECTOR;
    if (!is_aggregate)
        return rhs;

    Node *lhs = new_node(vm, ND_MEMZERO, tok);
    lhs->var  = var;
    return new_binary(vm, ND_COMMA, lhs, rhs, tok);
}

static uint64_t read_buf(char *buf, int sz) {
    if (sz == 1)
        return *buf;
    if (sz == 2)
        return *(uint16_t *)buf;
    if (sz == 4)
        return *(uint32_t *)buf;
    if (sz == 8)
        return *(uint64_t *)buf;
    unreachable();
    return 0;
}

static void write_buf(char *buf, uint64_t val, int sz) {
    if (sz == 1)
        *buf = val;
    else if (sz == 2)
        *(uint16_t *)buf = val;
    else if (sz == 4)
        *(uint32_t *)buf = val;
    else if (sz == 8)
        *(uint64_t *)buf = val;
    else
        unreachable();
}

static Relocation *write_gvar_data(VirtualMachine *vm, Relocation *cur,
                                   Initializer *init, Type *ty, char *buf,
                                   int offset) {
    // An aggregate/vector initialized from a whole-value expression rather
    // than a brace-list -- a compound literal (which, thanks to
    // in_const_gvar_init, always resolves here to a bare reference to an
    // anonymous constant global) -- has no children[] to recurse into.
    // GCC/clang extend constant-initializer folding to a compound literal's
    // own (constant) elements, but NOT to an arbitrary global reference by
    // value (`struct T x = y;` is rejected by real compilers even when y is
    // itself constant-initialized), so this gates on is_compound_literal,
    // not merely on having init_data. If the expression is such a
    // reference, splice its bytes and relocations in directly; otherwise
    // it's not something this compile-time serializer can evaluate (#720 --
    // this used to silently leave the region zeroed for the compound-literal
    // case instead of either working or erroring).
    if (init->expr && (ty->kind == TY_STRUCT || ty->kind == TY_UNION ||
                       ty->kind == TY_ARRAY || ty->kind == TY_VECTOR)) {
        Node *src = init->expr;
        if (src->kind == ND_VAR && src->var && src->var->is_compound_literal) {
            memcpy(buf + offset, src->var->init_data, ty->size);
            for (Relocation *r = src->var->rel; r; r = r->next) {
                Relocation *nr =
                    arena_alloc(&vm->compiler.parser_arena, sizeof(Relocation));
                memset(nr, 0, sizeof(Relocation));
                nr->offset = offset + r->offset;
                nr->label  = r->label;
                nr->addend = r->addend;
                cur->next  = nr;
                cur        = nr;
            }
            return cur;
        }
        error_tok(vm, src->tok,
                  "initializer element is not a compile-time constant");
    }

    if (ty->kind == TY_ARRAY) {
        int sz = ty->base->size;
        for (int i = 0; i < ty->array_len; i++)
            cur = write_gvar_data(vm, cur, init->children[i], ty->base, buf,
                                  offset + sz * i);
        return cur;
    }

    if (ty->kind == TY_STRUCT) {
        for (Member *mem = ty->members; mem; mem = mem->next) {
            if (mem->is_bitfield) {
                Node *expr = init->children[mem->idx]->expr;
                if (!expr)
                    break;

                char *loc = buf + offset + mem->offset;
                if (mem->ty->size > 8) {
                    // #1125: a bit-precise bitfield wider than 8 bytes (e.g.
                    // `_BitInt(128) f : 100;`) used to RMW the whole
                    // storage-unit container (mem->ty->size bytes) as a word
                    // array -- but `buf` is only allocated var->ty->size
                    // bytes (the *struct's* compact size, e.g. 25 for a
                    // `_BitInt(256) f : 193;` whose container is 32 bytes),
                    // so that write ran past the arena allocation. Use the
                    // same byte-granular __cccc_bitfield_insert the runtime
                    // RMW path uses (src/codegen_expr.c's ND_ASSIGN,
                    // mirrored here since this runs at parse/compile time,
                    // not inside the VM) -- it touches only the bytes the
                    // field actually spans.
                    //
                    // eval_wide writes mem->ty->size bytes (the *container*
                    // type's own width, e.g. 16 for `_BitInt(128) f : 60;`,
                    // not the 8 bytes ceil(60/64) alone would need) -- newval
                    // must be sized for that, even though only the low
                    // ceil(bit_width/64) words end up meaningful once
                    // truncated below.
                    int       container_words = mem->ty->size / 8;
                    int       value_words     = (mem->bit_width + 63) / 64;
                    uint64_t *newval = arena_alloc(&vm->compiler.parser_arena,
                                                   (size_t)container_words * 8);
                    eval_wide(vm, expr, mem->ty, newval);
                    __cccc_bitint_trunc(newval, value_words, mem->bit_width);
                    __cccc_bitfield_insert((unsigned char *)loc, newval,
                                           mem->bit_offset, mem->bit_width);
                } else {
                    uint64_t oldval = read_buf(loc, mem->ty->size);
                    uint64_t newval = eval(vm, expr);
                    // #1122: (1L << 64) is UB (and, being `1L`, was already
                    // wrong on an LP64 host for bit_width==63/64 -- the sign
                    // bit of the shift result overlaps the arithmetic).
                    // unsigned long long f : 64 in a global previously
                    // serialized silently as 0.
                    uint64_t mask = mem->bit_width >= 64
                                        ? ~0ULL
                                        : ((1ULL << mem->bit_width) - 1);
                    uint64_t combined =
                        oldval | ((newval & mask) << mem->bit_offset);
                    write_buf(loc, combined, mem->ty->size);
                }
            } else {
                cur = write_gvar_data(vm, cur, init->children[mem->idx],
                                      mem->ty, buf, offset + mem->offset);
            }
        }
        return cur;
    }

    if (ty->kind == TY_UNION) {
        if (!init->mem)
            return cur;
        return write_gvar_data(vm, cur, init->children[init->mem->idx],
                               init->mem->ty, buf, offset);
    }

    if (ty->kind == TY_VECTOR) {
        int sz = ty->base->size;
        for (int i = 0; i < ty->vec_len; i++)
            cur = write_gvar_data(vm, cur, init->children[i], ty->base, buf,
                                  offset + sz * i);
        return cur;
    }

    if (!init->expr)
        return cur;

    if (ty->kind == TY_FLOAT) {
        *(float *)(buf + offset) = eval_double(vm, init->expr);
        return cur;
    }

    if (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE) {
        // #1122: TY_LDOUBLE (size 16) used to fall through to the >8-byte
        // integer scalar tail and crash write_buf. This compiler stores
        // `long double` as a plain 8-byte double everywhere else (the VM's
        // FLDR_LOCAL/FSTR_LOCAL, serialize_init_bytes's TY_LDOUBLE arm) --
        // matching that here keeps the upper 8 bytes of the slot correctly
        // zeroed (gvar_initializer memsets buf before this call) rather than
        // reading/writing past what any consumer looks at.
        *(double *)(buf + offset) = eval_double(vm, init->expr);
        return cur;
    }

    if (ty->kind == TY_COMPLEX) {
        // #1122: TY_COMPLEX (size 8/16/32) used to fall through to the
        // integer scalar tail and crash (or, for _Complex float at size 8,
        // silently write an eval2'd integer bit pattern into the real+imag
        // pair). #1208: fold both parts. This compiler still has no
        // imaginary-literal syntax (`3.5if` is a parse error), but
        // `I`/`_Complex_I`/`CMPLX()` from complex.h desugar to
        // `__cccc_cmplx(...)` (an ND_COMPLEX node), which eval_complex now
        // folds -- so `static double _Complex z = 3.0 + 4.0*I;` is a
        // constant expression. The imaginary half is written at
        // `offset + ty->base->size` (the same real-then-imag contiguous
        // layout complex_part_offset() uses in codegen).
        double real = 0, imag = 0;
        eval_complex(vm, init->expr, &real, &imag);
        int part = ty->base ? ty->base->size : 8;
        if (ty->base && ty->base->kind == TY_FLOAT) {
            *(float *)(buf + offset)        = (float)real;
            *(float *)(buf + offset + part) = (float)imag;
        } else {
            *(double *)(buf + offset)        = real;
            *(double *)(buf + offset + part) = imag;
        }
        return cur;
    }

    if (is_decimal(ty)) {
        // #832: fold arbitrary decimal constant arithmetic (literals, casts,
        // +-*/, unary -, ?:, comma), not just a bare literal or a cast of
        // one -- eval_decimal walks the same node shapes eval_double does.
        // Always CCCC_DEC_ENV_STATIC internally (round-to-nearest, flags
        // discarded, host fenv untouched -- see eval_decimal's own comment).
        eval_decimal(vm, init->expr, dec_width_code(ty), buf + offset);
        return cur;
    }

    if (is_integer(ty) && ty->size > 8) {
        // #1122: __int128 / _BitInt(65..65535). A relocation (the address
        // of a global +/- an offset) can't be represented in a wide integer
        // slot; eval_wide_node has no ND_ADDR/ND_LABEL_VAL arm, so an
        // attempt to initialize a wide integer that way falls through to
        // its default case and reports "not a compile-time constant"
        // cleanly instead of silently mis-evaluating.
        eval_wide(vm, init->expr, ty, (uint64_t *)(buf + offset));
        return cur;
    }

    char   **label = NULL;
    uint64_t val   = eval2(vm, init->expr, &label);

    if (!label) {
        write_buf(buf + offset, val, ty->size);
        return cur;
    }

    Relocation *rel =
        arena_alloc(&vm->compiler.parser_arena, sizeof(Relocation));
    memset(rel, 0, sizeof(Relocation));
    rel->offset = offset;
    rel->label  = label;
    rel->addend = val;
    cur->next   = rel;
    return cur->next;
}

// Returns true if any node in the expression tree is an ND_MACRO_CALL.
// Used to detect scalar global initializers that require deferred evaluation.
static bool expr_contains_macro_call(Node *node) {
    if (!node)
        return false;
    if (node->kind == ND_MACRO_CALL)
        return true;
    return expr_contains_macro_call(node->lhs) ||
           expr_contains_macro_call(node->rhs) ||
           expr_contains_macro_call(node->cond) ||
           expr_contains_macro_call(node->then) ||
           expr_contains_macro_call(node->els);
}

// Initializers for global variables are evaluated at compile-time and
// embedded to .data section. This function serializes Initializer
// objects to a flat byte array. It is a compile error if an
// initializer list contains a non-constant expression.
//
// Exception: if the scalar initializer expression contains an ND_MACRO_CALL,
// serialization is deferred to cc_finalize_macro_gvar_inits (called from
// cc_expand_macros after all macros have been compiled and expanded).
// See ticket #613.
void gvar_initializer(VirtualMachine *vm, Token **rest, Token *tok, Obj *var) {
    // See in_const_gvar_init's declaration (#720): any compound literal
    // parsed while this is set resolves to an anonymous constant global
    // rather than an auto-storage local, regardless of lexical scope.
    bool prev_in_const_gvar_init    = vm->compiler.in_const_gvar_init;
    vm->compiler.in_const_gvar_init = true;
    Initializer *init = initializer(vm, rest, tok, var->ty, &var->ty);
    vm->compiler.in_const_gvar_init = prev_in_const_gvar_init;

    // For constexpr variables, save the initializer expression for compile-time
    // evaluation
    if (var->is_constexpr)
        validate_constexpr_initializer(vm, var, init, tok);

    // If the scalar initializer expression contains a macro call, defer
    // write_gvar_data to cc_finalize_macro_gvar_inits (after cc_expand_macros).
    // This avoids premature compile_all_macros which would break $symbol
    // forward-reference lookups in other comptime functions (#613).
    if (!var->is_constexpr && init->expr &&
        expr_contains_macro_call(init->expr)) {
        // Temporarily borrow constexpr_init (unused for non-constexpr vars) to
        // store the pending Initializer tree. constexpr_init_for_node() guards
        // on is_constexpr before reading this field, so there is no conflict.
        var->constexpr_init         = init;
        var->has_pending_macro_init = true;
        return;
    }

    Relocation head = {};
    char      *buf  = arena_alloc(&vm->compiler.parser_arena, var->ty->size);
    memset(buf, 0, var->ty->size);
    write_gvar_data(vm, &head, init, var->ty, buf, 0);
    var->init_data = buf;
    var->rel       = head.next;
}

// Finalize global variable initializers that were deferred because their
// scalar expression contained an ND_MACRO_CALL (see gvar_initializer above).
// Called from cc_expand_macros after compile_all_macros and the AST transform
// passes complete, so all macros are compiled and all symbols are defined.
void cc_finalize_macro_gvar_inits(VirtualMachine *vm, Obj *prog) {
    if (!vm || !prog)
        return;
    for (Obj *var = prog; var; var = var->next) {
        if (!var->has_pending_macro_init)
            continue;
        Initializer *init = (Initializer *)var->constexpr_init;
        // Expand macro calls in the scalar initializer expression.
        if (init->expr)
            init->expr = cc_eager_expand_macro_call(vm, init->expr);
        // Serialize the expanded expression to .data.
        Relocation head = {};
        char      *buf = arena_alloc(&vm->compiler.parser_arena, var->ty->size);
        memset(buf, 0, var->ty->size);
        write_gvar_data(vm, &head, init, var->ty, buf, 0);
        var->init_data = buf;
        var->rel       = head.next;
        // Clear the temporary storage.
        var->has_pending_macro_init = false;
        var->constexpr_init         = NULL;
    }
}

HashMap        typename_map;
pthread_once_t typename_map_once = PTHREAD_ONCE_INIT;

void init_typename_map(void) {
    static char *kw[] = {
        "void",          "_Bool",        "char",          "short",
        "int",           "long",         "struct",        "union",
        "typedef",       "enum",         "static",        "extern",
        "_Alignas",      "signed",       "unsigned",      "const",
        "volatile",      "auto",         "register",      "restrict",
        "__restrict",    "__restrict__", "_Noreturn",     "float",
        "double",        "typeof",       "typeof_unqual", "inline",
        "_Thread_local", "__thread",     "_Atomic",       "constexpr",
        "__block",       "_Complex",     "_Imaginary",    "_BitInt",
        "_Decimal32",    "_Decimal64",   "_Decimal128",   "__int128",
        "__int128_t",    "__uint128_t",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        hashmap_put_borrowed(&typename_map, kw[i], (void *)1);
}
