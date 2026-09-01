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

// Statement grammar: stmt(), compound_stmt(), asm statements, switch/case
// conflict checking, and the switch-fallthrough warning pass.

#include "./parse_internal.h"

// Returns true if a given token represents a type.
bool is_typename(VirtualMachine *vm, Token *tok) {
    pthread_once(&typename_map_once, init_typename_map);

    if (hashmap_get2(&typename_map, tok->loc, tok->len) ||
        find_typedef(vm, tok))
        return true;

    // #894: an unresolved identifier during the comptime parse may name a
    // typedef the demand-driven declaration index has seen but not yet
    // spliced in. Retry find_typedef() once after a successful splice --
    // deliberately NOT folded into find_typedef() itself, since the
    // $Name/$type(Name) reflection operators (primary(), parse_postfix.c)
    // call find_typedef() directly to reach into the *runtime*
    // Obj/Type tables, which must never be redirected through this index.
    if ((vm->compiler.in_macro_mode || vm->compiler.comptime_splice_active) &&
        tok->kind == TK_IDENT && cc_comptime_resolve_typename(vm, tok) &&
        find_typedef(vm, tok))
        return true;

    // "bool" is only a typename when it was actually classified as a C23
    // keyword; in pre-C23 modes it is downgraded to TK_IDENT and may be
    // used as an ordinary identifier (e.g. without <stdbool.h>), so it is
    // deliberately not added to the hashmap above (which matches by text
    // regardless of token kind).
    return tok->kind == TK_KEYWORD &&
           (equal(tok, "bool") || equal(tok, "thread_local"));
}

// asm-stmt = ("asm" | "__asm__" | "__asm") ("volatile" | "inline")* "("
//            string-literal ")" ";"
static Node *asm_stmt(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = new_node(vm, ND_ASM, tok);
    tok        = tok->next;

    while (equal(tok, "volatile") || equal(tok, "inline"))
        tok = tok->next;

    tok = skip(vm, tok, "(");
    if (tok->kind != TK_STR || tok->ty->base->kind != TY_CHAR)
        error_tok(vm, tok, "expected string literal, found '%.*s'", tok->len,
                  tok->loc);
    node->asm_str = tok->str;
    tok           = skip(vm, tok->next, ")");
    // The trailing ';' is part of the statement. In a block it was previously
    // left for the body loop to consume as an empty statement; consuming it
    // here also lets `Quote("asm(\"...\");")` parse cleanly (quote_core rejects
    // any tokens left over after the single parsed statement). Tolerated as
    // optional to preserve cccc's existing leniency about a missing ';'.
    if (equal(tok, ";"))
        tok = tok->next;
    *rest = tok;
    return node;
}

// #815/#816: report a case label ("c") whose value range collides with any
// node already in "chain". Shared by the parser's switch epilogue below and
// the comptime reflection switch builders (reflection.c's
// __builtin_ast_switch_add_case), so hand-written and macro-generated
// switches produce identical diagnostics. "chain" must already be known
// conflict-free among itself (true for both callers: the parser rescans the
// fully-built case_next list pairwise, and reflection.c calls this before
// splicing the new node in, so it only ever compares against prior entries).
//
// Case nodes built by the reflection API may carry a NULL tok (macro_call_tok
// can be unset -- see alloc_node in reflection.c), so this can't
// unconditionally deref tok->file/tok->loc the way plain error_tok() call sites
// do.
void check_case_conflict(VirtualMachine *vm, Node *chain, Node *c) {
    for (Node *o = chain; o; o = o->case_next) {
        if (c->begin > o->end || o->begin > c->end)
            continue;

        Node *later = o;
        if (c->tok && o->tok && c->tok->file == o->tok->file &&
            c->tok->loc > o->tok->loc)
            later = c;

        char *msg;
        char  buf[64];
        if (c->begin == c->end && o->begin == o->end) {
            snprintf(buf, sizeof(buf), "duplicate case value '%ld'", c->begin);
            msg = buf;
        } else {
            msg = "duplicate (or overlapping) case value";
        }

        if (later->tok)
            error_tok(vm, later->tok, "%s", msg);
        else
            error("%s", msg);
        return;
    }
}

// C23 §6.8.1: a label may precede a declaration at block scope.
// Pre-C23 bare declarations after labels are a hard error.
// Limitation: only handles object declarations; typedef/function-def after a
// label are not routed here.
static Node *stmt_or_decl(VirtualMachine *vm, Token **rest, Token *tok) {
    if (is_decl_start(vm, tok) && !equal(tok->next, ":")) {
        if (vm->compiler.c_std < CCCC_STD_C23)
            error_tok(vm, tok,
                      "a declaration may not appear directly after a label "
                      "(use --std=c23 or later)");
        VarAttr attr   = {};
        Type   *basety = declspec(vm, &tok, tok, &attr);
        return declaration(vm, rest, tok, basety, &attr);
    }
    return stmt(vm, rest, tok);
}

// #1098: out_cond/out_msg/out_msg_len (may be NULL, ignored) hand back the
// parsed condition Node and its exact message text/length -- eval() folds
// and discards val the same as before, but a caller that wants to
// re-emit the assert for -c=native (serialize_type.c's serialize_static_assert)
// needs the Node itself, not just the folded int64_t. tok->str/tok->len is
// the already-decoded string (escapes resolved), which is what a caller
// re-emitting the message must reuse rather than the raw source spelling.
Token *static_assert_decl(VirtualMachine *vm, Token *tok, Node **out_cond,
                          char **out_msg, int *out_msg_len) {
    bool   c23_static_assert = equal(tok, "static_assert");
    Token *start_tok         = tok;
    tok                      = skip(vm, tok->next, "(");
    Node     *cond_node      = conditional(vm, &tok, tok);
    long long val            = eval(vm, cond_node);
    char     *message        = "static assertion failed";
    int       message_len    = (int)strlen(message);

    if (consume(vm, &tok, tok, ",")) {
        if (tok->kind != TK_STR)
            error_tok(vm, tok, "expected string literal, found '%.*s'",
                      tok->len, tok->loc);
        // #1098: tok->len is the raw source span (quotes/backslashes
        // included) -- tok->str is the already-decoded, NUL-terminated
        // buffer read_string_literal() built (tokenize.c), so its real
        // length is strlen(), not tok->len.
        message     = tok->str;
        message_len = (int)strlen(message);
        tok         = tok->next;
    } else if (!c23_static_assert || vm->compiler.c_std < CCCC_STD_C23) {
        error_tok(vm, tok, "expected ','");
    }

    if (!val)
        error_tok(vm, start_tok, "%s", message);
    tok = skip(vm, tok, ")");
    tok = skip(vm, tok, ";");

    if (out_cond)
        *out_cond = cond_node;
    if (out_msg)
        *out_msg = message;
    if (out_msg_len)
        *out_msg_len = message_len;
    return tok;
}

static Node *expr_stmt(VirtualMachine *vm, Token **rest, Token *tok);
static void warn_switch_fallthrough(VirtualMachine *vm, Node *sw);

// stmt = "return" expr? ";"
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "switch" "(" expr ")" stmt
//      | "case" const-expr ("..." const-expr)? ":" stmt
//      | "default" ":" stmt
//      | "for" "(" expr-stmt expr? ";" expr? ")" stmt
//      | "while" "(" expr ")" stmt
//      | "do" stmt "while" "(" expr ")" ";"
//      | ("asm" | "__asm__" | "__asm") asm-stmt
//      | "goto" (ident | "*" expr) ";"
//      | "break" ";"
//      | "continue" ";"
//      | ident ":" stmt
//      | "{" compound-stmt
//      | expr-stmt
Node *stmt(VirtualMachine *vm, Token **rest, Token *tok) {
    if (equal(tok, "_Static_assert") || equal(tok, "static_assert")) {
        Node *cond    = NULL;
        char *msg     = NULL;
        int   msg_len = 0;
        *rest         = static_assert_decl(vm, tok, &cond, &msg, &msg_len);
        // #1098: stash on the otherwise-empty ND_BLOCK -- serialize_stmt.c's
        // ND_BLOCK case re-emits it for the host to re-check when the
        // condition folds a host-owned layout. See Node.static_assert_cond.
        Node *node                  = new_node(vm, ND_BLOCK, tok);
        node->static_assert_cond    = cond;
        node->static_assert_msg     = msg;
        node->static_assert_msg_len = msg_len;
        return node;
    }

    if (equal(tok, "return")) {
        Node *node = new_node(vm, ND_RETURN, tok);

        // Warn if this is a noreturn function attempting to return
        if (vm->compiler.current_fn && vm->compiler.current_fn->is_noreturn)
            warn_tok(vm, tok, CCCC_WARN_RETURN_TYPE,
                     "noreturn function should not return a value");

        // #965: a block literal's return type may still be pending inference
        // (block_literal() hasn't seen the full body yet) -- its declared
        // return type is a placeholder `ty_void` at this point, so none of
        // the mismatch warnings/casts below are meaningful yet. The real
        // type is inferred from these very `return` statements after the
        // body is fully parsed, and any needed cast is inserted then.
        bool block_infer_pending =
            vm->compiler.current_fn && vm->compiler.current_fn->is_block &&
            vm->compiler.current_fn->block_return_ty_pending;

        if (consume(vm, rest, tok->next, ";")) {
            if (vm->compiler.current_fn && !block_infer_pending) {
                Type *ty = vm->compiler.current_fn->ty->return_ty;
                if (ty->kind != TY_VOID) {
                    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
                        error_tok(vm, tok,
                                  "non-void aggregate function should return a "
                                  "value");
                    warn_tok(vm, tok, CCCC_WARN_RETURN_TYPE,
                             "non-void function should return a value");
                    node->lhs = new_cast(vm, new_num(vm, 0, tok), ty);
                }
            }
            return node;
        }

        Node *exp = expr(vm, &tok, tok->next);
        *rest     = skip(vm, tok, ";");

        add_type(vm, exp);
        // current_fn may be NULL when a $quote template is parsed at file scope
        // (e.g. inside a top-level pragma macro call that uses $quote("return
        // x;")). Guard the implicit return-type cast; types will be resolved by
        // add_type later, or by the caller establishing context via $with_fn.
        if (vm->compiler.current_fn && !block_infer_pending) {
            Type *ty = vm->compiler.current_fn->ty->return_ty;
            if (ty->kind == TY_VOID) {
                warn_tok(vm, node->tok, CCCC_WARN_RETURN_TYPE,
                         "void function should not return a value");
            } else if (ty->kind != TY_STRUCT && ty->kind != TY_UNION) {
                warn_implicit_conversion(vm, exp, ty, node->tok);
                exp = new_cast(vm, exp, ty);
            }
            if (vm->compiler.current_fn->ty->returns_nonnull &&
                (vm->compiler.warnings & CCCC_WARN_NONNULL) &&
                is_const_expr(vm, exp) && eval(vm, exp) == 0)
                warn_tok(vm, node->tok, CCCC_WARN_NONNULL,
                         "null returned from function declared with "
                         "'returns_nonnull'");
        }

        node->lhs = exp;
        return node;
    }

    if (equal(tok, "if")) {
        Node *node = new_node(vm, ND_IF, tok);
        tok        = skip(vm, tok->next, "(");
        node->cond = expr(vm, &tok, tok);
        tok        = skip(vm, tok, ")");

        // DCE-aware diagnostic suppression: when saw_diag_attr is set, check
        // whether the condition is a compile-time constant or an unsigned
        // boundary tautology.  We track a counter (not a bool) so nested dead
        // branches compose correctly, e.g. if(0){ if(1){ chk_fail(); } }.
        // Note: we suppress diagnostics inside the dead branch but still parse
        // and emit it — we do not prune the AST, so codegen is unaffected.
        int  bv        = vm->compiler.saw_diag_attr
                             ? static_branch_value(vm, node->cond)
                             : -1;
        bool then_dead = (bv == 0), else_dead = (bv == 1);

        if (then_dead)
            vm->compiler.dead_code_depth++;
        node->then = stmt(vm, &tok, tok);
        if (then_dead)
            vm->compiler.dead_code_depth--;

        if (equal(tok, "else")) {
            if (else_dead)
                vm->compiler.dead_code_depth++;
            node->els = stmt(vm, &tok, tok->next);
            if (else_dead)
                vm->compiler.dead_code_depth--;
        }
        *rest = tok;

        if (node->els &&
            (vm->compiler.warnings & CCCC_WARN_DUPLICATED_BRANCHES) &&
            nodes_structurally_equal(node->then, node->els))
            warn_tok(vm, node->tok, CCCC_WARN_DUPLICATED_BRANCHES,
                     "both branches of 'if' statement are identical");

        if (vm->compiler.warnings & CCCC_WARN_DUPLICATED_COND) {
            Node *conds[64];
            int   nconds = 0;
            for (Node *chain = node; chain && chain->kind == ND_IF;
                 chain       = chain->els) {
                for (int i = 0; i < nconds; i++) {
                    if (nodes_structurally_equal(conds[i], chain->cond)) {
                        warn_tok(
                            vm, chain->tok, CCCC_WARN_DUPLICATED_COND,
                            "duplicated condition in 'if'/'else if' chain");
                        break;
                    }
                }
                if (nconds < 64)
                    conds[nconds++] = chain->cond;
            }
        }

        return node;
    }

    if (equal(tok, "switch")) {
        Node *node = new_node(vm, ND_SWITCH, tok);
        tok        = skip(vm, tok->next, "(");
        node->cond = expr(vm, &tok, tok);
        tok        = skip(vm, tok, ")");

        if (vm->compiler.warnings & CCCC_WARN_SWITCH_BOOL) {
            add_type(vm, node->cond);
            if (node->cond->ty && node->cond->ty->kind == TY_BOOL)
                warn_tok(vm, node->tok, CCCC_WARN_SWITCH_BOOL,
                         "switch condition has boolean type");
        }

        Node *sw                    = vm->compiler.current_switch;
        vm->compiler.current_switch = node;

        char *brk                   = vm->compiler.brk_label;
        vm->compiler.brk_label = node->brk_label = new_unique_name(vm);
        int saved_brk_cld              = vm->compiler.brk_cleanup_depth;
        vm->compiler.brk_cleanup_depth = vm->compiler.cleanup_scope_depth;

        node->then                     = stmt(vm, rest, tok);

        warn_switch_fallthrough(vm, node);

        // #815: the C standard requires every case label's constant
        // expression to compare unequal to every other one in the same
        // switch -- a duplicate (or, for GNU case ranges, an overlap) is a
        // constraint violation and must be a compile-time diagnostic, not
        // silently-last-one-wins behavior. Checked here (post-parse, over
        // the fully-populated case_next chain) rather than at case
        // registration so sequential and nested duplicate labels report
        // identically.
        for (Node *c1 = node->case_next; c1; c1 = c1->case_next)
            check_case_conflict(vm, c1->case_next, c1);

        if (vm->compiler.warnings &
            (CCCC_WARN_SWITCH | CCCC_WARN_SWITCH_ENUM)) {
            add_type(vm, node->cond);
            Type *cond_ty = node->cond->ty;
            if (cond_ty && cond_ty->kind == TY_ENUM &&
                cond_ty->enum_constants) {
                bool has_default = node->default_case != NULL;
                bool check_sw =
                    (vm->compiler.warnings & CCCC_WARN_SWITCH) && !has_default;
                bool check_se =
                    !!(vm->compiler.warnings & CCCC_WARN_SWITCH_ENUM);
                if (check_sw || check_se) {
                    for (EnumConstant *ec = cond_ty->enum_constants; ec;
                         ec               = ec->next) {
                        bool covered = false;
                        for (Node *c = node->case_next; c; c = c->case_next) {
                            if (ec->value >= c->begin && ec->value <= c->end) {
                                covered = true;
                                break;
                            }
                        }
                        if (!covered) {
                            CCCCWarning which = (check_se && has_default)
                                                    ? CCCC_WARN_SWITCH_ENUM
                                                    : CCCC_WARN_SWITCH;
                            warn_tok(
                                vm, node->tok, which,
                                "enumeration value '%s' not handled in switch",
                                ec->name);
                        }
                    }
                }

                // #817 (mined from clang's Sema/switch.c test coverage):
                // the reverse of the check above -- a case label whose
                // value doesn't correspond to any enumerator of the
                // switch's enum-typed condition. Both directions are
                // gated the same way since they're the same class of
                // enum/switch mismatch.
                if (vm->compiler.warnings & CCCC_WARN_SWITCH) {
                    for (Node *c = node->case_next; c; c = c->case_next) {
                        bool matches = false;
                        for (EnumConstant *ec = cond_ty->enum_constants; ec;
                             ec               = ec->next) {
                            if (ec->value >= c->begin && ec->value <= c->end) {
                                matches = true;
                                break;
                            }
                        }
                        if (!matches)
                            warn_tok(vm, c->tok, CCCC_WARN_SWITCH,
                                     "case value not in enumerated type");
                    }
                }
            }
        }

        if ((vm->compiler.warnings & CCCC_WARN_SWITCH_DEFAULT) &&
            !node->default_case)
            warn_tok(vm, node->tok, CCCC_WARN_SWITCH_DEFAULT,
                     "switch statement has no default case");

        vm->compiler.current_switch    = sw;
        vm->compiler.brk_label         = brk;
        vm->compiler.brk_cleanup_depth = saved_brk_cld;
        return node;
    }

    if (equal(tok, "case")) {
        if (!vm->compiler.current_switch) {
            if (!error_tok_recover(vm, tok, "stray case")) {
                *rest = tok->next;
                return new_node(vm, ND_NULL_EXPR, tok);
            }
            // Skip to end of statement and return empty node
            tok   = skip_to_stmt_end(vm, tok);
            *rest = tok;
            return new_node(vm, ND_NULL_EXPR, tok);
        }

        Node *node                  = new_node(vm, ND_CASE, tok);
        Type *begin_layout_ty       = NULL;
        bool  begin_layout_is_align = false;
        int   begin = const_expr_layout(vm, &tok, tok->next, &begin_layout_ty,
                                        &begin_layout_is_align);
        int   end;
        Type *end_layout_ty       = begin_layout_ty;
        bool  end_layout_is_align = begin_layout_is_align;

        if (equal(tok, "...")) {
            // [GNU] Case ranges, e.g. "case 1 ... 5:"
            end = const_expr_layout(vm, &tok, tok->next, &end_layout_ty,
                                    &end_layout_is_align);
            if (end < begin)
                error_tok(vm, tok, "empty case range specified");
        } else {
            end = begin;
        }

        tok         = skip(vm, tok, ":");
        node->label = new_unique_name(vm);
        node->lhs   = stmt_or_decl(vm, rest, tok);
        node->begin = begin;
        node->end   = end;
        // #1095: see Node.case_begin_layout_ty's own comment.
        node->case_begin_layout_ty       = begin_layout_ty;
        node->case_end_layout_ty         = end_layout_ty;
        node->case_begin_layout_is_align = begin_layout_is_align;
        node->case_end_layout_is_align   = end_layout_is_align;
        node->case_next = vm->compiler.current_switch->case_next;
        vm->compiler.current_switch->case_next = node;
        return node;
    }

    if (equal(tok, "default")) {
        if (!vm->compiler.current_switch) {
            if (!error_tok_recover(vm, tok, "stray default")) {
                *rest = tok->next;
                return new_node(vm, ND_NULL_EXPR, tok);
            }
            // Skip to end of statement and return empty node
            tok   = skip_to_stmt_end(vm, tok);
            *rest = tok;
            return new_node(vm, ND_NULL_EXPR, tok);
        }

        // #815: a second "default:" silently overwrote the first with no
        // diagnostic. Must be checked here at registration -- by the time
        // the switch epilogue runs, default_case has already been
        // clobbered and the first label is gone.
        if (vm->compiler.current_switch->default_case)
            error_tok(vm, tok, "multiple default labels in one switch");

        Node *node                                = new_node(vm, ND_CASE, tok);
        tok                                       = skip(vm, tok->next, ":");
        node->label                               = new_unique_name(vm);
        node->lhs                                 = stmt_or_decl(vm, rest, tok);
        vm->compiler.current_switch->default_case = node;
        return node;
    }

    if (equal(tok, "for")) {
        Node *node = new_node(vm, ND_FOR, tok);
        tok        = skip(vm, tok->next, "(");

        enter_scope(vm);

        char *brk              = vm->compiler.brk_label;
        char *cont             = vm->compiler.cont_label;
        vm->compiler.brk_label = node->brk_label = new_unique_name(vm);
        vm->compiler.cont_label = node->cont_label = new_unique_name(vm);
        int saved_brk_cld_for           = vm->compiler.brk_cleanup_depth;
        int saved_cont_cld_for          = vm->compiler.cont_cleanup_depth;
        vm->compiler.brk_cleanup_depth  = vm->compiler.cleanup_scope_depth;
        vm->compiler.cont_cleanup_depth = vm->compiler.cleanup_scope_depth;

        if (is_decl_start(vm, tok)) {
            Type *basety = declspec(vm, &tok, tok, NULL);
            node->init   = declaration(vm, &tok, tok, basety, NULL);
        } else {
            node->init = expr_stmt(vm, &tok, tok);
        }

        if (!equal(tok, ";"))
            node->cond = expr(vm, &tok, tok);
        tok = skip(vm, tok, ";");

        // DCE-aware suppression: for(;0; inc){body} — cond statically 0 makes
        // both the increment expression and the body unreachable (#644, #646).
        bool for_cond_dead = node->cond && vm->compiler.saw_diag_attr &&
                             static_branch_value(vm, node->cond) == 0;

        if (!equal(tok, ")")) {
            if (for_cond_dead)
                vm->compiler.dead_code_depth++;
            node->inc = expr(vm, &tok, tok);
            if (for_cond_dead)
                vm->compiler.dead_code_depth--;
        }
        tok = skip(vm, tok, ")");

        if (for_cond_dead)
            vm->compiler.dead_code_depth++;
        node->then = stmt(vm, rest, tok);
        if (for_cond_dead)
            vm->compiler.dead_code_depth--;

        leave_scope(vm);
        vm->compiler.brk_label          = brk;
        vm->compiler.cont_label         = cont;
        vm->compiler.brk_cleanup_depth  = saved_brk_cld_for;
        vm->compiler.cont_cleanup_depth = saved_cont_cld_for;
        return node;
    }

    if (equal(tok, "while")) {
        Node *node             = new_node(vm, ND_FOR, tok);
        tok                    = skip(vm, tok->next, "(");
        node->cond             = expr(vm, &tok, tok);
        tok                    = skip(vm, tok, ")");

        char *brk              = vm->compiler.brk_label;
        char *cont             = vm->compiler.cont_label;
        vm->compiler.brk_label = node->brk_label = new_unique_name(vm);
        vm->compiler.cont_label = node->cont_label = new_unique_name(vm);
        int saved_brk_cld_whl           = vm->compiler.brk_cleanup_depth;
        int saved_cont_cld_whl          = vm->compiler.cont_cleanup_depth;
        vm->compiler.brk_cleanup_depth  = vm->compiler.cleanup_scope_depth;
        vm->compiler.cont_cleanup_depth = vm->compiler.cleanup_scope_depth;

        // DCE-aware suppression: while(0){...} — body is statically dead.
        bool whl_body_dead = vm->compiler.saw_diag_attr &&
                             static_branch_value(vm, node->cond) == 0;
        if (whl_body_dead)
            vm->compiler.dead_code_depth++;
        node->then = stmt(vm, rest, tok);
        if (whl_body_dead)
            vm->compiler.dead_code_depth--;

        vm->compiler.brk_label          = brk;
        vm->compiler.cont_label         = cont;
        vm->compiler.brk_cleanup_depth  = saved_brk_cld_whl;
        vm->compiler.cont_cleanup_depth = saved_cont_cld_whl;
        return node;
    }

    if (equal(tok, "do")) {
        Node *node             = new_node(vm, ND_DO, tok);

        char *brk              = vm->compiler.brk_label;
        char *cont             = vm->compiler.cont_label;
        vm->compiler.brk_label = node->brk_label = new_unique_name(vm);
        vm->compiler.cont_label = node->cont_label = new_unique_name(vm);
        int saved_brk_cld_do            = vm->compiler.brk_cleanup_depth;
        int saved_cont_cld_do           = vm->compiler.cont_cleanup_depth;
        vm->compiler.brk_cleanup_depth  = vm->compiler.cleanup_scope_depth;
        vm->compiler.cont_cleanup_depth = vm->compiler.cleanup_scope_depth;

        node->then                      = stmt(vm, &tok, tok->next);

        vm->compiler.brk_label          = brk;
        vm->compiler.cont_label         = cont;
        vm->compiler.brk_cleanup_depth  = saved_brk_cld_do;
        vm->compiler.cont_cleanup_depth = saved_cont_cld_do;

        tok                             = skip(vm, tok, "while");
        tok                             = skip(vm, tok, "(");
        node->cond                      = expr(vm, &tok, tok);
        tok                             = skip(vm, tok, ")");
        *rest                           = skip(vm, tok, ";");
        return node;
    }

    // #1130/NATIVE.md "asm(...) statement spelling is pending": accept
    // the __-wrapped alternate-keyword spellings too, not just bare "asm"
    // -- matches is_asm_label_tok's acceptance of all three for asm("sym")
    // declarator labels (parse_types.c).
    if (equal(tok, "asm") || equal(tok, "__asm__") || equal(tok, "__asm"))
        return asm_stmt(vm, rest, tok);

    if (equal(tok, "goto")) {
        if (equal(tok->next, "*")) {
            // [GNU] `goto *ptr` jumps to the address specified by `ptr`.
            Node *node = new_node(vm, ND_GOTO_EXPR, tok);
            node->lhs  = expr(vm, &tok, tok->next->next);
            *rest      = skip(vm, tok, ";");
            return node;
        }

        Node *node          = new_node(vm, ND_GOTO, tok);
        node->label         = get_ident(vm, tok->next);
        node->cleanup_chain = vm->compiler.cur_cleanup_chain;
        node->goto_next     = vm->compiler.gotos;
        vm->compiler.gotos  = node;
        *rest               = skip(vm, tok->next->next, ";");
        return node;
    }

    if (equal(tok, "break")) {
        if (!vm->compiler.brk_label) {
            // #1249: an eager Quote()/QuoteN() template is parsed
            // immediately, at the Quote() call site, against whatever loop
            // context happens to be live there -- not the loop it is later
            // spliced/attached into (see man/MACROS.md, "Deferred templates
            // with QuoteLazy"). Point at the fix instead of leaving the
            // reporter to wonder why a `break` inside what looks like a loop
            // body is "stray".
            const char *stray_break_msg =
                vm->compiler.comptime_splice_active
                    ? "stray break (a break/continue inside a Quote() "
                      "template binds to the loop enclosing the Quote() "
                      "call, not one it is later spliced into -- use "
                      "QuoteLazy() so the fragment is parsed inside "
                      "MakeWhile()/MakeFor()/MakeDoWhile()'s own loop "
                      "context, or WithLoop(loop) { LoopSetBody(loop, "
                      "Quote(...)); })"
                    : "stray break";
            if (!error_tok_recover(vm, tok, "%s", stray_break_msg)) {
                *rest = tok->next;
                return new_node(vm, ND_NULL_EXPR, tok);
            }
            // Skip to end of statement and return empty node
            tok   = skip_to_stmt_end(vm, tok);
            *rest = tok;
            return new_node(vm, ND_NULL_EXPR, tok);
        }
        Node *node                 = new_node(vm, ND_GOTO, tok);
        node->unique_label         = vm->compiler.brk_label;
        node->cleanup_target_depth = vm->compiler.brk_cleanup_depth;
        *rest                      = skip(vm, tok->next, ";");
        return node;
    }

    if (equal(tok, "continue")) {
        if (!vm->compiler.cont_label) {
            // #1249: see the matching comment on the "break" arm above.
            const char *stray_continue_msg =
                vm->compiler.comptime_splice_active
                    ? "stray continue (a break/continue inside a Quote() "
                      "template binds to the loop enclosing the Quote() "
                      "call, not one it is later spliced into -- use "
                      "QuoteLazy() so the fragment is parsed inside "
                      "MakeWhile()/MakeFor()/MakeDoWhile()'s own loop "
                      "context, or WithLoop(loop) { LoopSetBody(loop, "
                      "Quote(...)); })"
                    : "stray continue";
            if (!error_tok_recover(vm, tok, "%s", stray_continue_msg)) {
                *rest = tok->next;
                return new_node(vm, ND_NULL_EXPR, tok);
            }
            // Skip to end of statement and return empty node
            tok   = skip_to_stmt_end(vm, tok);
            *rest = tok;
            return new_node(vm, ND_NULL_EXPR, tok);
        }
        Node *node                 = new_node(vm, ND_GOTO, tok);
        node->unique_label         = vm->compiler.cont_label;
        node->cleanup_target_depth = vm->compiler.cont_cleanup_depth;
        *rest                      = skip(vm, tok->next, ";");
        return node;
    }

    VarAttr label_attr = {};
    tok                = attribute_list(vm, tok, NULL, &label_attr);
    tok                = c23_attribute_list(vm, tok, NULL, &label_attr);

    if (label_attr.is_fallthrough) {
        if (equal(tok, ";")) {
            *rest                = tok->next;
            Node *node           = new_node(vm, ND_BLOCK, tok);
            node->is_fallthrough = true;
            return node;
        }
    }

    if (tok->kind == TK_IDENT && equal(tok->next, ":")) {
        Node *node         = new_node(vm, ND_LABEL, tok);
        node->label        = arena_strndup(vm, tok->loc, tok->len);
        node->unique_label = new_unique_name(vm);
        // Record the active cleanup scope depth at this label so that
        // resolve_goto_labels can propagate it to each goto's
        // cleanup_target_depth. A goto landing here exits only cleanup scopes
        // *above* this depth.
        node->cleanup_scope_depth = vm->compiler.cleanup_scope_depth;
        node->cleanup_chain       = vm->compiler.cur_cleanup_chain;
        Token *body_tok           = tok->next->next;
        body_tok = attribute_list(vm, body_tok, NULL, &label_attr);
        body_tok = c23_attribute_list(vm, body_tok, NULL, &label_attr);
        node->label_maybe_unused = label_attr.is_maybe_unused;
        node->lhs                = stmt_or_decl(vm, rest, body_tok);
        node->goto_next          = vm->compiler.labels;
        vm->compiler.labels      = node;
        return node;
    }

    if (equal(tok, "{"))
        return compound_stmt(vm, rest, tok->next, NULL);

    return expr_stmt(vm, rest, tok);
}

// compound-stmt = (typedef | declaration | stmt)* "}"
Node *compound_stmt(VirtualMachine *vm, Token **rest, Token *tok,
                    Token **close_tok) {
    Node *node = new_node(vm, ND_BLOCK, tok);
    Node  head = {};
    Node *cur  = &head;

    enter_scope(vm);

    bool seen_stmt = false;
    bool scope_has_cleanup =
        false; // true once first cleanup var is seen in this scope
    while (!equal(tok, "}")) {
        if (is_decl_start(vm, tok) && !equal(tok->next, ":")) {
            if (seen_stmt && vm->compiler.c_std < CCCC_STD_C99)
                warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                         "mixing declarations and code is a C99 extension");
            VarAttr attr   = {};
            Type   *basety = declspec(vm, &tok, tok, &attr);

            if (attr.is_typedef) {
                if (has_custom_attrs(basety, &attr))
                    error_tok(vm, tok,
                              "custom attributes are only supported on "
                              "file-scope declarations");
                tok = parse_typedef(vm, tok, basety, &attr);
                continue;
            }

            if (is_function(vm, tok, basety)) {
                tok = is_function_decl_list(vm, tok, basety)
                          ? function_declaration_list(vm, tok, basety, &attr)
                          : function(vm, tok, basety, &attr);
                continue;
            }

            if (attr.is_extern) {
                tok = global_variable(vm, tok, basety, &attr);
                continue;
            }

            // Snapshot scope->vars before declaration so we can detect new
            // cleanup vars.
            VarScopeNode *vars_before = vm->compiler.scope->vars;
            cur = cur->next = declaration(vm, &tok, tok, basety, &attr);
            // If any newly declared var has cleanup_fn, push a cleanup scope
            // depth. This must happen immediately (not deferred) so that
            // break/continue nodes parsed after this see the updated
            // brk/cont_cleanup_depth.
            if (!scope_has_cleanup) {
                for (VarScopeNode *sv      = vm->compiler.scope->vars;
                     sv != vars_before; sv = sv->next) {
                    if (sv->var && sv->var->cleanup_fn) {
                        scope_has_cleanup = true;
                        vm->compiler.cleanup_scope_depth++;
                        // Push an ancestry node so gotos/labels can compute the
                        // LCA of their cleanup scopes. Arena-allocated because
                        // resolve_goto_labels reads it after compound_stmt
                        // returns.
                        CleanupChainNode *cn =
                            arena_alloc(&vm->compiler.parser_arena,
                                        sizeof(CleanupChainNode));
                        cn->depth  = vm->compiler.cleanup_scope_depth;
                        cn->parent = vm->compiler.cur_cleanup_chain;
                        vm->compiler.cur_cleanup_chain = cn;
                        break;
                    }
                }
            }
        } else {
            // Clear initializing_var when we start parsing statements
            // (non-declarations) This ensures const variables can be
            // initialized but not assigned later
            vm->compiler.initializing_var = NULL;
            cur = cur->next = stmt(vm, &tok, tok);
            seen_stmt       = true;
        }
        add_type(vm, cur);
    }

    // Also clear at end in case there are no statements after declarations
    vm->compiler.initializing_var = NULL;

    // Build CleanupVar list for this block (LIFO order = most-recently-declared
    // first). scope->vars uses prepend so its head is the most recently
    // declared var, which is exactly the right order for LIFO cleanup emission.
    if (scope_has_cleanup) {
        node->cleanup_scope_depth = vm->compiler.cleanup_scope_depth;
        CleanupVar  *cv_list      = NULL;
        CleanupVar **cv_tail      = &cv_list;
        for (VarScopeNode *sv = vm->compiler.scope->vars; sv; sv = sv->next) {
            if (sv->var && sv->var->cleanup_fn) {
                CleanupVar *cv =
                    arena_alloc(&vm->compiler.parser_arena, sizeof(CleanupVar));
                cv->var        = sv->var;
                cv->cleanup_fn = sv->var->cleanup_fn;
                cv->next       = NULL;
                *cv_tail       = cv;
                cv_tail        = &cv->next;
            }
        }
        node->cleanup_vars = cv_list; // LIFO order: codegen iterates directly
        vm->compiler.cleanup_scope_depth--;
        if (vm->compiler.cur_cleanup_chain)
            vm->compiler.cur_cleanup_chain =
                vm->compiler.cur_cleanup_chain->parent;
    }

    leave_scope(vm);

    node->body = head.next;
    if (close_tok)
        *close_tok = tok;
    *rest = tok->next;
    return node;
}

// Returns true if the expression has no observable side effects and its result
// can be safely discarded. Conservative: returns false for unknown node kinds.
static bool expr_has_no_side_effects(Node *n) {
    if (!n)
        return true;
    switch (n->kind) {
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
            return expr_has_no_side_effects(n->lhs) &&
                   expr_has_no_side_effects(n->rhs);
        case ND_NEG:
        case ND_NOT:
        case ND_BITNOT:
        case ND_CAST:
        case ND_ADDR:
            return expr_has_no_side_effects(n->lhs);
        case ND_DEREF:
            return expr_has_no_side_effects(n->lhs);
        case ND_COND:
            return expr_has_no_side_effects(n->cond) &&
                   expr_has_no_side_effects(n->then) &&
                   expr_has_no_side_effects(n->els);
        case ND_COMMA:
            return expr_has_no_side_effects(n->lhs) &&
                   expr_has_no_side_effects(n->rhs);
        case ND_MEMBER:
            return expr_has_no_side_effects(n->lhs);
        case ND_NUM:
        case ND_VAR:
            return true;
        default:
            return false;
    }
}

// Conservative structural equality for -Wduplicated-branches /
// -Wduplicated-cond. Returns false for unrecognised node kinds to avoid false
// positives.
bool nodes_structurally_equal(Node *a, Node *b) {
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    if (a->kind != b->kind)
        return false;
    switch (a->kind) {
        case ND_NUM:
            return a->val == b->val;
        case ND_VAR:
            return a->var == b->var;
        case ND_MEMBER:
            return a->member == b->member &&
                   nodes_structurally_equal(a->lhs, b->lhs);
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
            return nodes_structurally_equal(a->lhs, b->lhs) &&
                   nodes_structurally_equal(a->rhs, b->rhs);
        case ND_NEG:
        case ND_NOT:
        case ND_BITNOT:
        case ND_CAST:
        case ND_ADDR:
        case ND_DEREF:
        case ND_EXPR_STMT:
            return nodes_structurally_equal(a->lhs, b->lhs);
        case ND_COND:
            return nodes_structurally_equal(a->cond, b->cond) &&
                   nodes_structurally_equal(a->then, b->then) &&
                   nodes_structurally_equal(a->els, b->els);
        case ND_BLOCK: {
            Node *pa = a->body, *pb = b->body;
            while (pa && pb) {
                if (!nodes_structurally_equal(pa, pb))
                    return false;
                pa = pa->next;
                pb = pb->next;
            }
            return !pa && !pb;
        }
        case ND_RETURN:
            return nodes_structurally_equal(a->lhs, b->lhs);
        case ND_NULL_EXPR:
            return true;
        default:
            return false;
    }
}

// expr-stmt = expr? ";"
static Node *expr_stmt(VirtualMachine *vm, Token **rest, Token *tok) {
    if (equal(tok, ";")) {
        *rest = tok->next;
        return new_node(vm, ND_BLOCK, tok);
    }

    // #1242: a lone `$k;` in statement position materialises a QuoteLazy()
    // fragment right here -- inside this stmt()'s own live scope chain and
    // brk_label/cont_label -- rather than as an ordinary expression-statement
    // over a $k placeholder ND_VAR. Guard on the '$'+digit shape AND the
    // very next token actually being ';' before calling find_var: this is
    // only the "$k IS the whole statement" shortcut, not a general "an
    // expression-statement starts with $k" rule -- `$1 + 0;` or `$1(x);`
    // must fall through to the ordinary expr()/primary() path below, where a
    // lazy placeholder used as a sub-expression is handled instead (and, if
    // it turns out to hold a statement-kind fragment, diagnosed there rather
    // than silently only consuming `$1` and leaving `+ 0;` behind for
    // skip(vm, tok->next, ";") to choke on). Other '$'-prefixed forms (the
    // $identifier reflect operator, $dump_*/$forward_declare) are
    // '$'+alpha/'_', not '$'+digit, so this never touches them regardless.
    if (tok->kind == TK_IDENT && tok->len > 1 && tok->loc[0] == '$' &&
        tok->loc[1] >= '0' && tok->loc[1] <= '9' && equal(tok->next, ";")) {
        VarScope *vs = find_var(vm, tok);
        if (vs && vs->var && vs->var->lazy_quote) {
            Node *node = cc_quote_expand_lazy(vm, vs->var->lazy_quote,
                                              /*want_stmt=*/true);
            *rest      = skip(vm, tok->next, ";");
            return node;
        }
    }

    Node *node = new_node(vm, ND_EXPR_STMT, tok);
    node->lhs  = expr(vm, &tok, tok);

    // #1242: name the fix when a quote placeholder ($k or $@k -- any of
    // them, not only splice/lazy ones) in statement position is missing its
    // trailing ';' (e.g. a bare `{ $1 }` instead of `{ $1; }`), rather than
    // the generic "expected ';'" -- scoped to quote placeholders (their name
    // always starts with '$', which no ordinary C identifier can) so
    // ordinary code sees no change.
    if (!equal(tok, ";") && node->lhs && node->lhs->kind == ND_VAR &&
        node->lhs->var && node->lhs->var->name &&
        node->lhs->var->name[0] == '$')
        error_tok(vm, tok,
                  "quote placeholder '%s' in statement position must be "
                  "followed by ';' -- write `{ %s; }`, not `{ %s }`",
                  node->lhs->var->name, node->lhs->var->name,
                  node->lhs->var->name);

    *rest = skip(vm, tok, ";");

    add_type(vm, node->lhs);
    if (node->lhs && !(node->lhs->kind == ND_CAST && node->lhs->ty &&
                       node->lhs->ty->kind == TY_VOID)) {
        bool        nodiscard = false;
        const char *what      = NULL;
        if (node->lhs->kind == ND_FUNCALL && node->lhs->func_ty &&
            node->lhs->func_ty->is_nodiscard) {
            nodiscard = true;
            what      = "function";
        } else if (node->lhs->ty && node->lhs->ty->is_nodiscard) {
            nodiscard = true;
            what      = "type";
        }
        if (nodiscard) {
            if (node->lhs->func_ty && node->lhs->func_ty->nodiscard_msg)
                warn_tok(
                    vm, node->tok, CCCC_WARN_NODISCARD,
                    "ignoring return value of %s declared with 'nodiscard': %s",
                    what, node->lhs->func_ty->nodiscard_msg);
            else if (node->lhs->ty && node->lhs->ty->nodiscard_msg)
                warn_tok(
                    vm, node->tok, CCCC_WARN_NODISCARD,
                    "ignoring return value of %s declared with 'nodiscard': %s",
                    what, node->lhs->ty->nodiscard_msg);
            else
                warn_tok(
                    vm, node->tok, CCCC_WARN_NODISCARD,
                    "ignoring return value of %s declared with 'nodiscard'",
                    what);
        }

        if ((vm->compiler.warnings & CCCC_WARN_UNUSED_VALUE) &&
            expr_has_no_side_effects(node->lhs))
            warn_tok(vm, node->tok, CCCC_WARN_UNUSED_VALUE,
                     "expression result unused");
    }

    return node;
}

// Returns true if control can fall through to the statement after `n`.
static bool falls_through(Node *n) {
    if (!n)
        return true;
    switch (n->kind) {
        case ND_RETURN:
        case ND_GOTO:
        case ND_GOTO_EXPR:
        case ND_UNREACHABLE:
            return false;
        case ND_IF:
            if (!n->els)
                return true;
            return falls_through(n->then) || falls_through(n->els);
        case ND_BLOCK:
        case ND_STMT_EXPR:
            if (!n->body)
                return true;
            {
                Node *last = n->body;
                while (last->next)
                    last = last->next;
                return falls_through(last);
            }
        default:
            return true;
    }
}

static void warn_switch_fallthrough(VirtualMachine *vm, Node *sw) {
    if (!sw || sw->kind != ND_SWITCH || !sw->then)
        return;
    if (sw->then->kind != ND_BLOCK || !sw->then->body)
        return;

    Node *body       = sw->then->body;
    Node *group_case = NULL; // current case group's label node
    Node *annotated  = NULL; // last annotated [[fallthrough]] node in group

    for (Node *cur = body; cur; cur = cur->next) {
        if (cur->kind == ND_CASE) {
            if (group_case && annotated != (Node *)1) {
                // Check if the previous case group reaches the end
                // (annotated == non-NULL and also NOT sentinel-1 means
                // fallthrough annotated)
            }
            group_case = cur;
            annotated  = NULL;

            // Unwind nested case labels to find first real statement
            Node *c = cur;
            while (c && c->kind == ND_CASE && c->lhs && c->lhs->kind == ND_CASE)
                c = c->lhs;
            if (c && c->kind == ND_CASE && c->lhs) {
                if (c->lhs->is_fallthrough)
                    annotated = c->lhs;
            }
        } else {
            if (!group_case)
                continue;
            if (cur->is_fallthrough)
                annotated = cur;
        }
    }

    // Reset and redo properly with group_reaches_end tracking
    annotated              = NULL;
    group_case             = NULL;
    bool group_reaches_end = true;

    for (Node *cur = body; cur; cur = cur->next) {
        if (cur->kind == ND_CASE) {
            if (group_case && group_reaches_end && !annotated) {
                // The previous case group reaches the end (falls through to
                // this label) and it's not annotated with [[fallthrough]]
                warn_tok(vm, group_case->tok, CCCC_WARN_FALLTHROUGH,
                         "unannotated fallthrough between case labels");
            }
            group_case        = cur;
            annotated         = NULL;
            group_reaches_end = true;

            // Unwind nested case labels to find the first real statement
            Node *c = cur;
            while (c && c->kind == ND_CASE && c->lhs && c->lhs->kind == ND_CASE)
                c = c->lhs;
            if (c && c->kind == ND_CASE && c->lhs) {
                if (falls_through(c->lhs)) {
                    if (c->lhs->is_fallthrough)
                        annotated = c->lhs;
                } else {
                    group_reaches_end = false;
                }
            }
        } else {
            if (!group_case)
                continue;
            if (!group_reaches_end)
                continue;

            if (cur->is_fallthrough) {
                annotated = cur;
            }
            if (!falls_through(cur))
                group_reaches_end = false;
        }
    }
}
