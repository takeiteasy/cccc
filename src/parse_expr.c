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

// Expression grammar: the full C precedence chain from expr() down through
// cast(), plus constant-expression evaluation helpers (eval_double/
// eval_decimal) and pointer arithmetic (new_add/new_sub).

#include "./parse_internal.h"
#include <fenv.h> // host fenv.h -- #832 eval_decimal's fenv barrier

// expr = assign ("," expr)?
Node *expr(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = assign(vm, &tok, tok);

    if (equal(tok, ","))
        return new_binary(vm, ND_COMMA, node, expr(vm, rest, tok->next), tok);

    *rest = tok;
    return node;
}

double eval_double(VirtualMachine *vm, Node *node) {
    add_type(vm, node);

    // _Decimal32/64/128: node->fval is never populated for these -- the
    // ND_NUM case below would otherwise silently return 0.0 for any decimal
    // constant expression. A node whose *own* type is decimal only reaches
    // here through a decimal comparison used directly in a floating
    // constant-expression context (e.g. as a controlling condition), not
    // through the ND_CAST arm below (which now folds a decimal-to-binary-
    // float cast via eval_decimal, #832) -- so this is still a diagnostic,
    // not silent 0.
    if (is_decimal(node->ty))
        error_tok(vm, node->tok,
                  "_Decimal constant expressions are not supported in this context");

    if (is_integer(node->ty)) {
        if (node->ty->is_unsigned)
            return (unsigned long long)eval(vm, node);
        return eval(vm, node);
    }

    switch (node->kind) {
    case ND_ADD:
        return eval_double(vm, node->lhs) + eval_double(vm, node->rhs);
    case ND_SUB:
        return eval_double(vm, node->lhs) - eval_double(vm, node->rhs);
    case ND_MUL:
        return eval_double(vm, node->lhs) * eval_double(vm, node->rhs);
    case ND_DIV:
        return eval_double(vm, node->lhs) / eval_double(vm, node->rhs);
    case ND_NEG:
        return -eval_double(vm, node->lhs);
    case ND_COND:
        return eval_double(vm, node->cond) ? eval_double(vm, node->then)
                                           : eval_double(vm, node->els);
    case ND_COMMA:
        return eval_double(vm, node->rhs);
    case ND_CAST:
        // #832: a decimal-to-binary-float cast (e.g. `(double)(1.1dd +
        // 2.2dd)`) folds via eval_decimal + cccc_dec_to_bin instead of
        // recursing into eval_double, which would just hit this function's
        // own is_decimal(node->ty) guard above on the recursive call.
        if (is_decimal(node->lhs->ty)) {
            int w = dec_width_code(node->lhs->ty);
            unsigned char tmp[16];
            eval_decimal(vm, node->lhs, w, tmp);
            uint64_t bits = 0;
            bool dst_is_f32 = (node->ty->kind == TY_FLOAT);
            if (!cccc_dec_to_bin(w, tmp, dst_is_f32, &bits, CCCC_DEC_ENV_STATIC))
                error_tok(vm, node->tok,
                          "_Decimal literals require a build with CCCC_HAS_DECIMAL=1");
            if (dst_is_f32) {
                float fv; memcpy(&fv, &bits, 4);
                return (double)fv;
            }
            double dv; memcpy(&dv, &bits, 8);
            return dv;
        }
        // #780: route through eval_double unconditionally, even for an
        // integer operand -- its is_integer() head above applies the
        // correct unsigned widening (unsigned long long, not a bare
        // "eval()" whose int64_t return implicitly sign-converts to
        // double and loses the operand's unsignedness for values >= 2^63).
        return eval_double(vm, node->lhs);
    case ND_VAR:
    case ND_MEMBER: {
        Node *expr = constexpr_expr_for_node(node);
        if (!expr)
            error_tok(vm, node->tok, "not a compile-time constant");
        return eval_double(vm, expr);
    }
    case ND_NUM:
        return node->fval;
    default:
        error_tok(vm, node->tok, "not a compile-time constant");
        return 0;
    }
}

// #832: fold a decimal-typed constant expression at compile time into a
// 4/8/16-byte BID buffer. Mirrors eval_double's node-kind table above, but
// for decimal arithmetic (which eval_double explicitly refuses via its own
// is_decimal(node->ty) guard).
//
// Width discipline: each recursive call derives its own width from the
// *node's* type (dec_width_code(node->ty)), never trusts the caller's `w`
// beyond the final store -- usual_arith_conv is expected to have inserted
// ND_CAST nodes wherever two different decimal widths meet, so a mismatch
// between a node's own width and the width the caller asked for indicates a
// missing cast rather than something safe to paper over with the caller's
// value.
//
// Always CCCC_DEC_ENV_STATIC (round-to-nearest, flags discarded) -- this
// runs inside the *compiler* process, not the guest VM, so it must never
// observe or perturb the host FP environment. The outermost call wraps the
// whole recursive fold in a save/restore fenv barrier (mirroring
// src/macros.c's fenv_barrier_begin/end around comptime vm_eval()); nested
// recursive calls skip the barrier since it's already in effect.
static void eval_decimal_rec(VirtualMachine *vm, Node *node, int w, void *out) {
    add_type(vm, node);

    int node_w = dec_width_code(node->ty);
    if (node_w < 0 || node_w != w)
        error_tok(vm, node->tok,
                  "internal error: decimal constant-folding width mismatch "
                  "(missing usual-arithmetic-conversion cast?)");

    switch (node->kind) {
    case ND_NUM:
        if (!node->dec_digits)
            error_tok(vm, node->tok, "not a compile-time constant");
        if (!cccc_dec_encode_literal(node->dec_digits, w, out))
            error_tok(vm, node->tok,
                      "_Decimal literals require a build with CCCC_HAS_DECIMAL=1");
        return;
    case ND_CAST: {
        Type *src_ty = node->lhs->ty;
        if (is_decimal(src_ty)) {
            int src_w = dec_width_code(src_ty);
            unsigned char tmp[16];
            eval_decimal_rec(vm, node->lhs, src_w, tmp);
            if (!cccc_dec_convert(w, src_w, out, tmp, CCCC_DEC_ENV_STATIC))
                error_tok(vm, node->tok,
                          "_Decimal literals require a build with CCCC_HAS_DECIMAL=1");
            return;
        }
        if (is_integer(src_ty)) {
            int64_t v = eval(vm, node->lhs);
            if (!cccc_dec_from_int(w, out, v, src_ty->is_unsigned, CCCC_DEC_ENV_STATIC))
                error_tok(vm, node->tok,
                          "_Decimal literals require a build with CCCC_HAS_DECIMAL=1");
            return;
        }
        if (is_flonum(src_ty)) {
            double d = eval_double(vm, node->lhs);
            uint64_t bits;
            bool src_is_f32 = (src_ty->kind == TY_FLOAT);
            if (src_is_f32) {
                float fv = (float)d;
                uint32_t b32; memcpy(&b32, &fv, 4);
                bits = b32;
            } else {
                memcpy(&bits, &d, 8);
            }
            if (!cccc_dec_from_bin(w, out, bits, src_is_f32, CCCC_DEC_ENV_STATIC))
                error_tok(vm, node->tok,
                          "_Decimal literals require a build with CCCC_HAS_DECIMAL=1");
            return;
        }
        error_tok(vm, node->tok, "not a compile-time constant");
        return;
    }
    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: {
        unsigned char a[16], b[16];
        eval_decimal_rec(vm, node->lhs, w, a);
        eval_decimal_rec(vm, node->rhs, w, b);
        int op = node->kind == ND_ADD ? '+' : node->kind == ND_SUB ? '-' :
                 node->kind == ND_MUL ? '*' : '/';
        if (!cccc_dec_binop(op, w, out, a, b, CCCC_DEC_ENV_STATIC))
            error_tok(vm, node->tok,
                      "_Decimal literals require a build with CCCC_HAS_DECIMAL=1");
        return;
    }
    case ND_NEG: {
        unsigned char a[16];
        eval_decimal_rec(vm, node->lhs, w, a);
        if (!cccc_dec_neg(w, out, a))
            error_tok(vm, node->tok,
                      "_Decimal literals require a build with CCCC_HAS_DECIMAL=1");
        return;
    }
    case ND_COND:
        // Mirrors eval_double's ND_COND handling: the condition itself is
        // never decimal-typed in practice (C's ?: condition is an ordinary
        // scalar test), so eval_double's own int/flonum handling covers it.
        if (eval_double(vm, node->cond))
            eval_decimal_rec(vm, node->then, w, out);
        else
            eval_decimal_rec(vm, node->els, w, out);
        return;
    case ND_COMMA:
        eval_decimal_rec(vm, node->rhs, w, out);
        return;
    case ND_VAR:
    case ND_MEMBER: {
        Node *expr = constexpr_expr_for_node(node);
        if (!expr)
            error_tok(vm, node->tok, "not a compile-time constant");
        eval_decimal_rec(vm, expr, w, out);
        return;
    }
    default:
        error_tok(vm, node->tok, "not a compile-time constant");
        return;
    }
}

void eval_decimal(VirtualMachine *vm, Node *node, int w, void *out) {
    // #832: restore only the *rounding mode* on the way out, not the whole
    // fenv_t (in particular, NOT the exception-flag state). Pre-existing,
    // independently-verified issue: the compile phase can leave host FP
    // exception flags dirty before this ever runs -- e.g. tokenize.c's
    // convert_pp_number scans every floating/decimal literal's extent via a
    // host strtold() call whose value is discarded but whose side effect
    // isn't (strtold("1.1", NULL) alone sets FE_UNDERFLOW on at least one
    // verified platform). Round-tripping that dirty state through
    // fegetenv()/fesetenv() here would silently reintroduce it after our
    // own feclearexcept() below. The guest program's actual clean-start
    // guarantee comes from cc_run() (src/vm.c) resetting the host FP
    // environment exactly once, immediately before the compiled program
    // begins executing -- this function only needs to (a) fold under a
    // fixed, known rounding mode regardless of ambient state, and (b) not
    // leave *new* dirty flags of its own behind for whatever compiles next.
    int saved_round = fegetround();
    fesetround(FE_TONEAREST);
    feclearexcept(FE_ALL_EXCEPT);
    eval_decimal_rec(vm, node, w, out);
    feclearexcept(FE_ALL_EXCEPT);
    fesetround(saved_round);
}

// Convert op= operators to expressions containing an assignment.
//
// In general, `A op= C` is converted to ``tmp = &A, *tmp = *tmp op B`.
// However, if a given expression is of form `A.x op= C`, the input is
// converted to `tmp = &A, (*tmp).x = (*tmp).x op C` to handle assignments
// to bitfields.
Node *to_assign(VirtualMachine *vm, Node *binary) {
    add_type(vm, binary->lhs);
    add_type(vm, binary->rhs);
    Token *tok = binary->tok;

    // #999: struct_ref()'s three error-recovery paths (called from
    // postfix() to build binary->lhs before we ever get here) return a bare
    // `new_node(vm, ND_MEMBER, tok)` with lhs left NULL and ty = ty_error --
    // a placeholder standing in for "member access on something that
    // already failed to typecheck", not a real member node. The `A.x op= C`
    // branch just below unconditionally reads `binary->lhs->lhs->ty`,
    // which SEGVs on that placeholder (verified via ASan: NULL+0x10) instead
    // of surfacing whatever diagnostic error_tok_recover already queued --
    // turning a compile error into an unexplained crash. Propagate the
    // error type instead of descending into the ND_MEMBER branch.
    if (is_error_type(binary->lhs->ty) || is_error_type(binary->rhs->ty)) {
        Node *err_node = new_node(vm, ND_MEMBER, tok);
        err_node->ty = ty_error;
        return err_node;
    }

    // Convert `A.x op= C` to `tmp = &A, (*tmp).x = (*tmp).x op C`.
    if (binary->lhs->kind == ND_MEMBER) {
        Obj *var = new_lvar(vm, "", 0, pointer_to(vm, binary->lhs->lhs->ty));

        Node *expr1 =
            new_binary(vm, ND_ASSIGN, new_var_node(vm, var, tok),
                       new_unary(vm, ND_ADDR, binary->lhs->lhs, tok), tok);

        Node *expr2 = new_unary(
            vm, ND_MEMBER,
            new_unary(vm, ND_DEREF, new_var_node(vm, var, tok), tok), tok);
        expr2->member = binary->lhs->member;

        Node *expr3 = new_unary(
            vm, ND_MEMBER,
            new_unary(vm, ND_DEREF, new_var_node(vm, var, tok), tok), tok);
        expr3->member = binary->lhs->member;

        Node *expr4 = new_binary(
            vm, ND_ASSIGN, expr2,
            new_binary(vm, binary->kind, expr3, binary->rhs, tok), tok);

        return new_binary(vm, ND_COMMA, expr1, expr4, tok);
    }

    // If A is an atomic type, Convert `A op= B` to
    //
    // ({
    //   T1 *addr = &A; T2 val = (B); T1 old = *addr; T1 new;
    //   do {
    //    new = old op val;
    //   } while (!atomic_compare_exchange_strong(addr, &old, new));
    //   new;
    // })
    if (binary->lhs->ty->is_atomic) {
        Node head = {};
        Node *cur = &head;

        Obj *addr = new_lvar(vm, "", 0, pointer_to(vm, binary->lhs->ty));
        Obj *val = new_lvar(vm, "", 0, binary->rhs->ty);
        Obj *old = new_lvar(vm, "", 0, binary->lhs->ty);
        Obj *new = new_lvar(vm, "", 0, binary->lhs->ty);

        cur = cur->next =
            new_unary(vm, ND_EXPR_STMT,
                      new_binary(vm, ND_ASSIGN, new_var_node(vm, addr, tok),
                                 new_unary(vm, ND_ADDR, binary->lhs, tok), tok),
                      tok);

        cur = cur->next =
            new_unary(vm, ND_EXPR_STMT,
                      new_binary(vm, ND_ASSIGN, new_var_node(vm, val, tok),
                                 binary->rhs, tok),
                      tok);

        cur = cur->next = new_unary(
            vm, ND_EXPR_STMT,
            new_binary(
                vm, ND_ASSIGN, new_var_node(vm, old, tok),
                new_unary(vm, ND_DEREF, new_var_node(vm, addr, tok), tok), tok),
            tok);

        Node *loop = new_node(vm, ND_DO, tok);
        loop->brk_label = new_unique_name(vm);
        loop->cont_label = new_unique_name(vm);

        Node *body =
            new_binary(vm, ND_ASSIGN, new_var_node(vm, new, tok),
                       new_binary(vm, binary->kind, new_var_node(vm, old, tok),
                                  new_var_node(vm, val, tok), tok),
                       tok);

        loop->then = new_node(vm, ND_BLOCK, tok);
        loop->then->body = new_unary(vm, ND_EXPR_STMT, body, tok);

        Node *cas = new_node(vm, ND_CAS, tok);
        cas->cas_addr = new_var_node(vm, addr, tok);
        cas->cas_old = new_unary(vm, ND_ADDR, new_var_node(vm, old, tok), tok);
        cas->cas_new = new_var_node(vm, new, tok);
        loop->cond = new_unary(vm, ND_NOT, cas, tok);

        // #937: an `_Atomic` ntarray element's `+=`/`++`/`--` takes this
        // CAS-loop desugar rather than the plain ND_ASSIGN one above -- the
        // actual store is `cas`, an ND_CAS the ND_ASSIGN-only CHKNT emission
        // site (src/codegen.c) never sees. Copy hi/access-size/nt-terminator
        // from the original `binary->lhs` deref (e.g. `s[n]` in
        // `s[n] += 1`) onto `cas` itself so its own codegen case can emit
        // CHKNT against `desired` (cas_new, staged into REG_A2) the same way
        // ND_ASSIGN does. checked_bounds_lo is deliberately NOT copied here:
        // ND_CAS has no gen_addr-driven CHKR path to feed it, and cas_addr
        // (`&A`, via the `addr` local above) was already range-checked by
        // CHKR when `&binary->lhs` was evaluated -- the lo/hi-must-be-paired
        // invariant documented on Node.checked_bounds_lo/hi only applies to
        // an ND_DEREF's own CHKR check, not to this reuse. The guard fires
        // on the *attempted* desired value, so a CAS iteration that would
        // have failed the compare still traps -- correct, since the program
        // is still trying to write a non-null byte into the terminator slot.
        if (binary->lhs->kind == ND_DEREF && binary->lhs->checked_bounds_hi &&
            binary->lhs->checked_nt_terminator) {
            cas->checked_bounds_hi = clone_bounds_node(vm, binary->lhs->checked_bounds_hi);
            cas->checked_access_size = binary->lhs->checked_access_size;
            cas->checked_nt_terminator = true;
            // #945: carry the object-expression hoist init across too (if
            // any) -- `hi` above may read `*t`, so codegen's CHKNT site for
            // `cas` must re-run the same `t = &obj` init immediately before
            // it, exactly like every other checked_bounds_hi consumer.
            if (binary->lhs->checked_bounds_obj_init)
                cas->checked_bounds_obj_init =
                    clone_bounds_node(vm, binary->lhs->checked_bounds_obj_init);
        }
        // #943: back-link so propagate_checked_bounds()'s walk 3
        // (checked_prop_attach_scan(), which runs after to_assign() has
        // already desugared and discarded any other way to reach `cas`) can
        // mirror a PROPAGATED terminator-slot fact onto `cas` too -- the
        // copy immediately above only ever sees a DIRECT-access
        // checked_nt_terminator, already resolved at this parse-time point.
        // Unconditional (not gated on checked_bounds_hi/checked_nt_terminator
        // being set yet): binary->lhs may be an as-yet-unresolved propagated
        // deref at this point in parsing.
        if (binary->lhs->kind == ND_DEREF)
            binary->lhs->checked_rmw_mirror = cas;

        cur = cur->next = loop;
        cur = cur->next =
            new_unary(vm, ND_EXPR_STMT, new_var_node(vm, new, tok), tok);

        Node *node = new_node(vm, ND_STMT_EXPR, tok);
        node->body = head.next;
        return node;
    }

    // Convert `A op= B` to ``tmp = &A, *tmp = *tmp op B`.
    Obj *var = new_lvar(vm, "", 0, pointer_to(vm, binary->lhs->ty));

    // #919: if `A` is itself a checked-pointer-propagation candidate (`q +=
    // k`/`q++`/`q--`), this `&A` must not be treated as an address-escape by
    // propagate_checked_bounds()'s poison scan -- it never leaves the comma
    // expression built below, so it can't actually alias `q` anywhere. See
    // Node.is_rmw_temp_addr's comment.
    Node *addr_of_lhs = new_unary(vm, ND_ADDR, binary->lhs, tok);
    addr_of_lhs->is_rmw_temp_addr = true;
    Node *expr1 = new_binary(vm, ND_ASSIGN, new_var_node(vm, var, tok), addr_of_lhs, tok);

    Node *store_deref = new_unary(vm, ND_DEREF, new_var_node(vm, var, tok), tok);

    // #937: `binary->lhs` (the original `*p`/`p[i]`/`p->x` deref, e.g. `s[n]`
    // in `s[n] += 1`) already carries checked-pointer bounds if
    // set_checked_deref_bounds() populated them at its own parse site, but
    // `store_deref` (`*tmp` above) is a synthesized ND_DEREF over an unnamed
    // compiler temp -- find_checked_base() sees only the temp, which is
    // never itself declared checked, so it would get no bounds of its own.
    // Without this, CHKNT (#923's terminator guard) never sees the actual
    // store, only CHKR on `&binary->lhs` (via addr_of_lhs above) -- the exact
    // bypass #937 reports. Copy lo/hi/access-size/nt-terminator across so the
    // store is checked exactly as if it had been written `s[n] = *tmp op B`
    // directly. Clone (not alias) the bounds expressions, same reasoning as
    // every other per-deref-site copy (clone_bounds_node()'s comment in
    // parse_checked.c): each access site owns its own Node objects. Both lo
    // and hi must be copied together, never just the nt flag alone -- the
    // ND_ASSIGN codegen guard (src/codegen.c) only routes a store off the
    // promoted-local fast path (which never reaches CHKNT) when both are
    // non-NULL, so a hi-only copy would silently reintroduce the bypass.
    // Gated on ND_DEREF: `p += k`/`p++` (lhs is ND_VAR, #919's
    // is_rmw_temp_addr path) is untouched, matching checked_prop_attach_scan's
    // own propagation model, which only ever attaches bounds to ND_DEREF.
    // One redundant CHKR results per checked RMW (`&s[n]` and this `*tmp`
    // store both range-check the same address) -- acceptable at this
    // magnitude, not worth deduping.
    if (binary->lhs->kind == ND_DEREF && binary->lhs->checked_bounds_lo &&
        binary->lhs->checked_bounds_hi) {
        store_deref->checked_bounds_lo = clone_bounds_node(vm, binary->lhs->checked_bounds_lo);
        store_deref->checked_bounds_hi = clone_bounds_node(vm, binary->lhs->checked_bounds_hi);
        store_deref->checked_access_size = binary->lhs->checked_access_size;
        store_deref->checked_nt_terminator = binary->lhs->checked_nt_terminator;
        // #945: see the ND_CAS branch above -- same reasoning, this is the
        // non-atomic RMW desugar's synthesized store.
        if (binary->lhs->checked_bounds_obj_init)
            store_deref->checked_bounds_obj_init =
                clone_bounds_node(vm, binary->lhs->checked_bounds_obj_init);
    }
    // #943: back-link so propagate_checked_bounds()'s walk 3
    // (checked_prop_attach_scan()) can mirror a PROPAGATED terminator-slot
    // fact -- not yet resolved at this parse-time point -- onto
    // `store_deref` too, the same way the direct-access copy immediately
    // above already does for an already-resolved one. Unconditional, same
    // reasoning as the ND_CAS branch above.
    if (binary->lhs->kind == ND_DEREF)
        binary->lhs->checked_rmw_mirror = store_deref;

    Node *expr2 = new_binary(
        vm, ND_ASSIGN, store_deref,
        new_binary(vm, binary->kind,
                   new_unary(vm, ND_DEREF, new_var_node(vm, var, tok), tok),
                   binary->rhs, tok),
        tok);

    return new_binary(vm, ND_COMMA, expr1, expr2, tok);
}

// assign    = conditional (assign-op assign)?
// assign-op = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^="
//           | "<<=" | ">>="
Node *assign(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = conditional(vm, &tok, tok);

    if (equal(tok, "="))
        return new_binary(vm, ND_ASSIGN, node, assign(vm, rest, tok->next),
                          tok);

    if (equal(tok, "+="))
        return to_assign(vm,
                         new_add(vm, node, assign(vm, rest, tok->next), tok));

    if (equal(tok, "-="))
        return to_assign(vm,
                         new_sub(vm, node, assign(vm, rest, tok->next), tok));

    if (equal(tok, "*="))
        return to_assign(
            vm, new_binary(vm, ND_MUL, node, assign(vm, rest, tok->next), tok));

    if (equal(tok, "/="))
        return to_assign(
            vm, new_binary(vm, ND_DIV, node, assign(vm, rest, tok->next), tok));

    if (equal(tok, "%="))
        return to_assign(
            vm, new_binary(vm, ND_MOD, node, assign(vm, rest, tok->next), tok));

    if (equal(tok, "&="))
        return to_assign(vm, new_binary(vm, ND_BITAND, node,
                                        assign(vm, rest, tok->next), tok));

    if (equal(tok, "|="))
        return to_assign(vm, new_binary(vm, ND_BITOR, node,
                                        assign(vm, rest, tok->next), tok));

    if (equal(tok, "^="))
        return to_assign(vm, new_binary(vm, ND_BITXOR, node,
                                        assign(vm, rest, tok->next), tok));

    if (equal(tok, "<<="))
        return to_assign(
            vm, new_binary(vm, ND_SHL, node, assign(vm, rest, tok->next), tok));

    if (equal(tok, ">>="))
        return to_assign(
            vm, new_binary(vm, ND_SHR, node, assign(vm, rest, tok->next), tok));

    *rest = tok;
    return node;
}

static Node *logor(VirtualMachine *vm, Token **rest, Token *tok);

// conditional = logor ("?" expr? ":" conditional)?
Node *conditional(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *cond = logor(vm, &tok, tok);

    if (!equal(tok, "?")) {
        *rest = tok;
        return cond;
    }

    if (equal(tok->next, ":")) {
        add_type(vm, cond);

        // DCE-aware suppression: 1 ?: chk() — `b` is dead when `a` is
        // statically truthy (a ?: b == a ? a : b, so b is only reached when
        // a is falsy).  Matches standard-ternary els_dead direction (#645).
        bool elvis_els_dead = vm->compiler.saw_diag_attr &&
                              static_branch_value(vm, cond) == 1;

        // [GNU] `a ?: b` normally compiles as `tmp = a, tmp ? tmp : b` so
        // `a` is evaluated exactly once even when it has side effects or is
        // expensive to recompute. #949: when `a` is a plain, cheaply
        // re-readable operand -- ND_VAR/ND_NUM, and not volatile/_Atomic
        // (where two reads could legitimately observe different values) --
        // skip the temp and build `cond ? clone(cond) : b` directly instead.
        // This keeps a *pure* elvis expression, e.g. a checked-pointer
        // bounds declaration `count(n ?: 8)`, side-effect-free per
        // node_has_side_effects(): the temp form's ND_ASSIGN would
        // otherwise always trip that check, and is also unsound at file
        // scope, where a bounds expression may be resolved with no current
        // function to host the temp local (see resolve_bounds_tokens()).
        // Anything else keeps the temp desugar unchanged.
        bool cheap_reread = (cond->kind == ND_VAR || cond->kind == ND_NUM) &&
                            !(cond->ty && (cond->ty->is_volatile || cond->ty->is_atomic));

        if (cheap_reread) {
            Node *rhs = new_node(vm, ND_COND, tok);
            rhs->cond = cond;
            rhs->then = clone_bounds_node(vm, cond);
            if (elvis_els_dead) vm->compiler.dead_code_depth++;
            rhs->els = conditional(vm, rest, tok->next->next);
            if (elvis_els_dead) vm->compiler.dead_code_depth--;
            return rhs;
        }

        // Compile `a ?: b` as `tmp = a, tmp ? tmp : b`.
        Obj *var = new_lvar(vm, "", 0, cond->ty);
        Node *lhs =
            new_binary(vm, ND_ASSIGN, new_var_node(vm, var, tok), cond, tok);
        Node *rhs = new_node(vm, ND_COND, tok);
        rhs->cond = new_var_node(vm, var, tok);
        rhs->then = new_var_node(vm, var, tok);
        if (elvis_els_dead) vm->compiler.dead_code_depth++;
        rhs->els = conditional(vm, rest, tok->next->next);
        if (elvis_els_dead) vm->compiler.dead_code_depth--;
        return new_binary(vm, ND_COMMA, lhs, rhs, tok);
    }

    Node *node = new_node(vm, ND_COND, tok);
    node->cond = cond;

    // DCE-aware suppression: 0 ? dead() : live() — then branch is dead;
    // 1 ? live() : dead() — else branch is dead.
    int ternary_bv = vm->compiler.saw_diag_attr
                         ? static_branch_value(vm, cond)
                         : -1;
    bool then_dead = (ternary_bv == 0), else_dead = (ternary_bv == 1);

    if (then_dead) vm->compiler.dead_code_depth++;
    node->then = expr(vm, &tok, tok->next);
    if (then_dead) vm->compiler.dead_code_depth--;

    // Try to recover if ':' is missing
    if (!equal(tok, ":")) {
        if (vm->collect_errors &&
            error_tok_recover(vm, tok, "expected ':' in ternary operator")) {
            // Use 'then' expression as 'else' placeholder
            node->els = node->then;
            *rest = tok;
            return node;
        }
        tok = skip(vm, tok, ":");
    } else {
        tok = tok->next;
    }

    if (else_dead) vm->compiler.dead_code_depth++;
    node->els = conditional(vm, rest, tok);
    if (else_dead) vm->compiler.dead_code_depth--;
    return node;
}

static Node *logand(VirtualMachine *vm, Token **rest, Token *tok);

// logor = logand ("||" logand)*
static Node *logor(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = logand(vm, &tok, tok);
    while (equal(tok, "||")) {
        Token *start = tok;
        // DCE-aware suppression: true || chk() — RHS is statically dead.
        // static_branch_value handles both Tier-1 (const) and Tier-2
        // (unsigned tautology), matching the if-statement treatment.
        bool rhs_dead = vm->compiler.saw_diag_attr &&
                        static_branch_value(vm, node) == 1;
        if (rhs_dead) vm->compiler.dead_code_depth++;
        Node *rhs = logand(vm, &tok, tok->next);
        if (rhs_dead) vm->compiler.dead_code_depth--;
        if (vm->compiler.warnings & CCCC_WARN_LOGICAL_OP) {
            if (is_const_expr(vm, node))
                warn_tok(vm, start, CCCC_WARN_LOGICAL_OP,
                         "left operand of '||' is a constant expression");
            else if (is_const_expr(vm, rhs))
                warn_tok(vm, start, CCCC_WARN_LOGICAL_OP,
                         "right operand of '||' is a constant expression");
        }
        node = new_binary(vm, ND_LOGOR, node, rhs, start);
    }
    *rest = tok;
    return node;
}

static Node *bitor(VirtualMachine *vm, Token **rest, Token *tok);

// logand = bitor ("&&" bitor)*
static Node *logand(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = bitor(vm, &tok, tok);
    while (equal(tok, "&&")) {
        Token *start = tok;
        // DCE-aware suppression: false && chk() — RHS is statically dead.
        bool rhs_dead = vm->compiler.saw_diag_attr &&
                        static_branch_value(vm, node) == 0;
        if (rhs_dead) vm->compiler.dead_code_depth++;
        Node *rhs = bitor(vm, &tok, tok->next);
        if (rhs_dead) vm->compiler.dead_code_depth--;
        if (vm->compiler.warnings & CCCC_WARN_LOGICAL_OP) {
            if (is_const_expr(vm, node))
                warn_tok(vm, start, CCCC_WARN_LOGICAL_OP,
                         "left operand of '&&' is a constant expression");
            else if (is_const_expr(vm, rhs))
                warn_tok(vm, start, CCCC_WARN_LOGICAL_OP,
                         "right operand of '&&' is a constant expression");
        }
        node = new_binary(vm, ND_LOGAND, node, rhs, start);
    }
    *rest = tok;
    return node;
}

static Node *bitxor(VirtualMachine *vm, Token **rest, Token *tok);

// bitor = bitxor ("|" bitxor)*
static Node *bitor(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = bitxor(vm, &tok, tok);
    while (equal(tok, "|")) {
        Token *start = tok;
        node =
            new_binary(vm, ND_BITOR, node, bitxor(vm, &tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

static Node *bitand(VirtualMachine *vm, Token **rest, Token *tok);

// bitxor = bitand ("^" bitand)*
static Node *bitxor(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = bitand(vm, &tok, tok);
    while (equal(tok, "^")) {
        Token *start = tok;
        node =
            new_binary(vm, ND_BITXOR, node, bitand(vm, &tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

static Node *equality(VirtualMachine *vm, Token **rest, Token *tok);

// bitand = equality ("&" equality)*
static Node *bitand(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = equality(vm, &tok, tok);
    while (equal(tok, "&")) {
        Token *start = tok;
        node = new_binary(vm, ND_BITAND, node, equality(vm, &tok, tok->next),
                          start);
    }
    *rest = tok;
    return node;
}

static Node *relational(VirtualMachine *vm, Token **rest, Token *tok);

// equality = relational ("==" relational | "!=" relational)*
static Node *equality(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = relational(vm, &tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "==")) {
            Node *rhs = relational(vm, &tok, tok->next);
            if (vm->compiler.warnings & CCCC_WARN_FLOAT_EQUAL) {
                add_type(vm, node);
                add_type(vm, rhs);
                if (is_flonum(node->ty) && is_flonum(rhs->ty))
                    warn_tok(vm, start, CCCC_WARN_FLOAT_EQUAL,
                             "comparing floating-point values with == is unreliable");
            }
            if ((vm->compiler.warnings & CCCC_WARN_TAUTOLOGICAL_COMPARE) &&
                nodes_structurally_equal(node, rhs))
                warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                         "self-comparison always evaluates to true");
            if (vm->compiler.warnings & CCCC_WARN_ENUM_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (node->ty && rhs->ty &&
                    node->ty->kind == TY_ENUM && rhs->ty->kind == TY_ENUM &&
                    node->ty != rhs->ty && node->ty->enum_tag && rhs->ty->enum_tag)
                    warn_tok(vm, start, CCCC_WARN_ENUM_COMPARE,
                             "comparison between values of different enum types '%.*s' and '%.*s'",
                             node->ty->enum_tag->len, node->ty->enum_tag->loc,
                             rhs->ty->enum_tag->len, rhs->ty->enum_tag->loc);
            }
            node = new_binary(vm, ND_EQ, node, rhs, start);
            continue;
        }

        if (equal(tok, "!=")) {
            Node *rhs = relational(vm, &tok, tok->next);
            if (vm->compiler.warnings & CCCC_WARN_FLOAT_EQUAL) {
                add_type(vm, node);
                add_type(vm, rhs);
                if (is_flonum(node->ty) && is_flonum(rhs->ty))
                    warn_tok(vm, start, CCCC_WARN_FLOAT_EQUAL,
                             "comparing floating-point values with != is unreliable");
            }
            if ((vm->compiler.warnings & CCCC_WARN_TAUTOLOGICAL_COMPARE) &&
                nodes_structurally_equal(node, rhs))
                warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                         "self-comparison always evaluates to false");
            if (vm->compiler.warnings & CCCC_WARN_ENUM_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (node->ty && rhs->ty &&
                    node->ty->kind == TY_ENUM && rhs->ty->kind == TY_ENUM &&
                    node->ty != rhs->ty && node->ty->enum_tag && rhs->ty->enum_tag)
                    warn_tok(vm, start, CCCC_WARN_ENUM_COMPARE,
                             "comparison between values of different enum types '%.*s' and '%.*s'",
                             node->ty->enum_tag->len, node->ty->enum_tag->loc,
                             rhs->ty->enum_tag->len, rhs->ty->enum_tag->loc);
            }
            node = new_binary(vm, ND_NE, node, rhs, start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

static Node *shift(VirtualMachine *vm, Token **rest, Token *tok);

// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static Node *relational(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = shift(vm, &tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "<")) {
            Node *rhs = shift(vm, &tok, tok->next);
            if (vm->compiler.warnings & CCCC_WARN_TAUTOLOGICAL_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (nodes_structurally_equal(node, rhs))
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "self-comparison always evaluates to false");
                else if (is_integer(node->ty) && node->ty->is_unsigned &&
                         is_const_expr(vm, rhs) && eval(vm, rhs) == 0)
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "comparison of unsigned expression < 0 is always false");
            }
            if (vm->compiler.warnings & CCCC_WARN_ENUM_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (node->ty && rhs->ty &&
                    node->ty->kind == TY_ENUM && rhs->ty->kind == TY_ENUM &&
                    node->ty != rhs->ty && node->ty->enum_tag && rhs->ty->enum_tag)
                    warn_tok(vm, start, CCCC_WARN_ENUM_COMPARE,
                             "comparison between values of different enum types '%.*s' and '%.*s'",
                             node->ty->enum_tag->len, node->ty->enum_tag->loc,
                             rhs->ty->enum_tag->len, rhs->ty->enum_tag->loc);
            }
            node = new_binary(vm, ND_LT, node, rhs, start);
            continue;
        }

        if (equal(tok, "<=")) {
            Node *rhs = shift(vm, &tok, tok->next);
            if (vm->compiler.warnings & CCCC_WARN_TAUTOLOGICAL_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (nodes_structurally_equal(node, rhs))
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "self-comparison always evaluates to true");
            }
            if (vm->compiler.warnings & CCCC_WARN_ENUM_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (node->ty && rhs->ty &&
                    node->ty->kind == TY_ENUM && rhs->ty->kind == TY_ENUM &&
                    node->ty != rhs->ty && node->ty->enum_tag && rhs->ty->enum_tag)
                    warn_tok(vm, start, CCCC_WARN_ENUM_COMPARE,
                             "comparison between values of different enum types '%.*s' and '%.*s'",
                             node->ty->enum_tag->len, node->ty->enum_tag->loc,
                             rhs->ty->enum_tag->len, rhs->ty->enum_tag->loc);
            }
            node = new_binary(vm, ND_LE, node, rhs, start);
            continue;
        }

        if (equal(tok, ">")) {
            Node *rhs = shift(vm, &tok, tok->next);
            if (vm->compiler.warnings & CCCC_WARN_TAUTOLOGICAL_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (nodes_structurally_equal(node, rhs))
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "self-comparison always evaluates to false");
                else if (is_integer(rhs->ty) && rhs->ty->is_unsigned &&
                         is_const_expr(vm, node) && eval(vm, node) == 0)
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "comparison of 0 > unsigned expression is always false");
            }
            if (vm->compiler.warnings & CCCC_WARN_ENUM_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (node->ty && rhs->ty &&
                    node->ty->kind == TY_ENUM && rhs->ty->kind == TY_ENUM &&
                    node->ty != rhs->ty && node->ty->enum_tag && rhs->ty->enum_tag)
                    warn_tok(vm, start, CCCC_WARN_ENUM_COMPARE,
                             "comparison between values of different enum types '%.*s' and '%.*s'",
                             node->ty->enum_tag->len, node->ty->enum_tag->loc,
                             rhs->ty->enum_tag->len, rhs->ty->enum_tag->loc);
            }
            // note: a > b is stored as b < a
            node = new_binary(vm, ND_LT, rhs, node, start);
            continue;
        }

        if (equal(tok, ">=")) {
            Node *rhs = shift(vm, &tok, tok->next);
            if (vm->compiler.warnings & CCCC_WARN_TAUTOLOGICAL_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (nodes_structurally_equal(node, rhs))
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "self-comparison always evaluates to true");
                else if (is_integer(node->ty) && node->ty->is_unsigned &&
                         is_const_expr(vm, rhs) && eval(vm, rhs) == 0)
                    warn_tok(vm, start, CCCC_WARN_TAUTOLOGICAL_COMPARE,
                             "comparison of unsigned expression >= 0 is always true");
            }
            if (vm->compiler.warnings & CCCC_WARN_ENUM_COMPARE) {
                add_type(vm, node); add_type(vm, rhs);
                if (node->ty && rhs->ty &&
                    node->ty->kind == TY_ENUM && rhs->ty->kind == TY_ENUM &&
                    node->ty != rhs->ty && node->ty->enum_tag && rhs->ty->enum_tag)
                    warn_tok(vm, start, CCCC_WARN_ENUM_COMPARE,
                             "comparison between values of different enum types '%.*s' and '%.*s'",
                             node->ty->enum_tag->len, node->ty->enum_tag->loc,
                             rhs->ty->enum_tag->len, rhs->ty->enum_tag->loc);
            }
            // note: a >= b is stored as b <= a
            node = new_binary(vm, ND_LE, rhs, node, start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

static Node *add(VirtualMachine *vm, Token **rest, Token *tok);

// shift = add ("<<" add | ">>" add)*
static Node *shift(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = add(vm, &tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "<<")) {
            Node *rhs = add(vm, &tok, tok->next);
            // Check for error types
            add_type(vm, node);
            add_type(vm, rhs);
            if (is_error_type(node->ty) || is_error_type(rhs->ty)) {
                node = new_binary(vm, ND_SHL, node, rhs, start);
                node->ty = ty_error;
                continue;
            }
            if (is_const_expr(vm, rhs)) {
                int64_t rv = eval(vm, rhs);
                if ((vm->compiler.warnings & CCCC_WARN_SHIFT_NEGATIVE_VALUE) && rv < 0)
                    warn_tok(vm, start, CCCC_WARN_SHIFT_NEGATIVE_VALUE,
                             "left shift by negative amount %lld is undefined behaviour", rv);
                if ((vm->compiler.warnings & CCCC_WARN_SHIFT_OVERFLOW) && rv >= 0) {
                    // integer promotion: types smaller than int promote to int (4 bytes)
                    int bw = (node->ty->size < 4 ? 4 : node->ty->size) * 8;
                    if (rv >= bw)
                        warn_tok(vm, start, CCCC_WARN_SHIFT_OVERFLOW,
                                 "left shift amount %lld >= width of type (%d bits)", rv, bw);
                }
            }
            node = new_binary(vm, ND_SHL, node, rhs, start);
            continue;
        }

        if (equal(tok, ">>")) {
            Node *rhs = add(vm, &tok, tok->next);
            // Check for error types
            add_type(vm, node);
            add_type(vm, rhs);
            if (is_error_type(node->ty) || is_error_type(rhs->ty)) {
                node = new_binary(vm, ND_SHR, node, rhs, start);
                node->ty = ty_error;
                continue;
            }
            if (is_const_expr(vm, rhs)) {
                int64_t rv = eval(vm, rhs);
                if ((vm->compiler.warnings & CCCC_WARN_SHIFT_NEGATIVE_VALUE) && rv < 0)
                    warn_tok(vm, start, CCCC_WARN_SHIFT_NEGATIVE_VALUE,
                             "right shift by negative amount %lld is undefined behaviour", rv);
                if ((vm->compiler.warnings & CCCC_WARN_SHIFT_OVERFLOW) && rv >= 0) {
                    int bw = (node->ty->size < 4 ? 4 : node->ty->size) * 8;
                    if (rv >= bw)
                        warn_tok(vm, start, CCCC_WARN_SHIFT_OVERFLOW,
                                 "right shift amount %lld >= width of type (%d bits)", rv, bw);
                }
            }
            node = new_binary(vm, ND_SHR, node, rhs, start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// In C, `+` operator is overloaded to perform the pointer arithmetic.
// If p is a pointer, p+n adds not n but sizeof(*p)*n to the value of p,
// so that p+n points to the location n elements (not bytes) ahead of p.
// In other words, we need to scale an integer value before adding to a
// pointer value. This function takes care of the scaling.
Node *new_add(VirtualMachine *vm, Node *lhs, Node *rhs, Token *tok) {
    add_type(vm, lhs);
    add_type(vm, rhs);

    // Early exit for error types to prevent cascading errors
    if (is_error_type(lhs->ty) || is_error_type(rhs->ty)) {
        Node *node = new_binary(vm, ND_ADD, lhs, rhs, tok);
        node->ty = ty_error;
        return node;
    }

    // Checked C-style checked-pointer arithmetic rule (#770/#482-484): a
    // [[cccc::single]] pointer represents exactly one object and rejects all
    // pointer arithmetic (matches Checked C's _Ptr<T>); [[cccc::array]]/
    // [[cccc::ntarray]] pointers allow it, same as a plain pointer. This is
    // a parse/type-check diagnostic, always on regardless of
    // --checked-pointers (the flag only gates runtime CHKR emission) -- see
    // CheckedKind's comment in cccc.h.
    if ((lhs->ty->kind == TY_PTR && lhs->ty->checked_kind == CHECKED_SINGLE) ||
        (rhs->ty->kind == TY_PTR && rhs->ty->checked_kind == CHECKED_SINGLE))
        error_tok(vm, tok,
                  "pointer arithmetic on a [[cccc::single]] pointer is not "
                  "allowed -- it refers to exactly one object");

    // num + num
    if (is_numeric(lhs->ty) && is_numeric(rhs->ty))
        return new_binary(vm, ND_ADD, lhs, rhs, tok);

    // vec + vec / vec + scalar (element-wise; GNU vector extension,
    // tracker #72). Must come before the `->base` pointer checks below: a
    // vector's `base` is its element type (array/pointer-duality field),
    // not something to decay through pointer arithmetic.
    if (is_vector(lhs->ty) || is_vector(rhs->ty))
        return new_binary(vm, ND_ADD, lhs, rhs, tok);

    if (lhs->ty->base && rhs->ty->base)
        error_tok(vm, tok, "cannot add two pointers");

    // Canonicalize `num + ptr` to `ptr + num`.
    if (!lhs->ty->base && rhs->ty->base) {
        Node *tmp = lhs;
        lhs = rhs;
        rhs = tmp;
    }

    if (!lhs->ty->base)
        error_tok(vm, tok, "invalid operands to + (expected pointer and integer)");

    // void* arithmetic is a GNU extension; we allow it for compatibility
    if (lhs->ty->base->kind == TY_VOID) {
        warn_tok(vm, tok, CCCC_WARN_POINTER_ARITH,
                 "pointer of type 'void *' used in arithmetic");
        rhs = new_binary(vm, ND_MUL, rhs,
                         new_long(vm, get_vm_size(lhs->ty->base), tok), tok);
        return new_binary(vm, ND_ADD, lhs, rhs, tok);
    }

    // VLA + num
    if (lhs->ty->base->kind == TY_VLA) {
        rhs = new_binary(vm, ND_MUL, rhs,
                         new_var_node(vm, lhs->ty->base->vla_size, tok), tok);
        return new_binary(vm, ND_ADD, lhs, rhs, tok);
    }

    // Function pointer arithmetic is a GNU extension
    if (lhs->ty->base->kind == TY_FUNC)
        warn_tok(vm, tok, CCCC_WARN_POINTER_ARITH,
                 "pointer to a function used in arithmetic");

    // ptr + num
    rhs = new_binary(vm, ND_MUL, rhs,
                     new_long(vm, get_vm_size(lhs->ty->base), tok), tok);
    return new_binary(vm, ND_ADD, lhs, rhs, tok);
}

// Like `+`, `-` is overloaded for the pointer type.
Node *new_sub(VirtualMachine *vm, Node *lhs, Node *rhs, Token *tok) {
    add_type(vm, lhs);
    add_type(vm, rhs);

    // Early exit for error types to prevent cascading errors
    if (is_error_type(lhs->ty) || is_error_type(rhs->ty)) {
        Node *node = new_binary(vm, ND_SUB, lhs, rhs, tok);
        node->ty = ty_error;
        return node;
    }

    // Checked-pointer arithmetic rule -- see the matching comment in
    // new_add() above.
    if ((lhs->ty->kind == TY_PTR && lhs->ty->checked_kind == CHECKED_SINGLE) ||
        (rhs->ty->kind == TY_PTR && rhs->ty->checked_kind == CHECKED_SINGLE))
        error_tok(vm, tok,
                  "pointer arithmetic on a [[cccc::single]] pointer is not "
                  "allowed -- it refers to exactly one object");

    // num - num
    if (is_numeric(lhs->ty) && is_numeric(rhs->ty))
        return new_binary(vm, ND_SUB, lhs, rhs, tok);

    // vec - vec / vec - scalar (element-wise; GNU vector extension,
    // tracker #72). See the matching comment in new_add() above.
    if (is_vector(lhs->ty) || is_vector(rhs->ty))
        return new_binary(vm, ND_SUB, lhs, rhs, tok);

    if (!lhs->ty->base)
        error_tok(vm, tok, "invalid operands to - (left operand is not a pointer)");

    // VLA - num. #976: must not fire for VLA - VLA-row-pointer (`&v[1] -
    // &v[0]`) -- rhs->ty->base guards that off, since a plain integer offset
    // has no ->base but a pointer operand does; without the guard this
    // branch ran unconditionally whenever lhs was VLA-row-pointer-typed,
    // treating rhs (itself a pointer) as though it were a raw element count
    // and multiplying two pointer-ish values together, so the genuine ptr-ptr
    // arm below was unreachable for the VLA case.
    if (lhs->ty->base->kind == TY_VLA && !rhs->ty->base) {
        rhs = new_binary(vm, ND_MUL, rhs,
                         new_var_node(vm, lhs->ty->base->vla_size, tok), tok);
        add_type(vm, rhs);
        Node *node = new_binary(vm, ND_SUB, lhs, rhs, tok);
        node->ty = lhs->ty;
        return node;
    }

    // ptr - num
    if (lhs->ty->base && is_integer(rhs->ty)) {
        if (lhs->ty->base->kind == TY_VOID)
            warn_tok(vm, tok, CCCC_WARN_POINTER_ARITH,
                     "pointer of type 'void *' used in arithmetic");
        else if (lhs->ty->base->kind == TY_FUNC)
            warn_tok(vm, tok, CCCC_WARN_POINTER_ARITH,
                     "pointer to a function used in arithmetic");
        rhs = new_binary(vm, ND_MUL, rhs,
                         new_long(vm, get_vm_size(lhs->ty->base), tok), tok);
        add_type(vm, rhs);
        Node *node = new_binary(vm, ND_SUB, lhs, rhs, tok);
        node->ty = lhs->ty;
        return node;
    }

    // ptr - ptr, which returns how many elements are between the two.
    if (lhs->ty->base && rhs->ty->base) {
        if (lhs->ty->base->kind == TY_VOID || rhs->ty->base->kind == TY_VOID)
            warn_tok(vm, tok, CCCC_WARN_POINTER_ARITH,
                     "pointer of type 'void *' used in arithmetic");
        else if (lhs->ty->base->kind == TY_FUNC || rhs->ty->base->kind == TY_FUNC)
            warn_tok(vm, tok, CCCC_WARN_POINTER_ARITH,
                     "pointer to a function used in arithmetic");
        Node *node = new_binary(vm, ND_SUB, lhs, rhs, tok);
        node->ty = ty_long;
        // #976: when lhs->ty->base is TY_VLA (e.g. `&v[1] - &v[0]` on a 2-D
        // VLA, both sides `int (*)[m]`), TY_VLA's `size` is always the
        // placeholder 8 a pointer-sized new_type() call gives it (vla_of(),
        // type.c), not the row's runtime byte size -- divide by vla_size
        // instead, mirroring the VLA arm the ptr-num cases above this
        // function already have. Guarded on vla_size being non-NULL (unlike
        // those ptr-num arms, which are only reached from a declared VLA's
        // own arithmetic): a TY_VLA base can also arrive here from a
        // declarator like `int (*p)[m]` for which compute_vla_size never
        // ran, in which case the constant-size divide below is the best
        // available fallback -- no worse than before this fix.
        //
        // The cast to ty_long is required, not cosmetic: vla_size is an Obj
        // of type ty_ulong (parse.c, compute_vla_size), and dividing a
        // ty_long numerator by a ty_ulong denominator promotes the whole
        // division to unsigned via usual_arith_conv, which would turn a
        // negative result (e.g. `&v[0] - &v[1]` == -1) into a huge positive
        // garbage value instead.
        if (lhs->ty->base->kind == TY_VLA && lhs->ty->base->vla_size) {
            Node *divisor = new_cast(
                vm, new_var_node(vm, lhs->ty->base->vla_size, tok), ty_long);
            add_type(vm, divisor);
            return new_binary(vm, ND_DIV, node, divisor, tok);
        }
        return new_binary(vm, ND_DIV, node,
                          new_num(vm, lhs->ty->base->size, tok), tok);
    }

    error_tok(vm, tok, "invalid operands to -");
    return NULL;
}

static Node *mul(VirtualMachine *vm, Token **rest, Token *tok);

// add = mul ("+" mul | "-" mul)*
static Node *add(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = mul(vm, &tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "+")) {
            node = new_add(vm, node, mul(vm, &tok, tok->next), start);
            continue;
        }

        if (equal(tok, "-")) {
            node = new_sub(vm, node, mul(vm, &tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// mul = cast ("*" cast | "/" cast | "%" cast)*
static Node *mul(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = cast(vm, &tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "*")) {
            Node *rhs = cast(vm, &tok, tok->next);
            // Check for error types
            add_type(vm, node);
            add_type(vm, rhs);
            if (is_error_type(node->ty) || is_error_type(rhs->ty)) {
                node = new_binary(vm, ND_MUL, node, rhs, start);
                node->ty = ty_error;
                continue;
            }
            node = new_binary(vm, ND_MUL, node, rhs, start);
            continue;
        }

        if (equal(tok, "/")) {
            Node *rhs = cast(vm, &tok, tok->next);
            // Check for error types
            add_type(vm, node);
            add_type(vm, rhs);
            if (is_error_type(node->ty) || is_error_type(rhs->ty)) {
                node = new_binary(vm, ND_DIV, node, rhs, start);
                node->ty = ty_error;
                continue;
            }
            node = new_binary(vm, ND_DIV, node, rhs, start);
            continue;
        }

        if (equal(tok, "%")) {
            Node *rhs = cast(vm, &tok, tok->next);
            // Check for error types
            add_type(vm, node);
            add_type(vm, rhs);
            if (is_error_type(node->ty) || is_error_type(rhs->ty)) {
                node = new_binary(vm, ND_MOD, node, rhs, start);
                node->ty = ty_error;
                continue;
            }
            node = new_binary(vm, ND_MOD, node, rhs, start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// cast = "(" type-name ")" cast | unary
Node *cast(VirtualMachine *vm, Token **rest, Token *tok) {
    if (is_compound_literal_head(vm, tok))
        return unary(vm, rest, tok);

    if (equal(tok, "(") && is_typename(vm, tok->next)) {
        Token *start = tok;
        Type *ty = typename(vm, &tok, tok->next);
        tok = skip(vm, tok, ")");

        Node *expr = cast(vm, &tok, tok);

        // Warn when a cast discards the _Atomic qualifier from a pointer type.
        if ((vm->flags & CCCC_THREAD_SAFETY) &&
            !vm->compiler.in_type_lookahead) {
            add_type(vm, expr);
            if (expr->ty && ty &&
                expr->ty->kind == TY_PTR && ty->kind == TY_PTR &&
                expr->ty->base && ty->base &&
                expr->ty->base->is_atomic && !ty->base->is_atomic) {
                warn_tok(vm, start, CCCC_WARN_DISCARDED_QUALIFIERS,
                         "cast discards '_Atomic' qualifier from pointer type; "
                         "non-atomic access to atomic object may cause data races");
            }
        }

        // -Wcast-qual and -Wcast-align: need expr->ty populated.
        if (!vm->compiler.in_type_lookahead &&
            (vm->compiler.warnings & (CCCC_WARN_CAST_QUAL | CCCC_WARN_CAST_ALIGN))) {
            add_type(vm, expr);
        }

        // -Wcast-qual: explicit cast drops const/volatile/restrict from pointee.
        if (!vm->compiler.in_type_lookahead &&
            (vm->compiler.warnings & CCCC_WARN_CAST_QUAL) &&
            expr->ty && ty &&
            expr->ty->kind == TY_PTR && ty->kind == TY_PTR &&
            expr->ty->base && ty->base) {
            Type *fb = expr->ty->base, *tb = ty->base;
            char qbuf[128]; qbuf[0] = '\0';
            if (fb->is_const    && !tb->is_const)    strcat(qbuf, "'const'");
            if (fb->is_volatile && !tb->is_volatile) {
                if (qbuf[0]) strcat(qbuf, ", ");
                strcat(qbuf, "'volatile'");
            }
            if (fb->is_restrict && !tb->is_restrict) {
                if (qbuf[0]) strcat(qbuf, ", ");
                strcat(qbuf, "'restrict'");
            }
            if (qbuf[0]) {
                int qcount = (fb->is_const    && !tb->is_const) +
                             (fb->is_volatile && !tb->is_volatile) +
                             (fb->is_restrict && !tb->is_restrict);
                warn_tok(vm, start, CCCC_WARN_CAST_QUAL,
                         "cast discards %s qualifier%s from pointer target type",
                         qbuf, qcount > 1 ? "s" : "");
            }
        }

        // -Wcast-align: explicit cast raises pointer alignment requirement.
        if (!vm->compiler.in_type_lookahead &&
            (vm->compiler.warnings & CCCC_WARN_CAST_ALIGN) &&
            expr->ty && ty &&
            expr->ty->kind == TY_PTR && ty->kind == TY_PTR &&
            expr->ty->base && ty->base &&
            ty->base->align > expr->ty->base->align)
            warn_tok(vm, start, CCCC_WARN_CAST_ALIGN,
                     "cast increases required alignment of target type");

        // type cast
        Node *node = new_cast(vm, expr, ty);
        node->tok = start;
        *rest = tok;
        return node;
    }

    return unary(vm, rest, tok);
}

// GNU vector_size lane lvalue helper (tracker #715, used by __builtin_shuffle):
// builds `vec_expr[index]` the same way the `[` postfix subscript parse site
// lowers a vector subscript -- &vec_expr cast to an element-pointer, then
// ordinary pointer-offset + deref. `vec_expr` must not yet have ->ty set
// (e.g. a fresh new_var_node) since new_cast() below type-checks it. `index`
// may be a compile-time constant (new_num) or, since #723, a runtime
// expression (e.g. a masked/wrapped lane index) -- the lowering is identical
// either way because vector subscripting already supports a runtime index
// (verified: `a[i]` / `r[j] = ...` with a variable `i`/`j` compiles and runs
// correctly through this same ND_ADDR/ND_DEREF path).
Node *vector_lane_ref(VirtualMachine *vm, Node *vec_expr, Type *elem_ty,
                              Node *index, Token *tok) {
    Node *addr = new_unary(vm, ND_ADDR, vec_expr, tok);
    addr = new_cast(vm, addr, pointer_to(vm, elem_ty));
    return new_unary(vm, ND_DEREF, new_add(vm, addr, index, tok), tok);
}

// __builtin_classify_type's result (ticket #721, extended by #829): gcc's
// typeclass.h codes, reused where a CCCC type maps directly onto one. Only
// the exact numeric values of CCCC_VECTOR_TYPE_CLASS and
// CCCC_DECIMAL_TYPE_CLASS are load-bearing (they're the discriminants
// <stdarg.h>'s va_arg uses to detect a by-pointer variadic vector/decimal
// argument -- see the widened predicate in include/stdarg.h's va_arg macro);
// the rest exist for __has_builtin/gcc-compatibility and are not otherwise
// consumed by CCCC itself.
enum {
    CCCC_VOID_TYPE_CLASS = 0,
    CCCC_INTEGER_TYPE_CLASS = 1,
    CCCC_CHAR_TYPE_CLASS = 2,
    CCCC_ENUMERAL_TYPE_CLASS = 3,
    CCCC_BOOLEAN_TYPE_CLASS = 4,
    CCCC_POINTER_TYPE_CLASS = 5,
    CCCC_REAL_TYPE_CLASS = 8,
    CCCC_COMPLEX_TYPE_CLASS = 9,
    CCCC_FUNCTION_TYPE_CLASS = 10,
    CCCC_RECORD_TYPE_CLASS = 12,
    CCCC_UNION_TYPE_CLASS = 13,
    CCCC_ARRAY_TYPE_CLASS = 14,
    // No gcc equivalent -- _Decimal32/64/128 (#829) isn't in gcc's
    // typeclass.h either (gcc classifies it as REAL_TYPE_CLASS, but CCCC's
    // va_arg needs a distinct discriminant since decimal, unlike binary
    // float, is read back by pointer -- see CCCC_VECTOR_TYPE_CLASS below).
    CCCC_DECIMAL_TYPE_CLASS = 98,
    // No gcc equivalent -- vector_size vectors aren't in gcc's typeclass.h.
    CCCC_VECTOR_TYPE_CLASS = 99,
};

int64_t classify_type_code(Type *ty) {
    if (!ty)
        return -1; // no_type_class
    if (is_vector(ty))
        return CCCC_VECTOR_TYPE_CLASS;
    if (is_decimal(ty))
        return CCCC_DECIMAL_TYPE_CLASS;
    switch (ty->kind) {
    case TY_VOID:
        return CCCC_VOID_TYPE_CLASS;
    case TY_BOOL:
        return CCCC_BOOLEAN_TYPE_CLASS;
    case TY_CHAR:
        return CCCC_CHAR_TYPE_CLASS;
    case TY_ENUM:
        return CCCC_ENUMERAL_TYPE_CLASS;
    case TY_PTR:
        return CCCC_POINTER_TYPE_CLASS;
    case TY_FLOAT:
    case TY_DOUBLE:
    case TY_LDOUBLE:
        return CCCC_REAL_TYPE_CLASS;
    case TY_COMPLEX:
        return CCCC_COMPLEX_TYPE_CLASS;
    case TY_FUNC:
        return CCCC_FUNCTION_TYPE_CLASS;
    case TY_STRUCT:
        return CCCC_RECORD_TYPE_CLASS;
    case TY_UNION:
        return CCCC_UNION_TYPE_CLASS;
    case TY_ARRAY:
    case TY_VLA:
        return CCCC_ARRAY_TYPE_CLASS;
    default:
        // SHORT, INT, LONG, BITINT, NULLPTR_T, etc.
        return CCCC_INTEGER_TYPE_CLASS;
    }
}
